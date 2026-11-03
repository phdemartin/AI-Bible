#include "ModuleInstallerDialog.h"

#include <swmgr.h>
#include <swmodule.h>
#include <installmgr.h>
#include <remotetrans.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QHeaderView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QLocale>
#include <QMessageBox>
#include <QInputDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QMetaObject>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPointer>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QEventLoop>
#include <QTimer>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <cstring>

#include <zlib.h>
#include <curl/curl.h>

using namespace sword;

namespace {

// StatusReporter permettant de remonter la progression des téléchargements vers l'interface Qt
class QtInstallStatusReporter : public StatusReporter {
public:
    std::function<void(const QString&)> onStageMessage;
    std::function<void(unsigned long, unsigned long)> onProgressUpdate;

    void preStatus(long totalBytes, long completedBytes, const char* message) override {
        if (onStageMessage && message) {
            QString msg = QString::fromUtf8(message).trimmed();
            if (totalBytes > 0) {
                msg += QString(" (%1 / %2 Ko)")
                           .arg(completedBytes / 1024)
                           .arg(totalBytes / 1024);
            }
            onStageMessage(msg);
        }
    }

    void update(unsigned long totalBytes, unsigned long completedBytes) override {
        if (onProgressUpdate) {
            onProgressUpdate(totalBytes, completedBytes);
        }
    }
};

struct CurlWriteContext {
    QByteArray* buffer = nullptr;
};

size_t curlWriteToByteArray(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* ctx = static_cast<CurlWriteContext*>(userp);
    if (ctx && ctx->buffer && total > 0) {
        ctx->buffer->append(static_cast<const char*>(contents), static_cast<qsizetype>(total));
    }
    return total;
}

struct CurlProgressContext {
    const std::atomic<bool>* cancelFlag = nullptr;
    std::function<void(qint64, qint64)> progressCb;
};

int curlXferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<CurlProgressContext*>(clientp);
    if (!ctx) return 0;
    if (ctx->cancelFlag && ctx->cancelFlag->load()) {
        return 1; // Abort transfer
    }
    if (ctx->progressCb && dltotal > 0) {
        ctx->progressCb(static_cast<qint64>(dlnow), static_cast<qint64>(dltotal));
    }
    return 0;
}

QNetworkAccessManager& sharedNetworkManager() {
    static QNetworkAccessManager* s_nam = nullptr;
    if (!s_nam) {
        QNetworkProxyFactory::setUseSystemConfiguration(false);
        s_nam = new QNetworkAccessManager(QCoreApplication::instance());
        s_nam->setProxy(QNetworkProxy::NoProxy);
    }
    return *s_nam;
}

// Téléchargement robuste et non-bloquant en mémoire :
// 1) Utilise Qt6 QNetworkAccessManager sur le thread principal (moteur TLS natif Windows Schannel / OpenSSL Linux)
// 2) Bascule automatiquement sur libcurl dans un thread dédié en cas d'échec ou pour les dépôts FTP
bool downloadUrlToBytes(
    const QString& urlStr,
    QByteArray& outData,
    const std::atomic<bool>& cancelFlag,
    std::function<void(qint64, qint64)> progressCb = nullptr,
    int timeoutMs = 12000)
{
    outData.clear();
    if (cancelFlag.load()) return false;

    const QByteArray userAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AI-Bible/1.1";

    // Essai 1 : Qt6 QNetworkAccessManager pour HTTP / HTTPS
    if (urlStr.startsWith("http://", Qt::CaseInsensitive) || urlStr.startsWith("https://", Qt::CaseInsensitive)) {
        QNetworkAccessManager& nam = sharedNetworkManager();
        QNetworkRequest req{QUrl(urlStr)};
        req.setRawHeader("User-Agent", userAgent);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setMaximumRedirectsAllowed(6);

#ifndef QT_NO_SSL
        if (urlStr.startsWith("https://", Qt::CaseInsensitive) && QSslSocket::supportsSsl()) {
            QSslConfiguration sslConf = QSslConfiguration::defaultConfiguration();
            sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
            req.setSslConfiguration(sslConf);
        }
#endif

        QEventLoop loop;
        QTimer inactivityTimer;
        inactivityTimer.setSingleShot(true);

        QTimer cancelCheckTimer;
        cancelCheckTimer.setInterval(80);

        QNetworkReply* reply = nam.get(req);

        QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 received, qint64 total) {
            inactivityTimer.start(timeoutMs);
            if (progressCb && total > 0) {
                progressCb(received, total);
            }
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&inactivityTimer, &QTimer::timeout, &loop, [&]() {
            if (reply->isRunning()) reply->abort();
            loop.quit();
        });
        QObject::connect(&cancelCheckTimer, &QTimer::timeout, &loop, [&]() {
            if (cancelFlag.load()) {
                if (reply->isRunning()) reply->abort();
                loop.quit();
            }
        });

        inactivityTimer.start(timeoutMs);
        cancelCheckTimer.start();
        if (!reply->isFinished()) {
            loop.exec();
        }
        cancelCheckTimer.stop();
        inactivityTimer.stop();

        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = (reply->error() == QNetworkReply::NoError && (httpStatus == 200 || httpStatus == 0));
        if (ok) {
            outData = reply->readAll();
        }
        reply->deleteLater();
        if (ok && !outData.isEmpty()) {
            return true;
        }
        outData.clear();
        if (cancelFlag.load()) return false;
    }

    // Essai 2 : libcurl en mémoire dans un thread court (pour FTP et en secours HTTP/HTTPS)
    static bool s_curlInited = false;
    if (!s_curlInited) {
        curl_global_init(CURL_GLOBAL_ALL);
        s_curlInited = true;
    }

    bool curlSuccess = false;
    QEventLoop curlLoop;
    QThread* curlThread = QThread::create([&]() {
        CURL* curl = curl_easy_init();
        if (!curl) return;

        auto safeProgressCb = [&](qint64 dlNow, qint64 dlTot) {
            if (progressCb && dlTot > 0) {
                QMetaObject::invokeMethod(&curlLoop, [progressCb, dlNow, dlTot]() {
                    progressCb(dlNow, dlTot);
                }, Qt::QueuedConnection);
            }
        };

        CurlWriteContext writeCtx{&outData};
        CurlProgressContext progCtx{&cancelFlag, safeProgressCb};

        QByteArray urlUtf8 = urlStr.toUtf8();
        curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.constData());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.constData());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteToByteArray);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeCtx);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 6L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 1L);
        curl_easy_setopt(curl, CURLOPT_FTP_USE_EPRT, 0L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(std::min(timeoutMs, 8000)));
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 8L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlXferInfoCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progCtx);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && !outData.isEmpty()) {
            curlSuccess = true;
        }
    });

    QObject::connect(curlThread, &QThread::finished, &curlLoop, &QEventLoop::quit);
    curlThread->start();
    if (curlThread->isRunning()) {
        curlLoop.exec();
    }
    curlThread->wait(1000);
    delete curlThread;

    if (!curlSuccess || outData.isEmpty()) {
        outData.clear();
        return false;
    }
    return true;
}

// Décompresse une archive .tar.gz (mods.d.tar.gz) en mémoire avec zlib et extrait tous les fichiers .conf dans modsDir
bool extractTarGzToModsDir(const QByteArray& gzBytes, const QString& modsDir) {
    if (gzBytes.size() < 32) return false;
    const auto* raw = reinterpret_cast<const unsigned char*>(gzBytes.constData());
    if (raw[0] != 0x1f || raw[1] != 0x8b) {
        return false; // Pas un flux gzip valide
    }

    z_stream zs{};
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(gzBytes.constData()));
    zs.avail_in = static_cast<uInt>(gzBytes.size());

    if (inflateInit2(&zs, 15 + 32) != Z_OK) {
        return false;
    }

    QByteArray tarBytes;
    tarBytes.reserve(gzBytes.size() * 4);
    char chunk[65536];
    int ret = Z_OK;
    while (ret == Z_OK) {
        zs.next_out = reinterpret_cast<Bytef*>(chunk);
        zs.avail_out = sizeof(chunk);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            return false;
        }
        qsizetype produced = static_cast<qsizetype>(sizeof(chunk) - zs.avail_out);
        if (produced > 0) {
            tarBytes.append(chunk, produced);
        }
    }
    inflateEnd(&zs);

    if (tarBytes.size() < 512) return false;

    QDir dir(modsDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
    QDir().mkpath(modsDir);

    auto parseTarOctal = [](const char* p, int len) -> qint64 {
        if (static_cast<unsigned char>(p[0]) & 0x80) {
            // Extension GNU base-256
            qint64 val = 0;
            for (int i = 1; i < len; ++i) {
                val = (val << 8) | static_cast<unsigned char>(p[i]);
            }
            return val;
        }
        qint64 val = 0;
        for (int i = 0; i < len; ++i) {
            char c = p[i];
            if (c == ' ' || c == '\0') continue;
            if (c >= '0' && c <= '7') {
                val = (val << 3) + (c - '0');
            }
        }
        return val;
    };

    int extractedCount = 0;
    qsizetype offset = 0;
    const qsizetype totalSize = tarBytes.size();

    while (offset + 512 <= totalSize) {
        const char* hdr = tarBytes.constData() + offset;
        if (hdr[0] == '\0') {
            break; // Fin de l'archive tar
        }

        qint64 fileSize = parseTarOctal(hdr + 124, 12);
        char typeflag = hdr[156];

        QString name = QString::fromUtf8(hdr, strnlen(hdr, 100));
        QString prefix = QString::fromUtf8(hdr + 345, strnlen(hdr + 345, 155));
        QString fullPath = prefix.isEmpty() ? name : (prefix + "/" + name);

        qsizetype dataOffset = offset + 512;
        if (fileSize < 0 || dataOffset + fileSize > totalSize) {
            break;
        }

        if ((typeflag == '0' || typeflag == '\0') && fullPath.endsWith(".conf", Qt::CaseInsensitive) && fileSize > 0) {
            QString baseName = QFileInfo(fullPath).fileName();
            if (!baseName.isEmpty()) {
                QFile outFile(modsDir + "/" + baseName);
                if (outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    outFile.write(tarBytes.constData() + dataOffset, fileSize);
                    outFile.close();
                    ++extractedCount;
                    if ((extractedCount % 50) == 0) {
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                    }
                }
            }
        }

        qsizetype paddedData = static_cast<qsizetype>(((fileSize + 511) / 512) * 512);
        offset = dataOffset + paddedData;
    }

    return extractedCount > 0;
}

