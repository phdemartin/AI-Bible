#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>
#include <QThread>
#include <QWidget>
#include <algorithm>

namespace {

class StartupSplashScreen : public QWidget {
public:
    explicit StartupSplashScreen(const QPixmap& splashPix)
        : QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground, false);
        const int targetW = 960;
        const int targetH = 524; // Exact 2816:1536 aspect ratio
        setFixedSize(targetW, targetH);

        if (!splashPix.isNull()) {
            m_bg = splashPix.scaled(targetW, targetH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        } else {
            m_bg = QPixmap(targetW, targetH);
            m_bg.fill(QColor("#0B1628"));
        }

        if (QScreen* scr = QGuiApplication::primaryScreen()) {
            const QRect avail = scr->availableGeometry();
            move(avail.center() - QPoint(targetW / 2, targetH / 2));
        }
    }

    void updateProgress(int percent, const QString& statusText) {
        m_percent = std::clamp(percent, 0, 100);
        if (!statusText.isEmpty()) {
            m_statusText = statusText;
        }
        repaint();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const int W = width();
        const int H = height();

        // 1. Fond de l'écran de démarrage (Splash_screen.png)
        p.drawPixmap(0, 0, m_bg);

        // 2. Coordonnées exactes de l'intérieur du cadre doré de la barre d'avancement dans Splash_screen.png
        // Cadre doré : x=[0.2937, 0.7056], y=[0.9004, 0.9401]
        const qreal barLeft   = W * 0.2955;
        const qreal barRight  = W * 0.7038;
        const qreal barTop    = H * 0.9040;
        const qreal barBottom = H * 0.9365;
        const qreal barMaxW   = barRight - barLeft;
        const qreal barH      = barBottom - barTop;
        const qreal radius    = barH / 2.0;

        // Fond intérieur légèrement assombri pour un contraste optimal
        QRectF innerTrackRect(barLeft, barTop, barMaxW, barH);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(8, 17, 32, 165));
        p.drawRoundedRect(innerTrackRect, radius, radius);

        // Remplissage doré lumineux proportionnel au pourcentage (m_percent)
        if (m_percent > 0) {
            qreal fillW = std::max(barH, (barMaxW * m_percent) / 100.0);
            fillW = std::min(fillW, barMaxW);
            QRectF fillRect(barLeft, barTop, fillW, barH);

            QLinearGradient grad(fillRect.left(), fillRect.top(), fillRect.right(), fillRect.top());
            grad.setColorAt(0.0, QColor("#B45309"));
            grad.setColorAt(0.45, QColor("#F59E0B"));
            grad.setColorAt(0.85, QColor("#FDE68A"));
            grad.setColorAt(1.0, QColor("#FFFBEB"));

            p.setBrush(grad);
            p.drawRoundedRect(fillRect, radius, radius);

            // Reflet supérieur subtil dans la barre
            QRectF highlightRect(barLeft + 3.0, barTop + 1.5, std::max(0.0, fillW - 6.0), barH * 0.38);
            p.setBrush(QColor(255, 255, 255, 70));
            p.drawRoundedRect(highlightRect, radius * 0.5, radius * 0.5);
        }

        // Pourcentage affiché au centre de la barre dorée
        QFont pctFont = font();
        pctFont.setPointSize(9);
        pctFont.setBold(true);
        p.setFont(pctFont);
        const QString pctStr = QString("%1 %").arg(m_percent);

        p.setPen(QColor(5, 10, 20, 220));
        p.drawText(innerTrackRect.translated(1.0, 1.0), Qt::AlignCenter, pctStr);
        p.setPen(QColor("#FFFBEB"));
        p.drawText(innerTrackRect, Qt::AlignCenter, pctStr);

