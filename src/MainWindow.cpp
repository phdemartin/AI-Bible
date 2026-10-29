#include "MainWindow.h"
#include "ModuleInstallerDialog.h"

#include <QApplication>
#include <QWindow>
#include <QStatusBar>
#include <QGuiApplication>
#include <QScreen>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>
#include <algorithm>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(std::make_unique<SwordEngine>(QDir::homePath() + "/.sword"))
    , m_gemini(new GeminiClient(this))
{
    setObjectName("BibleStudyStudioMainWindow");
    setWindowTitle("Logos & Lumière — Studio Biblique");
    setMinimumSize(960, 600);
    resize(1880, 1020);

    // Empêcher la barre de statut de redimensionner la fenêtre lors d'un message long
    statusBar()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    m_mainHorizontalSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainHorizontalSplitter->setHandleWidth(6);
    m_mainHorizontalSplitter->setChildrenCollapsible(false);
    setCentralWidget(m_mainHorizontalSplitter);

    // Style visuel des barres de séparation des colonnes (↔) et des hauteurs (↕)
    setStyleSheet(
        "QSplitter::handle:horizontal { "
        "    background-color: #1E293B; "
        "    border-left: 1px solid #334155; "
        "    border-right: 1px solid #334155; "
        "    width: 6px; "
        "} "
        "QSplitter::handle:horizontal:hover { "
        "    background-color: #0284C7; "
        "    border-left: 1px solid #38BDF8; "
        "    border-right: 1px solid #38BDF8; "
        "} "
        "QSplitter::handle:vertical { "
        "    background-color: #1E293B; "
        "    border-top: 1px solid #38BDF8; "
        "    border-bottom: 1px solid #334155; "
        "    height: 7px; "
        "} "
        "QSplitter::handle:vertical:hover { "
        "    background-color: #0284C7; "
        "    border-top: 1px solid #7DD3FC; "
        "    border-bottom: 1px solid #7DD3FC; "
        "}"
    );

    m_leftScreen = new LeftScreenWidget(m_engine.get(), this);
    m_centerScreen = new CenterScreenWidget(m_engine.get(), m_gemini, this);
    m_rightScreen = new RightScreenWidget(m_engine.get(), m_gemini, this);

    m_mainHorizontalSplitter->addWidget(m_leftScreen);
    m_mainHorizontalSplitter->addWidget(m_centerScreen);
    m_mainHorizontalSplitter->addWidget(m_rightScreen);

    m_mainHorizontalSplitter->setStretchFactor(0, 2);
    m_mainHorizontalSplitter->setStretchFactor(1, 5);
    m_mainHorizontalSplitter->setStretchFactor(2, 5);

    // Sauvegarder automatiquement la largeur des colonnes et les hauteurs dès qu'on déplace une séparation
    connect(m_mainHorizontalSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_isLoadingSettings) return;
        m_savedMainSplitterSizes = m_mainHorizontalSplitter->sizes();
        saveSettings();
    });
    connect(m_centerScreen, &CenterScreenWidget::columnSplitterMoved, this, [this]() {
        if (m_isLoadingSettings) return;
        m_savedCenterSplitterSizes = m_centerScreen->splitterSizes();
        saveSettings();
    });
    connect(m_rightScreen, &RightScreenWidget::rightSplitterMoved, this, [this]() {
        if (m_isLoadingSettings) return;
        m_savedRightSplitterSizes = m_rightScreen->splitterSizes();
        m_savedRightVerticalSplitterSizes = m_rightScreen->verticalSplitterSizes();
        saveSettings();
    });

    // Connexions Colonne de Gauche (Arbre des Documents) -> 4 Fenêtres Interchangeables
    connect(m_leftScreen, &LeftScreenWidget::referenceSelected, this, [this](const QString& book, int ch, int v) {
        navigateTo(book, ch, v, true);
    });
    connect(m_leftScreen, &LeftScreenWidget::openReferenceInPaneRequested, this,
            [this](int targetPaneIdx, const QString& swordKey, const QString& bibleMod) {
        openReferenceInSpecificPane(targetPaneIdx, swordKey, bibleMod, 0);
    });
    connect(m_leftScreen, &LeftScreenWidget::openDocumentInPaneRequested, this, [this](int idx, const QString& kind, const QString& modName) {
        int targetIdx = isPaneActiveInLayout(idx) ? idx : pickFallbackActivePane(-1, idx);
        UniversalPaneWidget* p = paneAt(targetIdx);
        if (!p) return;
        if (kind == "BIBLE") {
            p->setBibleModule(modName, true);
        } else if (kind == "COMMENTARY") {
            p->setCommentaryModule(modName, true);
        } else if (kind == "DICTIONARY") {
            p->setDictionaryModule(modName, true);
        } else if (kind == "AI") {
            p->setPaneMode("AI", false);
        } else if (kind == "NOTE") {
            if (modName != "CURRENT") {
                QString b; int c = 1, v = 1;
                m_engine->splitSwordKey(modName, b, c, v);
                p->loadPassage(b, c, v, true, false);
            }
            p->setPaneMode("NOTES", false);
        }
        syncLeftTreeBibles();
        saveSettings();
    });
    connect(m_leftScreen, &LeftScreenWidget::personalNoteSelected, this, [this](const QString& swordKey) {
        QString b; int c = 1, v = 1;
        m_engine->splitSwordKey(swordKey, b, c, v);
        navigateTo(b, c, v, true);

        UniversalPaneWidget* target = nullptr;
        for (int i = 0; i < paneCount(); ++i) {
            if (isPaneActiveInLayout(i) && paneAt(i) && paneAt(i)->paneMode() == "NOTES") {
                target = paneAt(i);
                break;
            }
        }
        if (!target) {
            target = paneAt(pickFallbackActivePane(0, 2));
            if (target) target->setPaneMode("NOTES", false);
        }
        if (target) {
            target->loadPassage(b, c, v, true, false);
            target->refreshPersonalNoteView(false);
        }
        statusBar()->showMessage(
            QString("📝 Fiche d'étude personnelle ouverte pour %1.").arg(m_engine->toFrenchReference(swordKey)),
            6000
        );
        saveSettings();
    });
    connect(m_leftScreen, &LeftScreenWidget::bibleSelectedForColumn, this, [this](int colIndex, const QString& bibleMod) {
        int targetIdx = isPaneActiveInLayout(colIndex) ? colIndex : pickFallbackActivePane(-1, colIndex);
        UniversalPaneWidget* p = paneAt(targetIdx);
        if (!p) p = paneAt(0);
        if (p) p->setBibleModule(bibleMod, true);
        syncLeftTreeBibles();
        saveSettings();
    });
    connect(m_leftScreen, &LeftScreenWidget::commentarySelected, this, [this](const QString& commMod) {
        UniversalPaneWidget* target = nullptr;
        for (int i = 0; i < paneCount(); ++i) {
            if (isPaneActiveInLayout(i) && paneAt(i) && paneAt(i)->paneMode() == "COMMENTARY") {
                target = paneAt(i);
                break;
            }
        }
        if (!target) target = paneAt(pickFallbackActivePane(0, 2));
        if (target) target->setCommentaryModule(commMod, true);
        syncLeftTreeBibles();
        saveSettings();
    });
    connect(m_leftScreen, &LeftScreenWidget::dictionarySelected, this, [this](const QString& dictMod) {
        UniversalPaneWidget* target = nullptr;
        for (int i = 0; i < paneCount(); ++i) {
            if (isPaneActiveInLayout(i) && paneAt(i) && paneAt(i)->paneMode() == "DICTIONARY") {
                target = paneAt(i);
                break;
            }
        }
        if (!target) target = paneAt(pickFallbackActivePane(0, 2));
        if (target) target->setDictionaryModule(dictMod, true);
        syncLeftTreeBibles();
        saveSettings();
    });
    connect(m_leftScreen, &LeftScreenWidget::styleChanged, this, [this]() {
        saveSettings();
    });
    connect(m_leftScreen, &LeftScreenWidget::applyStyleToAllRequested, this, &MainWindow::applyStyleToAllModules);
    connect(m_leftScreen, &LeftScreenWidget::openModuleInstallerRequested, this, &MainWindow::openModuleInstallerDialog);
    connect(m_leftScreen, &LeftScreenWidget::statusNotification, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 6000);
    });

    // Connexions de la barre centrale
    connect(m_centerScreen, &CenterScreenWidget::linkStateChanged, this, [this](bool linked) {
        if (linked && paneAt(0)) {
            QString b = paneAt(0)->book();
            int ch = paneAt(0)->chapter();
            int v = paneAt(0)->verse();
            for (int i = 1; i < paneCount(); ++i) {
                if (paneAt(i) && paneAt(i)->isLinked()) {
                    paneAt(i)->loadPassage(b, ch, v, true, false);
                }
            }
        }
        saveSettings();
    });
    connect(m_centerScreen, &CenterScreenWidget::styleChanged, this, [this]() {
        applyStylePalette(m_centerScreen->paneStyle());
        saveSettings();
    });
    connect(m_centerScreen, &CenterScreenWidget::applyStyleToAllRequested, this, &MainWindow::applyStyleToAllModules);
    connect(m_centerScreen, &CenterScreenWidget::openLayoutSelectorRequested, this, [this]() {
        openLayoutSelectorDialog(false);
    });
    connect(m_centerScreen, &CenterScreenWidget::toggleLeftSidebarRequested, this, &MainWindow::toggleLeftSidebar);
    connect(m_centerScreen, &CenterScreenWidget::statusNotification, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 6000);
    });

    // Connexions universelles pour les 4 Fenêtres de base (et toute fenêtre dynamique ajoutée via ➕⬇ / ➕➡)
    for (int i = 0; i < 4; ++i) {
        UniversalPaneWidget* p = paneAt(i);
        if (p) connectPaneSignals(p);
    }

    loadSettings();
    if (paneAt(0)) {
        applyStylePalette(paneAt(0)->paneStyle());
    }

    // Ré-appliquer les largeurs et hauteurs sauvegardées après l'affichage initial de la fenêtre
    QTimer::singleShot(60, this, [this]() {
        applySavedSplitterSizes();
    });

    // Au premier démarrage (ou si l'utilisateur a coché "Proposer à chaque démarrage"),
    // ouvrir le sélecteur visuel de mise en page et de configuration d'écran (1 ou 2 écrans).
    if (m_needsInitialLayoutPrompt || m_layoutConfig.showOnStartup) {
        bool firstStart = m_needsInitialLayoutPrompt;
        QTimer::singleShot(140, this, [this, firstStart]() {
            openLayoutSelectorDialog(firstStart);
        });
    }

    statusBar()->showMessage("Prêt — Utilisez les boutons ➕⬇, ➕➡ et ✕ en haut à droite de chaque fenêtre pour ajouter ou fermer des fenêtres à volonté.");
}