uint16_t readLe16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readLe32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Extrait une archive .zip de module SWORD (packages/<Mod>.zip) directement dans destSwordDir (~/.sword)
bool extractZipBufferToDir(const QByteArray& zipBytes, const QString& destSwordDir) {
    const qsizetype size = zipBytes.size();
    if (size < 64) return false;
    const auto* raw = reinterpret_cast<const unsigned char*>(zipBytes.constData());
    if (raw[0] != 'P' || raw[1] != 'K' || raw[2] != 0x03 || raw[3] != 0x04) {
        return false;
    }

    // Rechercher End of Central Directory (PK\x05\x06) depuis la fin
    qsizetype minSearch = std::max<qsizetype>(0, size - 65557);
    qsizetype eocdPos = -1;
    for (qsizetype pos = size - 22; pos >= minSearch; --pos) {
        if (raw[pos] == 'P' && raw[pos + 1] == 'K' && raw[pos + 2] == 0x05 && raw[pos + 3] == 0x06) {
            eocdPos = pos;
            break;
        }
    }
    if (eocdPos < 0) return false;

    uint16_t cdEntries = readLe16(raw + eocdPos + 10);
    uint32_t cdOffset = readLe32(raw + eocdPos + 16);
    if (cdOffset >= static_cast<uint32_t>(size)) return false;

    int extractedFiles = 0;
    qsizetype cdPos = static_cast<qsizetype>(cdOffset);

    for (uint16_t i = 0; i < cdEntries && cdPos + 46 <= size; ++i) {
        if (raw[cdPos] != 'P' || raw[cdPos + 1] != 'K' || raw[cdPos + 2] != 0x01 || raw[cdPos + 3] != 0x02) {
            break;
        }

        uint16_t compMethod = readLe16(raw + cdPos + 10);
        uint32_t compSize = readLe32(raw + cdPos + 20);
        uint32_t uncompSize = readLe32(raw + cdPos + 24);
        uint16_t nameLen = readLe16(raw + cdPos + 28);
        uint16_t extraLen = readLe16(raw + cdPos + 30);
        uint16_t commentLen = readLe16(raw + cdPos + 32);
        uint32_t localOffset = readLe32(raw + cdPos + 42);

        if (cdPos + 46 + nameLen > size) break;
        QString entryName = QString::fromUtf8(zipBytes.constData() + cdPos + 46, nameLen);
        entryName.replace('\\', '/');
        while (entryName.startsWith("./")) entryName.remove(0, 2);
        while (entryName.startsWith('/')) entryName.remove(0, 1);

        cdPos += 46 + nameLen + extraLen + commentLen;

        if (entryName.isEmpty() || entryName.contains("..")) continue;

        QString outPath = destSwordDir + "/" + entryName;
        if (entryName.endsWith('/')) {
            QDir().mkpath(outPath);
            continue;
        }

        qsizetype lPos = static_cast<qsizetype>(localOffset);
        if (lPos + 30 > size) continue;
        if (raw[lPos] != 'P' || raw[lPos + 1] != 'K' || raw[lPos + 2] != 0x03 || raw[lPos + 3] != 0x04) {
            continue;
        }

        uint16_t lNameLen = readLe16(raw + lPos + 26);
        uint16_t lExtraLen = readLe16(raw + lPos + 28);
        qsizetype dataPos = lPos + 30 + lNameLen + lExtraLen;
        if (dataPos + static_cast<qsizetype>(compSize) > size) continue;

        QDir().mkpath(QFileInfo(outPath).absolutePath());

        if (compMethod == 0) {
            // STORED
            QFile f(outPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(zipBytes.constData() + dataPos, compSize);
                f.close();
                ++extractedFiles;
            }
        } else if (compMethod == 8) {
            // DEFLATE
            QByteArray uncompData(static_cast<qsizetype>(uncompSize), Qt::Uninitialized);
            z_stream zs{};
            zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(zipBytes.constData() + dataPos));
            zs.avail_in = static_cast<uInt>(compSize);
            zs.next_out = reinterpret_cast<Bytef*>(uncompData.data());
            zs.avail_out = static_cast<uInt>(uncompSize);

            if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
                int zret = inflate(&zs, Z_FINISH);
                qsizetype totalOut = static_cast<qsizetype>(zs.total_out);
                inflateEnd(&zs);
                if (zret == Z_STREAM_END || (zret == Z_OK && totalOut == static_cast<qsizetype>(uncompSize))) {
                    QFile f(outPath);
                    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        f.write(uncompData.constData(), totalOut);
                        f.close();
                        ++extractedFiles;
                    }
                }
            }
        }
    }

    return extractedFiles > 0;
}

// Retourne la liste ordonnée des URLs de base (HTTPS/HTTP prioritaires) pour un dépôt SWORD
QStringList candidateBaseUrlsForRepo(const RemoteRepoInfo& repo) {
    QStringList urls;
    QString host = repo.host.trimmed();
    QString dir = repo.directory.trimmed();
    while (dir.endsWith('/')) dir.chop(1);
    if (!dir.startsWith('/')) dir.prepend('/');

    // Miroirs officiels HTTPS/HTTP connus (sans repli FTP port 21 qui déclenche le pare-feu Windows)
    if (host.compare("ftp.crosswire.org", Qt::CaseInsensitive) == 0 ||
        host.compare("crosswire.org", Qt::CaseInsensitive) == 0) {
        QString httpDir = dir.startsWith("/ftpmirror") ? dir : ("/ftpmirror" + dir);
        urls << QString("https://crosswire.org%1").arg(httpDir);
        urls << QString("http://crosswire.org%1").arg(httpDir);
        return urls;
    }

    if (host.compare("ftp.ebible.org", Qt::CaseInsensitive) == 0 ||
        host.compare("ebible.org", Qt::CaseInsensitive) == 0) {
        urls << QString("https://ebible.org%1").arg(dir);
        urls << QString("http://ebible.org%1").arg(dir);
        return urls;
    }

    if (host.compare("ftp.xiphos.org", Qt::CaseInsensitive) == 0) {
        urls << QString("http://ftp.xiphos.org%1").arg(dir);
        return urls;
    }

    if (repo.type.startsWith("HTTPS", Qt::CaseInsensitive)) {
        urls << QString("https://%1%2").arg(host, dir);
        urls << QString("http://%1%2").arg(host, dir);
    } else if (repo.type.startsWith("HTTP", Qt::CaseInsensitive)) {
        urls << QString("http://%1%2").arg(host, dir);
        urls << QString("https://%1%2").arg(host, dir);
    } else {
        urls << QString("ftp://%1%2").arg(host, dir);
    }
    return urls;
}

// Retourne la liste ordonnée des URLs d'archives .zip possibles pour un module donné
QStringList candidateZipUrlsForModule(const RemoteRepoInfo& repo, const QString& modName) {
    QStringList zipUrls;
    const QStringList bases = candidateBaseUrlsForRepo(repo);

    // Cas particulier IBT : /pub/modsword/raw -> /pub/modsword/rawzip/<Mod>.zip
    if (repo.host.contains("ibt.org.ru", Qt::CaseInsensitive)) {
        zipUrls << QString("ftp://%1/pub/modsword/rawzip/%2.zip").arg(repo.host, modName);
    }

    // Cas particulier STEP Bible : /pub/sword/zip/<Mod>.zip
    if (repo.host.contains("stepbible.org", Qt::CaseInsensitive)) {
        zipUrls << QString("ftp://%1/pub/sword/zip/%2.zip").arg(repo.host, modName);
    }

    for (const QString& base : bases) {
        if (base.startsWith("http", Qt::CaseInsensitive)) {
            zipUrls << QString("%1/packages/%2.zip").arg(base, modName);
            zipUrls << QString("%1/zip/%2.zip").arg(base, modName);
        }
    }
    for (const QString& base : bases) {
        if (base.startsWith("ftp://", Qt::CaseInsensitive)) {
            zipUrls << QString("%1/packages/%2.zip").arg(base, modName);
            zipUrls << QString("%1/zip/%2.zip").arg(base, modName);
        }
    }
    zipUrls.removeDuplicates();
    return zipUrls;
}

