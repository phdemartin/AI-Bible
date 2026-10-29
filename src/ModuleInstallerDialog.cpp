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
#include <algorithm>

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

} // namespace

QString ModuleInstallerDialog::installMgrDirPath() {
    return QDir::homePath() + "/.sword/InstallMgr";
}

void ModuleInstallerDialog::ensureDefaultInstallMgrConf(const QString& installMgrDir) {
    QDir().mkpath(installMgrDir);
    QString confFile = installMgrDir + "/InstallMgr.conf";

    if (QFileInfo::exists(confFile) && QFileInfo(confFile).size() > 80) {
        return;
    }

    QFile f(confFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream out(&f);
    out << "[General]\n"
        << "PassiveFTP=true\n"
        << "TimeoutMillis=20000\n"
        << "UnverifiedPeerAllowed=true\n\n"
        << "[Sources]\n"
        << "FTPSource=CrossWire|ftp.crosswire.org|/pub/sword/raw|||20081216195754\n"
        << "FTPSource=Bible.org|ftp.crosswire.org|/pub/bible.org/sword|||20090514005700\n"
        << "FTPSource=STEP Bible|ftp.stepbible.org|/pub/sword|||20240314004500\n"
        << "FTPSource=Xiphos|ftp.xiphos.org|/pub/xiphos|||20090514005900\n"
        << "FTPSource=eBible.org|ftp.ebible.org|/sword|||20151110161300\n"
        << "FTPSource=Lockman Foundation|ftp.crosswire.org|/pub/sword/lockmanraw|||20190608211800\n"
        << "FTPSource=CrossWire Wycliffe|ftp.crosswire.org|/pub/sword/wyclifferaw|||20120711005000\n"
        << "FTPSource=CrossWire Attic|ftp.crosswire.org|/pub/sword/atticraw|||20120224005100\n"
        << "FTPSource=CrossWire Beta|ftp.crosswire.org|/pub/sword/betaraw|||20090224125400\n"
        << "FTPSource=CrossWire av11n|ftp.crosswire.org|/pub/sword/avraw|||20120224005000\n"
        << "FTPSource=CrossWire av11n Attic|ftp.crosswire.org|/pub/sword/avatticraw|||20120711005100\n"
        << "FTPSource=Crosswire Bible Society|ftp.crosswire.org|/pub/sword/raw|||ftp.crosswire.org\n"
        << "FTPSource=Deutsche Bibelgesellschaft|ftp.crosswire.org|/pub/sword/dbgraw|||20171021160700\n"
        << "FTPSource=IBT|ftp.ibt.org.ru|/pub/modsword/raw|||20120224005200\n";
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
    if (c.isEmpty() || c == "und") return "🌍 Non spécifié / Universel (und)";
    if (c == "mul") return "🌍 Multilingue / Lexiques (mul)";

    static const QMap<QString, QString> known = {
        {"fr",  "🇫🇷 Français"},
        {"es",  "🇪🇸 Español"},
        {"en",  "🇬🇧 English"},
        {"grc", "🏛️ Grec ancien / Koïné"},
        {"he",  "🕍 Hébreu biblique"},
        {"hbo", "🕍 Hébreu ancien"},
        {"la",  "🏛️ Latin"},
        {"pt",  "🇵🇹 Português"},
        {"de",  "🇩🇪 Deutsch (Allemand)"},
        {"it",  "🇮🇹 Italiano"},
        {"ht",  "🇭🇹 Créole Haïtien"},
        {"ru",  "🇷🇺 Русский (Russe)"},
        {"uk",  "🇺🇦 Українська (Ukrainien)"},
        {"ar",  "🇸🇦 Arabe (العربية)"},
        {"zh",  "🇨🇳 Chinois (中文)"},
        {"nl",  "🇳🇱 Nederlands (Néerlandais)"},
        {"ro",  "🇷🇴 Română (Roumain)"},
        {"pl",  "🇵🇱 Polski (Polonais)"},
        {"el",  "🇬🇷 Grec moderne"},
        {"ko",  "🇰🇷 Coréen (한국어)"},
        {"ja",  "🇯🇵 Japonais (日本語)"},
        {"vi",  "🇻🇳 Vietnamien (Tiếng Việt)"},
        {"tr",  "🇹🇷 Turc (Türkçe)"},
        {"fa",  "🇮🇷 Persan (فارسی)"},
        {"hi",  "🇮🇳 Hindi"},
        {"id",  "🇮🇩 Indonésien"},
        {"tl",  "🇵🇭 Tagalog (Philippines)"},
        {"sw",  "🇰🇪 Swahili"},
        {"mg",  "🇲🇬 Malgache"},
        {"am",  "🇪🇹 Amharique"},
        {"syc", "🏛️ Syriaque / Araméen"},
        {"cop", "🏛️ Copte"},
        {"got", "🏛️ Gothique"},
        {"eo",  "🌍 Espéranto"},
        {"ca",  "🇪🇸 Catalan"},
        {"eu",  "🇪🇸 Basque"},
        {"br",  "🇫🇷 Breton"},
        {"co",  "🇫🇷 Corse"},
        {"cs",  "🇨🇿 Tchèque"},
        {"sk",  "🇸🇰 Slovaque"},
        {"hu",  "🇭🇺 Hongrois"},
        {"sv",  "🇸🇪 Suédois"},
        {"no",  "🇳🇴 Norvégien"},
        {"da",  "🇩🇰 Danois"},
        {"fi",  "🇫🇮 Finnois"},
        {"bg",  "🇧🇬 Bulgare"},
        {"sr",  "🇷🇸 Serbe"},
        {"hr",  "🇭🇷 Croate"},
        {"th",  "🇹🇭 Thaï"},
        {"my",  "🇲🇲 Birman"},
        {"km",  "🇰🇭 Khmer"},
        {"lo",  "🇱🇦 Lao"},
        {"af",  "🇿🇦 Afrikaans"},
        {"yo",  "🇳🇬 Yoruba"},
        {"ha",  "🇳🇬 Haoussa"},
        {"ig",  "🇳🇬 Igbo"},
        {"zu",  "🇿🇦 Zoulou"},
        {"ln",  "🇨🇩 Lingala"},
        {"rw",  "🇷🇼 Kinyarwanda"},
        {"rn",  "🇧🇮 Kirundi"},
        {"wo",  "🇸🇳 Wolof"},
        {"bm",  "🇲🇱 Bambara"}
    };

    if (known.contains(c)) {
        return QString("%1 [%2]").arg(known.value(c), c);
    }

    QLocale loc(c);
    if (loc.language() != QLocale::C && loc.language() != QLocale::AnyLanguage) {
        QString frLang = QLocale(QLocale::French).languageToString(loc.language());
        if (frLang.isEmpty()) frLang = QLocale::languageToString(loc.language());
        if (!frLang.isEmpty() && frLang.toLower() != "c") {
            return QString("🌐 %1 [%2]").arg(frLang, c);
        }
    }

    return QString("🌐 Langue [%1]").arg(c);
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

void ModuleInstallerDialog::loadCatalogFromDisk() {
    m_updatingUi = true;
    m_repos.clear();
    m_allModules.clear();
    m_langModuleCounts.clear();

    // 1. Identifier les modules déjà installés localement dans ~/.sword
    QMap<QString, QString> installedVersions;
    QSet<QString> seenModuleNames;

    SWMgr* localMgr = m_engine ? m_engine->rawMgr() : nullptr;
    if (localMgr) {
        for (const auto& pair : localMgr->Modules) {
            SWModule* mod = pair.second;
            if (!mod) continue;
            QString name = QString::fromUtf8(mod->getName());
            QString ver = QString::fromUtf8(mod->getConfigEntry("Version") ? mod->getConfigEntry("Version") : "");
            installedVersions.insert(name, ver);
        }
    }

    // 2. Parcourir toutes les sources distantes configurées dans InstallMgr
    InstallMgr installMgr(m_installMgrDir.toUtf8().constData());
    installMgr.setUserDisclaimerConfirmed(true);

    bool firstLoadSources = m_checkedSources.isEmpty();

    for (const auto& srcPair : installMgr.sources) {
        InstallSource* is = srcPair.second;
        if (!is) continue;

        RemoteRepoInfo repo;
        repo.caption = QString::fromUtf8(is->caption.c_str()).trimmed();
        repo.type = QString::fromUtf8(is->type.c_str()).trimmed();
        repo.host = QString::fromUtf8(is->source.c_str()).trimmed();
        repo.directory = QString::fromUtf8(is->directory.c_str()).trimmed();
        repo.uid = QString::fromUtf8(is->uid.c_str()).trimmed();

        // Éviter le doublon exact Crosswire / CrossWire
        if (repo.caption == "Crosswire") continue;

        SWMgr* rmgr = is->getMgr();
        if (rmgr) {
            for (const auto& modPair : rmgr->Modules) {
                SWModule* mod = modPair.second;
                if (!mod) continue;

                QString modName = QString::fromUtf8(mod->getName()).trimmed();
                if (modName.isEmpty()) continue;
                ++repo.cachedModCount;

                // Si ce module a déjà été ajouté depuis un dépôt prioritaire (ex: CrossWire), ne pas le dupliquer
                if (seenModuleNames.contains(modName)) continue;
                seenModuleNames.insert(modName);

                CatalogModuleItem item;
                item.name = modName;
                item.description = QString::fromUtf8(mod->getDescription() ? mod->getDescription() : "").trimmed();
                item.rawType = QString::fromUtf8(mod->getType() ? mod->getType() : "").trimmed();
                item.langCode = QString::fromUtf8(mod->getLanguage() ? mod->getLanguage() : "und").trimmed().toLower();
                if (item.langCode.isEmpty()) item.langCode = "und";
                item.langDisplay = languageDisplayLabel(item.langCode);
                item.version = QString::fromUtf8(mod->getConfigEntry("Version") ? mod->getConfigEntry("Version") : "");
                item.installSize = QString::fromUtf8(mod->getConfigEntry("InstallSize") ? mod->getConfigEntry("InstallSize") : "");
                item.aboutText = QString::fromUtf8(mod->getConfigEntry("About") ? mod->getConfigEntry("About") : "");
                item.sourceCaption = repo.caption;
                item.isInstalled = installedVersions.contains(modName);
                if (item.isInstalled && !item.version.isEmpty() &&
                    !installedVersions.value(modName).isEmpty() &&
                    installedVersions.value(modName) != item.version) {
                    item.hasUpdate = true;
                }
                item.isCiphered = (mod->getConfigEntry("CipherKey") != nullptr);

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
                for (auto it = mod->getConfig().lower_bound("Feature"); it != mod->getConfig().upper_bound("Feature"); ++it) {
                    feats << QString::fromUtf8(it->second.c_str());
                }
                for (auto it = mod->getConfig().lower_bound("GlobalOptionFilter"); it != mod->getConfig().upper_bound("GlobalOptionFilter"); ++it) {
                    QString gf = QString::fromUtf8(it->second.c_str());
                    if (gf.contains("Strong", Qt::CaseInsensitive) && !feats.contains("Strong's Numbers")) {
                        feats << "Numéros Strong";
                    }
                    if (gf.contains("Morph", Qt::CaseInsensitive) && !feats.contains("Morphologie")) {
                        feats << "Morphologie";
                    }
                }
                item.featuresText = feats.join(", ");

                m_allModules.push_back(item);
                m_langModuleCounts[item.langCode] += 1;
            }
        }

        m_repos.push_back(repo);
        if (firstLoadSources) {
            m_checkedSources.insert(repo.caption);
        }
    }

    // 3. Ajouter aussi les modules installés localement qui ne figurent dans aucun catalogue distant (ex: modules personnalisés Fr_Emmaus, Fr_Greek...)
    if (localMgr) {
        for (const auto& modPair : localMgr->Modules) {
            SWModule* mod = modPair.second;
            if (!mod) continue;
            QString modName = QString::fromUtf8(mod->getName()).trimmed();
            if (modName.isEmpty() || seenModuleNames.contains(modName)) continue;
            seenModuleNames.insert(modName);

            CatalogModuleItem item;
            item.name = modName;
            item.description = QString::fromUtf8(mod->getDescription() ? mod->getDescription() : "").trimmed();
            item.rawType = QString::fromUtf8(mod->getType() ? mod->getType() : "").trimmed();
            item.langCode = QString::fromUtf8(mod->getLanguage() ? mod->getLanguage() : "und").trimmed().toLower();
            if (item.langCode.isEmpty()) item.langCode = "und";
            item.langDisplay = languageDisplayLabel(item.langCode);
            item.version = QString::fromUtf8(mod->getConfigEntry("Version") ? mod->getConfigEntry("Version") : "");
            item.sourceCaption = "Installé localement";
            item.isInstalled = true;

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

            m_allModules.push_back(item);
            m_langModuleCounts[item.langCode] += 1;
        }
        if (firstLoadSources) {
            m_checkedSources.insert("Installé localement");
        }
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
        sItem->setToolTip(QString("Serveur : %1%2").arg(repo.host, repo.directory));
        m_sourceListWidget->addItem(sItem);
    }

    m_updatingUi = false;
    rebuildLanguageListWidget();
    rebuildModuleTreeWidget();
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

        auto* item = new QListWidgetItem(label);
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
             mod.langDisplay.toHtmlEscaped(),
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
        "FreSegond", "FreLSG", "FreCrampon", "FreMartin", "FreBDM1744",
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
    typeCombo->addItem("FTPSource (FTP standard)", "FTPSource");
    typeCombo->addItem("HTTPSPackagePreference (HTTPS)", "HTTPSPackagePreference");
    typeCombo->addItem("HTTPPackagePreference (HTTP)", "HTTPPackagePreference");
    auto* hostEdit = new QLineEdit("ftp.crosswire.org");
    auto* dirEdit = new QLineEdit("/pub/sword/raw");

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
    m_refreshSourcesBtn->setEnabled(!busy);
    m_addSourceBtn->setEnabled(!busy);
    m_starterPackBtn->setEnabled(!busy);
    m_installBtn->setEnabled(!busy && !m_checkedModulesToSource.isEmpty());
    m_uninstallBtn->setEnabled(!busy && !m_checkedModulesToSource.isEmpty());
    m_closeBtn->setEnabled(!busy);
    m_cancelBtn->setVisible(busy);
    m_progressBar->setVisible(busy);
    if (!statusMsg.isEmpty()) {
        m_statusLabel->setText(statusMsg);
    }
}

void ModuleInstallerDialog::onCancelOperation() {
    m_cancelRequested = true;
    m_statusLabel->setText("⏹ Arrêt demandé... Fin du transfert en cours...");
}

void ModuleInstallerDialog::onRefreshSelectedSourcesFromInternet() {
    QStringList selectedRepos = m_checkedSources.values();
    selectedRepos.removeAll("Installé localement");
    if (selectedRepos.isEmpty()) {
        QMessageBox::information(this, "Aucun dépôt coché", "Veuillez cocher au moins un dépôt dans la liste « 🌐 2. Sites / Dépôts de modules ».");
        return;
    }

    m_cancelRequested = false;
    setBusyState(true, "⏳ Connexion aux dépôts en ligne et actualisation des catalogues...");
    m_progressBar->setRange(0, selectedRepos.size());
    m_progressBar->setValue(0);

    QString installMgrDir = m_installMgrDir;

    m_workerThread = QThread::create([this, installMgrDir, selectedRepos]() {
        QtInstallStatusReporter reporter;
        reporter.onStageMessage = [this](const QString& msg) {
            QMetaObject::invokeMethod(this, [this, msg]() {
                m_statusLabel->setText("🌐 " + msg);
            }, Qt::QueuedConnection);
        };

        InstallMgr mgr(installMgrDir.toUtf8().constData(), &reporter);
        mgr.setUserDisclaimerConfirmed(true);
        mgr.setFTPPassive(true);
        mgr.setUnverifiedPeerAllowed(true);
        mgr.setTimeoutMillis(20000);

        int done = 0;
        int okCount = 0;
        for (const QString& cap : selectedRepos) {
            if (m_cancelRequested) break;
            QMetaObject::invokeMethod(this, [this, cap, done, total = selectedRepos.size()]() {
                m_statusLabel->setText(QString("🔄 Actualisation du catalogue « %1 » (%2/%3)...").arg(cap).arg(done + 1).arg(total));
                m_progressBar->setValue(done);
            }, Qt::QueuedConnection);

            auto it = mgr.sources.find(cap.toUtf8().constData());
            if (it != mgr.sources.end() && it->second) {
                int r = mgr.refreshRemoteSource(it->second);
                if (r == 0) ++okCount;
            }
            ++done;
        }

        QMetaObject::invokeMethod(this, [this, okCount, total = selectedRepos.size()]() {
            loadCatalogFromDisk();
            setBusyState(false, QString("✓ Catalogues actualisés (%1/%2 dépôts mis à jour).").arg(okCount).arg(total));
        }, Qt::QueuedConnection);
    });

    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() { m_workerThread = nullptr; });
    m_workerThread->start();
}

void ModuleInstallerDialog::onInstallCheckedModules() {
    if (m_checkedModulesToSource.isEmpty()) return;

    QList<QPair<QString, QString>> tasks;
    for (auto it = m_checkedModulesToSource.begin(); it != m_checkedModulesToSource.end(); ++it) {
        if (it.value() != "Installé localement") {
            tasks.push_back({it.key(), it.value()});
        }
    }

    if (tasks.isEmpty()) {
        QMessageBox::information(this, "Déjà installés", "Les modules cochés sont uniquement des modules locaux déjà installés.");
        return;
    }

    m_cancelRequested = false;
    setBusyState(true, QString("📥 Téléchargement et installation de %1 module(s)...").arg(tasks.size()));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);

    QString installMgrDir = m_installMgrDir;
    QString destSwordDir = m_engine ? m_engine->swordPath() : (QDir::homePath() + "/.sword");

    m_workerThread = QThread::create([this, installMgrDir, destSwordDir, tasks]() {
        QtInstallStatusReporter reporter;
        reporter.onStageMessage = [this](const QString& msg) {
            QMetaObject::invokeMethod(this, [this, msg]() {
                m_statusLabel->setText("📥 " + msg);
            }, Qt::QueuedConnection);
        };
        reporter.onProgressUpdate = [this](unsigned long total, unsigned long completed) {
            if (total > 0) {
                int pct = int((completed * 100ULL) / total);
                QMetaObject::invokeMethod(this, [this, pct]() {
                    m_progressBar->setValue(std::clamp(pct, 0, 100));
                }, Qt::QueuedConnection);
            }
        };

        InstallMgr mgr(installMgrDir.toUtf8().constData(), &reporter);
        mgr.setUserDisclaimerConfirmed(true);
        mgr.setFTPPassive(true);
        mgr.setUnverifiedPeerAllowed(true);
        mgr.setTimeoutMillis(25000);

        SWMgr destMgr(destSwordDir.toUtf8().constData());

        int successCount = 0;
        QStringList installedNames;

        for (int i = 0; i < tasks.size(); ++i) {
            if (m_cancelRequested) break;
            const QString modName = tasks[i].first;
            const QString srcCap = tasks[i].second;

            QMetaObject::invokeMethod(this, [this, modName, srcCap, i, total = tasks.size()]() {
                m_statusLabel->setText(
                    QString("📥 Téléchargement de « %1 » depuis %2 (%3/%4)...")
                        .arg(modName, srcCap)
                        .arg(i + 1)
                        .arg(total)
                );
                m_progressBar->setValue(int((i * 100) / std::max(1, int(total))));
            }, Qt::QueuedConnection);

            auto it = mgr.sources.find(srcCap.toUtf8().constData());
            if (it != mgr.sources.end() && it->second) {
                int res = mgr.installModule(&destMgr, nullptr, modName.toUtf8().constData(), it->second);
                if (res == 0) {
                    ++successCount;
                    installedNames << modName;
                }
            }
        }

        QMetaObject::invokeMethod(this, [this, successCount, installedNames, total = tasks.size()]() {
            m_checkedModulesToSource.clear();
            if (m_engine) {
                m_engine->reloadModules();
            }
            loadCatalogFromDisk();
            setBusyState(
                false,
                QString("✅ %1/%2 module(s) installé(s) avec succès (%3) — Disponibles immédiatement dans vos 4 fenêtres !")
                    .arg(successCount)
                    .arg(total)
                    .arg(installedNames.join(", "))
            );
            if (successCount > 0) {
                emit modulesChanged();
            }
        }, Qt::QueuedConnection);
    });

    connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_workerThread, &QThread::finished, this, [this]() { m_workerThread = nullptr; });
    m_workerThread->start();
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

    InstallMgr mgr(m_installMgrDir.toUtf8().constData());
    QString destSwordDir = m_engine ? m_engine->swordPath() : (QDir::homePath() + "/.sword");
    SWMgr destMgr(destSwordDir.toUtf8().constData());

    int removed = 0;
    for (const QString& modName : modNames) {
        if (mgr.removeModule(&destMgr, modName.toUtf8().constData()) == 0) {
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
