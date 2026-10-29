#pragma once

#include "SwordEngine.h"
#include "GeminiClient.h"

#include <QWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
#include <QTabWidget>
#include <QStackedWidget>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QProcess>
#include <QPoint>
#include <QJsonObject>
#include <functional>

// Crée un petit bouton discret "roue dentée" (⚙) pour le coin supérieur droit d'un module
QToolButton* createGearButton(const QString& tooltip, QWidget* parent = nullptr);

// Ouvre la boîte de dialogue d'apparence (taille des lettres, contraste, couleurs) pour un module
void openModuleSettingsDialog(
    QWidget* parent,
    const QString& moduleTitle,
    PaneStyle& inOutStyle,
    std::function<void(const PaneStyle&)> onLiveUpdate,
    std::function<void(const PaneStyle&)> onApplyToAllModules = nullptr,
    QWidget* extraModuleSection = nullptr
);

// ============================================================================
// InteractiveTextBrowser :
// - Sélection & Ctrl+C propres (préservée au clic droit)
// - Glisser-déposer (Drag & Drop) du numéro de verset d'une fenêtre vers une autre
// - Ctrl + Molette = Agrandir / réduire les lettres à la volée
// - Clic Gauche ou Clic Milieu sur un mot -> Dictionnaire Grec / Hébreu + surlignage
// - Clic Droit sur une sélection -> Ajouter à la Note Personnelle du verset ou Rechercher dans l'IA
// ============================================================================
class InteractiveTextBrowser : public QTextBrowser {
    Q_OBJECT
public:
    explicit InteractiveTextBrowser(bool isBibleView, QWidget* parent = nullptr);

    void setIsBibleView(bool isBible);
    bool isBibleView() const { return m_isBibleView; }

    void setPassageContext(const QString& book, int chapter, const QString& moduleName, const QString& frenchBookName = QString(), int activeVerse = 1);
    QString cleanSelectedText() const;

    void highlightWord(int verse, const QString& strongCode, const QString& word, int wordIdx, bool matchByIdx);
    void clearWordHighlight();
    void reapplyWordHighlight();
    int selectedVerseNumber() const;
    int topVisibleVerseNumber() const;

signals:
    void wordDictionaryRequested(int verse, const QString& strongCode, const QString& morphCode, const QString& word, int wordIndex);
    void wordAiResearchRequested(int verse, const QString& strongCode, const QString& morphCode, const QString& word, int wordIndex);
    void verseAiPreparedRequested(int verse, bool autoSend);
    void selectionAiPreparedRequested(int verse, const QString& selectedText, bool autoSend);
    void selectionAiResearchRequested(const QString& selectedText, bool autoSend);
    void addSelectionToPersonalNoteRequested(int verse, const QString& selectedText, bool openNotePane);
    void verseActivated(int verse);
    void verseDropped(const QString& book, int chapter, int verse);
    void openReferenceInPaneRequested(int targetPaneIndex, const QString& rawRef);
    void fontSizeDeltaRequested(int deltaPt);
    void statusNotification(const QString& msg);

protected:
    QMimeData* createMimeDataFromSelection() const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool extractWordInfoAtPos(const QPoint& pos, int& outVerse, QString& outStrong, QString& outMorph, QString& outWord, int& outWordIdx, bool* outIsVerseNum = nullptr) const;
    bool extractRefLinkAtPos(const QPoint& pos, QString& outRawRef, QString& outDisplayLabel) const;

    bool m_isBibleView = false;
    QPoint m_pressPos;
    int m_dragStartVerse = 0;

    QString m_contextBook = "John";
    QString m_contextFrenchBook = "Jean";
    int m_contextChapter = 1;
    int m_contextVerse = 1;
    QString m_contextModule = "FreLSG";

    int m_hlVerse = 0;
    QString m_hlStrong;
    QString m_hlWord;
    int m_hlWordIdx = -1;
    bool m_hlMatchByIdx = false;
};

// ============================================================================
// COLONNE DE GAUCHE :
// Navigation + Arbre épuré des Documents + Roue dentée (⚙) en haut à droite
// ============================================================================
class LeftScreenWidget : public QWidget {
    Q_OBJECT
public:
    explicit LeftScreenWidget(SwordEngine* engine, QWidget* parent = nullptr);