// Secours HTTP/HTTPS : télécharge tous les fichiers du dossier DataPath d'un module
bool downloadModuleDirectoryHttp(
    const RemoteRepoInfo& repo,
    const CatalogModuleItem& modItem,
    const QString& destSwordDir,
    const std::atomic<bool>& cancelFlag,
    std::function<void(qint64, qint64)> progressCb)
{
    if (modItem.dataPath.isEmpty()) return false;
    QString relDir = modItem.dataPath.trimmed();
    relDir.replace('\\', '/');
    while (relDir.startsWith("./")) relDir.remove(0, 2);
    while (relDir.startsWith('/')) relDir.remove(0, 1);

    // Pour les dictionnaires / GenBooks, DataPath pointe vers le préfixe de fichier (ex: modules/lexdict/zld/frebailly/dict)
    QString drvLower = modItem.modDrv.toLower();
    if (!relDir.endsWith('/') && (drvLower.contains("ld") || drvLower.contains("genbook"))) {
        int lastSlash = relDir.lastIndexOf('/');
        if (lastSlash > 0) {
            relDir = relDir.left(lastSlash + 1);
        }
    }
    if (!relDir.endsWith('/')) relDir += '/';

    const QStringList bases = candidateBaseUrlsForRepo(repo);
    for (const QString& base : bases) {
        if (!base.startsWith("http", Qt::CaseInsensitive)) continue;
        QString dirUrl = base + "/" + relDir;
        QByteArray htmlBytes;
        if (!downloadUrlToBytes(dirUrl, htmlBytes, cancelFlag, nullptr, 12000)) {
            continue;
        }
        QString html = QString::fromUtf8(htmlBytes);
        static const QRegularExpression rxHref(R"rx(<a\s+href="([^"?/#]+)")rx", QRegularExpression::CaseInsensitiveOption);
        auto it = rxHref.globalMatch(html);
        QStringList files;
        while (it.hasNext()) {
            QString href = it.next().captured(1).trimmed();
            if (href.isEmpty() || href == "." || href == ".." || href.startsWith('/') || href.endsWith('/')) continue;
            if (!files.contains(href)) files << href;
        }
        if (files.isEmpty()) continue;

        QString targetModDir = destSwordDir + "/" + relDir;
        QDir().mkpath(targetModDir);

        int downloaded = 0;
        for (const QString& fn : files) {
            if (cancelFlag.load()) return false;
            QByteArray fileData;
            if (!downloadUrlToBytes(dirUrl + fn, fileData, cancelFlag, progressCb, 20000)) {
                return false;
            }
            QFile outF(targetModDir + fn);
            if (!outF.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            outF.write(fileData);
            outF.close();
            ++downloaded;
        }

        if (downloaded > 0 && !modItem.confFilePath.isEmpty() && QFileInfo::exists(modItem.confFilePath)) {
            QString destConfDir = destSwordDir + "/mods.d";
            QDir().mkpath(destConfDir);
            QString destConfFile = destConfDir + "/" + QFileInfo(modItem.confFilePath).fileName();
            QFile::remove(destConfFile);
            QFile::copy(modItem.confFilePath, destConfFile);
            return true;
        }
    }
    return false;
}

// Parseur rapide et 100% natif Qt d'un fichier .conf de module SWORD
bool parseSwordConfFile(const QString& confFilePath, CatalogModuleItem& item) {
    QFile f(confFilePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

    const QString content = QString::fromUtf8(f.readAll());
    f.close();

    QString modName;
    QMap<QString, QStringList> entries;
    QString lastKey;

    const QStringList lines = content.split('\n');
    for (QString rawLine : lines) {
        if (rawLine.endsWith('\r')) rawLine.chop(1);
        QString trimmed = rawLine.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith(';')) continue;

        if (modName.isEmpty() && trimmed.startsWith('[') && trimmed.contains(']')) {
            modName = trimmed.mid(1, trimmed.indexOf(']') - 1).trimmed();
            continue;
        }

        int eq = rawLine.indexOf('=');
        if (eq > 0 && !rawLine.startsWith(' ') && !rawLine.startsWith('\t')) {
            QString k = rawLine.left(eq).trimmed();
            QString v = rawLine.mid(eq + 1).trimmed();
            if (v.endsWith('\\')) v.chop(1);
            entries[k.toLower()].append(v);
            lastKey = k.toLower();
        } else if (!lastKey.isEmpty() && !entries[lastKey].isEmpty()) {
            QString cont = trimmed;
            if (cont.endsWith('\\')) cont.chop(1);
            entries[lastKey].last() += " " + cont;
        }
    }

    if (modName.isEmpty()) return false;

    auto firstVal = [&](const char* key) -> QString {
        auto it = entries.find(QString::fromLatin1(key).toLower());
        if (it != entries.end() && !it->isEmpty()) return it->first();
        return QString();
    };

    item.name = modName;
    item.confFilePath = confFilePath;
    item.description = firstVal("Description");
    item.modDrv = firstVal("ModDrv");
    item.dataPath = firstVal("DataPath");
    item.version = firstVal("Version");
    item.installSize = firstVal("InstallSize");
    item.aboutText = firstVal("About");
    item.langCode = firstVal("Lang").trimmed().toLower();
    if (item.langCode.isEmpty()) item.langCode = "und";
    item.isCiphered = entries.contains("cipherkey");

    QString explicitType = firstVal("Type");
    QString category = firstVal("Category");
    QString drv = item.modDrv.toLower();

    if (!explicitType.isEmpty()) {
        item.rawType = explicitType;
    } else if (!category.isEmpty() &&
               category.compare("Biblical Texts", Qt::CaseInsensitive) != 0 &&
               category.compare("Commentaries", Qt::CaseInsensitive) != 0 &&
               category.compare("Lexicons / Dictionaries", Qt::CaseInsensitive) != 0) {
        item.rawType = category;
    } else if (drv == "rawtext" || drv == "rawtext4" || drv == "ztext" || drv == "ztext4") {
        item.rawType = "Biblical Texts";
    } else if (drv == "rawcom" || drv == "rawcom4" || drv == "zcom" || drv == "zcom4" || drv == "hrefcom") {
        item.rawType = "Commentaries";
    } else if (drv == "rawld" || drv == "rawld4" || drv == "zld") {
        item.rawType = "Lexicons / Dictionaries";
    } else {
        item.rawType = category.isEmpty() ? QStringLiteral("Generic Books") : category;
    }

    if (item.rawType == "Biblical Texts") {
        item.categoryKey = "BIBLE";
        item.categoryDisplay = "📖 Bible";
    } else if (item.rawType == "Commentaries") {
        item.categoryKey = "COMMENTARY";
        item.categoryDisplay = "📚 Commentaire";
    } else if (item.rawType == "Lexicons / Dictionaries") {
        item.categoryKey = "DICTIONARY";
        item.categoryDisplay = "🏛️ Dictionnaire";
    } else {
        item.categoryKey = "OTHER";
        item.categoryDisplay = "📓 " + item.rawType;
    }

    QStringList feats;
    for (const QString& f : entries.value("feature")) {
        if (!f.isEmpty() && !feats.contains(f)) feats << f;
    }
    for (const QString& gf : entries.value("globaloptionfilter")) {
        if (gf.contains("Strong", Qt::CaseInsensitive) && !feats.contains("Numéros Strong")) {
            feats << "Numéros Strong";
        }
        if (gf.contains("Morph", Qt::CaseInsensitive) && !feats.contains("Morphologie")) {
            feats << "Morphologie";
        }
    }
    item.featuresText = feats.join(", ");
    return true;
}

} // namespace

QString ModuleInstallerDialog::installMgrDirPath() {
    return QDir::homePath() + "/.sword/InstallMgr";
}

void ModuleInstallerDialog::ensureDefaultInstallMgrConf(const QString& installMgrDir) {
    QDir().mkpath(installMgrDir);
    QString confFile = installMgrDir + "/InstallMgr.conf";

    QStringList customLines;
    if (QFileInfo::exists(confFile)) {
        QFile existing(confFile);
        if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(existing.readAll());
            existing.close();
            if (content.contains("CatalogConfigVersion=3")) {
                return;
            }
            static const QSet<QString> builtInCaptions = {
                "CrossWire", "Crosswire", "Crosswire Bible Society",
                "Bible.org", "STEP Bible", "Xiphos", "eBible.org",
                "Lockman Foundation", "CrossWire Wycliffe", "CrossWire Attic",
                "CrossWire Beta", "CrossWire av11n", "CrossWire av11n Attic",
                "Deutsche Bibelgesellschaft", "IBT"
            };
            for (const QString& line : content.split('\n')) {
                QString tr = line.trimmed();
                if (tr.contains("Source=") || tr.contains("PackagePreference=")) {
                    QString afterEq = tr.section('=', 1);
                    QString cap = afterEq.section('|', 0, 0).trimmed();
                    if (!cap.isEmpty() && !builtInCaptions.contains(cap)) {
                        customLines << tr;
                    }
                }
            }
        }
    }

    QFile f(confFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream out(&f);
    out << "[General]\n"
        << "PassiveFTP=true\n"
        << "TimeoutMillis=12000\n"
        << "UnverifiedPeerAllowed=true\n"
        << "CatalogConfigVersion=3\n\n"
        << "[Sources]\n"
        << "HTTPSSource=CrossWire|crosswire.org|/ftpmirror/pub/sword/raw|||20081216195754\n"
        << "HTTPSSource=eBible.org|ebible.org|/sword|||20151110161300\n"
        << "HTTPSource=Xiphos|ftp.xiphos.org|/pub/xiphos|||20090514005900\n"
        << "HTTPSSource=Bible.org|crosswire.org|/ftpmirror/pub/bible.org/sword|||20090514005700\n"
        << "HTTPSSource=Lockman Foundation|crosswire.org|/ftpmirror/pub/sword/lockmanraw|||20190608211800\n"
        << "HTTPSSource=CrossWire Wycliffe|crosswire.org|/ftpmirror/pub/sword/wyclifferaw|||20120711005000\n"
        << "HTTPSSource=CrossWire Attic|crosswire.org|/ftpmirror/pub/sword/atticraw|||20120224005100\n"
        << "HTTPSSource=CrossWire Beta|crosswire.org|/ftpmirror/pub/sword/betaraw|||20090224125400\n"
        << "HTTPSSource=Deutsche Bibelgesellschaft|crosswire.org|/ftpmirror/pub/sword/dbgraw|||20171021160700\n";
    for (const QString& custom : customLines) {
        out << custom << "\n";
    }
    f.close();
}

int ModuleInstallerDialog::languageSortPriority(const QString& rawLangCode) {
    QString c = rawLangCode.trimmed().toLower();
    if (c == "fr") return 1;
    if (c == "es") return 2;
    if (c == "en") return 3;
    if (c == "grc") return 4;
    if (c == "he" || c == "hbo") return 5;
    if (c == "la") return 6;
    if (c == "pt") return 7;
    if (c == "de") return 8;
    if (c == "it") return 9;
    if (c == "ht") return 10;
    if (c == "ru") return 11;
    if (c == "ar") return 12;
    if (c == "zh") return 13;
    if (c == "nl") return 14;
    if (c == "ro") return 15;
    if (c == "pl") return 16;
    if (c == "el") return 17;
    if (c == "mul" || c == "und") return 18;
    return 100;
}

QString ModuleInstallerDialog::languageDisplayLabel(const QString& rawLangCode) {
    QString c = rawLangCode.trimmed().toLower();
    if (c.isEmpty() || c == "und") return "Non spécifié / Universel (und)";
    if (c == "mul") return "Multilingue / Lexiques (mul)";

    static const QMap<QString, QString> known = {
        {"fr",  "Français"},
        {"es",  "Español"},
        {"en",  "English"},
        {"grc", "Grec ancien / Koïné"},
        {"he",  "Hébreu biblique"},
        {"hbo", "Hébreu ancien"},
        {"la",  "Latin"},
        {"pt",  "Português"},
        {"de",  "Deutsch (Allemand)"},
        {"it",  "Italiano"},
        {"ht",  "Créole Haïtien"},
        {"ru",  "Русский (Russe)"},
        {"uk",  "Українська (Ukrainien)"},
        {"ar",  "Arabe (العربية)"},
        {"zh",  "Chinois (中文)"},
        {"nl",  "Nederlands (Néerlandais)"},
        {"ro",  "Română (Roumain)"},
        {"pl",  "Polski (Polonais)"},
        {"el",  "Grec moderne"},
        {"ko",  "Coréen (한국어)"},
        {"ja",  "Japonais (日本語)"},
        {"vi",  "Vietnamien (Tiếng Việt)"},
        {"tr",  "Turc (Türkçe)"},
        {"fa",  "Persan (فارسی)"},
        {"hi",  "Hindi"},
        {"id",  "Indonésien"},
        {"tl",  "Tagalog (Philippines)"},
        {"sw",  "Swahili"},
        {"mg",  "Malgache"},
        {"am",  "Amharique"},
        {"syc", "Syriaque / Araméen"},
        {"cop", "Copte"},
        {"got", "Gothique"},
        {"eo",  "Espéranto"},
        {"ca",  "Catalan"},
        {"eu",  "Basque"},
        {"br",  "Breton"},
        {"co",  "Corse"},
        {"cs",  "Tchèque"},
        {"sk",  "Slovaque"},
        {"hu",  "Hongrois"},
        {"sv",  "Suédois"},
        {"no",  "Norvégien"},
        {"da",  "Danois"},
        {"fi",  "Finnois"},
        {"bg",  "Bulgare"},
        {"sr",  "Serbe"},
        {"hr",  "Croate"},
        {"th",  "Thaï"},
        {"my",  "Birman"},
        {"km",  "Khmer"},
        {"lo",  "Lao"},
        {"af",  "Afrikaans"},
        {"yo",  "Yoruba"},
        {"ha",  "Haoussa"},
        {"ig",  "Igbo"},
        {"zu",  "Zoulou"},
        {"ln",  "Lingala"},
        {"rw",  "Kinyarwanda"},
        {"rn",  "Kirundi"},
        {"wo",  "Wolof"},
        {"bm",  "Bambara"}
    };

    if (known.contains(c)) {
        return QString("%1 [%2]").arg(known.value(c), c);
    }

    QLocale loc(c);
    if (loc.language() != QLocale::C && loc.language() != QLocale::AnyLanguage) {
        QString frLang = QLocale(QLocale::French).languageToString(loc.language());
        if (frLang.isEmpty()) frLang = QLocale::languageToString(loc.language());
        if (!frLang.isEmpty() && frLang.toLower() != "c") {
            return QString("%1 [%2]").arg(frLang, c);
        }
    }

    return QString("Langue [%1]").arg(c);
}

ModuleInstallerDialog::ModuleInstallerDialog(SwordEngine* engine, QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
    , m_installMgrDir(installMgrDirPath())
{
    setWindowTitle("📥 Télécharger & Gérer les Modules Bibliques (Bibles, Commentaires, Strong...)");
    resize(1360, 840);
    setMinimumSize(940, 620);

    ensureDefaultInstallMgrConf(m_installMgrDir);

    // Par défaut, cocher Français, Espagnol, Anglais, Grec ancien, Hébreu et Multilingue/Lexiques
    m_checkedLangs = {"fr", "es", "en", "grc", "he", "hbo", "mul"};

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);

    // Bandeau supérieur explicatif
    auto* topBanner = new QLabel(
        "<b>📚 Bibliothèque Mondiale SWORD (CrossWire, eBible.org, STEP Bible, Xiphos, Bible.org, Lockman...)</b> — "
        "1️⃣ <b>Cochez les langues souhaitées</b> à gauche • "
        "2️⃣ <b>Cochez les modules</b> (Bibles, Commentaires, Dictionnaires Strong) dans le tableau • "
        "3️⃣ Cliquez sur <b>« 📥 Installer les modules cochés »</b>."
    );
    topBanner->setWordWrap(true);
    topBanner->setStyleSheet(
        "background-color: #1E293B; color: #E2E8F0; border: 1px solid #38BDF8; "
        "border-radius: 6px; padding: 8px 12px; font-size: 10.5pt;"
    );
    rootLayout->addWidget(topBanner);

    auto* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setChildrenCollapsible(false);

    // ========================================================================
    // COLONNE DE GAUCHE : 1. Liste des langues à cocher + 2. Sources en ligne
    // ========================================================================
    auto* leftColWidget = new QWidget();
    auto* leftColLayout = new QVBoxLayout(leftColWidget);
    leftColLayout->setContentsMargins(0, 0, 0, 0);
    leftColLayout->setSpacing(8);

    // --- Groupe 1 : Toutes les langues à cocher ---
    auto* langGroup = new QGroupBox("🌍 1. Langues à afficher (Cochez les langues souhaitées)");
    auto* langLayout = new QVBoxLayout(langGroup);
    langLayout->setSpacing(5);

    m_langSearchEdit = new QLineEdit();
    m_langSearchEdit->setPlaceholderText("🔎 Chercher une langue (ex: Français, Español, Grec, Créole, Português...)");
    m_langSearchEdit->setClearButtonEnabled(true);
    connect(m_langSearchEdit, &QLineEdit::textChanged, this, &ModuleInstallerDialog::onLanguageFilterTextChanged);
    langLayout->addWidget(m_langSearchEdit);

    auto* langQuickRow = new QHBoxLayout();
    langQuickRow->setSpacing(4);

    auto* btnDefaultLangs = new QPushButton("⭐ FR + ES + EN + Grec/Hébreu");
    btnDefaultLangs->setIcon(SwordEngine::languageFlagIcon("fr_es"));
    btnDefaultLangs->setIconSize(QSize(22, 15));
    btnDefaultLangs->setCursor(Qt::PointingHandCursor);
    btnDefaultLangs->setToolTip("Cocher uniquement Français, Español, English, Grec ancien et Hébreu");
    btnDefaultLangs->setStyleSheet(
        "QPushButton { background-color: rgba(16, 185, 129, 0.22); color: #34D399; "
        "border: 1px solid #10B981; border-radius: 4px; font-weight: bold; padding: 4px 6px; }"
        "QPushButton:hover { background-color: rgba(16, 185, 129, 0.35); color: #FFFFFF; }"
    );
    connect(btnDefaultLangs, &QPushButton::clicked, this, &ModuleInstallerDialog::onSelectDefaultLanguages);

    auto* btnAllLangs = new QPushButton("☑️ Toutes");
    btnAllLangs->setCursor(Qt::PointingHandCursor);
    btnAllLangs->setToolTip("Cocher toutes les langues disponibles");
    connect(btnAllLangs, &QPushButton::clicked, this, &ModuleInstallerDialog::onSelectAllLanguages);

    auto* btnNoneLangs = new QPushButton("☐ Aucune");
    btnNoneLangs->setCursor(Qt::PointingHandCursor);
    btnNoneLangs->setToolTip("Décocher toutes les langues");
    connect(btnNoneLangs, &QPushButton::clicked, this, &ModuleInstallerDialog::onUnselectAllLanguages);

    langQuickRow->addWidget(btnDefaultLangs, 1);
    langQuickRow->addWidget(btnAllLangs);
    langQuickRow->addWidget(btnNoneLangs);
    langLayout->addLayout(langQuickRow);

    m_langListWidget = new QListWidget();
    m_langListWidget->setAlternatingRowColors(true);
    m_langListWidget->setIconSize(QSize(24, 16));
    connect(m_langListWidget, &QListWidget::itemChanged, this, &ModuleInstallerDialog::onLanguageItemChanged);
    langLayout->addWidget(m_langListWidget, 1);

    m_langSummaryLabel = new QLabel();
    m_langSummaryLabel->setStyleSheet("color: #94A3B8; font-size: 9.5pt;");
    langLayout->addWidget(m_langSummaryLabel);

    leftColLayout->addWidget(langGroup, 3);

    // --- Groupe 2 : Sources / Dépôts en ligne ---
    auto* srcGroup = new QGroupBox("🌐 2. Sites / Dépôts de modules (Sources)");
    auto* srcLayout = new QVBoxLayout(srcGroup);
    srcLayout->setSpacing(5);

    m_sourceListWidget = new QListWidget();
    m_sourceListWidget->setAlternatingRowColors(true);
    connect(m_sourceListWidget, &QListWidget::itemChanged, this, &ModuleInstallerDialog::onSourceItemChanged);
    srcLayout->addWidget(m_sourceListWidget, 1);

    auto* srcBtnRow = new QHBoxLayout();
    m_refreshSourcesBtn = new QPushButton("🔄 Actualiser les catalogues en ligne");
    m_refreshSourcesBtn->setCursor(Qt::PointingHandCursor);
    m_refreshSourcesBtn->setToolTip(
        "Télécharger depuis Internet la dernière liste des modules disponibles pour les dépôts cochés ci-dessus"
    );
    m_refreshSourcesBtn->setStyleSheet(
        "QPushButton { background-color: rgba(2, 132, 199, 0.22); color: #38BDF8; "
        "border: 1px solid #0284C7; border-radius: 5px; font-weight: bold; padding: 5px 8px; }"
        "QPushButton:hover { background-color: rgba(2, 132, 199, 0.38); color: #FFFFFF; }"
    );
    connect(m_refreshSourcesBtn, &QPushButton::clicked, this, &ModuleInstallerDialog::onRefreshSelectedSourcesFromInternet);

    m_addSourceBtn = new QPushButton("➕ Ajouter site");
    m_addSourceBtn->setCursor(Qt::PointingHandCursor);
    m_addSourceBtn->setToolTip("Ajouter un nouveau serveur / dépôt SWORD distant (FTP ou HTTP)");
    connect(m_addSourceBtn, &QPushButton::clicked, this, &ModuleInstallerDialog::onAddCustomSource);

    srcBtnRow->addWidget(m_refreshSourcesBtn, 1);
    srcBtnRow->addWidget(m_addSourceBtn);
    srcLayout->addLayout(srcBtnRow);

    leftColLayout->addWidget(srcGroup, 2);
    mainSplitter->addWidget(leftColWidget);

    // ========================================================================
    // COLONNE DE DROITE : Filtres de catégorie + Tableau des modules + Aperçu
    // ========================================================================
    auto* rightColWidget = new QWidget();
    auto* rightColLayout = new QVBoxLayout(rightColWidget);
    rightColLayout->setContentsMargins(0, 0, 0, 0);
    rightColLayout->setSpacing(6);

    auto* filterGroup = new QGroupBox("🔎 3. Filtrer et cocher les modules à installer");
    auto* filterLayout = new QVBoxLayout(filterGroup);
    filterLayout->setSpacing(6);

    auto* filterTopRow = new QHBoxLayout();
    m_modSearchEdit = new QLineEdit();
    m_modSearchEdit->setPlaceholderText("🔍 Chercher un module par nom ou description (ex: Segond, Reina, Valera, LBLA, Strong, Darby, Martin, TSK...)");
    m_modSearchEdit->setClearButtonEnabled(true);
    connect(m_modSearchEdit, &QLineEdit::textChanged, this, [this](const QString&) {
        rebuildModuleTreeWidget();
    });

    m_starterPackBtn = new QPushButton("⚡ Cocher le Pack d'étude recommandé (FR + ES + Strong)");
    m_starterPackBtn->setCursor(Qt::PointingHandCursor);
    m_starterPackBtn->setToolTip(
        "Coche automatiquement une sélection de Bibles françaises, espagnoles, textes originaux grec/hébreu et dictionnaires Strong"
    );
    m_starterPackBtn->setStyleSheet(
        "QPushButton { background-color: rgba(245, 158, 11, 0.22); color: #FBBF24; "
        "border: 1px solid #D97706; border-radius: 5px; font-weight: bold; padding: 5px 10px; }"
        "QPushButton:hover { background-color: rgba(245, 158, 11, 0.38); color: #FFFFFF; }"
    );
    connect(m_starterPackBtn, &QPushButton::clicked, this, &ModuleInstallerDialog::onSelectRecommendedStarterPack);

    filterTopRow->addWidget(m_modSearchEdit, 1);
    filterTopRow->addWidget(m_starterPackBtn);
    filterLayout->addLayout(filterTopRow);

    auto* catRow = new QHBoxLayout();
    m_chkBibles = new QCheckBox("📖 Bibles");
    m_chkBibles->setChecked(true);
    m_chkCommentaries = new QCheckBox("📚 Commentaires");
    m_chkCommentaries->setChecked(true);
    m_chkDictionaries = new QCheckBox("🏛️ Dictionnaires / Lexiques Strong");
    m_chkDictionaries->setChecked(true);
    m_chkOther = new QCheckBox("📓 Autres (Dévotions / Livres)");
    m_chkOther->setChecked(false);
    m_chkOnlyInstalled = new QCheckBox("✅ Afficher uniquement les modules déjà installés");
    m_chkOnlyInstalled->setChecked(false);

    for (QCheckBox* cb : {m_chkBibles, m_chkCommentaries, m_chkDictionaries, m_chkOther, m_chkOnlyInstalled}) {
        connect(cb, &QCheckBox::toggled, this, [this](bool) {
            rebuildModuleTreeWidget();
        });
    }

    m_modCountLabel = new QLabel();
    m_modCountLabel->setStyleSheet("color: #38BDF8; font-weight: bold;");

    catRow->addWidget(m_chkBibles);
    catRow->addWidget(m_chkCommentaries);
    catRow->addWidget(m_chkDictionaries);
    catRow->addWidget(m_chkOther);
    catRow->addSpacing(12);
    catRow->addWidget(m_chkOnlyInstalled);
    catRow->addStretch();
    catRow->addWidget(m_modCountLabel);
    filterLayout->addLayout(catRow);

    rightColLayout->addWidget(filterGroup);

    // Splitter vertical entre le tableau des modules et la fiche descriptive du module sélectionné
    auto* rightVertSplitter = new QSplitter(Qt::Vertical);
    rightVertSplitter->setChildrenCollapsible(false);

    m_modTree = new QTreeWidget();
    m_modTree->setRootIsDecorated(false);
    m_modTree->setAlternatingRowColors(true);
    m_modTree->setIconSize(QSize(22, 15));
    m_modTree->setSortingEnabled(true);
    m_modTree->setHeaderLabels({
        "☐ Module",
        "État",
        "Catégorie",
        "Langue",
        "Description complète",
        "Dépôt (Source)",
        "Version"
    });
    m_modTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_modTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_modTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_modTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_modTree->header()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_modTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_modTree->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);

    connect(m_modTree, &QTreeWidget::itemChanged, this, &ModuleInstallerDialog::onModuleTreeItemChanged);
    connect(m_modTree, &QTreeWidget::itemSelectionChanged, this, &ModuleInstallerDialog::onModuleSelectionChanged);
    rightVertSplitter->addWidget(m_modTree);

    m_detailsBrowser = new QTextBrowser();
    m_detailsBrowser->setOpenExternalLinks(true);
    m_detailsBrowser->setMaximumHeight(190);
    m_detailsBrowser->setHtml(
        "<div style='color:#94A3B8; padding:6px;'>"
        "👉 Cliquez sur un module dans la liste ci-dessus pour voir sa description détaillée, ses fonctionnalités (numéros Strong, morphologie) et sa licence. "
        "Cochez la case <b>☐</b> à gauche d'un ou plusieurs modules puis cliquez sur <b>« 📥 Installer les modules cochés »</b> en bas."
        "</div>"
    );
    rightVertSplitter->addWidget(m_detailsBrowser);
    rightVertSplitter->setStretchFactor(0, 4);
    rightVertSplitter->setStretchFactor(1, 1);

    rightColLayout->addWidget(rightVertSplitter, 1);
    mainSplitter->addWidget(rightColWidget);

    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 7);
    rootLayout->addWidget(mainSplitter, 1);

    // ========================================================================
    // BARRE INFÉRIEURE : Progression + Boutons Installer / Désinstaller / Fermer
    // ========================================================================
    auto* bottomBar = new QHBoxLayout();
    bottomBar->setSpacing(8);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFixedWidth(220);
    m_progressBar->setVisible(false);

    m_statusLabel = new QLabel("✓ Prêt — Cochez les modules de votre choix puis cliquez sur « 📥 Installer ».");
    m_statusLabel->setStyleSheet("color: #34D399; font-weight: bold;");

    m_installBtn = new QPushButton("📥 Installer les modules cochés (0)");
    m_installBtn->setCursor(Qt::PointingHandCursor);
    m_installBtn->setEnabled(false);
    m_installBtn->setStyleSheet(
        "QPushButton { background-color: #0284C7; color: #FFFFFF; "
        "border: 1px solid #38BDF8; border-radius: 6px; font-weight: bold; font-size: 10.5pt; padding: 7px 14px; }"
        "QPushButton:hover { background-color: #0369A1; }"
        "QPushButton:disabled { background-color: #1E293B; color: #64748B; border-color: #334155; }"
    );
    connect(m_installBtn, &QPushButton::clicked, this, &ModuleInstallerDialog::onInstallCheckedModules);

    m_uninstallBtn = new QPushButton("🗑️ Désinstaller les modules cochés");
    m_uninstallBtn->setCursor(Qt::PointingHandCursor);
    m_uninstallBtn->setEnabled(false);
    m_uninstallBtn->setStyleSheet(
        "QPushButton { background-color: rgba(239, 68, 68, 0.20); color: #FCA5A5; "
        "border: 1px solid #DC2626; border-radius: 6px; font-weight: bold; padding: 7px 12px; }"
        "QPushButton:hover { background-color: rgba(239, 68, 68, 0.35); color: #FFFFFF; }"
        "QPushButton:disabled { background-color: #1E293B; color: #64748B; border-color: #334155; }"
    );
    connect(m_uninstallBtn, &QPushButton::clicked, this, &ModuleInstallerDialog::onUninstallCheckedModules);

    m_cancelBtn = new QPushButton("⏹ Arrêter");
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background-color: #DC2626; color: #FFFFFF; "
        "border: 1px solid #F87171; border-radius: 6px; font-weight: bold; padding: 7px 12px; }"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, &ModuleInstallerDialog::onCancelOperation);

    m_closeBtn = new QPushButton("Fermer");
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(
        "QPushButton { background-color: #334155; color: #F8FAFC; "
        "border: 1px solid #475569; border-radius: 6px; font-weight: bold; padding: 7px 14px; }"
        "QPushButton:hover { background-color: #475569; }"
    );
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    bottomBar->addWidget(m_progressBar);
    bottomBar->addWidget(m_statusLabel, 1);
    bottomBar->addWidget(m_cancelBtn);
    bottomBar->addWidget(m_uninstallBtn);
    bottomBar->addWidget(m_installBtn);
    bottomBar->addWidget(m_closeBtn);
    rootLayout->addLayout(bottomBar);

    loadCatalogFromDisk();
}