MainWindow::~MainWindow() = default;

int MainWindow::paneCount() const {
    return 4 + m_dynamicSplits.size();
}

UniversalPaneWidget* MainWindow::paneAt(int index) const {
    if (index == 0 && m_centerScreen) return m_centerScreen->pane(0);
    if (index == 1 && m_centerScreen) return m_centerScreen->pane(1);
    if (index == 2 && m_rightScreen) return m_rightScreen->pane(0);
    if (index == 3 && m_rightScreen) return m_rightScreen->pane(1);
    int dynIdx = index - 4;
    if (dynIdx >= 0 && dynIdx < m_dynamicSplits.size()) {
        return m_dynamicSplits[dynIdx].createdPane;
    }
    return nullptr;
}

bool MainWindow::isPaneActiveInLayout(int index) const {
    UniversalPaneWidget* p = paneAt(index);
    if (!p || p->isHidden()) return false;
    if (m_centerScreen && m_centerScreen->isAncestorOf(p) && m_centerScreen->isHidden()) return false;
    if (m_rightScreen && m_rightScreen->isAncestorOf(p) && m_rightScreen->isHidden()) return false;
    return true;
}

int MainWindow::pickFallbackActivePane(int excludeIndex, int preferredIndex) const {
    if (preferredIndex >= 0 && preferredIndex < paneCount() && preferredIndex != excludeIndex && isPaneActiveInLayout(preferredIndex)) {
        return preferredIndex;
    }
    for (int j = paneCount() - 1; j >= 0; --j) {
        if (j != excludeIndex && isPaneActiveInLayout(j)) {
            return j;
        }
    }
    return 0;
}