    void syncCurrentReference(const QString& swordBook, int chapter, int verse);
    void syncSelectedBibles(const QString& bible1, const QString& bible2);
    void refreshPersonalNotesTree();
    void reloadModuleLists();

    const PaneStyle& paneStyle() const { return m_style; }
    void setPaneStyle(const PaneStyle& style);

signals:
    void referenceSelected(const QString& swordBook, int chapter, int verse);
    void openReferenceInPaneRequested(int targetPaneIndex, const QString& swordKey, const QString& preferredBibleMod);
    void bibleSelectedForColumn(int columnIndex, const QString& bibleMod);
    void commentarySelected(const QString& commMod);
    void dictionarySelected(const QString& dictMod);
    void personalNoteSelected(const QString& swordKey);
    void openDocumentInPaneRequested(int paneIndex, const QString& kind, const QString& modName);
    void styleChanged();
    void applyStyleToAllRequested(const PaneStyle& style);
    void statusNotification(const QString& msg);
    void openModuleInstallerRequested();

private slots:
    void onQuickRefSubmitted();
    void onBookComboChanged(int index);
    void onChapterSpinChanged(int chapter);
    void onVerseSpinChanged(int verse);
    void onTreeFilterChanged(const QString& text);
    void onDocumentTreeClicked(QTreeWidgetItem* item, int column);
    void onDocumentTreeContextMenu(const QPoint& pos);
    void onSearchExecuted();
    void onSearchResultClicked(QListWidgetItem* item);
    void onSearchResultContextMenu(const QPoint& pos);
    void onOpenGearSettings();

private:
    void populateBookCombo();
    void populateDocumentTree();
    void applyStyleToWidgets();

    SwordEngine* m_engine;
    PaneStyle m_style;

    QLineEdit* m_quickRefEdit;
    QComboBox* m_bookCombo;
    QSpinBox* m_chapterSpin;
    QSpinBox* m_verseSpin;
    QLineEdit* m_treeFilterEdit;
    QTreeWidget* m_docTree;

    QLineEdit* m_searchInput;
    QComboBox* m_searchBibleCombo;
    QLabel* m_searchStatusLabel;
    QListWidget* m_searchResultsList;

    QString m_currentBook = "John";
    int m_currentChapter = 1;
    int m_currentVerse = 1;
    int m_defaultTargetCol = 0;
    bool m_updating = false;
};

// ============================================================================
// UniversalPaneWidget :
// Fenêtre universelle interchangeable (utilisée pour les 4 fenêtres :
// Fenêtre 1 & 2 au Centre, Fenêtre 3 & 4 à Droite).
// Permet de choisir à volonté via un menu déroulant en haut :
//   - 📖 Bible (Français, Anglais, Espagnol, Grec, Hébreu)
//   - 📚 Commentaires & TSK (synchronisé ou indépendant)
//   - 🏛️ Dictionnaire Grec / Hébreu (Strong Auto ou lexique au choix)
//   - 🤖 IA Gemini & Sources (avec Switch 💬 Chat Gemini <-> 📚 Sources Locales + 🎤 Dicter + 🧹 Nettoyer)
//   - 📝 Notes Personnelles (fiches d'étude indexées par verset avec extraits IA + annotations)
// ============================================================================
class UniversalPaneWidget : public QWidget {
    Q_OBJECT
public:
    explicit UniversalPaneWidget(int paneIndex, SwordEngine* engine, GeminiClient* gemini, QWidget* parent = nullptr);
    ~UniversalPaneWidget() override;

    int paneIndex() const { return m_paneIndex; }
    void setPaneIndex(int index);

    // Mode de la fenêtre : "BIBLE", "COMMENTARY", "DICTIONARY", "AI", "NOTES"
    QString paneMode() const { return m_paneMode; }
    void setPaneMode(const QString& mode, bool emitSignal = false);

    // Modules sélectionnés
    QString bibleModule() const { return m_bibleMod; }
    void setBibleModule(const QString& mod, bool switchToBibleMode = false);

    QString commentaryModule() const { return m_commMod; }
    void setCommentaryModule(const QString& mod, bool switchToCommMode = false);

    QString dictionaryModule() const { return m_dictMod; }
    void setDictionaryModule(const QString& mod, bool switchToDictMode = false);

    bool isAiChatMode() const { return m_aiChatMode; }
    void setAiChatMode(bool chatMode);