ModuleInstallerDialog::~ModuleInstallerDialog() {
    m_cancelRequested = true;
    if (m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        m_workerThread->wait(1500);
    }
}

void ModuleInstallerDialog::reject() {
    m_cancelRequested = true;
    QDialog::reject();
}

void ModuleInstallerDialog::loadCatalogFromDisk() {
    m_updatingUi = true;
    m_repos.clear();
    m_allModules.clear();
    m_langModuleCounts.clear();

    // 1. Identifier les modules déjà installés localement dans ~/.sword
    QMap<QString, QString> installedVersions;
    QSet<QString> seenModuleNames;

    QString destSwordDir = m_engine ? m_engine->swordPath() : (QDir::homePath() + "/.sword");
    QDir localModsDir(destSwordDir + "/mods.d");
    for (const QFileInfo& fi : localModsDir.entryInfoList({"*.conf"}, QDir::Files)) {
        CatalogModuleItem locItem;
        if (parseSwordConfFile(fi.absoluteFilePath(), locItem) && !locItem.name.isEmpty()) {
            installedVersions.insert(locItem.name, locItem.version);
        }
    }

    SWMgr* localMgr = m_engine ? m_engine->rawMgr() : nullptr;
    if (localMgr) {
        for (const auto& pair : localMgr->Modules) {
            SWModule* mod = pair.second;
            if (!mod) continue;
            QString name = QString::fromUtf8(mod->getName());
            QString ver = QString::fromUtf8(mod->getConfigEntry("Version") ? mod->getConfigEntry("Version") : "");
            if (!installedVersions.contains(name)) {
                installedVersions.insert(name, ver);
            }
        }
    }

    // 2. Lire directement InstallMgr.conf en Qt (compatible chemins Unicode Windows & Linux)
    bool firstLoadSources = m_checkedSources.isEmpty();
    int totalRemoteCached = 0;

    QFile confFile(m_installMgrDir + "/InstallMgr.conf");
    if (confFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(confFile.readAll()).split('\n');
        confFile.close();

        QSet<QString> seenCaptions;
        for (const QString& rawLine : lines) {
            QString tr = rawLine.trimmed();
            if (tr.isEmpty() || tr.startsWith('#') || tr.startsWith('[') || !tr.contains('=')) continue;

            QString key = tr.section('=', 0, 0).trimmed();
            QString val = tr.section('=', 1).trimmed();
            if (!key.endsWith("Source", Qt::CaseInsensitive) &&
                !key.endsWith("PackagePreference", Qt::CaseInsensitive)) {
                continue;
            }

            const QStringList parts = val.split('|');
            if (parts.size() < 3) continue;

            RemoteRepoInfo repo;
            repo.caption = parts[0].trimmed();
            if (repo.caption.isEmpty() || repo.caption == "Crosswire" || seenCaptions.contains(repo.caption)) {
                continue;
            }
            seenCaptions.insert(repo.caption);

            if (key.startsWith("HTTPS", Qt::CaseInsensitive)) repo.type = "HTTPS";
            else if (key.startsWith("HTTP", Qt::CaseInsensitive)) repo.type = "HTTP";
            else repo.type = "FTP";

            repo.host = parts[1].trimmed();
            repo.directory = parts[2].trimmed();
            repo.uid = (parts.size() >= 6 && !parts[5].trimmed().isEmpty()) ? parts[5].trimmed() : repo.host;

            QDir repoModsDir(m_installMgrDir + "/" + repo.uid + "/mods.d");
            const QFileInfoList confFiles = repoModsDir.entryInfoList({"*.conf"}, QDir::Files, QDir::Name);
            for (const QFileInfo& fi : confFiles) {
                if (fi.fileName().compare("globals.conf", Qt::CaseInsensitive) == 0) continue;
                CatalogModuleItem item;
                if (!parseSwordConfFile(fi.absoluteFilePath(), item)) continue;
                ++repo.cachedModCount;
                ++totalRemoteCached;

                if (seenModuleNames.contains(item.name)) continue;
                seenModuleNames.insert(item.name);

                item.langDisplay = languageDisplayLabel(item.langCode);
                item.sourceCaption = repo.caption;
                item.isInstalled = installedVersions.contains(item.name);
                if (item.isInstalled && !item.version.isEmpty() &&
                    !installedVersions.value(item.name).isEmpty() &&
                    installedVersions.value(item.name) != item.version) {
                    item.hasUpdate = true;
                }

                m_allModules.push_back(item);
                m_langModuleCounts[item.langCode] += 1;
            }

            m_repos.push_back(repo);
            if (firstLoadSources) {
                m_checkedSources.insert(repo.caption);
            }
        }
    }

    // 3. Ajouter aussi les modules installés localement qui ne figurent dans aucun catalogue distant
    for (const QFileInfo& fi : localModsDir.entryInfoList({"*.conf"}, QDir::Files, QDir::Name)) {
        CatalogModuleItem item;
        if (!parseSwordConfFile(fi.absoluteFilePath(), item)) continue;
        if (seenModuleNames.contains(item.name)) continue;
        seenModuleNames.insert(item.name);

        item.langDisplay = languageDisplayLabel(item.langCode);
        item.sourceCaption = "Installé localement";
        item.isInstalled = true;
        m_allModules.push_back(item);
        m_langModuleCounts[item.langCode] += 1;
    }
    if (firstLoadSources) {
        m_checkedSources.insert("Installé localement");
    }

    // 4. Remplir la liste des sources distantes
    m_sourceListWidget->clear();
    for (const auto& repo : m_repos) {
        auto* sItem = new QListWidgetItem(
            QString("%1 (%2 modules) — %3").arg(repo.caption).arg(repo.cachedModCount).arg(repo.host)
        );
        sItem->setData(Qt::UserRole, repo.caption);
        sItem->setFlags(sItem->flags() | Qt::ItemIsUserCheckable);
        sItem->setCheckState(m_checkedSources.contains(repo.caption) ? Qt::Checked : Qt::Unchecked);
        sItem->setToolTip(QString("Serveur : %1%2 (%3)").arg(repo.host, repo.directory, repo.type));
        m_sourceListWidget->addItem(sItem);
    }

    m_updatingUi = false;
    rebuildLanguageListWidget();
    rebuildModuleTreeWidget();

    // 5. Si aucun catalogue distant n'est encore en cache (premier lancement sous Windows/Linux),
    //    lancer automatiquement l'actualisation en ligne !
    if (totalRemoteCached == 0 && !m_autoRefreshTriggered && !m_isBusy) {
        m_autoRefreshTriggered = true;
        QTimer::singleShot(250, this, &ModuleInstallerDialog::onRefreshSelectedSourcesFromInternet);
    }
}