void MainWindow::connectPaneSignals(UniversalPaneWidget* p) {
    if (!p) return;

    connect(p, &UniversalPaneWidget::paneConfigChanged, this, [this](int) {
        if (m_isLoadingSettings) return;
        syncLeftTreeBibles();
        saveSettings();
    });

    connect(p, &UniversalPaneWidget::passageChanged, this, [this](int sourceIdx, const QString& book, int ch, int v) {
        if (m_isLoadingSettings) return;
        m_currentBook = book;
        m_currentChapter = ch;
        m_currentVerse = v;
        m_leftScreen->syncCurrentReference(book, ch, v);

        UniversalPaneWidget* src = paneAt(sourceIdx);
        if (src && src->isLinked()) {
            for (int j = 0; j < paneCount(); ++j) {
                if (j == sourceIdx) continue;
                UniversalPaneWidget* other = paneAt(j);
                if (other && other->isLinked()) {
                    other->loadPassage(book, ch, v, true, false);
                }
            }
        }
        saveSettings();
    });

    connect(p, &UniversalPaneWidget::bibleScrolled, this, [this](int sourceIdx, double ratio, int topVisibleVerse) {
        if (m_isLoadingSettings) return;
        UniversalPaneWidget* src = paneAt(sourceIdx);
        if (!src || !src->isLinked()) return;

        for (int j = 0; j < paneCount(); ++j) {
            if (j == sourceIdx) continue;
            UniversalPaneWidget* other = paneAt(j);
            if (!other || !other->isLinked()) continue;
            if (other->book() != src->book() || other->chapter() != src->chapter()) continue;

            if (other->paneMode() == "BIBLE") {
                other->setScrollRatio(ratio);
            } else if (other->paneMode() == "COMMENTARY" || other->paneMode() == "NOTES") {
                other->updateCommentaryVerseOnly(topVisibleVerse);
            }
        }
    });

    connect(p, &UniversalPaneWidget::wordClickedInBible, this,
            [this](int sourceIdx, const QString& swordKey, const QString& strongCode, const QString& morphCode, const QString& word, int wordIndex) {
        QString b; int c = 1, v = 1;
        m_engine->splitSwordKey(swordKey, b, c, v);

        WordToken tok = m_engine->resolveClickedWordToToken(swordKey, word, strongCode, morphCode, wordIndex);
        QString resolvedStrong = !tok.strongCode.isEmpty() ? tok.strongCode : strongCode.trimmed();

        // Surligner le mot dans toutes les fenêtres Bible ouvertes sur ce chapitre
        for (int j = 0; j < paneCount(); ++j) {
            UniversalPaneWidget* bp = paneAt(j);
            if (!bp || bp->paneMode() != "BIBLE") continue;
            if (j == sourceIdx) {
                bp->highlightWordInBible(v, resolvedStrong, word, wordIndex, true);
            } else if (bp->book() == b && bp->chapter() == c) {
                bp->highlightWordInBible(v, resolvedStrong, word, -1, false);
            }
        }

        // Afficher la définition Grecque / Hébraïque dans toute fenêtre active en mode Dictionnaire
        bool shownInDict = false;
        for (int j = 0; j < paneCount(); ++j) {
            UniversalPaneWidget* dp = paneAt(j);
            if (isPaneActiveInLayout(j) && dp && dp->paneMode() == "DICTIONARY") {
                dp->showWordDefinition(swordKey, strongCode, morphCode, word, wordIndex);
                shownInDict = true;
            }
        }

        // Si aucune fenêtre active n'est en mode Dictionnaire, basculer une fenêtre active en Dictionnaire
        if (!shownInDict) {
            int targetIdx = pickFallbackActivePane(sourceIdx, 2);
            UniversalPaneWidget* dp = paneAt(targetIdx);
            if (dp) {
                dp->setPaneMode("DICTIONARY", false);
                dp->showWordDefinition(swordKey, strongCode, morphCode, word, wordIndex);
                statusBar()->showMessage(
                    QString("🏛️ Fenêtre %1 basculée sur Dictionnaire Grec/Hébreu pour « %2 ».").arg(targetIdx + 1).arg(word),
                    5000
                );
            }
        }
    });

    connect(p, &UniversalPaneWidget::aiResearchRequested, this,
            [this](int sourceIdx, const QString& swordKey, const QString& sourceLabel, const QString& selectedText, bool autoSend) {
        QStringList activeBibles = collectActiveBibles();
        bool sentToAi = false;
        for (int j = 0; j < paneCount(); ++j) {
            UniversalPaneWidget* ap = paneAt(j);
            if (isPaneActiveInLayout(j) && ap && ap->paneMode() == "AI") {
                ap->preparePassageForAiQuestion(swordKey, sourceLabel, selectedText, activeBibles, autoSend);
                sentToAi = true;
            }
        }

        if (!sentToAi) {
            int targetIdx = pickFallbackActivePane(sourceIdx, 3);
            UniversalPaneWidget* ap = paneAt(targetIdx);
            if (ap) {
                ap->setPaneMode("AI", false);
                ap->preparePassageForAiQuestion(swordKey, sourceLabel, selectedText, activeBibles, autoSend);
            }
        }

        statusBar()->showMessage(
            QString("🤖 Sélection (%1 — %2) prête dans la fenêtre IA — écrivez ou dictez (🎤) votre question.")
                .arg(m_engine->toFrenchReference(swordKey), sourceLabel),
            6000
        );
    });

    connect(p, &UniversalPaneWidget::addExcerptToPersonalNoteRequested, this,
            [this](int sourceIdx, const QString& swordKey, const QString& sourceTitle, const QString& excerptText, const QString& preferredBibleMod, bool openNotePane) {
        QString b; int c = 1, v = 1;
        m_engine->splitSwordKey(swordKey, b, c, v);

        m_engine->appendExcerptToPersonalNote(swordKey, sourceTitle, excerptText, preferredBibleMod);
        PersonalNote note = m_engine->getPersonalNote(swordKey);

        if (m_leftScreen) {
            m_leftScreen->refreshPersonalNotesTree();
        }

        // Mettre à jour le badge 📝 sur les versets des fenêtres Bible ouvertes sur ce chapitre
        for (int j = 0; j < paneCount(); ++j) {
            UniversalPaneWidget* bp = paneAt(j);
            if (bp && bp->paneMode() == "BIBLE" && bp->book() == b && bp->chapter() == c) {
                bp->refreshContent(false);
            }
        }

        // Mettre à jour toute fenêtre active déjà en mode NOTES
        bool shownInNotesPane = false;
        for (int j = 0; j < paneCount(); ++j) {
            UniversalPaneWidget* np = paneAt(j);
            if (isPaneActiveInLayout(j) && np && np->paneMode() == "NOTES") {
                np->loadPassage(b, c, v, false, false);
                np->refreshPersonalNoteView(true);
                shownInNotesPane = true;
            }
        }

        // Si l'utilisateur a demandé d'ouvrir la fiche et qu'aucune fenêtre active n'est en mode NOTES
        if (openNotePane && !shownInNotesPane) {
            int targetIdx = pickFallbackActivePane(sourceIdx, 2);
            UniversalPaneWidget* np = paneAt(targetIdx);
            if (np) {
                np->loadPassage(b, c, v, false, false);
                np->setPaneMode("NOTES", false);
                np->refreshPersonalNoteView(true);
            }
            saveSettings();
        }

        statusBar()->showMessage(
            QString("📝 Extrait #%1 (%2) ajouté à la note personnelle de %3 !")
                .arg(note.excerptCount)
                .arg(sourceTitle)
                .arg(note.frenchRef),
            6000
        );
    });

    connect(p, &UniversalPaneWidget::openPersonalNoteForVerseRequested, this,
            [this](int sourceIdx, const QString& swordKey) {
        QString b; int c = 1, v = 1;
        m_engine->splitSwordKey(swordKey, b, c, v);

        UniversalPaneWidget* target = nullptr;
        for (int j = 0; j < paneCount(); ++j) {
            if (isPaneActiveInLayout(j) && paneAt(j) && paneAt(j)->paneMode() == "NOTES") {
                target = paneAt(j);
                break;
            }
        }
        if (!target) {
            int targetIdx = pickFallbackActivePane(sourceIdx, 2);
            target = paneAt(targetIdx);
            if (target) {
                target->setPaneMode("NOTES", false);
            }
        }
        if (target) {
            target->loadPassage(b, c, v, false, false);
            target->refreshPersonalNoteView(false);
        }
        statusBar()->showMessage(
            QString("📝 Fiche d'étude personnelle ouverte pour %1.").arg(m_engine->toFrenchReference(swordKey)),
            6000
        );
        saveSettings();
    });

    connect(p, &UniversalPaneWidget::personalNoteModified, this,
            [this, p](const QString& swordKey) {
        QString b; int c = 1, v = 1;
        m_engine->splitSwordKey(swordKey, b, c, v);

        if (m_leftScreen) {
            m_leftScreen->refreshPersonalNotesTree();
        }

        for (int j = 0; j < paneCount(); ++j) {
            UniversalPaneWidget* other = paneAt(j);
            if (!other || other == p) continue;
            if (other->paneMode() == "BIBLE" && other->book() == b && other->chapter() == c) {
                other->refreshContent(false);
            } else if (other->paneMode() == "NOTES" && other->swordKey() == swordKey) {
                other->refreshPersonalNoteView(false);
            }
        }
    });

    connect(p, &UniversalPaneWidget::navigateToReferenceRequested, this, [this](int, const QString& swordKey) {
        QString b; int c = 1, v = 1;
        m_engine->splitSwordKey(swordKey, b, c, v);
        navigateTo(b, c, v, true);
    });

    connect(p, &UniversalPaneWidget::navigateToReferenceInPaneRequested, this,
            [this](int sourcePaneIdx, int targetPaneIdx, const QString& swordKey) {
        openReferenceInSpecificPane(targetPaneIdx, swordKey, QString(), sourcePaneIdx);
    });

    connect(p, &UniversalPaneWidget::requestStudyContext, this, [this](QString& outCtx, QStringList& outActiveBibles) {
        outActiveBibles = collectActiveBibles();
        QString key = QString("%1 %2:%3").arg(m_currentBook).arg(m_currentChapter).arg(m_currentVerse);
        outCtx = m_engine->buildGeminiStudyContext(
            key,
            outActiveBibles,
            collectCurrentStrongCode(),
            QString(),
            collectCurrentCommentaryModule()
        );
    });

    connect(p, &UniversalPaneWidget::apiKeyUpdated, this, [this](const QString&, const QString&) {
        for (int j = 0; j < paneCount(); ++j) {
            if (paneAt(j)) paneAt(j)->notifyGeminiUpdated();
        }
        saveSettings();
    });

    connect(p, &UniversalPaneWidget::styleChanged, this, [this, p]() {
        if (p->paneIndex() == 0 && paneAt(0)) {
            applyStylePalette(paneAt(0)->paneStyle());
        }
        saveSettings();
    });

    connect(p, &UniversalPaneWidget::applyStyleToAllRequested, this, &MainWindow::applyStyleToAllModules);

    connect(p, &UniversalPaneWidget::statusNotification, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 6000);
    });

    connect(p, &UniversalPaneWidget::splitPaneRequested, this, [this](UniversalPaneWidget* srcPane, int orient, const QString& mode) {
        splitPane(srcPane, static_cast<Qt::Orientation>(orient), mode, true);
    });

    connect(p, &UniversalPaneWidget::closePaneRequested, this, [this](UniversalPaneWidget* targetPane) {
        closePane(targetPane);
    });
}