    QString aiResponseLanguage() const { return m_aiResponseLang; }
    void setAiResponseLanguage(const QString& langCode, bool emitSignal = false);

    // Synchronisation (Lier / Délier)
    bool isLinked() const { return m_linked; }
    void setLinked(bool linked, bool emitSignal = false);

    // Passage courant (Livre, Chapitre, Verset)
    QString book() const { return m_book; }
    int chapter() const { return m_chapter; }
    int verse() const { return m_verse; }
    QString swordKey() const;
    void loadPassage(const QString& swordBook, int chapter, int verse, bool scrollToVerse = true, bool emitSignal = false);
    void updateCommentaryVerseOnly(int verse);

    // Défilement synchronisé entre Bibles
    double scrollRatio() const;
    void setScrollRatio(double ratio);

    // Surlignage et Dictionnaire Grec/Hébreu
    void highlightWordInBible(int verse, const QString& resolvedStrong, const QString& word, int wordIdx, bool exactWord);
    void clearWordHighlight();
    void showWordDefinition(const QString& swordKey, const QString& strongCode, const QString& morphCode, const QString& clickedWord, int wordIndex);
    QString currentStrongCode() const { return m_activeStrongCode; }

    // Recherche IA Gemini & Sources
    void preparePassageForAiQuestion(
        const QString& swordKey,
        const QString& sourceLabel,
        const QString& excerptOrVerseText,
        const QStringList& activeBibles,
        bool autoSend
    );
    void notifyGeminiUpdated();

    // Notes Personnelles indexées par verset
    void refreshPersonalNoteView(bool scrollToBottom = false);

    QList<int> aiSplitterSizes() const;
    void setAiSplitterSizes(const QList<int>& sizes);

    // Style visuel (taille des lettres, contraste, couleurs)
    const PaneStyle& paneStyle() const { return m_style; }
    void setPaneStyle(const PaneStyle& style);

    void refreshContent(bool scrollToVerse = false);
    void reloadModuleLists();

    // Sérialisation JSON complète de la fenêtre
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

signals:
    void paneConfigChanged(int paneIndex);
    void passageChanged(int paneIndex, const QString& swordBook, int chapter, int verse);
    void bibleScrolled(int paneIndex, double ratio, int topVisibleVerse);
    void wordClickedInBible(int paneIndex, const QString& swordKey, const QString& strongCode, const QString& morphCode, const QString& word, int wordIndex);
    void aiResearchRequested(int paneIndex, const QString& swordKey, const QString& sourceLabel, const QString& selectedText, bool autoSend);
    void addExcerptToPersonalNoteRequested(int sourcePaneIndex, const QString& swordKey, const QString& sourceTitle, const QString& excerptText, const QString& preferredBibleMod, bool openNotePane);
    void openPersonalNoteForVerseRequested(int sourcePaneIndex, const QString& swordKey);
    void personalNoteModified(const QString& swordKey);
    void navigateToReferenceRequested(int paneIndex, const QString& swordKey);
    void navigateToReferenceInPaneRequested(int sourcePaneIndex, int targetPaneIndex, const QString& swordKey);
    void requestStudyContext(QString& outContext, QStringList& outActiveBibles);
    void apiKeyUpdated(const QString& newKey, const QString& modelName);
    void styleChanged();
    void applyStyleToAllRequested(const PaneStyle& style);
    void statusNotification(const QString& msg);
    void splitPaneRequested(UniversalPaneWidget* sourcePane, int orientation, const QString& initialMode);
    void closePaneRequested(UniversalPaneWidget* targetPane);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTypeComboChanged(int index);
    void onPrevChapter();
    void onNextChapter();
    void onDictManualSearch();
    void onBrowserAnchorClicked(const QUrl& url);
    void onSendCustomAiQuestion();
    void onClearAiWindow();
    void onToggleMicDictation();
    void onCopyLatestAiForWordProcessor();
    void onOpenGearSettings();

private:
    void showSplitMenu(int orientation, QWidget* anchorBtn);
    void handleVerseDroppedInAi(const QString& book, int chapter, int verse, const QString& sourceModule = QString());
    void copyTurnToWordProcessorClipboard(int turnIndex);
    void stopMicDictation();
    void syncHeaderControls();
    void updateLinkButtonUi();
    void updateAiModeSwitchUi();
    void updateAiLanguageButtonsUi();
    void populateAiLanguageCombo();
    QString effectiveAiLanguageCode() const;
    void populateNotesCombo();
    void scrollBibleToActiveVerse();
    void renderGeminiConversation();
    QString buildLocalExegesisFallbackHtml(const QString& userQuestion) const;