void ModuleInstallerDialog::rebuildLanguageListWidget() {
    m_updatingUi = true;
    m_langListWidget->clear();

    QString filter = m_langSearchEdit ? m_langSearchEdit->text().trimmed().toLower() : QString();

    QStringList langCodes = m_langModuleCounts.keys();
    std::sort(langCodes.begin(), langCodes.end(), [](const QString& a, const QString& b) {
        int pa = languageSortPriority(a);
        int pb = languageSortPriority(b);
        if (pa != pb) return pa < pb;
        return languageDisplayLabel(a).localeAwareCompare(languageDisplayLabel(b)) < 0;
    });

    int shownCount = 0;
    for (const QString& code : langCodes) {
        int count = m_langModuleCounts.value(code, 0);
        if (count <= 0) continue;

        QString disp = languageDisplayLabel(code);
        QString label = QString("%1 — %2 module%3").arg(disp).arg(count).arg(count > 1 ? "s" : "");

        if (!filter.isEmpty() && !label.toLower().contains(filter) && !code.contains(filter)) {
            continue;
        }

        auto* item = new QListWidgetItem(SwordEngine::languageFlagIcon(code), label);
        item->setData(Qt::UserRole, code);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        bool checked = m_checkedLangs.contains(code);
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        if (languageSortPriority(code) <= 10) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
        m_langListWidget->addItem(item);
        ++shownCount;
    }

    if (m_langSummaryLabel) {
        m_langSummaryLabel->setText(
            QString("✓ %1 langue(s) cochée(s) sur %2 langues disponibles (%3 modules au total)")
                .arg(m_checkedLangs.size())
                .arg(langCodes.size())
                .arg(m_allModules.size())
        );
    }

    m_updatingUi = false;
}