UniversalPaneWidget* MainWindow::splitPane(UniversalPaneWidget* sourcePane, Qt::Orientation orientation, const QString& initialMode, bool recordAndSave) {
    if (!sourcePane) return nullptr;
    auto* parentSplitter = qobject_cast<QSplitter*>(sourcePane->parentWidget());
    if (!parentSplitter) return nullptr;

    int idxInParent = parentSplitter->indexOf(sourcePane);
    if (idxInParent < 0) return nullptr;

    QList<int> oldParentSizes = parentSplitter->sizes();
    int totalDim = (orientation == Qt::Vertical)
        ? std::max(320, sourcePane->height())
        : std::max(320, sourcePane->width());

    int newPaneIdx = paneCount();
    auto* newPane = new UniversalPaneWidget(newPaneIdx, m_engine.get(), m_gemini, this);
    newPane->setPaneStyle(sourcePane->paneStyle());
    newPane->setBibleModule(sourcePane->bibleModule(), false);
    newPane->setCommentaryModule(sourcePane->commentaryModule(), false);
    newPane->setDictionaryModule(sourcePane->dictionaryModule(), false);
    newPane->setAiResponseLanguage(sourcePane->aiResponseLanguage(), false);
    newPane->setLinked(sourcePane->isLinked(), false);
    newPane->loadPassage(sourcePane->book(), sourcePane->chapter(), sourcePane->verse(), true, false);
    newPane->setPaneMode(initialMode, false);

    connectPaneSignals(newPane);

    auto* subSplitter = new QSplitter(orientation, parentSplitter);
    subSplitter->setHandleWidth(6);
    subSplitter->setChildrenCollapsible(false);
    connect(subSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (!m_isLoadingSettings) saveSettings();
    });

    parentSplitter->insertWidget(idxInParent, subSplitter);
    subSplitter->addWidget(sourcePane);
    subSplitter->addWidget(newPane);
    sourcePane->show();
    newPane->show();
    subSplitter->show();

    if (oldParentSizes.size() == parentSplitter->count()) {
        parentSplitter->setSizes(oldParentSizes);
    }
    subSplitter->setSizes({totalDim / 2, totalDim / 2});

    DynamicSplitRecord rec;
    rec.targetPaneIndex = sourcePane->paneIndex();
    rec.orientation = orientation;
    rec.createdPane = newPane;
    rec.createdSplitter = subSplitter;
    m_dynamicSplits.push_back(rec);

    if (recordAndSave && !m_isLoadingSettings) {
        syncLeftTreeBibles();
        saveSettings();
        QString dirText = (orientation == Qt::Vertical) ? "en dessous de" : "à droite de";
        statusBar()->showMessage(
            QString("➕ Fenêtre %1 (%2) ajoutée %3 la Fenêtre %4 — glissez la barre de séparation pour ajuster sa taille.")
                .arg(newPaneIdx + 1)
                .arg(initialMode)
                .arg(dirText)
                .arg(sourcePane->paneIndex() + 1),
            6500
        );
    }
    return newPane;
}

void MainWindow::closePane(UniversalPaneWidget* targetPane) {
    if (!targetPane) return;

    int activeCount = 0;
    for (int i = 0; i < paneCount(); ++i) {
        if (isPaneActiveInLayout(i)) ++activeCount;
    }
    if (activeCount <= 1) {
        statusBar()->showMessage("⚠️ Impossible de fermer la dernière fenêtre : il doit rester au moins une fenêtre ouverte à l'écran.", 6000);
        return;
    }

    int closedIdx = targetPane->paneIndex();

    // Cas 1 : Fenêtre dynamique créée via ➕⬇ ou ➕➡
    int dynIdx = -1;
    for (int k = 0; k < m_dynamicSplits.size(); ++k) {
        if (m_dynamicSplits[k].createdPane == targetPane) {
            dynIdx = k;
            break;
        }
    }

    if (dynIdx >= 0) {
        QSplitter* subSplitter = m_dynamicSplits[dynIdx].createdSplitter;
        if (subSplitter) {
            QWidget* sibling = nullptr;
            if (subSplitter->count() == 2) {
                sibling = (subSplitter->widget(0) == targetPane) ? subSplitter->widget(1) : subSplitter->widget(0);
            }
            auto* grandParent = qobject_cast<QSplitter*>(subSplitter->parentWidget());
            if (grandParent && sibling) {
                QList<int> gpSizes = grandParent->sizes();
                int posInGp = grandParent->indexOf(subSplitter);
                if (posInGp >= 0) {
                    grandParent->insertWidget(posInGp, sibling);
                    sibling->show();
                    if (gpSizes.size() == grandParent->count()) {
                        grandParent->setSizes(gpSizes);
                    }
                }
            }
            subSplitter->hide();
            subSplitter->deleteLater();
        }
        m_dynamicSplits.removeAt(dynIdx);
        targetPane->hide();
        targetPane->deleteLater();

        // Ré-indexer les fenêtres dynamiques restantes
        for (int k = 0; k < m_dynamicSplits.size(); ++k) {
            if (m_dynamicSplits[k].createdPane) {
                m_dynamicSplits[k].createdPane->setPaneIndex(4 + k);
            }
        }
    } else {
        // Cas 2 : L'une des 4 fenêtres de base (0..3)
        targetPane->setVisible(false);

        // Si plus aucune fenêtre dans la colonne de droite n'est visible, masquer m_rightScreen pour donner toute la largeur au centre
        if (m_rightScreen) {
            bool anyRightVisible = false;
            for (int i = 0; i < paneCount(); ++i) {
                UniversalPaneWidget* p = paneAt(i);
                if (p && !p->isHidden() && m_rightScreen->isAncestorOf(p)) {
                    anyRightVisible = true;
                    break;
                }
            }
            if (!anyRightVisible) {
                m_rightScreen->setVisible(false);
            }
        }
    }

    syncLeftTreeBibles();
    saveSettings();
    statusBar()->showMessage(
        QString("✕ Fenêtre %1 fermée — cliquez sur ➕⬇ ou ➕➡ dans n'importe quelle fenêtre (ou sur « 📐 Mise en page ») pour en rouvrir une.")
            .arg(closedIdx + 1),
        6500
    );
}

void MainWindow::clearDynamicSplits() {
    // Dépiler en ordre inverse (LIFO) pour restaurer proprement la hiérarchie des QSplitters de base
    for (int k = m_dynamicSplits.size() - 1; k >= 0; --k) {
        QSplitter* subSplitter = m_dynamicSplits[k].createdSplitter;
        UniversalPaneWidget* createdPane = m_dynamicSplits[k].createdPane;
        if (subSplitter) {
            QWidget* sibling = nullptr;
            if (subSplitter->count() == 2) {
                sibling = (subSplitter->widget(0) == createdPane) ? subSplitter->widget(1) : subSplitter->widget(0);
            }
            auto* grandParent = qobject_cast<QSplitter*>(subSplitter->parentWidget());
            if (grandParent && sibling) {
                int posInGp = grandParent->indexOf(subSplitter);
                if (posInGp >= 0) {
                    grandParent->insertWidget(posInGp, sibling);
                }
            }
            delete subSplitter;
        }
        delete createdPane;
    }
    m_dynamicSplits.clear();
}

void MainWindow::toggleLeftSidebar() {
    m_layoutConfig.showLeftSidebar = !m_layoutConfig.showLeftSidebar;
    if (m_leftScreen) {
        m_leftScreen->setVisible(m_layoutConfig.showLeftSidebar);
    }
    if (m_centerScreen) {
        m_centerScreen->setLeftSidebarVisibleState(m_layoutConfig.showLeftSidebar);
    }
    saveSettings();
}

void MainWindow::openLayoutSelectorDialog(bool isFirstStartup) {
    // Synchroniser les modes actuels des 4 fenêtres dans m_layoutConfig
    for (int i = 0; i < 4; ++i) {
        if (paneAt(i)) {
            m_layoutConfig.paneModes[i] = paneAt(i)->paneMode();
        }
    }

    LayoutSelectorDialog dlg(m_layoutConfig, isFirstStartup, this);
    connect(&dlg, &LayoutSelectorDialog::openModuleInstallerRequested, this, &MainWindow::openModuleInstallerDialog);
    if (dlg.exec() == QDialog::Accepted) {
        m_needsInitialLayoutPrompt = false;
        applyWorkspaceLayout(dlg.selectedConfig(), true, true);
        saveSettings();
    } else if (isFirstStartup) {
        m_needsInitialLayoutPrompt = false;
        saveSettings();
    }
}

