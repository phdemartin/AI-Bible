#pragma once

#include "SwordEngine.h"
#include "GeminiClient.h"
#include "ScreenPanels.h"
#include "LayoutSelectorDialog.h"

#include <QMainWindow>
#include <QSplitter>
#include <QVector>
#include <QList>
#include <memory>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

public slots:
    void applyStyleToAllModules(const PaneStyle& style);
    void openModuleInstallerDialog();
    void openLayoutSelectorDialog(bool isFirstStartup = false);
    void toggleLeftSidebar();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    struct DynamicSplitRecord {
        int targetPaneIndex = 0;
        Qt::Orientation orientation = Qt::Vertical;
        UniversalPaneWidget* createdPane = nullptr;
        QSplitter* createdSplitter = nullptr;
    };

    int paneCount() const;
    UniversalPaneWidget* paneAt(int index) const;
    bool isPaneActiveInLayout(int index) const;
    int pickFallbackActivePane(int excludeIndex = -1, int preferredIndex = -1) const;
    void connectPaneSignals(UniversalPaneWidget* p);
    UniversalPaneWidget* splitPane(UniversalPaneWidget* sourcePane, Qt::Orientation orientation, const QString& initialMode, bool recordAndSave = true);
    void closePane(UniversalPaneWidget* targetPane);
    void clearDynamicSplits();
    void applyWorkspaceLayout(const WorkspaceLayoutConfig& cfg, bool applyScreenGeometry, bool updatePaneModes);

    QStringList collectActiveBibles() const;
    QString collectCurrentStrongCode() const;
    QString collectCurrentCommentaryModule() const;
    void syncLeftTreeBibles();

    void applyStylePalette(const PaneStyle& style);
    void applySavedSplitterSizes();
    void navigateTo(const QString& swordBook, int chapter, int verse, bool recordHistory = true);
    void openReferenceInSpecificPane(int targetPaneIndex, const QString& swordKey, const QString& preferredBibleMod = QString(), int sourcePaneIndex = 0);
    void loadSettings();
    void saveSettings() const;
    QString settingsFilePath() const;

    std::unique_ptr<SwordEngine> m_engine;
    GeminiClient* m_gemini;

    QSplitter* m_mainHorizontalSplitter;
    LeftScreenWidget* m_leftScreen;
    CenterScreenWidget* m_centerScreen;
    RightScreenWidget* m_rightScreen;

    QVector<DynamicSplitRecord> m_dynamicSplits;
    WorkspaceLayoutConfig m_layoutConfig;
    bool m_needsInitialLayoutPrompt = false;

    QString m_currentBook = "John";
    int m_currentChapter = 1;
    int m_currentVerse = 1;

    QList<int> m_savedMainSplitterSizes;
    QList<int> m_savedCenterSplitterSizes;
    QList<int> m_savedRightSplitterSizes;
    QList<int> m_savedRightVerticalSplitterSizes;

    QVector<QString> m_history;
    int m_historyIndex = -1;
    bool m_isClosing = false;
    bool m_isLoadingSettings = false;
};