void ModuleInstallerDialog::rebuildModuleTreeWidget() {
    if (!m_modTree) return;
    m_updatingUi = true;
    m_modTree->setSortingEnabled(false);
    m_modTree->clear();

    QString search = m_modSearchEdit ? m_modSearchEdit->text().trimmed().toLower() : QString();
    bool showBibles = m_chkBibles && m_chkBibles->isChecked();
    bool showComms = m_chkCommentaries && m_chkCommentaries->isChecked();
    bool showDicts = m_chkDictionaries && m_chkDictionaries->isChecked();
    bool showOther = m_chkOther && m_chkOther->isChecked();
    bool onlyInstalled = m_chkOnlyInstalled && m_chkOnlyInstalled->isChecked();

    int visibleCount = 0;
    int installedVisibleCount = 0;

    for (int idx = 0; idx < m_allModules.size(); ++idx) {
        const auto& mod = m_allModules[idx];

        if (!m_checkedLangs.contains(mod.langCode)) continue;
        if (!m_checkedSources.contains(mod.sourceCaption) && mod.sourceCaption != "Installé localement") continue;

        if (mod.categoryKey == "BIBLE" && !showBibles) continue;
        if (mod.categoryKey == "COMMENTARY" && !showComms) continue;
        if (mod.categoryKey == "DICTIONARY" && !showDicts) continue;
        if (mod.categoryKey == "OTHER" && !showOther) continue;

        if (onlyInstalled && !mod.isInstalled) continue;

        if (!search.isEmpty()) {
            bool match = mod.name.toLower().contains(search) ||
                         mod.description.toLower().contains(search) ||
                         mod.langDisplay.toLower().contains(search) ||
                         mod.sourceCaption.toLower().contains(search) ||
                         mod.featuresText.toLower().contains(search);
            if (!match) continue;
        }

        auto* item = new QTreeWidgetItem();
        item->setData(0, Qt::UserRole, idx);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        bool isChecked = m_checkedModulesToSource.contains(mod.name);
        item->setCheckState(0, isChecked ? Qt::Checked : Qt::Unchecked);

        const QIcon flagIcon = SwordEngine::languageFlagIcon(mod.langCode);
        item->setIcon(0, flagIcon);
        item->setText(0, mod.name);
        if (mod.isInstalled) {
            item->setText(1, mod.hasUpdate ? "⬆️ Mise à jour" : "✅ Installé");
            item->setForeground(1, QColor(mod.hasUpdate ? "#FBBF24" : "#34D399"));
            ++installedVisibleCount;
        } else {
            item->setText(1, mod.isCiphered ? "🔒 Clé requise" : "🆕 Disponible");
            item->setForeground(1, QColor("#38BDF8"));
        }

        item->setText(2, mod.categoryDisplay);
        item->setIcon(3, flagIcon);
        item->setText(3, mod.langDisplay);
        item->setText(4, mod.description);
        item->setText(5, mod.sourceCaption);
        item->setText(6, mod.version);

        m_modTree->addTopLevelItem(item);
        ++visibleCount;
    }

    m_modTree->setSortingEnabled(true);
    m_modTree->sortItems(3, Qt::AscendingOrder);

    if (m_modCountLabel) {
        m_modCountLabel->setText(
            QString("📊 %1 modules affichés (%2 déjà installés) • %3 coché(s)")
                .arg(visibleCount)
                .arg(installedVisibleCount)
                .arg(m_checkedModulesToSource.size())
        );
    }

    m_updatingUi = false;
    updateActionButtonsState();
}

void ModuleInstallerDialog::onLanguageFilterTextChanged(const QString&) {
    rebuildLanguageListWidget();
}

void ModuleInstallerDialog::onSelectDefaultLanguages() {
    m_checkedLangs = {"fr", "es", "en", "grc", "he", "hbo", "mul"};
    rebuildLanguageListWidget();
    rebuildModuleTreeWidget();
}

void ModuleInstallerDialog::onSelectAllLanguages() {
    for (auto it = m_langModuleCounts.begin(); it != m_langModuleCounts.end(); ++it) {
        if (it.value() > 0) m_checkedLangs.insert(it.key());
    }
    rebuildLanguageListWidget();
    rebuildModuleTreeWidget();
}

void ModuleInstallerDialog::onUnselectAllLanguages() {
    m_checkedLangs.clear();
    rebuildLanguageListWidget();
    rebuildModuleTreeWidget();
}