        // 3. Texte d'état du scan des modules juste sous la barre d'avancement
        if (!m_statusText.isEmpty()) {
            QRectF statusRect(W * 0.10, H * 0.942, W * 0.80, H * 0.052);
            QFont stFont = font();
            stFont.setPointSize(10);
            stFont.setBold(false);
            p.setFont(stFont);

            QString elided = p.fontMetrics().elidedText(m_statusText, Qt::ElideMiddle, int(statusRect.width()));

            // Ombre portée douce pour une lisibilité parfaite sur le fond bleu nuit
            p.setPen(QColor(4, 9, 18, 230));
            p.drawText(statusRect.translated(1.0, 1.0), Qt::AlignHCenter | Qt::AlignVCenter, elided);

            p.setPen(QColor("#FDE68A"));
            p.drawText(statusRect, Qt::AlignHCenter | Qt::AlignVCenter, elided);
        }

        // 4. Bordure dorée subtile autour de l'écran de démarrage
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(217, 168, 94, 140), 1.5));
        p.drawRect(QRectF(0.75, 0.75, W - 1.5, H - 1.5));
    }

private:
    QPixmap m_bg;
    int m_percent = 0;
    QString m_statusText;
};

QString findAssetPath(const QStringList& candidates) {
    for (const QString& path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("BibleStudyStudio");
    app.setApplicationDisplayName("AI Bible — Logos & Lumière");
    app.setOrganizationName("LogosLumiere");

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString swordDir = QDir::homePath() + "/.sword";

    if (qEnvironmentVariableIsEmpty("CURL_CA_BUNDLE") && QFileInfo::exists(appDir + "/ca-bundle.crt")) {
        qputenv("CURL_CA_BUNDLE", (appDir + "/ca-bundle.crt").toUtf8());
    }

    // 1. Chargement de l'icône de l'application (depuis ~/.sword/icon.png, le dossier de l'app ou la ressource intégrée)
    const QString iconPath = findAssetPath({
        swordDir + "/icon.png",
        appDir + "/icon.png",
        appDir + "/../icon.png",
        appDir + "/../share/ai-bible/icon.png",
        "/usr/share/ai-bible/icon.png",
        ":/resources/icon.png",
        appDir + "/ai_bible_icon.svg",
        appDir + "/../share/icons/hicolor/scalable/apps/ai_bible_icon.svg"
    });
    QIcon appIcon;
    if (!iconPath.isEmpty()) {
        appIcon = QIcon(iconPath);
        app.setWindowIcon(appIcon);
    }

    // 2. Chargement de l'écran de démarrage (depuis ~/.sword/Splash_screen.png, le dossier de l'app ou la ressource intégrée)
    const QString splashPath = findAssetPath({
        swordDir + "/Splash_screen.png",
        appDir + "/Splash_screen.png",
        appDir + "/../Splash_screen.png",
        appDir + "/../share/ai-bible/Splash_screen.png",
        "/usr/share/ai-bible/Splash_screen.png",
        ":/resources/Splash_screen.png"
    });

    const QPixmap splashPix(splashPath);
    StartupSplashScreen splash(splashPix);
    if (!appIcon.isNull()) {
        splash.setWindowIcon(appIcon);
    }
    splash.updateProgress(
        2,
        SwordEngine::uiTr("Démarrage de AI Bible — Logos & Lumière...",
                          "Starting AI Bible — Logos & Light...",
                          "Iniciando AI Bible — Logos y Luz...")
    );
    splash.show();
    splash.raise();
    QCoreApplication::processEvents();

    QElapsedTimer stepTimer;
    stepTimer.start();

    auto progressCallback = [&](int pct, const QString& msg) {
        splash.updateProgress(pct, msg);
        // Assurer une fluidité visuelle de la barre d'avancement pendant le scan des modules
        if (stepTimer.elapsed() < 5) {
            QThread::msleep(4);
        }
        stepTimer.restart();
    };

    MainWindow win(nullptr, progressCallback);
    if (!appIcon.isNull()) {
        win.setWindowIcon(appIcon);
    }

    splash.updateProgress(
        100,
        SwordEngine::uiTr("Prêt !", "Ready!", "¡Listo!")
    );
    win.show();
    splash.close();

    return app.exec();
}