    int m_paneIndex = 0;
    SwordEngine* m_engine = nullptr;
    GeminiClient* m_gemini = nullptr;
    PaneStyle m_style;

    QString m_paneMode = "BIBLE"; // "BIBLE", "COMMENTARY", "DICTIONARY", "AI", "NOTES"
    QString m_bibleMod = "FreSegond";
    QString m_commMod = "ALL_FR";
    QString m_dictMod = "AUTO_STRONG";
    bool m_aiChatMode = true;
    QString m_aiResponseLang = "auto"; // "auto" (suit la langue de la Bible active / de l'OS), ou code ISO ("fr", "es", "en", "de", "pt"...)
    bool m_linked = true;

    QString m_book = "John";
    int m_chapter = 1;
    int m_verse = 1;

    QString m_activeStrongCode = "G3056";
    QString m_activeMorphCode;
    QString m_activeClickedWord = "Parole";

    QString m_preparedSwordKey;
    QString m_preparedBibleMod;
    QString m_preparedExcerptText;
    QStringList m_preparedActiveBibles;
    QString m_localFallbackCardHtml;
    QString m_lastSourcesQuery;
    bool m_sourcesCleared = false;

    // Ligne 1 : Sélecteur du type de fenêtre + contrôles du mode + boutons d'ajout/suppression de fenêtre (➕⬇, ➕➡, ✕) + roue dentée ⚙
    QComboBox* m_typeCombo = nullptr;
    QStackedWidget* m_headerModeStack = nullptr;
    QToolButton* m_splitDownBtn = nullptr;
    QToolButton* m_splitRightBtn = nullptr;
    QToolButton* m_closePaneBtn = nullptr;
    QToolButton* m_gearBtn = nullptr;

    // Header Page 0 : Bible
    QComboBox* m_bibleCombo = nullptr;
    QPushButton* m_bibleLinkBtn = nullptr;

    // Header Page 1 : Commentaire
    QComboBox* m_commCombo = nullptr;
    QPushButton* m_commLinkBtn = nullptr;

    // Header Page 2 : Dictionnaire
    QComboBox* m_dictCombo = nullptr;
    QLineEdit* m_dictSearchEdit = nullptr;

    // Header Page 3 : IA Gemini & Sources + Menu déroulant des drapeaux (🇫🇷 Français, 🇪🇸 Español, 🇬🇧 English)
    QPushButton* m_modeChatBtn = nullptr;
    QPushButton* m_modeSourcesBtn = nullptr;
    QComboBox* m_aiLangCombo = nullptr;
    QPushButton* m_topClearAiBtn = nullptr;

    // Header Page 4 : Notes Personnelles
    QComboBox* m_notesListCombo = nullptr;
    QPushButton* m_notesAddBlockBtn = nullptr;
    QPushButton* m_notesBoldBtn = nullptr;
    QPushButton* m_notesHighlightBtn = nullptr;
    QPushButton* m_notesCopyForWordBtn = nullptr;
    QPushButton* m_notesDeleteBtn = nullptr;
    QPushButton* m_notesLinkBtn = nullptr;

    // Ligne 2 : Barre de navigation Livre / Chapitre / Verset (pour Bible, Commentaire et Notes)
    QWidget* m_navRowWidget = nullptr;
    QLabel* m_refTitleLabel = nullptr;
    QComboBox* m_bookCombo = nullptr;
    QSpinBox* m_chapterSpin = nullptr;
    QSpinBox* m_verseSpin = nullptr;
    QLineEdit* m_quickRefEdit = nullptr;

    // Corps principal (Page 0 = Browser standard, Page 1 = Panneau IA, Page 2 = Éditeur Notes Personnelles)
    QStackedWidget* m_bodyStack = nullptr;
    InteractiveTextBrowser* m_browser = nullptr;