void ModuleInstallerDialog::onLanguageItemChanged(QListWidgetItem* item) {
    if (m_updatingUi || !item) return;
    QString code = item->data(Qt::UserRole).toString();
    if (item->checkState() == Qt::Checked) {
        m_checkedLangs.insert(code);
    } else {
        m_checkedLangs.remove(code);
    }
    if (m_langSummaryLabel) {
        m_langSummaryLabel->setText(
            QString("✓ %1 langue(s) cochée(s) sur %2 langues disponibles")
                .arg(m_checkedLangs.size())
                .arg(m_langModuleCounts.size())
        );
    }
    rebuildModuleTreeWidget();
}

void ModuleInstallerDialog::onSourceItemChanged(QListWidgetItem* item) {
    if (m_updatingUi || !item) return;
    QString src = item->data(Qt::UserRole).toString();
    if (item->checkState() == Qt::Checked) {
        m_checkedSources.insert(src);
    } else {
        m_checkedSources.remove(src);
    }
    rebuildModuleTreeWidget();
}

void ModuleInstallerDialog::onModuleTreeItemChanged(QTreeWidgetItem* item, int column) {
    if (m_updatingUi || !item || column != 0) return;
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_allModules.size()) return;
    const auto& mod = m_allModules[idx];

    if (item->checkState(0) == Qt::Checked) {
        m_checkedModulesToSource.insert(mod.name, mod.sourceCaption);
    } else {
        m_checkedModulesToSource.remove(mod.name);
    }
    updateActionButtonsState();
}

void ModuleInstallerDialog::onModuleSelectionChanged() {
    auto items = m_modTree->selectedItems();
    if (items.isEmpty()) return;
    int idx = items.first()->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_allModules.size()) return;
    const auto& mod = m_allModules[idx];

    QString aboutClean = mod.aboutText;
    aboutClean.replace("\\par", "<br>");

    QString html = QString(R"(
        <div style="font-family:'Noto Sans',sans-serif; font-size:10pt; color:#E2E8F0;">
            <div style="font-size:11.5pt; font-weight:bold; color:#38BDF8; margin-bottom:4px;">
                %1 — %2
            </div>
            <div style="color:#FBBF24; margin-bottom:6px;">
                <b>Catégorie :</b> %3 &nbsp;|&nbsp;
                <b>Langue :</b> %4 &nbsp;|&nbsp;
                <b>Dépôt :</b> %5 &nbsp;|&nbsp;
                <b>Version :</b> %6
                %7
            </div>
            %8
            <div style="color:#CBD5E1; margin-top:6px;">%9</div>
        </div>
    )")
        .arg(mod.name.toHtmlEscaped(),
             mod.description.toHtmlEscaped(),
             mod.categoryDisplay.toHtmlEscaped(),
             QString("%1 %2").arg(SwordEngine::languageFlagHtml(mod.langCode), mod.langDisplay.toHtmlEscaped()),
             mod.sourceCaption.toHtmlEscaped(),
             mod.version.isEmpty() ? "1.0" : mod.version.toHtmlEscaped(),
             mod.installSize.isEmpty() ? "" : QString(" &nbsp;|&nbsp; <b>Taille :</b> %1 Ko").arg(mod.installSize.toInt() / 1024),
             mod.featuresText.isEmpty() ? "" : QString("<div style='color:#34D399;'><b>Fonctionnalités :</b> %1</div>").arg(mod.featuresText.toHtmlEscaped()),
             aboutClean.isEmpty() ? "<i>Aucune note complémentaire fournie par le dépôt.</i>" : aboutClean);

    m_detailsBrowser->setHtml(html);
}

void ModuleInstallerDialog::updateActionButtonsState() {
    int count = m_checkedModulesToSource.size();
    m_installBtn->setText(QString("📥 Installer / Mettre à jour les modules cochés (%1)").arg(count));
    m_installBtn->setEnabled(count > 0);
    m_uninstallBtn->setEnabled(count > 0);

    if (m_modCountLabel) {
        m_modCountLabel->setText(
            QString("📊 %1 modules affichés • %2 coché(s)")
                .arg(m_modTree ? m_modTree->topLevelItemCount() : 0)
                .arg(count)
        );
    }
}

void ModuleInstallerDialog::onSelectRecommendedStarterPack() {
    // S'assurer que FR, ES, EN, Grec et Hébreu sont cochés
    m_checkedLangs.insert("fr");
    m_checkedLangs.insert("es");
    m_checkedLangs.insert("en");
    m_checkedLangs.insert("grc");
    m_checkedLangs.insert("he");
    m_checkedLangs.insert("mul");

    static const QSet<QString> recommended = {
        "FreSegond", "FreSegond1910", "FreLSG", "FreJND", "FreCrampon", "FreMartin", "FreBDM1744",
        "FreStrongsGreek", "FreStrongsHebrew", "FreBailly", "FreJer",
        "SpaRV", "SpaRV1909", "SpaRVG", "LBLA",
        "KJV", "TR", "Byz", "WLC", "LXX",
        "StrongsGreek", "StrongsHebrew", "Robinson", "TSK", "MHCC"
    };

    int newlyChecked = 0;
    for (const auto& mod : m_allModules) {
        if (recommended.contains(mod.name) && !mod.isInstalled) {
            m_checkedModulesToSource.insert(mod.name, mod.sourceCaption);
            ++newlyChecked;
        }
    }

    // Si tous les recommandés sont déjà installés, les cocher quand même pour montrer le pack
    if (newlyChecked == 0) {
        for (const auto& mod : m_allModules) {
            if (recommended.contains(mod.name)) {
                m_checkedModulesToSource.insert(mod.name, mod.sourceCaption);
                ++newlyChecked;
            }
        }
    }

    rebuildLanguageListWidget();
    rebuildModuleTreeWidget();
    m_statusLabel->setText(
        QString("⚡ Pack d'étude recommandé sélectionné (%1 modules FR / ES / Grec / Hébreu / Strong) — Cliquez sur « 📥 Installer » !")
            .arg(m_checkedModulesToSource.size())
    );
}

void ModuleInstallerDialog::onAddCustomSource() {
    QDialog dlg(this);
    dlg.setWindowTitle("➕ Ajouter un dépôt SWORD distant");
    auto* form = new QFormLayout(&dlg);

    auto* nameEdit = new QLineEdit("Mon Dépôt SWORD");
    auto* typeCombo = new QComboBox();
    typeCombo->addItem("HTTPSSource (HTTPS sécurisé — Recommandé)", "HTTPSSource");
    typeCombo->addItem("HTTPSource (HTTP standard)", "HTTPSource");
    typeCombo->addItem("FTPSource (FTP standard)", "FTPSource");
    auto* hostEdit = new QLineEdit("crosswire.org");
    auto* dirEdit = new QLineEdit("/ftpmirror/pub/sword/raw");

    form->addRow("Nom du dépôt :", nameEdit);
    form->addRow("Protocole :", typeCombo);
    form->addRow("Serveur (Hôte) :", hostEdit);
    form->addRow("Dossier sur le serveur :", dirEdit);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(box);

    if (dlg.exec() != QDialog::Accepted) return;

    QString cap = nameEdit->text().trimmed();
    QString tp = typeCombo->currentData().toString();
    QString host = hostEdit->text().trimmed();
    QString dir = dirEdit->text().trimmed();
    if (cap.isEmpty() || host.isEmpty() || dir.isEmpty()) return;

    QString confFile = m_installMgrDir + "/InstallMgr.conf";
    QFile f(confFile);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out << tp << "=" << cap << "|" << host << "|" << dir << "|||" << host << "\n";
        f.close();
    }

    m_checkedSources.insert(cap);
    loadCatalogFromDisk();
    m_statusLabel->setText(QString("✓ Dépôt « %1 » ajouté. Cliquez sur « 🔄 Actualiser les catalogues en ligne » pour charger ses modules.").arg(cap));
}

void ModuleInstallerDialog::setBusyState(bool busy, const QString& statusMsg) {
    m_isBusy = busy;
    m_refreshSourcesBtn->setEnabled(!busy);
    m_addSourceBtn->setEnabled(!busy);
    m_starterPackBtn->setEnabled(!busy);
    m_installBtn->setEnabled(!busy && !m_checkedModulesToSource.isEmpty());
    m_uninstallBtn->setEnabled(!busy && !m_checkedModulesToSource.isEmpty());
    m_cancelBtn->setVisible(busy);
    m_progressBar->setVisible(busy);
    if (!statusMsg.isEmpty()) {
        m_statusLabel->setText(statusMsg);
    }
}

void ModuleInstallerDialog::onCancelOperation() {
    m_cancelRequested = true;
    m_statusLabel->setText("⏹ Arrêt immédiat du transfert...");
}

