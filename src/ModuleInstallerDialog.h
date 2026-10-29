#pragma once

#include "SwordEngine.h"

#include <QDialog>
#include <QListWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QTextBrowser>
#include <QThread>
#include <QSet>
#include <QMap>
#include <QVector>
#include <atomic>

struct RemoteRepoInfo {
    QString caption;      // e.g. "CrossWire", "eBible.org", "STEP Bible"
    QString type;         // "FTPSource", "HTTPSPackagePreference", "HTTPPackagePreference"
    QString host;         // e.g. "ftp.crosswire.org"
    QString directory;    // e.g. "/pub/sword/raw"
    QString uid;          // e.g. "20081216195754"
    int cachedModCount = 0;
};

struct CatalogModuleItem {
    QString name;
    QString description;
    QString categoryKey;      // "BIBLE", "COMMENTARY", "DICTIONARY", "OTHER"
    QString categoryDisplay;  // "📖 Bible", "📚 Commentaire", "🏛️ Dictionnaire", "📓 Autre"
    QString rawType;
    QString langCode;         // e.g. "fr", "es", "en", "grc", "he"
    QString langDisplay;      // e.g. "🇫🇷 Français (fr)"
    QString version;
    QString installSize;
    QString sourceCaption;    // e.g. "CrossWire"
    QString aboutText;
    QString featuresText;
    bool isInstalled = false;
    bool hasUpdate = false;
    bool isCiphered = false;
};

class ModuleInstallerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ModuleInstallerDialog(SwordEngine* engine, QWidget* parent = nullptr);
    ~ModuleInstallerDialog() override;

    static void ensureDefaultInstallMgrConf(const QString& installMgrDir);
    static QString installMgrDirPath();
    static QString languageDisplayLabel(const QString& rawLangCode);
    static int languageSortPriority(const QString& rawLangCode);

signals:
    void modulesChanged();

private slots:
    void onLanguageFilterTextChanged(const QString& text);
    void onSelectDefaultLanguages();
    void onSelectAllLanguages();
    void onUnselectAllLanguages();
    void onLanguageItemChanged(QListWidgetItem* item);
    void onSourceItemChanged(QListWidgetItem* item);
    void onAddCustomSource();
    void onRefreshSelectedSourcesFromInternet();
    void onModuleTreeItemChanged(QTreeWidgetItem* item, int column);
    void onModuleSelectionChanged();
    void onSelectRecommendedStarterPack();
    void onInstallCheckedModules();
    void onUninstallCheckedModules();
    void onCancelOperation();

private:
    void loadCatalogFromDisk();
    void rebuildLanguageListWidget();
    void rebuildModuleTreeWidget();
    void updateActionButtonsState();
    void setBusyState(bool busy, const QString& statusMsg = QString());

    SwordEngine* m_engine = nullptr;
    QString m_installMgrDir;

    QVector<RemoteRepoInfo> m_repos;
    QVector<CatalogModuleItem> m_allModules;
    QMap<QString, int> m_langModuleCounts;
    QSet<QString> m_checkedLangs;
    QSet<QString> m_checkedSources;
    QMap<QString, QString> m_checkedModulesToSource; // modName -> sourceCaption

    // Colonne Gauche : Langues à cocher + Sources distantes
    QLineEdit* m_langSearchEdit = nullptr;
    QListWidget* m_langListWidget = nullptr;
    QLabel* m_langSummaryLabel = nullptr;

    QListWidget* m_sourceListWidget = nullptr;
    QPushButton* m_refreshSourcesBtn = nullptr;
    QPushButton* m_addSourceBtn = nullptr;

    // Colonne Droite : Filtres de catégorie + Table des modules + Aperçu + Progression
    QLineEdit* m_modSearchEdit = nullptr;
    QCheckBox* m_chkBibles = nullptr;
    QCheckBox* m_chkCommentaries = nullptr;
    QCheckBox* m_chkDictionaries = nullptr;
    QCheckBox* m_chkOther = nullptr;
    QCheckBox* m_chkOnlyInstalled = nullptr;

    QLabel* m_modCountLabel = nullptr;
    QTreeWidget* m_modTree = nullptr;
    QTextBrowser* m_detailsBrowser = nullptr;

    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_starterPackBtn = nullptr;
    QPushButton* m_installBtn = nullptr;
    QPushButton* m_uninstallBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    QThread* m_workerThread = nullptr;
    std::atomic<bool> m_cancelRequested{false};
    bool m_updatingUi = false;
};