    // Composants IA (Page 1)
    QLabel* m_geminiStatusLabel = nullptr;
    QSplitter* m_aiVerticalSplitter = nullptr;
    InteractiveTextBrowser* m_geminiBrowser = nullptr;
    QPlainTextEdit* m_geminiInputEdit = nullptr;
    QPushButton* m_copyForWordBtn = nullptr;
    QPushButton* m_clearAiBtn = nullptr;
    QPushButton* m_micDictateBtn = nullptr;
    QPushButton* m_sendAiBtn = nullptr;
    QProcess* m_dictationProcess = nullptr;
    QString m_lastPartialDictation;

    // Composants Notes Personnelles (Page 2)
    QLabel* m_notesStatusLabel = nullptr;
    QTextEdit* m_notesEditor = nullptr;
    bool m_loadingNoteHtml = false;

    bool m_updatingControls = false;
    bool m_syncingScroll = false;
};

// ============================================================================
// COLONNE DU CENTRE :
// Contient les Fenêtres 1 et 2 (UniversalPaneWidget) côte à côte + barre de liaison rapide
// ============================================================================
class CenterScreenWidget : public QWidget {
    Q_OBJECT
public:
    explicit CenterScreenWidget(SwordEngine* engine, GeminiClient* gemini, QWidget* parent = nullptr);

    UniversalPaneWidget* pane(int index) const { return (index >= 0 && index < 2) ? m_panes[index] : nullptr; }

    bool isColumnsLinked() const { return m_columnsLinked; }
    void setColumnsLinked(bool linked);

    void setSecondPaneVisible(bool visible);
    bool isSecondPaneVisible() const;

    void setLeftSidebarVisibleState(bool visible);

    QList<int> splitterSizes() const;
    void setSplitterSizes(const QList<int>& sizes);

    const PaneStyle& paneStyle() const { return m_panes[0]->paneStyle(); }
    void setPaneStyle(const PaneStyle& style);

signals:
    void linkStateChanged(bool linked);
    void columnSplitterMoved();
    void styleChanged();
    void applyStyleToAllRequested(const PaneStyle& style);
    void openLayoutSelectorRequested();
    void toggleLeftSidebarRequested();
    void statusNotification(const QString& msg);

private slots:
    void onToggleLinkColumns();
    void onOpenGearSettings();

private:
    void updateLinkButtonUi();

    SwordEngine* m_engine = nullptr;
    GeminiClient* m_gemini = nullptr;
    QPushButton* m_toggleSidebarBtn = nullptr;
    QPushButton* m_linkToggleBtn = nullptr;
    QPushButton* m_layoutPresetBtn = nullptr;
    QLabel* m_syncStatusLabel = nullptr;
    QSplitter* m_columnsSplitter = nullptr;
    UniversalPaneWidget* m_panes[2] = {nullptr, nullptr};
    bool m_columnsLinked = true;
};

// ============================================================================
// COLONNE DE DROITE :
// Contient les Fenêtres 3 et 4 (UniversalPaneWidget) côte à côte (ou Haut/Bas)
// + séparateur vertical réglable en bas pour s'adapter à un 2e écran moins haut.
// ============================================================================
class RightScreenWidget : public QWidget {
    Q_OBJECT
public:
    explicit RightScreenWidget(SwordEngine* engine, GeminiClient* gemini, QWidget* parent = nullptr);
    ~RightScreenWidget() override = default;

    UniversalPaneWidget* pane(int index) const { return (index >= 0 && index < 2) ? m_panes[index] : nullptr; }

    void setOrientation(Qt::Orientation orientation);
    Qt::Orientation orientation() const;

    void setSecondPaneVisible(bool visible);
    bool isSecondPaneVisible() const;

    void setBottomHeightAdjusterVisible(bool visible);
    bool isBottomHeightAdjusterVisible() const;

    QList<int> splitterSizes() const;
    void setSplitterSizes(const QList<int>& sizes);

    QList<int> verticalSplitterSizes() const;
    void setVerticalSplitterSizes(const QList<int>& sizes);

signals:
    void rightSplitterMoved();

private:
    SwordEngine* m_engine = nullptr;
    GeminiClient* m_gemini = nullptr;
    QSplitter* m_rightVerticalSplitter = nullptr;
    QSplitter* m_mainSplitter = nullptr;
    QWidget* m_bottomSpacerWidget = nullptr;
    UniversalPaneWidget* m_panes[2] = {nullptr, nullptr};
};