void ModuleInstallerDialog::onRefreshSelectedSourcesFromInternet() {
    if (m_isBusy) return;

    QVector<RemoteRepoInfo> reposToRefresh;
    for (const auto& repo : m_repos) {
        if (m_checkedSources.contains(repo.caption)) {
            reposToRefresh.push_back(repo);
        }
    }

    if (reposToRefresh.isEmpty()) {
        QMessageBox::information(this, "Aucun dépôt coché", "Veuillez cocher au moins un dépôt dans la liste « 🌐 2. Sites / Dépôts de modules ».");
        return;
    }

    m_cancelRequested = false;
    setBusyState(true, "⏳ Connexion HTTPS/HTTP aux dépôts en ligne et actualisation des catalogues...");
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(2);
    QCoreApplication::processEvents();

    QPointer<ModuleInstallerDialog> self(this);
    const QString installMgrDir = m_installMgrDir;
    int done = 0;
    int okCount = 0;
    const int total = reposToRefresh.size();

    for (const RemoteRepoInfo& repo : reposToRefresh) {
        if (!self || m_cancelRequested.load()) break;

        const QString cap = repo.caption;
        int startPct = std::max(2, int((done * 100) / std::max(1, total)));
        m_statusLabel->setText(QString("🔄 Actualisation du catalogue « %1 » (%2/%3)...").arg(cap).arg(done + 1).arg(total));
        m_progressBar->setValue(startPct);
        QCoreApplication::processEvents();

        bool repoUpdated = false;
        const QString targetModsDir = installMgrDir + "/" + repo.uid + "/mods.d";
        const QStringList baseUrls = candidateBaseUrlsForRepo(repo);

        for (const QString& baseUrl : baseUrls) {
            if (!self || m_cancelRequested.load()) break;
            QString tarGzUrl = baseUrl + "/mods.d.tar.gz";

            QByteArray gzBytes;
            auto progCb = [self, cap, done, total](qint64 dlNow, qint64 dlTotal) {
                if (!self) return;
                if (dlTotal > 0) {
                    int repoPct = int((dlNow * 100LL) / dlTotal);
                    int overallPct = int(((done * 100) + repoPct) / std::max(1, total));
                    self->m_statusLabel->setText(
                        QString("🔄 Actualisation de « %1 » (%2/%3) — %4 / %5 Ko")
                            .arg(cap)
                            .arg(done + 1)
                            .arg(total)
                            .arg(dlNow / 1024)
                            .arg(dlTotal / 1024)
                    );
                    self->m_progressBar->setValue(std::clamp(overallPct, 2, 99));
                }
            };

            if (downloadUrlToBytes(tarGzUrl, gzBytes, m_cancelRequested, progCb, 12000)) {
                if (!self || m_cancelRequested.load()) break;
                if (extractTarGzToModsDir(gzBytes, targetModsDir)) {
                    repoUpdated = true;
                    break;
                }
            }
        }

        if (!self) return;

        if (repoUpdated) {
            ++okCount;
            // Mettre à jour en direct le compteur de modules du dépôt dans la liste de gauche
            QDir repoModsDir(targetModsDir);
            int modCount = std::max(0, int(repoModsDir.entryInfoList({"*.conf"}, QDir::Files).size()) - 1);
            for (int r = 0; r < m_sourceListWidget->count(); ++r) {
                QListWidgetItem* sItem = m_sourceListWidget->item(r);
                if (sItem && sItem->data(Qt::UserRole).toString() == cap) {
                    sItem->setText(QString("%1 (%2 modules) — %3").arg(cap).arg(modCount).arg(repo.host));
                    break;
                }
            }
        }
        ++done;
        m_progressBar->setValue(std::clamp(int((done * 100) / std::max(1, total)), 5, 100));
        QCoreApplication::processEvents();
    }

    if (!self) return;

    const bool wasCancelled = m_cancelRequested.load();
    loadCatalogFromDisk();
    if (wasCancelled) {
        setBusyState(
            false,
            QString("⏹ Actualisation interrompue (%1/%2 dépôts mis à jour — %3 modules disponibles).")
                .arg(okCount)
                .arg(total)
                .arg(m_allModules.size())
        );
    } else {
        setBusyState(
            false,
            QString("✅ Catalogues actualisés (%1/%2 dépôts en ligne mis à jour — %3 modules disponibles).")
                .arg(okCount)
                .arg(total)
                .arg(m_allModules.size())
        );
    }
}

void ModuleInstallerDialog::onInstallCheckedModules() {
    if (m_isBusy || m_checkedModulesToSource.isEmpty()) return;

    struct InstallTask {
        CatalogModuleItem modItem;
        RemoteRepoInfo repo;
    };

    QVector<InstallTask> tasks;
    for (auto it = m_checkedModulesToSource.begin(); it != m_checkedModulesToSource.end(); ++it) {
        const QString modName = it.key();
        const QString srcCap = it.value();
        if (srcCap == "Installé localement") continue;

        CatalogModuleItem foundMod;
        foundMod.name = modName;
        foundMod.sourceCaption = srcCap;
        for (const auto& m : m_allModules) {
            if (m.name == modName) {
                foundMod = m;
                break;
            }
        }

        RemoteRepoInfo foundRepo;
        foundRepo.caption = srcCap;
        for (const auto& r : m_repos) {
            if (r.caption == foundMod.sourceCaption || r.caption == srcCap) {
                foundRepo = r;
                break;
            }
        }
        if (!foundRepo.host.isEmpty()) {
            tasks.push_back({foundMod, foundRepo});
        }
    }

    if (tasks.isEmpty()) {
        QMessageBox::information(this, "Déjà installés", "Les modules cochés sont uniquement des modules locaux déjà installés.");
        return;
    }

    m_cancelRequested = false;
    setBusyState(true, QString("📥 Téléchargement et installation de %1 module(s)...").arg(tasks.size()));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(2);
    QCoreApplication::processEvents();

    QPointer<ModuleInstallerDialog> self(this);
    const QString destSwordDir = m_engine ? m_engine->swordPath() : (QDir::homePath() + "/.sword");

    QDir().mkpath(destSwordDir + "/mods.d");
    QDir().mkpath(destSwordDir + "/modules");

    int successCount = 0;
    QStringList installedNames;
    const int total = tasks.size();

    for (int i = 0; i < total; ++i) {
        if (!self || m_cancelRequested.load()) break;
        const CatalogModuleItem& modItem = tasks[i].modItem;
        const RemoteRepoInfo& repo = tasks[i].repo;
        const QString modName = modItem.name;
        const QString srcCap = repo.caption;

        m_statusLabel->setText(
            QString("📥 Téléchargement de « %1 » depuis %2 (%3/%4)...")
                .arg(modName, srcCap)
                .arg(i + 1)
                .arg(total)
        );
        m_progressBar->setValue(std::max(2, int((i * 100) / std::max(1, total))));
        QCoreApplication::processEvents();

        auto progCb = [self, modName, srcCap, i, total](qint64 dlNow, qint64 dlTotal) {
            if (!self) return;
            if (dlTotal > 0) {
                int modPct = int((dlNow * 100LL) / dlTotal);
                int overallPct = int(((i * 100) + modPct) / std::max(1, total));
                self->m_statusLabel->setText(
                    QString("📥 Téléchargement de « %1 » depuis %2 (%3/%4) — %5 / %6 Ko")
                        .arg(modName, srcCap)
                        .arg(i + 1)
                        .arg(total)
                        .arg(dlNow / 1024)
                        .arg(dlTotal / 1024)
                );
                self->m_progressBar->setValue(std::clamp(overallPct, 2, 99));
            }
        };

        bool installedOk = false;

        // Étape 1 (Prioritaire) : Téléchargement de l'archive .zip du module via HTTPS/HTTP
        const QStringList zipUrls = candidateZipUrlsForModule(repo, modName);
        for (const QString& zipUrl : zipUrls) {
            if (!self || m_cancelRequested.load()) break;
            QByteArray zipBytes;
            if (downloadUrlToBytes(zipUrl, zipBytes, m_cancelRequested, progCb, 20000)) {
                if (!self || m_cancelRequested.load()) break;
                if (extractZipBufferToDir(zipBytes, destSwordDir)) {
                    // S'assurer que le fichier .conf est bien présent dans destSwordDir/mods.d
                    if (!modItem.confFilePath.isEmpty() && QFileInfo::exists(modItem.confFilePath)) {
                        QString destConfFile = destSwordDir + "/mods.d/" + QFileInfo(modItem.confFilePath).fileName();
                        if (!QFileInfo::exists(destConfFile)) {
                            QFile::copy(modItem.confFilePath, destConfFile);
                        }
                    }
                    installedOk = true;
                    break;
                }
            }
        }

        // Étape 2 (Secours) : Téléchargement direct des fichiers du dossier DataPath en HTTP/HTTPS
        if (!installedOk && self && !m_cancelRequested.load()) {
            installedOk = downloadModuleDirectoryHttp(repo, modItem, destSwordDir, m_cancelRequested, progCb);
        }

        if (installedOk) {
            ++successCount;
            installedNames << modName;
        }
    }

    if (!self) return;

    m_checkedModulesToSource.clear();
    if (m_engine) {
        m_engine->reloadModules();
    }
    loadCatalogFromDisk();
    if (successCount > 0) {
        setBusyState(
            false,
            QString("✅ %1/%2 module(s) installé(s) avec succès (%3) — Disponibles immédiatement dans vos fenêtres !")
                .arg(successCount)
                .arg(total)
                .arg(installedNames.join(", "))
        );
        emit modulesChanged();
    } else if (m_cancelRequested.load()) {
        setBusyState(false, "⏹ Installation interrompue.");
    } else {
        setBusyState(
            false,
            "⚠️ Échec du téléchargement des modules sélectionnés. Vérifiez votre connexion Internet puis réessayez."
        );
    }
}

void ModuleInstallerDialog::onUninstallCheckedModules() {
    if (m_checkedModulesToSource.isEmpty()) return;

    QStringList modNames = m_checkedModulesToSource.keys();
    auto ans = QMessageBox::question(
        this,
        "Confirmer la désinstallation",
        QString("Voulez-vous vraiment désinstaller les %1 module(s) coché(s) :\n%2 ?")
            .arg(modNames.size())
            .arg(modNames.join(", ")),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (ans != QMessageBox::Yes) return;

    QString destSwordDir = m_engine ? m_engine->swordPath() : (QDir::homePath() + "/.sword");
    QDir localModsDir(destSwordDir + "/mods.d");

    int removed = 0;
    for (const QString& modName : modNames) {
        bool removedOne = false;
        for (const QFileInfo& fi : localModsDir.entryInfoList({"*.conf"}, QDir::Files)) {
            CatalogModuleItem item;
            if (!parseSwordConfFile(fi.absoluteFilePath(), item)) continue;
            if (item.name.compare(modName, Qt::CaseInsensitive) != 0) continue;

            if (!item.dataPath.isEmpty()) {
                QString relDir = item.dataPath.trimmed();
                relDir.replace('\\', '/');
                while (relDir.startsWith("./")) relDir.remove(0, 2);
                while (relDir.startsWith('/')) relDir.remove(0, 1);

                QString drvLower = item.modDrv.toLower();
                if (!relDir.endsWith('/') && (drvLower.contains("ld") || drvLower.contains("genbook"))) {
                    int lastSlash = relDir.lastIndexOf('/');
                    if (lastSlash > 0) {
                        relDir = relDir.left(lastSlash + 1);
                    }
                }
                if (relDir.startsWith("modules/") && !relDir.contains("..")) {
                    QDir dataDir(destSwordDir + "/" + relDir);
                    if (dataDir.exists()) {
                        dataDir.removeRecursively();
                    }
                }
            }
            if (QFile::remove(fi.absoluteFilePath())) {
                removedOne = true;
            }
        }
        if (removedOne) {
            ++removed;
        }
    }

    m_checkedModulesToSource.clear();
    if (m_engine) {
        m_engine->reloadModules();
    }
    loadCatalogFromDisk();
    m_statusLabel->setText(QString("🗑️ %1 module(s) désinstallé(s).").arg(removed));
    if (removed > 0) {
        emit modulesChanged();
    }
}