void MainWindow::applyWorkspaceLayout(const WorkspaceLayoutConfig& cfg, bool applyScreenGeometry, bool updatePaneModes) {
    m_isLoadingSettings = true;
    m_layoutConfig = cfg;

    // Si l'utilisateur choisit un modèle prédéfini dans « 📐 Mise en page », réinitialiser les divisions dynamiques
    if (updatePaneModes) {
        clearDynamicSplits();
    }

    // S'assurer que les fenêtres de base principales (Fenêtre 1 et Fenêtre 3) sont de nouveau visibles
    if (m_centerScreen && m_centerScreen->pane(0)) m_centerScreen->pane(0)->setVisible(true);
    if (m_rightScreen && m_rightScreen->pane(0)) m_rightScreen->pane(0)->setVisible(true);

    // 1. Appliquer les modes des fenêtres si demandé
    if (updatePaneModes) {
        for (int i = 0; i < 4; ++i) {
            if (paneAt(i) && !cfg.paneModes[i].isEmpty()) {
                paneAt(i)->setPaneMode(cfg.paneModes[i], false);
            }
        }
    }

    // 2. Visibilité du panneau Bibliothèque à gauche
    if (m_leftScreen) {
        m_leftScreen->setVisible(cfg.showLeftSidebar);
    }
    if (m_centerScreen) {
        m_centerScreen->setLeftSidebarVisibleState(cfg.showLeftSidebar);
    }

    // 3. Visibilité et orientation des colonnes (2_COLS, 3_MIXED, 4_MIXED, 3_COLS, 4_COLS)
    if (cfg.presetId == "2_COLS") {
        m_centerScreen->setVisible(true);
        m_centerScreen->setSecondPaneVisible(true);
        m_rightScreen->setVisible(false);
    } else if (cfg.presetId == "3_MIXED") {
        m_centerScreen->setVisible(true);
        m_centerScreen->setSecondPaneVisible(false);
        m_rightScreen->setVisible(true);
        m_rightScreen->setOrientation(Qt::Vertical);
        m_rightScreen->setSecondPaneVisible(true);
    } else if (cfg.presetId == "4_MIXED") {
        m_centerScreen->setVisible(true);
        m_centerScreen->setSecondPaneVisible(true);
        m_rightScreen->setVisible(true);
        m_rightScreen->setOrientation(Qt::Vertical);
        m_rightScreen->setSecondPaneVisible(true);
    } else if (cfg.presetId == "3_COLS") {
        m_centerScreen->setVisible(true);
        m_centerScreen->setSecondPaneVisible(true);
        m_rightScreen->setVisible(true);
        m_rightScreen->setOrientation(Qt::Horizontal);
        m_rightScreen->setSecondPaneVisible(false);
    } else { // "4_COLS"
        m_centerScreen->setVisible(true);
        m_centerScreen->setSecondPaneVisible(true);
        m_rightScreen->setVisible(true);
        m_rightScreen->setOrientation(Qt::Horizontal);
        m_rightScreen->setSecondPaneVisible(true);
    }

    // 4. Barre d'ajustement de hauteur sur l'écran de droite (uniquement en mode 2 écrans si activé)
    bool showRightAdjuster = (cfg.screenMode == 2 && cfg.rightHeightAdjuster);
    m_rightScreen->setBottomHeightAdjusterVisible(showRightAdjuster);

    // 5. Géométrie des écrans et proportions des colonnes (quand l'utilisateur applique une mise en page)
    if (applyScreenGeometry) {
        const auto screens = QGuiApplication::screens();
        int totalW = width();
        int totalH = height();
        int leftW = cfg.showLeftSidebar ? 320 : 0;

        if (cfg.screenMode == 2 && screens.size() >= 2) {
            QRect s0 = screens[0]->availableGeometry();
            QRect s1 = screens[1]->availableGeometry();
            QRect united = s0.united(s1);
            showNormal();
            setGeometry(united);
            totalW = united.width();
            totalH = united.height();

            if (cfg.presetId == "2_COLS") {
                m_savedMainSplitterSizes = {leftW, totalW - leftW, 0};
                m_savedCenterSplitterSizes = {std::max(300, s0.width() - leftW), s1.width()};
            } else {
                int centerW = std::max(400, s0.width() - leftW);
                int rightW = std::max(400, s1.width());
                m_savedMainSplitterSizes = {leftW, centerW, rightW};
                m_savedCenterSplitterSizes = {centerW / 2, centerW / 2};
                if (m_rightScreen->orientation() == Qt::Vertical) {
                    m_savedRightSplitterSizes = {s1.height() / 2, s1.height() / 2};
                } else {
                    m_savedRightSplitterSizes = {rightW / 2, rightW / 2};
                }
            }

            if (showRightAdjuster && s1.height() < s0.height()) {
                int topH = std::max(300, s1.height() - 40);
                int botH = std::max(40, s0.height() - s1.height());
                m_savedRightVerticalSplitterSizes = {topH, botH};
            }
        } else {
            // Mode 1 Écran (ou 1 seul moniteur branché)
            QScreen* scr = screen() ? screen() : QGuiApplication::primaryScreen();
            if (scr) {
                QRect avail = scr->availableGeometry();
                showNormal();
                setGeometry(avail);
                totalW = avail.width();
                totalH = avail.height();
            }
            int remW = std::max(600, totalW - leftW);
            if (cfg.presetId == "2_COLS") {
                m_savedMainSplitterSizes = {leftW, remW, 0};
                m_savedCenterSplitterSizes = {remW / 2, remW / 2};
            } else if (cfg.presetId == "3_MIXED") {
                int cW = int(remW * 0.52);
                int rW = remW - cW;
                m_savedMainSplitterSizes = {leftW, cW, rW};
                m_savedRightSplitterSizes = {totalH / 2, totalH / 2};
            } else if (cfg.presetId == "4_MIXED") {
                int cW = int(remW * 0.60);
                int rW = remW - cW;
                m_savedMainSplitterSizes = {leftW, cW, rW};
                m_savedCenterSplitterSizes = {cW / 2, cW / 2};
                m_savedRightSplitterSizes = {totalH / 2, totalH / 2};
            } else if (cfg.presetId == "3_COLS") {
                int cW = int(remW * 0.66);
                int rW = remW - cW;
                m_savedMainSplitterSizes = {leftW, cW, rW};
                m_savedCenterSplitterSizes = {cW / 2, cW / 2};
            } else { // "4_COLS"
                int cW = remW / 2;
                int rW = remW - cW;
                m_savedMainSplitterSizes = {leftW, cW, rW};
                m_savedCenterSplitterSizes = {cW / 2, cW / 2};
                m_savedRightSplitterSizes = {rW / 2, rW / 2};
            }
        }
    }

    m_isLoadingSettings = false;
    applySavedSplitterSizes();
    syncLeftTreeBibles();

    statusBar()->showMessage(
        QString("📐 Mise en page appliquée : Mode %1 Écran(s) — Disposition « %2 ».")
            .arg(cfg.screenMode)
            .arg(cfg.presetId),
        6000
    );
}

QStringList MainWindow::collectActiveBibles() const {
    QStringList list;
    for (int i = 0; i < paneCount(); ++i) {
        UniversalPaneWidget* p = paneAt(i);
        if (isPaneActiveInLayout(i) && p && p->paneMode() == "BIBLE" && !p->bibleModule().isEmpty()) {
            if (!list.contains(p->bibleModule())) {
                list.push_back(p->bibleModule());
            }
        }
    }
    if (list.isEmpty()) {
        if (paneAt(0) && !paneAt(0)->bibleModule().isEmpty()) list.push_back(paneAt(0)->bibleModule());
        if (paneAt(1) && !paneAt(1)->bibleModule().isEmpty() && !list.contains(paneAt(1)->bibleModule())) {
            list.push_back(paneAt(1)->bibleModule());
        }
    }
    return list;
}

QString MainWindow::collectCurrentStrongCode() const {
    for (int i = 0; i < paneCount(); ++i) {
        UniversalPaneWidget* p = paneAt(i);
        if (isPaneActiveInLayout(i) && p && p->paneMode() == "DICTIONARY" && !p->currentStrongCode().isEmpty()) {
            return p->currentStrongCode();
        }
    }
    return paneAt(2) ? paneAt(2)->currentStrongCode() : QString("G3056");
}

QString MainWindow::collectCurrentCommentaryModule() const {
    for (int i = 0; i < paneCount(); ++i) {
        UniversalPaneWidget* p = paneAt(i);
        if (isPaneActiveInLayout(i) && p && p->paneMode() == "COMMENTARY" && !p->commentaryModule().isEmpty()) {
            return p->commentaryModule();
        }
    }
    return QString("ALL_FR");
}

void MainWindow::syncLeftTreeBibles() {
    QString b1 = (paneAt(0) && paneAt(0)->paneMode() == "BIBLE") ? paneAt(0)->bibleModule() : QString();
    QString b2 = (paneAt(1) && paneAt(1)->paneMode() == "BIBLE") ? paneAt(1)->bibleModule() : QString();
    if (b1.isEmpty() && paneAt(0)) b1 = paneAt(0)->bibleModule();
    if (b2.isEmpty() && paneAt(1)) b2 = paneAt(1)->bibleModule();
    if (m_leftScreen) {
        m_leftScreen->syncSelectedBibles(b1, b2);
    }
}

