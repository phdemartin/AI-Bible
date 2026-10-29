#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QCheckBox;
class QComboBox;
class QPushButton;
class LayoutCardWidget;

struct WorkspaceLayoutConfig {
    QString presetId = "3_MIXED";      // "2_COLS", "3_MIXED", "4_MIXED", "3_COLS", "4_COLS"
    int screenMode = 1;                // 1 = 1 Écran (Principal), 2 = 2 Écrans (Bureau étendu)
    bool showLeftSidebar = true;       // Afficher le panneau Bibliothèque & Navigation à gauche
    bool rightHeightAdjuster = false;  // Afficher la barre d'ajustement de hauteur à droite (pour 2e écran moins haut)
    bool showOnStartup = false;        // Toujours afficher cette fenêtre au démarrage
    QString paneModes[4] = {"BIBLE", "BIBLE", "DICTIONARY", "AI"};
};

class LayoutSelectorDialog : public QDialog {
    Q_OBJECT
public:
    explicit LayoutSelectorDialog(
        const WorkspaceLayoutConfig& currentConfig,
        bool isFirstStartup = false,
        QWidget* parent = nullptr
    );
    ~LayoutSelectorDialog() override = default;

    WorkspaceLayoutConfig selectedConfig() const { return m_config; }

signals:
    void openModuleInstallerRequested();

private slots:
    void onScreenModeSelected(int screenMode);
    void onPresetSelected(const QString& presetId);
    void updateUiState();

private:
    WorkspaceLayoutConfig m_config;
    bool m_isFirstStartup = false;

    QPushButton* m_oneScreenBtn = nullptr;
    QPushButton* m_twoScreensBtn = nullptr;
    QLabel* m_screenDetectedLabel = nullptr;
    QCheckBox* m_rightHeightAdjustCheck = nullptr;
    QCheckBox* m_showLeftSidebarCheck = nullptr;
    QCheckBox* m_showOnStartupCheck = nullptr;

    QVector<LayoutCardWidget*> m_cards;

    QWidget* m_paneRolesWidgets[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel* m_paneRoleLabels[4] = {nullptr, nullptr, nullptr, nullptr};
    QComboBox* m_paneRoleCombos[4] = {nullptr, nullptr, nullptr, nullptr};
};