void MainWindow::applySavedSplitterSizes() {
    m_isLoadingSettings = true;
    if (m_savedMainSplitterSizes.size() == 3 &&
        (m_savedMainSplitterSizes[0] + m_savedMainSplitterSizes[1] + m_savedMainSplitterSizes[2] > 100)) {
        m_mainHorizontalSplitter->setSizes(m_savedMainSplitterSizes);
    }
    if (m_savedCenterSplitterSizes.size() == 2 &&
        (m_savedCenterSplitterSizes[0] + m_savedCenterSplitterSizes[1] > 50)) {
        m_centerScreen->setSplitterSizes(m_savedCenterSplitterSizes);
    }
    if (m_savedRightSplitterSizes.size() == 2 &&
        (m_savedRightSplitterSizes[0] + m_savedRightSplitterSizes[1] > 50)) {
        m_rightScreen->setSplitterSizes(m_savedRightSplitterSizes);
    }
    if (m_savedRightVerticalSplitterSizes.size() == 2 &&
        (m_savedRightVerticalSplitterSizes[0] + m_savedRightVerticalSplitterSizes[1] > 50)) {
        m_rightScreen->setVerticalSplitterSizes(m_savedRightVerticalSplitterSizes);
    }
    m_isLoadingSettings = false;
}

void MainWindow::applyStyleToAllModules(const PaneStyle& style) {
    PaneStyle leftSt = style;
    leftSt.fontSizePt = std::clamp(style.fontSizePt - 3, 10, 22);
    m_leftScreen->setPaneStyle(leftSt);
    for (int i = 0; i < paneCount(); ++i) {
        if (paneAt(i)) paneAt(i)->setPaneStyle(style);
    }
    applyStylePalette(style);
    saveSettings();
    statusBar()->showMessage("✨ Style (couleurs, contraste et police) appliqué à toutes les fenêtres.", 5000);
}

void MainWindow::openModuleInstallerDialog() {
    ModuleInstallerDialog dlg(m_engine.get(), this);
    connect(&dlg, &ModuleInstallerDialog::modulesChanged, this, [this]() {
        m_leftScreen->reloadModuleLists();
        for (int i = 0; i < paneCount(); ++i) {
            if (paneAt(i)) {
                paneAt(i)->reloadModuleLists();
            }
        }
        syncLeftTreeBibles();
        statusBar()->showMessage("📚 Bibliothèque mise à jour — les nouveaux modules sont disponibles dans toutes les fenêtres !", 8000);
    });
    dlg.exec();
}

void MainWindow::applyStylePalette(const PaneStyle& style) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette pal;
    QColor bg(style.bgColor);
    QColor card(style.cardBgColor);
    QColor txt(style.textColor);
    QColor acc(style.accentColor);

    pal.setColor(QPalette::Window, card.isValid() ? card : QColor("#0F172A"));
    pal.setColor(QPalette::WindowText, txt.isValid() ? txt : QColor("#E2E8F0"));
    pal.setColor(QPalette::Base, bg.isValid() ? bg : QColor("#141821"));
    pal.setColor(QPalette::AlternateBase, card.isValid() ? card : QColor("#1E293B"));
    pal.setColor(QPalette::ToolTipBase, card.isValid() ? card : QColor("#1E293B"));
    pal.setColor(QPalette::ToolTipText, txt.isValid() ? txt : QColor("#F8FAFC"));
    pal.setColor(QPalette::Text, txt.isValid() ? txt : QColor("#E2E8F0"));
    pal.setColor(QPalette::Button, card.isValid() ? card : QColor("#1E293B"));
    pal.setColor(QPalette::ButtonText, txt.isValid() ? txt : QColor("#E2E8F0"));
    pal.setColor(QPalette::BrightText, Qt::white);
    pal.setColor(QPalette::Highlight, acc.isValid() ? acc : QColor("#0284C7"));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    QApplication::setPalette(pal);
}

void MainWindow::navigateTo(const QString& swordBook, int chapter, int verse, bool recordHistory) {
    m_currentBook = swordBook;
    m_currentChapter = chapter;
    m_currentVerse = verse;

    QString key = QString("%1 %2:%3").arg(swordBook).arg(chapter).arg(verse);
    if (recordHistory) {
        if (m_historyIndex < 0 || m_history.value(m_historyIndex) != key) {
            while (m_history.size() > m_historyIndex + 1) {
                m_history.removeLast();
            }
            m_history.push_back(key);
            m_historyIndex = m_history.size() - 1;
        }
    }

    m_leftScreen->syncCurrentReference(swordBook, chapter, verse);

    bool anyLinked = false;
    for (int i = 0; i < paneCount(); ++i) {
        UniversalPaneWidget* p = paneAt(i);
        if (p && p->isLinked()) {
            p->loadPassage(swordBook, chapter, verse, true, false);
            anyLinked = true;
        }
    }
    if ((!anyLinked || (paneAt(0) && !paneAt(0)->isLinked())) && paneAt(0)) {
        paneAt(0)->loadPassage(swordBook, chapter, verse, true, false);
    }

    saveSettings();
}

void MainWindow::openReferenceInSpecificPane(int targetPaneIndex, const QString& swordKey, const QString& preferredBibleMod, int sourcePaneIndex) {
    QString b; int c = 1, v = 1;
    m_engine->splitSwordKey(swordKey, b, c, v);
    if (b.isEmpty()) return;

    const QString displayRef = m_engine->toFrenchReference(swordKey);

    // Cas 1 : Création d'une nouvelle fenêtre Bible dédiée (sans perturber l'étude principale)
    if (targetPaneIndex == -1) {
        UniversalPaneWidget* src = paneAt(sourcePaneIndex);
        if (!src || !isPaneActiveInLayout(sourcePaneIndex)) {
            src = paneAt(0);
        }
        UniversalPaneWidget* created = splitPane(src, Qt::Vertical, "BIBLE", true);
        if (created) {
            created->setLinked(false, false);
            if (!preferredBibleMod.isEmpty()) {
                created->setBibleModule(preferredBibleMod, false);
            }
            created->loadPassage(b, c, v, true, false);
            syncLeftTreeBibles();
            saveSettings();
            statusBar()->showMessage(
                SwordEngine::uiTr(
                    QString("📖 %1 ouvert dans la nouvelle Fenêtre %2 (indépendante de l'étude principale).").arg(displayRef).arg(created->paneIndex() + 1),
                    QString("📖 %1 opened in new Window %2 (independent from main study).").arg(displayRef).arg(created->paneIndex() + 1),
                    QString("📖 %1 abierto en la nueva Ventana %2 (independiente del estudio principal).").arg(displayRef).arg(created->paneIndex() + 1)
                ),
                6000
            );
        }
        return;
    }

    // Cas 2 : Fenêtre 1 (fenêtre de travail principale par défaut)
    if (targetPaneIndex == 0) {
        if (paneAt(0)) {
            if (!preferredBibleMod.isEmpty()) {
                paneAt(0)->setBibleModule(preferredBibleMod, false);
            }
            if (paneAt(0)->paneMode() != "BIBLE") {
                paneAt(0)->setPaneMode("BIBLE", false);
            }
        }
        navigateTo(b, c, v, true);
        syncLeftTreeBibles();
        statusBar()->showMessage(
            SwordEngine::uiTr(
                QString("📖 %1 ouvert dans la Fenêtre 1 (fenêtre par défaut).").arg(displayRef),
                QString("📖 %1 opened in Window 1 (default window).").arg(displayRef),
                QString("📖 %1 abierto en la Ventana 1 (ventana por defecto).").arg(displayRef)
            ),
            5000
        );
        return;
    }

    // Cas 3 : Fenêtre spécifique choisie par clic droit (Fenêtre 2, 3, 4, 5...)
    UniversalPaneWidget* target = paneAt(targetPaneIndex);
    if (!target) {
        targetPaneIndex = pickFallbackActivePane(0, targetPaneIndex);
        target = paneAt(targetPaneIndex);
    }
    if (!target) return;

    // Rendre la fenêtre cible (et son conteneur parent) visible si elle était masquée
    target->setVisible(true);
    if (m_centerScreen && m_centerScreen->isAncestorOf(target)) {
        m_centerScreen->setVisible(true);
    }
    if (m_rightScreen && m_rightScreen->isAncestorOf(target)) {
        if (m_rightScreen->isHidden()) {
            m_rightScreen->setVisible(true);
            QList<int> s = m_mainHorizontalSplitter->sizes();
            if (s.size() == 3 && s[2] < 120) {
                int give = std::max(360, (s[1] + s[2]) / 2);
                s[1] = std::max(360, s[1] - give);
                s[2] = give;
                m_mainHorizontalSplitter->setSizes(s);
            }
        }
    }

    // Délier cette fenêtre pour ne pas interrompre l'étude en cours dans la Fenêtre 1
    target->setLinked(false, false);
    if (targetPaneIndex == 1 && m_centerScreen) {
        m_centerScreen->setColumnsLinked(false);
    }

    if (!preferredBibleMod.isEmpty()) {
        target->setBibleModule(preferredBibleMod, false);
    }
    if (target->paneMode() != "BIBLE") {
        target->setPaneMode("BIBLE", false);
    }

    target->loadPassage(b, c, v, true, false);
    syncLeftTreeBibles();
    saveSettings();

    statusBar()->showMessage(
        SwordEngine::uiTr(
            QString("📖 %1 ouvert dans la Fenêtre %2 (sans modifier votre étude dans la Fenêtre 1).").arg(displayRef).arg(targetPaneIndex + 1),
            QString("📖 %1 opened in Window %2 (without changing your study in Window 1).").arg(displayRef).arg(targetPaneIndex + 1),
            QString("📖 %1 abierto en la Ventana %2 (sin modificar su estudio en la Ventana 1).").arg(displayRef).arg(targetPaneIndex + 1)
        ),
        6000
    );
}

QString MainWindow::settingsFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + "/.config/BibleStudyStudio";
    QDir().mkpath(dir);
    return dir + "/config_2cols.json";
}

void MainWindow::loadSettings() {
    m_isLoadingSettings = true;
    clearDynamicSplits();

    const auto screens = QGuiApplication::screens();
    bool hasTwoMonitors = (screens.size() >= 2);
    m_layoutConfig.screenMode = hasTwoMonitors ? 2 : 1;
    m_layoutConfig.presetId = hasTwoMonitors ? "4_COLS" : "3_MIXED";
    m_layoutConfig.showLeftSidebar = true;
    m_layoutConfig.rightHeightAdjuster = hasTwoMonitors && (screens[0]->size().height() != screens[1]->size().height());
    m_layoutConfig.showOnStartup = false;

    QFile f(settingsFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        m_needsInitialLayoutPrompt = true;
        m_savedMainSplitterSizes = {380, 1100, 1000};
        m_savedCenterSplitterSizes = {550, 550};
        m_savedRightSplitterSizes = {480, 520};
        m_savedRightVerticalSplitterSizes = {760, 210};
        applyWorkspaceLayout(m_layoutConfig, false, false);
        applySavedSplitterSizes();
        syncLeftTreeBibles();
        m_isLoadingSettings = false;
        navigateTo(m_currentBook, m_currentChapter, m_currentVerse, true);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        m_needsInitialLayoutPrompt = true;
        m_isLoadingSettings = false;
        navigateTo(m_currentBook, m_currentChapter, m_currentVerse, true);
        return;
    }
    QJsonObject obj = doc.object();

    if (!obj.contains("layoutPreset")) {
        m_needsInitialLayoutPrompt = true;
    } else {
        m_layoutConfig.presetId = obj.value("layoutPreset").toString(m_layoutConfig.presetId);
        m_layoutConfig.screenMode = obj.value("screenMode").toInt(m_layoutConfig.screenMode);
        m_layoutConfig.showLeftSidebar = obj.value("showLeftSidebar").toBool(true);
        m_layoutConfig.rightHeightAdjuster = obj.value("rightHeightAdjuster").toBool(m_layoutConfig.rightHeightAdjuster);
        m_layoutConfig.showOnStartup = obj.value("showLayoutOnStartup").toBool(false);
    }

    if (obj.contains("windowGeometry")) {
        QByteArray geom = QByteArray::fromBase64(obj.value("windowGeometry").toString().toLatin1());
        if (!geom.isEmpty()) {
            restoreGeometry(geom);
        }
    }

    auto readIntArray = [](const QJsonObject& o, const QString& key) -> QList<int> {
        QList<int> res;
        if (o.contains(key) && o.value(key).isArray()) {
            for (const auto& v : o.value(key).toArray()) {
                res.push_back(v.toInt());
            }
        }
        return res;
    };

    m_savedMainSplitterSizes = readIntArray(obj, "mainSplitterSizes");
    if (m_savedMainSplitterSizes.size() != 3) {
        m_savedMainSplitterSizes = {380, 1100, 1000};
    }

    m_savedCenterSplitterSizes = readIntArray(obj, "centerSplitterSizes");
    if (m_savedCenterSplitterSizes.size() != 2) {
        m_savedCenterSplitterSizes = {550, 550};
    }

    m_savedRightSplitterSizes = readIntArray(obj, "rightSplitterSizes");
    if (m_savedRightSplitterSizes.size() != 2) {
        m_savedRightSplitterSizes = {480, 520};
    }

    m_savedRightVerticalSplitterSizes = readIntArray(obj, "rightVerticalSplitterSizes");
    if (m_savedRightVerticalSplitterSizes.size() != 2) {
        m_savedRightVerticalSplitterSizes = {760, 210};
    }

    if (obj.contains("apiKey") && !obj.value("apiKey").toString().isEmpty()) {
        m_gemini->setApiKey(obj.value("apiKey").toString());
    }
    if (obj.contains("model")) {
        m_gemini->setModel(obj.value("model").toString());
    }
    m_currentBook = obj.value("currentBook").toString("John");
    m_currentChapter = obj.value("currentChapter").toInt(1);
    m_currentVerse = obj.value("currentVerse").toInt(1);

    if (obj.contains("leftStyle") && obj.value("leftStyle").isObject()) {
        m_leftScreen->setPaneStyle(PaneStyle::fromJson(obj.value("leftStyle").toObject()));
    }

    // Nouveau format : tableau JSON "panes" pour les 4 fenêtres interchangeables
    if (obj.contains("panes") && obj.value("panes").isArray()) {
        QJsonArray panesArr = obj.value("panes").toArray();
        for (int i = 0; i < 4 && i < panesArr.size(); ++i) {
            if (paneAt(i) && panesArr.at(i).isObject()) {
                paneAt(i)->fromJson(panesArr.at(i).toObject());
                m_layoutConfig.paneModes[i] = paneAt(i)->paneMode();
            }
        }
        if (paneAt(0) && paneAt(1)) {
            m_centerScreen->setColumnsLinked(paneAt(0)->isLinked() && paneAt(1)->isLinked());
        }
    } else {
        // Rétrocompatibilité avec les configurations précédentes
        if (obj.contains("centerStyle") && obj.value("centerStyle").isObject()) {
            PaneStyle cs = PaneStyle::fromJson(obj.value("centerStyle").toObject());
            if (paneAt(0)) paneAt(0)->setPaneStyle(cs);
            if (paneAt(1)) paneAt(1)->setPaneStyle(cs);
        }
        if (obj.contains("dictStyle") && obj.value("dictStyle").isObject() && paneAt(2)) {
            paneAt(2)->setPaneStyle(PaneStyle::fromJson(obj.value("dictStyle").toObject()));
        }
        if (obj.contains("commStyle") && obj.value("commStyle").isObject() && paneAt(3)) {
            paneAt(3)->setPaneStyle(PaneStyle::fromJson(obj.value("commStyle").toObject()));
        }

        QString b1 = obj.value("bible1").toString("FreSegond");
        QString b2 = obj.value("bible2").toString("KJV");
        if (paneAt(0)) {
            paneAt(0)->setPaneMode("BIBLE", false);
            paneAt(0)->setBibleModule(b1, false);
        }
        if (paneAt(1)) {
            paneAt(1)->setPaneMode("BIBLE", false);
            paneAt(1)->setBibleModule(b2, false);
        }
        if (paneAt(2)) {
            paneAt(2)->setPaneMode("DICTIONARY", false);
        }
        if (paneAt(3)) {
            paneAt(3)->setPaneMode("AI", false);
            if (obj.contains("commMod")) {
                paneAt(3)->setCommentaryModule(obj.value("commMod").toString("ALL_FR"), false);
            }
            if (obj.contains("aiChatMode")) {
                paneAt(3)->setAiChatMode(obj.value("aiChatMode").toBool(true));
            }
            QList<int> aiVertSizes = readIntArray(obj, "aiVerticalSplitterSizes");
            if (aiVertSizes.size() == 2) {
                paneAt(3)->setAiSplitterSizes(aiVertSizes);
            }
        }

        bool linked = obj.value("columnsLinked").toBool(true);
        m_centerScreen->setColumnsLinked(linked);
        for (int i = 0; i < 4; ++i) {
            if (paneAt(i)) {
                paneAt(i)->loadPassage(m_currentBook, m_currentChapter, m_currentVerse, true, false);
            }
        }
    }

    applyWorkspaceLayout(m_layoutConfig, false, false);

    // Restaurer les divisions dynamiques ajoutées via ➕⬇ / ➕➡ (s'il y en a)
    if (obj.contains("dynamicSplits") && obj.value("dynamicSplits").isArray()) {
        QJsonArray splitsArr = obj.value("dynamicSplits").toArray();
        for (const auto& val : splitsArr) {
            if (!val.isObject()) continue;
            QJsonObject so = val.toObject();
            int targetIdx = so.value("targetPaneIndex").toInt(0);
            Qt::Orientation orient = (so.value("orientation").toString("V") == "H") ? Qt::Horizontal : Qt::Vertical;
            QJsonObject paneObj = so.value("pane").toObject();
            QString initMode = paneObj.value("paneMode").toString("COMMENTARY");

            UniversalPaneWidget* targetPane = paneAt(targetIdx);
            if (targetPane) {
                UniversalPaneWidget* created = splitPane(targetPane, orient, initMode, false);
                if (created) {
                    created->fromJson(paneObj);
                    QList<int> spSizes = readIntArray(so, "splitterSizes");
                    if (!m_dynamicSplits.isEmpty() && m_dynamicSplits.back().createdSplitter && spSizes.size() == 2 && (spSizes[0] + spSizes[1] > 40)) {
                        m_dynamicSplits.back().createdSplitter->setSizes(spSizes);
                    }
                }
            }
        }
    }

    // Restaurer la visibilité personnalisée des 4 fenêtres de base (si l'utilisateur en a fermé une avec ✕)
    if (obj.contains("basePaneVisible") && obj.value("basePaneVisible").isArray()) {
        QJsonArray visArr = obj.value("basePaneVisible").toArray();
        if (visArr.size() == 4) {
            for (int i = 0; i < 4; ++i) {
                if (paneAt(i)) {
                    paneAt(i)->setVisible(visArr.at(i).toBool(true));
                }
            }
            // Vérifier si m_rightScreen contient au moins une fenêtre visible
            if (m_rightScreen) {
                bool anyRightVis = false;
                for (int i = 0; i < paneCount(); ++i) {
                    UniversalPaneWidget* p = paneAt(i);
                    if (p && !p->isHidden() && m_rightScreen->isAncestorOf(p)) {
                        anyRightVis = true;
                        break;
                    }
                }
                m_rightScreen->setVisible(anyRightVis);
            }
            // Garantir qu'au moins une fenêtre est visible à l'écran
            int activeCount = 0;
            for (int i = 0; i < paneCount(); ++i) {
                if (isPaneActiveInLayout(i)) ++activeCount;
            }
            if (activeCount == 0 && paneAt(0)) {
                m_centerScreen->setVisible(true);
                paneAt(0)->setVisible(true);
            }
        }
    }

    for (int i = 0; i < paneCount(); ++i) {
        if (paneAt(i)) paneAt(i)->notifyGeminiUpdated();
    }

    syncLeftTreeBibles();
    m_leftScreen->syncCurrentReference(m_currentBook, m_currentChapter, m_currentVerse);
    m_isLoadingSettings = false;
    applySavedSplitterSizes();
}

void MainWindow::saveSettings() const {
    if (m_isLoadingSettings) return;

    QJsonObject obj;
    obj["windowGeometry"] = QString::fromLatin1(saveGeometry().toBase64());
    if (!m_needsInitialLayoutPrompt) {
        obj["layoutPreset"] = m_layoutConfig.presetId;
        obj["screenMode"] = m_layoutConfig.screenMode;
        obj["showLeftSidebar"] = m_layoutConfig.showLeftSidebar;
        obj["rightHeightAdjuster"] = m_layoutConfig.rightHeightAdjuster;
        obj["showLayoutOnStartup"] = m_layoutConfig.showOnStartup;
    }

    auto toJsonArray = [](const QList<int>& list) -> QJsonArray {
        QJsonArray arr;
        for (int v : list) arr.append(v);
        return arr;
    };

    QList<int> mainSizes = m_mainHorizontalSplitter ? m_mainHorizontalSplitter->sizes() : m_savedMainSplitterSizes;
    if (mainSizes.size() == 3 && (mainSizes[0] + mainSizes[1] + mainSizes[2] > 100)) {
        obj["mainSplitterSizes"] = toJsonArray(mainSizes);
    } else if (m_savedMainSplitterSizes.size() == 3) {
        obj["mainSplitterSizes"] = toJsonArray(m_savedMainSplitterSizes);
    }

    QList<int> centerSizes = m_centerScreen ? m_centerScreen->splitterSizes() : m_savedCenterSplitterSizes;
    if (centerSizes.size() == 2 && (centerSizes[0] + centerSizes[1] > 50)) {
        obj["centerSplitterSizes"] = toJsonArray(centerSizes);
    } else if (m_savedCenterSplitterSizes.size() == 2) {
        obj["centerSplitterSizes"] = toJsonArray(m_savedCenterSplitterSizes);
    }

    QList<int> rightSizes = m_rightScreen ? m_rightScreen->splitterSizes() : m_savedRightSplitterSizes;
    if (rightSizes.size() == 2 && (rightSizes[0] + rightSizes[1] > 50)) {
        obj["rightSplitterSizes"] = toJsonArray(rightSizes);
    } else if (m_savedRightSplitterSizes.size() == 2) {
        obj["rightSplitterSizes"] = toJsonArray(m_savedRightSplitterSizes);
    }

    QList<int> rightVertSizes = m_rightScreen ? m_rightScreen->verticalSplitterSizes() : m_savedRightVerticalSplitterSizes;
    if (rightVertSizes.size() == 2 && (rightVertSizes[0] + rightVertSizes[1] > 50)) {
        obj["rightVerticalSplitterSizes"] = toJsonArray(rightVertSizes);
    } else if (m_savedRightVerticalSplitterSizes.size() == 2) {
        obj["rightVerticalSplitterSizes"] = toJsonArray(m_savedRightVerticalSplitterSizes);
    }

    obj["apiKey"] = m_gemini->apiKey();
    obj["model"] = m_gemini->model();
    obj["currentBook"] = m_currentBook;
    obj["currentChapter"] = m_currentChapter;
    obj["currentVerse"] = m_currentVerse;

    obj["leftStyle"] = m_leftScreen->paneStyle().toJson();

    QJsonArray panesArr;
    QJsonArray baseVisArr;
    for (int i = 0; i < 4; ++i) {
        if (paneAt(i)) {
            panesArr.append(paneAt(i)->toJson());
            baseVisArr.append(isPaneActiveInLayout(i));
        } else {
            baseVisArr.append(false);
        }
    }
    obj["panes"] = panesArr;
    obj["basePaneVisible"] = baseVisArr;

    QJsonArray splitsArr;
    for (const auto& rec : m_dynamicSplits) {
        if (!rec.createdPane || !rec.createdSplitter) continue;
        QJsonObject so;
        so["targetPaneIndex"] = rec.targetPaneIndex;
        so["orientation"] = (rec.orientation == Qt::Vertical) ? "V" : "H";
        so["splitterSizes"] = toJsonArray(rec.createdSplitter->sizes());
        so["pane"] = rec.createdPane->toJson();
        splitsArr.append(so);
    }
    obj["dynamicSplits"] = splitsArr;

    QFile f(settingsFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (!m_isLoadingSettings && isVisible()) {
        saveSettings();
    }
}

void MainWindow::moveEvent(QMoveEvent* event) {
    QMainWindow::moveEvent(event);
    if (!m_isLoadingSettings && isVisible()) {
        saveSettings();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_isClosing) {
        event->accept();
        return;
    }
    m_isClosing = true;
    saveSettings();
    event->accept();
    QApplication::quit();
}


