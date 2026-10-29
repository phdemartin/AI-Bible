#include "ScreenPanels.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QScrollBar>
#include <QSlider>
#include <QCheckBox>
#include <QColorDialog>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QTimer>
#include <QTextBlock>
#include <QDrag>
#include <QPainter>
#include <QPixmap>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

// ============================================================================
// Bouton Roue Dentée (⚙) & Fenêtre de Réglages Visuels par Module
// ============================================================================
QToolButton* createGearButton(const QString& tooltip, QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText("⚙");
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(30, 28);
    btn->setStyleSheet(
        "QToolButton { font-size: 14pt; border: 1px solid rgba(148,163,184,0.25); border-radius: 6px; background: transparent; padding: 0px; }"
        "QToolButton:hover { background: rgba(56,189,248,0.18); border-color: rgba(56,189,248,0.6); }"
    );
    return btn;
}

void openModuleSettingsDialog(
    QWidget* parent,
    const QString& moduleTitle,
    PaneStyle& inOutStyle,
    std::function<void(const PaneStyle&)> onLiveUpdate,
    std::function<void(const PaneStyle&)> onApplyToAllModules,
    QWidget* extraModuleSection
) {
    QDialog dlg(parent);
    dlg.setWindowTitle("⚙ Réglages — " + moduleTitle);
    dlg.setMinimumWidth(420);

    auto* root = new QVBoxLayout(&dlg);
    root->setSpacing(10);

    auto* formGroup = new QGroupBox("Affichage, Taille des lettres, Contraste & Couleurs");
    auto* form = new QFormLayout(formGroup);
    form->setSpacing(8);

    // 1. Taille des lettres (Police)
    auto* sizeRow = new QHBoxLayout();
    auto* sizeSlider = new QSlider(Qt::Horizontal);
    sizeSlider->setRange(10, 32);
    sizeSlider->setValue(inOutStyle.fontSizePt);

    auto* sizeSpin = new QSpinBox();
    sizeSpin->setRange(10, 32);
    sizeSpin->setSuffix(" pt");
    sizeSpin->setValue(inOutStyle.fontSizePt);

    sizeRow->addWidget(sizeSlider, 1);
    sizeRow->addWidget(sizeSpin);
    form->addRow("Taille des lettres :", sizeRow);

    // 2. Interligne (Espacement vertical)
    auto* lineRow = new QHBoxLayout();
    auto* lineSlider = new QSlider(Qt::Horizontal);
    lineSlider->setRange(120, 230);
    lineSlider->setValue(inOutStyle.lineSpacingPct);
    auto* lineLabel = new QLabel(QString("%1 %").arg(inOutStyle.lineSpacingPct));
    lineLabel->setMinimumWidth(48);
    lineRow->addWidget(lineSlider, 1);
    lineRow->addWidget(lineLabel);
    form->addRow("Espacement lignes :", lineRow);

    // 3. Contraste & Police
    auto* boldCheck = new QCheckBox("Contraste renforcé (lettres plus épaisses)");
    boldCheck->setChecked(inOutStyle.highContrastBold);
    form->addRow("Contraste :", boldCheck);

    auto* fontTypeCombo = new QComboBox();
    fontTypeCombo->addItem("Sérif (Lecture classique)", true);
    fontTypeCombo->addItem("Sans-Sérif (Moderne épuré)", false);
    fontTypeCombo->setCurrentIndex(inOutStyle.useSerifFont ? 0 : 1);
    form->addRow("Style de police :", fontTypeCombo);

    // 4. Thème de couleurs & Contraste prédéfini
    auto* presetCombo = new QComboBox();
    const QStringList presets = {
        "Sombre Doux",
        "Noir Contraste Élevé",
        "Noir & Ambre (Confort Nocturne)",
        "Clair Papier (Sépia)",
        "Blanc Contraste Élevé",
        "Vert Repos Visuel"
    };
    for (const QString& p : presets) {
        presetCombo->addItem(p, p);
    }
    int pIdx = presetCombo->findData(inOutStyle.presetName);
    if (pIdx >= 0) presetCombo->setCurrentIndex(pIdx);
    form->addRow("Thème & Contraste :", presetCombo);

    // 5. Couleurs personnalisées (Fond, Texte, Accent)
    auto* colorsRow = new QHBoxLayout();
    auto* bgBtn = new QPushButton("🎨 Fond");
    auto* fgBtn = new QPushButton("🎨 Texte");
    auto* accBtn = new QPushButton("🎨 Accent");

    auto updateColorButtons = [&]() {
        bgBtn->setStyleSheet(QString("background-color:%1; color:%2; border:1px solid #64748B; padding:4px 8px; border-radius:4px;")
            .arg(inOutStyle.bgColor, inOutStyle.textColor));
        fgBtn->setStyleSheet(QString("background-color:%1; color:%2; border:1px solid #64748B; padding:4px 8px; border-radius:4px;")
            .arg(inOutStyle.textColor, inOutStyle.bgColor));
        accBtn->setStyleSheet(QString("background-color:%1; color:#FFFFFF; border:1px solid #64748B; padding:4px 8px; border-radius:4px;")
            .arg(inOutStyle.accentColor));
    };
    updateColorButtons();

    colorsRow->addWidget(bgBtn);
    colorsRow->addWidget(fgBtn);
    colorsRow->addWidget(accBtn);
    form->addRow("Couleurs :", colorsRow);

    root->addWidget(formGroup);

    // Section additionnelle propre au module (s'il y en a une)
    if (extraModuleSection) {
        root->addWidget(extraModuleSection);
    }

    // Bouton "Appliquer à toutes les fenêtres" + Fermer
    auto* bottomRow = new QHBoxLayout();
    if (onApplyToAllModules) {
        auto* applyAllBtn = new QPushButton("✨ Appliquer ce style à toutes les fenêtres");
        QObject::connect(applyAllBtn, &QPushButton::clicked, &dlg, [&]() {
            onApplyToAllModules(inOutStyle);
        });
        bottomRow->addWidget(applyAllBtn);
    }
    bottomRow->addStretch(1);
    auto* closeBtn = new QPushButton("Fermer");
    closeBtn->setDefault(true);
    QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    bottomRow->addWidget(closeBtn);
    root->addLayout(bottomRow);

    // Connexions en temps réel
    QObject::connect(sizeSlider, &QSlider::valueChanged, &dlg, [&](int val) {
        if (sizeSpin->value() != val) sizeSpin->setValue(val);
        inOutStyle.fontSizePt = val;
        if (onLiveUpdate) onLiveUpdate(inOutStyle);
    });
    QObject::connect(sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dlg, [&](int val) {
        if (sizeSlider->value() != val) sizeSlider->setValue(val);
        inOutStyle.fontSizePt = val;
        if (onLiveUpdate) onLiveUpdate(inOutStyle);
    });
    QObject::connect(lineSlider, &QSlider::valueChanged, &dlg, [&](int val) {
        lineLabel->setText(QString("%1 %").arg(val));
        inOutStyle.lineSpacingPct = val;
        if (onLiveUpdate) onLiveUpdate(inOutStyle);
    });
    QObject::connect(boldCheck, &QCheckBox::toggled, &dlg, [&](bool checked) {
        inOutStyle.highContrastBold = checked;
        if (onLiveUpdate) onLiveUpdate(inOutStyle);
    });
    QObject::connect(fontTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [&](int idx) {
        inOutStyle.useSerifFont = fontTypeCombo->itemData(idx).toBool();
        if (onLiveUpdate) onLiveUpdate(inOutStyle);
    });
    QObject::connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [&](int idx) {
        QString pName = presetCombo->itemData(idx).toString();
        bool keepSerif = inOutStyle.useSerifFont;
        int keepLine = inOutStyle.lineSpacingPct;
        inOutStyle = PaneStyle::preset(pName, inOutStyle.fontSizePt);
        inOutStyle.useSerifFont = keepSerif;
        inOutStyle.lineSpacingPct = keepLine;
        boldCheck->setChecked(inOutStyle.highContrastBold);
        updateColorButtons();
        if (onLiveUpdate) onLiveUpdate(inOutStyle);
    });
    QObject::connect(bgBtn, &QPushButton::clicked, &dlg, [&]() {
        QColor c = QColorDialog::getColor(QColor(inOutStyle.bgColor), &dlg, "Couleur du fond");
        if (c.isValid()) {
            inOutStyle.bgColor = c.name();
            inOutStyle.cardBgColor = c.lighter(118).name();
            updateColorButtons();
            if (onLiveUpdate) onLiveUpdate(inOutStyle);
        }
    });
    QObject::connect(fgBtn, &QPushButton::clicked, &dlg, [&]() {
        QColor c = QColorDialog::getColor(QColor(inOutStyle.textColor), &dlg, "Couleur du texte");
        if (c.isValid()) {
            inOutStyle.textColor = c.name();
            updateColorButtons();
            if (onLiveUpdate) onLiveUpdate(inOutStyle);
        }
    });
    QObject::connect(accBtn, &QPushButton::clicked, &dlg, [&]() {
        QColor c = QColorDialog::getColor(QColor(inOutStyle.accentColor), &dlg, "Couleur d'accentuation");
        if (c.isValid()) {
            inOutStyle.accentColor = c.name();
            updateColorButtons();
            if (onLiveUpdate) onLiveUpdate(inOutStyle);
        }
    });

    dlg.exec();
}

// ============================================================================
// InteractiveTextBrowser Implementation
// ============================================================================
InteractiveTextBrowser::InteractiveTextBrowser(bool isBibleView, QWidget* parent)
    : QTextBrowser(parent), m_isBibleView(isBibleView)
{
    setOpenExternalLinks(false);
    setOpenLinks(false);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setFrameShape(QFrame::NoFrame);
    setAcceptDrops(true);
    if (viewport()) {
        viewport()->setAcceptDrops(true);
    }
}

void InteractiveTextBrowser::setIsBibleView(bool isBible) {
    m_isBibleView = isBible;
}

void InteractiveTextBrowser::setPassageContext(const QString& book, int chapter, const QString& moduleName, const QString& frenchBookName, int activeVerse) {
    m_contextBook = book;
    m_contextFrenchBook = frenchBookName.isEmpty() ? book : frenchBookName;
    m_contextChapter = chapter;
    m_contextVerse = (activeVerse > 0) ? activeVerse : 1;
    m_contextModule = moduleName;
}

QString InteractiveTextBrowser::cleanSelectedText() const {
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) return {};

    QString txt = cursor.selectedText();
    txt.replace(QChar(0x2029), "\n");
    txt.replace(QChar(0x2028), "\n");
    txt.replace(QRegularExpression("[ \\t]+"), " ");
    txt.replace(QRegularExpression(" +([,.;:!?])"), "\\1");
    return txt.trimmed();
}

QMimeData* InteractiveTextBrowser::createMimeDataFromSelection() const {
    QMimeData* mime = new QMimeData();
    mime->setText(cleanSelectedText());
    return mime;
}

bool InteractiveTextBrowser::extractWordInfoAtPos(
    const QPoint& pos,
    int& outVerse,
    QString& outStrong,
    QString& outMorph,
    QString& outWord,
    int& outWordIdx,
    bool* outIsVerseNum
) const {
    outVerse = 0;
    outStrong.clear();
    outMorph.clear();
    outWord.clear();
    outWordIdx = 0;
    if (outIsVerseNum) *outIsVerseNum = false;

    QString href = anchorAt(pos);
    if (!href.isEmpty()) {
        QUrl url(href);
        if (url.scheme() == "app" && url.host() == "word") {
            QUrlQuery q(url);
            outVerse = q.queryItemValue("v").toInt();
            outWordIdx = q.queryItemValue("idx").toInt();
            outStrong = q.queryItemValue("strong");
            outMorph = q.queryItemValue("morph");
            outWord = q.queryItemValue("w");
            return true;
        }
        if (url.scheme() == "app" && url.host() == "verse") {
            outVerse = url.path().mid(1).toInt();
            if (outIsVerseNum) *outIsVerseNum = (outVerse > 0);
            return outVerse > 0;
        }
    }

    QTextCursor cur = cursorForPosition(pos);
    cur.select(QTextCursor::WordUnderCursor);
    QString candidate = cur.selectedText().trimmed();
    if (!candidate.isEmpty()) {
        bool isNumber = false;
        int vnum = candidate.toInt(&isNumber);
        if (isNumber && vnum > 0) {
            outVerse = vnum;
            if (outIsVerseNum) *outIsVerseNum = true;
            return true;
        }
        outWord = candidate;
        return true;
    }
    return false;
}

void InteractiveTextBrowser::highlightWord(int verse, const QString& strongCode, const QString& word, int wordIdx, bool matchByIdx) {
    m_hlVerse = verse;
    m_hlStrong = strongCode.trimmed();
    m_hlWord = word.trimmed();
    m_hlWordIdx = wordIdx;
    m_hlMatchByIdx = matchByIdx;
    reapplyWordHighlight();
}

void InteractiveTextBrowser::clearWordHighlight() {
    m_hlVerse = 0;
    m_hlStrong.clear();
    m_hlWord.clear();
    m_hlWordIdx = -1;
    m_hlMatchByIdx = false;
    setExtraSelections({});
}

void InteractiveTextBrowser::reapplyWordHighlight() {
    if (m_hlVerse <= 0 && m_hlWord.isEmpty() && m_hlStrong.isEmpty()) {
        setExtraSelections({});
        return;
    }

    QList<QTextEdit::ExtraSelection> selections;
    QTextDocument* doc = document();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            QTextCharFormat fmt = frag.charFormat();
            if (!fmt.isAnchor()) continue;

            QString href = fmt.anchorHref();
            if (!href.startsWith("app://word")) continue;

            QUrl url(href);
            QUrlQuery q(url);
            int v = q.queryItemValue("v").toInt();
            if (m_hlVerse > 0 && v != m_hlVerse) continue;

            int idx = q.queryItemValue("idx").toInt();
            QString w = q.queryItemValue("w");
            QString st = q.queryItemValue("strong");

            bool exactIdxMatch = (m_hlMatchByIdx && m_hlWordIdx >= 0 && idx == m_hlWordIdx);
            bool exactWordFallback = (m_hlMatchByIdx && m_hlWordIdx < 0 && !m_hlWord.isEmpty() &&
                                      w.compare(m_hlWord, Qt::CaseInsensitive) == 0);
            bool strongMatch = (!m_hlStrong.isEmpty() && !st.isEmpty() &&
                                st.compare(m_hlStrong, Qt::CaseInsensitive) == 0);

            if (exactIdxMatch || exactWordFallback || strongMatch) {
                QTextCursor cur(doc);
                cur.setPosition(frag.position());
                cur.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);

                QTextEdit::ExtraSelection extra;
                extra.cursor = cur;
                if (exactIdxMatch || exactWordFallback) {
                    extra.format.setBackground(QColor("#F59E0B"));
                    extra.format.setForeground(QColor("#0F172A"));
                    extra.format.setFontWeight(QFont::Bold);
                } else {
                    extra.format.setBackground(QColor("#FDE047"));
                    extra.format.setForeground(QColor("#0F172A"));
                    extra.format.setFontWeight(QFont::Bold);
                }
                selections.push_back(extra);
            }
        }
    }
    setExtraSelections(selections);
}

int InteractiveTextBrowser::selectedVerseNumber() const {
    QTextCursor cur = textCursor();
    if (!cur.hasSelection()) return 0;

    int startPos = cur.selectionStart();
    int endPos = cur.selectionEnd();
    QTextDocument* doc = document();

    // Cherche d'abord dans les fragments couverts par la sélection ou dans le bloc de départ
    QTextBlock startBlock = doc->findBlock(startPos);
    for (QTextBlock b = startBlock; b.isValid() && b.position() <= endPos; b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            QTextCharFormat fmt = frag.charFormat();
            if (!fmt.isAnchor()) continue;
            QString href = fmt.anchorHref();
            QUrl url(href);
            if (url.scheme() == "app" && url.host() == "verse") {
                int v = url.path().mid(1).toInt();
                if (v > 0) return v;
            }
            if (url.scheme() == "app" && url.host() == "word") {
                QUrlQuery q(url);
                int v = q.queryItemValue("v").toInt();
                if (v > 0) return v;
            }
        }
    }

    // Fallback : si le texte sélectionné commence par un numéro de verset (ex: "6 Para entender...")
    QString sel = cleanSelectedText();
    static const QRegularExpression rxLeadingVerse(R"(^\s*(\d{1,3})\b)");
    auto m = rxLeadingVerse.match(sel);
    if (m.hasMatch()) {
        int v = m.captured(1).toInt();
        if (v > 0) return v;
    }
    return 0;
}

int InteractiveTextBrowser::topVisibleVerseNumber() const {
    QTextCursor cur = cursorForPosition(QPoint(25, 25));
    QTextBlock block = cur.block();
    for (int step = 0; step < 5 && block.isValid(); ++step, block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            QTextCharFormat fmt = frag.charFormat();
            if (!fmt.isAnchor()) continue;
            QString href = fmt.anchorHref();
            QUrl url(href);
            if (url.scheme() == "app" && url.host() == "verse") {
                int v = url.path().mid(1).toInt();
                if (v > 0) return v;
            }
            if (url.scheme() == "app" && url.host() == "word") {
                QUrlQuery q(url);
                int v = q.queryItemValue("v").toInt();
                if (v > 0) return v;
            }
        }
    }
    return 0;
}

void InteractiveTextBrowser::mousePressEvent(QMouseEvent* event) {
    m_pressPos = event->pos();
    m_dragStartVerse = 0;

    // IMPORTANT : Sur Clic Droit, ne pas appeler QTextBrowser::mousePressEvent
    // car Qt déplace le curseur sur le lien <a href> sous la souris et efface la sélection !
    if (event->button() == Qt::RightButton) {
        event->accept();
        return;
    }

    if (m_isBibleView && event->button() == Qt::LeftButton) {
        int verse = 0, wordIdx = 0;
        QString strong, morph, word;
        bool isVerseNum = false;
        if (extractWordInfoAtPos(event->pos(), verse, strong, morph, word, wordIdx, &isVerseNum)) {
            if (isVerseNum && verse > 0) {
                m_dragStartVerse = verse;
            }
        }
    }

    if (m_isBibleView && event->button() == Qt::MiddleButton) {
        int verse = 0, wordIdx = 0;
        QString strong, morph, word;
        if (extractWordInfoAtPos(event->pos(), verse, strong, morph, word, wordIdx)) {
            if (!word.isEmpty() || !strong.isEmpty()) {
                highlightWord(verse, strong, word, wordIdx, true);
                emit wordDictionaryRequested(verse, strong, morph, word, wordIdx);
            } else if (verse > 0) {
                emit verseActivated(verse);
            }
        }
        event->accept();
        return;
    }

    QTextBrowser::mousePressEvent(event);
}

void InteractiveTextBrowser::mouseMoveEvent(QMouseEvent* event) {
    if (m_isBibleView && (event->buttons() & Qt::LeftButton) && m_dragStartVerse > 0) {
        if ((event->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            int v = m_dragStartVerse;
            m_dragStartVerse = 0;

            auto* drag = new QDrag(this);
            auto* mime = new QMimeData();
            QString frBook = m_contextFrenchBook.isEmpty() ? m_contextBook : m_contextFrenchBook;
            QString frRef = QString("%1 %2:%3").arg(frBook).arg(m_contextChapter).arg(v);
            QString payload = QString("%1|%2|%3|%4")
                .arg(m_contextBook)
                .arg(m_contextChapter)
                .arg(v)
                .arg(m_contextModule);
            mime->setData("application/x-bible-verse-ref", payload.toUtf8());
            mime->setText(frRef);
            drag->setMimeData(mime);

            QPixmap badge(280, 34);
            badge.fill(Qt::transparent);
            {
                QPainter p(&badge);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setBrush(QColor("#0284C7"));
                p.setPen(QPen(QColor("#38BDF8"), 1.5));
                p.drawRoundedRect(badge.rect().adjusted(1, 1, -1, -1), 7, 7);
                p.setPen(Qt::white);
                QFont f = font();
                f.setPointSize(10);
                f.setBold(true);
                p.setFont(f);
                p.drawText(badge.rect(), Qt::AlignCenter,
                           QString("📖 %1 → Déposer sur une autre fenêtre").arg(frRef));
            }
            drag->setPixmap(badge);
            drag->setHotSpot(QPoint(25, 17));
            drag->exec(Qt::CopyAction);
            event->accept();
            return;
        }
    }
    QTextBrowser::mouseMoveEvent(event);
}

void InteractiveTextBrowser::mouseReleaseEvent(QMouseEvent* event) {
    m_dragStartVerse = 0;

    // Si l'utilisateur vient de sélectionner un morceau de texte à la souris (dans la Bible,
    // le Dictionnaire ou le Commentaire), on termine la sélection SANS déclencher de clic sur un lien !
    bool dragged = (event->pos() - m_pressPos).manhattanLength() > 6 || textCursor().hasSelection();
    if (dragged) {
        QTextEdit::mouseReleaseEvent(event);
        return;
    }

    // Simple clic gauche dans la Bible (sans sélection) -> affiche UNIQUEMENT le dictionnaire Grec/Hébreu
    if (m_isBibleView && event->button() == Qt::LeftButton) {
        int verse = 0, wordIdx = 0;
        QString strong, morph, word;
        bool isVerseNum = false;
        if (extractWordInfoAtPos(event->pos(), verse, strong, morph, word, wordIdx, &isVerseNum)) {
            if (isVerseNum && verse > 0) {
                emit verseActivated(verse);
            } else if (!word.isEmpty() || !strong.isEmpty()) {
                highlightWord(verse, strong, word, wordIdx, true);
                emit wordDictionaryRequested(verse, strong, morph, word, wordIdx);
            } else if (verse > 0) {
                emit verseActivated(verse);
            }
            event->accept();
            return;
        }
    }

    QTextBrowser::mouseReleaseEvent(event);
}

void InteractiveTextBrowser::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-bible-verse-ref")) {
        event->acceptProposedAction();
        return;
    }
    QTextBrowser::dragEnterEvent(event);
}

void InteractiveTextBrowser::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-bible-verse-ref")) {
        event->acceptProposedAction();
        return;
    }
    QTextBrowser::dragMoveEvent(event);
}

void InteractiveTextBrowser::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasFormat("application/x-bible-verse-ref")) {
        QString payload = QString::fromUtf8(event->mimeData()->data("application/x-bible-verse-ref"));
        QStringList parts = payload.split('|');
        if (parts.size() >= 3) {
            QString book = parts[0];
            int ch = parts[1].toInt();
            int v = parts[2].toInt();
            if (!book.isEmpty() && ch > 0 && v > 0) {
                emit verseDropped(book, ch, v);
                event->acceptProposedAction();
                return;
            }
        }
    }
    QTextBrowser::dropEvent(event);
}

void InteractiveTextBrowser::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y() > 0 ? 1 : -1;
        emit fontSizeDeltaRequested(delta);
        event->accept();
        return;
    }
    QTextBrowser::wheelEvent(event);
}

void InteractiveTextBrowser::contextMenuEvent(QContextMenuEvent* event) {
    QString sel = cleanSelectedText();

    // Menu contextuel sur un morceau de texte sélectionné (dans Gemini, Bible, Dictionnaire ou Commentaire)
    if (!sel.isEmpty()) {
        int selVerse = (m_isBibleView && selectedVerseNumber() > 0) ? selectedVerseNumber() : m_contextVerse;
        if (selVerse <= 0) selVerse = 1;
        QString frBook = m_contextFrenchBook.isEmpty() ? m_contextBook : m_contextFrenchBook;
        QString frRef = QString("%1 %2:%3").arg(frBook).arg(m_contextChapter).arg(selVerse);

        QMenu menu(this);
        QAction* actAddNote = menu.addAction(QString("📝 Ajouter la sélection à la note de %1").arg(frRef));
        connect(actAddNote, &QAction::triggered, this, [sel, selVerse, this]() {
            emit addSelectionToPersonalNoteRequested(selVerse, sel, false);
        });

        QAction* actAddNoteOpen = menu.addAction(QString("📝 Ajouter à la note de %1 et ouvrir la fiche").arg(frRef));
        connect(actAddNoteOpen, &QAction::triggered, this, [sel, selVerse, this]() {
            emit addSelectionToPersonalNoteRequested(selVerse, sel, true);
        });

        menu.addSeparator();

        QAction* actPrepAi = menu.addAction("🤖 Rechercher dans l'IA (mettre la sélection et poser ma question)");
        connect(actPrepAi, &QAction::triggered, this, [sel, selVerse, this]() {
            if (m_isBibleView) {
                emit selectionAiPreparedRequested(selVerse, sel, false);
            } else {
                emit selectionAiResearchRequested(sel, false);
            }
        });

        QAction* actAutoAi = menu.addAction("⚡ Rechercher dans l'IA (analyse immédiate de la sélection)");
        connect(actAutoAi, &QAction::triggered, this, [sel, selVerse, this]() {
            if (m_isBibleView) {
                emit selectionAiPreparedRequested(selVerse, sel, true);
            } else {
                emit selectionAiResearchRequested(sel, true);
            }
        });

        menu.addSeparator();
        QAction* actCopyWord = menu.addAction("📋 Copier la sélection pour Traitement de texte (LibreOffice / Word)");
        connect(actCopyWord, &QAction::triggered, this, [sel, frRef, this]() {
            auto* mime = new QMimeData();
            mime->setText(sel);
            QString title = m_contextModule.isEmpty() ? frRef : QString("%1 — %2").arg(frRef, m_contextModule);
            mime->setHtml(GeminiClient::markdownToWordProcessorHtml(sel, title));
            QApplication::clipboard()->setMimeData(mime);
            emit statusNotification("📋 Sélection copiée (format riche propre sans fond noir) — prête à coller (Ctrl+V) dans votre traitement de texte !");
        });

        QAction* actCopy = menu.addAction("📋 Copier en texte brut (Ctrl+C)");
        connect(actCopy, &QAction::triggered, this, [sel, this]() {
            QApplication::clipboard()->setText(sel);
            emit statusNotification("✓ Texte sélectionné copié dans le presse-papiers.");
        });

        menu.exec(event->globalPos());
        return;
    }

    // Sans texte sélectionné : AUCUNE recherche IA automatique sur un mot !
    QMenu menu(this);
    QAction* hintAct = menu.addAction("💡 Sélectionnez du texte à la souris pour « 📝 Ajouter à la note » ou « 🤖 Rechercher dans l'IA »");
    hintAct->setEnabled(false);
    menu.addSeparator();
    QAction* actAll = menu.addAction("Tout sélectionner (Ctrl+A)");
    connect(actAll, &QAction::triggered, this, &QTextBrowser::selectAll);
    menu.exec(event->globalPos());
}

// ============================================================================
// LeftScreenWidget Implementation (Colonne de gauche : Arbre des Documents & Recherche)
// ============================================================================
LeftScreenWidget::LeftScreenWidget(SwordEngine* engine, QWidget* parent)
    : QWidget(parent), m_engine(engine), m_style(PaneStyle::preset("Sombre Doux", 12))
{
    m_style.useSerifFont = false;
    setMinimumWidth(160);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    // Barre supérieure compacte : Livre | Chap. | Verset | ⚙
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(4);

    m_bookCombo = new QComboBox();
    m_bookCombo->setMaxVisibleItems(28);
    m_bookCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_bookCombo->setMinimumContentsLength(6);
    m_chapterSpin = new QSpinBox();
    m_chapterSpin->setPrefix("Ch. ");
    m_chapterSpin->setRange(1, 150);
    m_verseSpin = new QSpinBox();
    m_verseSpin->setPrefix("V. ");
    m_verseSpin->setRange(1, 176);

    connect(m_bookCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LeftScreenWidget::onBookComboChanged);
    connect(m_chapterSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &LeftScreenWidget::onChapterSpinChanged);
    connect(m_verseSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &LeftScreenWidget::onVerseSpinChanged);


    auto* gearBtn = createGearButton("⚙ Réglages de l'Arbre des Documents (taille des lettres, contraste, couleurs)");
    connect(gearBtn, &QToolButton::clicked, this, &LeftScreenWidget::onOpenGearSettings);

    topRow->addWidget(m_bookCombo, 1);
    topRow->addWidget(m_chapterSpin);
    topRow->addWidget(m_verseSpin);
    topRow->addWidget(gearBtn);
    rootLayout->addLayout(topRow);

    // Onglets légers : Documents | Recherche
    auto* leftTabs = new QTabWidget();

    // --- ONGLET 1 : ARBRE DES DOCUMENTS ---
    auto* treeTab = new QWidget();
    auto* treeLayout = new QVBoxLayout(treeTab);
    treeLayout->setContentsMargins(4, 4, 4, 4);
    treeLayout->setSpacing(4);

    auto* installModulesBtn = new QPushButton("📥 Télécharger / Gérer des Modules");
    installModulesBtn->setCursor(Qt::PointingHandCursor);
    installModulesBtn->setToolTip(
        "Ouvrir le gestionnaire de téléchargement de modules (Bibles, Commentaires, Dictionnaires Strong)\n"
        "depuis CrossWire, eBible.org, STEP Bible, Xiphos, Bible.org... dans toutes les langues !"
    );
    installModulesBtn->setStyleSheet(
        "QPushButton { background-color: rgba(2, 132, 199, 0.24); color: #38BDF8; "
        "border: 1px solid #0284C7; border-radius: 5px; font-weight: bold; padding: 5px 8px; }"
        "QPushButton:hover { background-color: rgba(2, 132, 199, 0.42); color: #FFFFFF; }"
    );
    connect(installModulesBtn, &QPushButton::clicked, this, &LeftScreenWidget::openModuleInstallerRequested);
    treeLayout->addWidget(installModulesBtn);

    m_treeFilterEdit = new QLineEdit();
    m_treeFilterEdit->setPlaceholderText("🔍 Filtrer les documents (ou taper ex: Jean 3:16 + Entrée)...");
    m_treeFilterEdit->setClearButtonEnabled(true);
    connect(m_treeFilterEdit, &QLineEdit::textChanged, this, &LeftScreenWidget::onTreeFilterChanged);
    connect(m_treeFilterEdit, &QLineEdit::returnPressed, this, &LeftScreenWidget::onQuickRefSubmitted);
    treeLayout->addWidget(m_treeFilterEdit);

    m_docTree = new QTreeWidget();
    m_docTree->setHeaderLabels({"Module", "Description"});
    m_docTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_docTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_docTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_docTree, &QTreeWidget::itemClicked, this, &LeftScreenWidget::onDocumentTreeClicked);
    connect(m_docTree, &QTreeWidget::customContextMenuRequested, this, &LeftScreenWidget::onDocumentTreeContextMenu);

    treeLayout->addWidget(m_docTree, 1);
    leftTabs->addTab(treeTab, "🌳 Documents");

    // --- ONGLET 2 : RECHERCHE ---
    auto* searchTab = new QWidget();
    auto* searchLayout = new QVBoxLayout(searchTab);
    searchLayout->setContentsMargins(4, 4, 4, 4);
    searchLayout->setSpacing(4);

    auto* sRow = new QHBoxLayout();
    m_searchInput = new QLineEdit();
    m_searchInput->setPlaceholderText("Mot ou Strong (ex: grâce, G3056)...");
    auto* sBtn = new QPushButton("Chercher");
    connect(m_searchInput, &QLineEdit::returnPressed, this, &LeftScreenWidget::onSearchExecuted);
    connect(sBtn, &QPushButton::clicked, this, &LeftScreenWidget::onSearchExecuted);
    sRow->addWidget(m_searchInput, 1);
    sRow->addWidget(sBtn);
    searchLayout->addLayout(sRow);

    m_searchBibleCombo = new QComboBox();
    searchLayout->addWidget(m_searchBibleCombo);

    m_searchStatusLabel = new QLabel();
    searchLayout->addWidget(m_searchStatusLabel);

    m_searchResultsList = new QListWidget();
    connect(m_searchResultsList, &QListWidget::itemClicked, this, &LeftScreenWidget::onSearchResultClicked);
    searchLayout->addWidget(m_searchResultsList, 1);

    leftTabs->addTab(searchTab, "🔍 Recherche");

    rootLayout->addWidget(leftTabs, 1);

    populateBookCombo();
    populateDocumentTree();
    applyStyleToWidgets();
}

void LeftScreenWidget::setPaneStyle(const PaneStyle& style) {
    m_style = style;
    applyStyleToWidgets();
}

void LeftScreenWidget::applyStyleToWidgets() {
    QString weight = m_style.highContrastBold ? "600" : "400";
    QString qss = QString(
        "QTreeWidget, QListWidget { background-color: %1; color: %2; font-size: %3pt; font-weight: %4; border: 1px solid %5; }"
        "QTreeWidget::item { padding: 4px 2px; }"
        "QTreeWidget::item:selected, QListWidget::item:selected { background-color: %6; color: #FFFFFF; }"
    ).arg(m_style.bgColor, m_style.textColor)
     .arg(m_style.fontSizePt)
     .arg(weight, m_style.borderColor, m_style.accentColor);

    m_docTree->setStyleSheet(qss);
    m_searchResultsList->setStyleSheet(qss);
}

void LeftScreenWidget::onOpenGearSettings() {
    auto* extraBox = new QGroupBox("Option d'ouverture des Bibles depuis l'arbre");
    auto* extraLayout = new QFormLayout(extraBox);
    auto* colCombo = new QComboBox();
    colCombo->addItem("Ouvrir dans la Colonne 1 (Gauche)", 0);
    colCombo->addItem("Ouvrir dans la Colonne 2 (Droite)", 1);
    colCombo->setCurrentIndex(m_defaultTargetCol);
    connect(colCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), extraBox, [this, colCombo](int idx) {
        m_defaultTargetCol = colCombo->itemData(idx).toInt();
    });
    extraLayout->addRow("Clic gauche sur une Bible :", colCombo);

    openModuleSettingsDialog(
        this,
        "Arbre des Documents (Écran Gauche)",
        m_style,
        [this](const PaneStyle& s) {
            setPaneStyle(s);
            emit styleChanged();
        },
        [this](const PaneStyle& s) {
            emit applyStyleToAllRequested(s);
        },
        extraBox
    );
}

void LeftScreenWidget::populateBookCombo() {
    m_updating = true;
    m_bookCombo->clear();
    for (const auto& b : m_engine->getBooks()) {
        m_bookCombo->addItem(b.frenchName, b.swordName);
    }
    m_updating = false;
}

void LeftScreenWidget::populateDocumentTree() {
    m_docTree->clear();
    m_searchBibleCombo->clear();

    auto makeCat = [this](const QString& title) -> QTreeWidgetItem* {
        auto* item = new QTreeWidgetItem(m_docTree, {title, ""});
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setExpanded(true);
        return item;
    };

    makeCat("📝 Mes Notes Personnelles");
    QTreeWidgetItem* catBiblesFr = makeCat("🇫🇷 Bibles — Français");
    QTreeWidgetItem* catBiblesEn = makeCat("🇬🇧 Bibles — Anglais");
    QTreeWidgetItem* catBiblesEs = makeCat("🇪🇸 Bibles — Espagnol");
    QTreeWidgetItem* catBiblesOrig = makeCat("🏛️ Bibles — Grec / Hébreu / Autres");
    catBiblesOrig->setExpanded(false);

    QTreeWidgetItem* catCommentaries = makeCat("📚 Commentaires");
    QTreeWidgetItem* catDictionaries = makeCat("📖 Dictionnaires");

    for (const auto& b : m_engine->getBibleModules()) {
        m_searchBibleCombo->addItem(QString("%1 — %2").arg(b.name, b.description), b.name);

        QTreeWidgetItem* parentCat = catBiblesOrig;
        if (b.language == "fr") parentCat = catBiblesFr;
        else if (b.language == "en") parentCat = catBiblesEn;
        else if (b.language == "es") parentCat = catBiblesEs;

        auto* item = new QTreeWidgetItem(parentCat, {b.name, b.description});
        item->setData(0, Qt::UserRole, "BIBLE");
        item->setData(0, Qt::UserRole + 1, b.name);
        item->setToolTip(0, QString("%1 — %2\n(Clic droit = choisir dans quelle fenêtre l'ouvrir)").arg(b.name, b.description));
        item->setToolTip(1, item->toolTip(0));
    }

    {
        auto* allFr = new QTreeWidgetItem(catCommentaries, {"★ Tous FR + TSK", "Tous les Commentaires Français + TSK"});
        allFr->setData(0, Qt::UserRole, "COMMENTARY");
        allFr->setData(0, Qt::UserRole + 1, "ALL_FR");
        QFont f = allFr->font(0);
        f.setBold(true);
        allFr->setFont(0, f);
    }
    for (const auto& cm : m_engine->getCommentaryModules()) {
        QString prefix = (cm.language == "fr") ? "🇫🇷 " : (cm.language == "en" ? "🇬🇧 " : "");
        auto* item = new QTreeWidgetItem(catCommentaries, {prefix + cm.name, cm.description});
        item->setData(0, Qt::UserRole, "COMMENTARY");
        item->setData(0, Qt::UserRole + 1, cm.name);
    }

    {
        auto* autoDict = new QTreeWidgetItem(catDictionaries, {"★ Grec/Hébreu Auto", "Dictionnaire Strong Grec (NT) & Hébreu (AT)"});
        autoDict->setData(0, Qt::UserRole, "DICTIONARY");
        autoDict->setData(0, Qt::UserRole + 1, "AUTO_STRONG");
        QFont f = autoDict->font(0);
        f.setBold(true);
        autoDict->setFont(0, f);
    }
    for (const auto& lx : m_engine->getLexiconModules()) {
        QString prefix = (lx.language == "fr") ? "🇫🇷 " : "";
        auto* item = new QTreeWidgetItem(catDictionaries, {prefix + lx.name, lx.description});
        item->setData(0, Qt::UserRole, "DICTIONARY");
        item->setData(0, Qt::UserRole + 1, lx.name);
    }

    refreshPersonalNotesTree();
}

void LeftScreenWidget::refreshPersonalNotesTree() {
    if (!m_docTree || m_docTree->topLevelItemCount() == 0) return;
    QTreeWidgetItem* catNotes = m_docTree->topLevelItem(0);
    if (!catNotes || !catNotes->text(0).startsWith("📝")) return;

    while (catNotes->childCount() > 0) {
        delete catNotes->takeChild(0);
    }

    auto* openEditorItem = new QTreeWidgetItem(catNotes, {"★ Fiche du verset actif", "Ouvrir l'éditeur de Notes Personnelles"});
    openEditorItem->setData(0, Qt::UserRole, "NOTE");
    openEditorItem->setData(0, Qt::UserRole + 1, "CURRENT");
    QFont bf = openEditorItem->font(0);
    bf.setBold(true);
    openEditorItem->setFont(0, bf);

    const auto notes = m_engine->getAllPersonalNotes();
    catNotes->setText(0, QString("📝 Mes Notes Personnelles (%1)").arg(notes.size()));
    for (const auto& n : notes) {
        QString sub = QString("%1 extrait(s) — %2").arg(n.excerptCount).arg(n.updatedAt);
        auto* item = new QTreeWidgetItem(catNotes, {"📌 " + n.frenchRef, sub});
        item->setData(0, Qt::UserRole, "NOTE");
        item->setData(0, Qt::UserRole + 1, n.swordKey);
        item->setToolTip(0, QString("Note personnelle sur %1 (%2)\nCliquez pour ouvrir ce verset et sa fiche d'étude.").arg(n.frenchRef, sub));
        item->setToolTip(1, item->toolTip(0));
    }
}

void LeftScreenWidget::reloadModuleLists() {
    populateDocumentTree();
    if (m_treeFilterEdit && !m_treeFilterEdit->text().trimmed().isEmpty()) {
        onTreeFilterChanged(m_treeFilterEdit->text());
    }
}

void LeftScreenWidget::onTreeFilterChanged(const QString& text) {
    QString filter = text.trimmed();
    for (int i = 0; i < m_docTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* cat = m_docTree->topLevelItem(i);
        int visibleChildren = 0;
        for (int j = 0; j < cat->childCount(); ++j) {
            QTreeWidgetItem* child = cat->child(j);
            bool match = filter.isEmpty() ||
                         child->text(0).contains(filter, Qt::CaseInsensitive) ||
                         child->text(1).contains(filter, Qt::CaseInsensitive);
            child->setHidden(!match);
            if (match) visibleChildren++;
        }
        cat->setHidden(visibleChildren == 0);
        if (!filter.isEmpty() && visibleChildren > 0) {
            cat->setExpanded(true);
        }
    }
}

void LeftScreenWidget::onDocumentTreeClicked(QTreeWidgetItem* item, int) {
    if (!item) return;
    QString kind = item->data(0, Qt::UserRole).toString();
    QString modName = item->data(0, Qt::UserRole + 1).toString();
    if (kind.isEmpty() || modName.isEmpty()) return;

    if (kind == "BIBLE") {
        emit bibleSelectedForColumn(m_defaultTargetCol, modName);
        emit statusNotification(QString("📖 Bible « %1 » ouverte.").arg(modName));
    } else if (kind == "COMMENTARY") {
        emit commentarySelected(modName);
        emit statusNotification(QString("📚 Commentaire « %1 » ouvert.").arg(modName));
    } else if (kind == "DICTIONARY") {
        emit dictionarySelected(modName);
        emit statusNotification(QString("🏛️ Dictionnaire « %1 » ouvert.").arg(modName));
    } else if (kind == "NOTE") {
        QString key = (modName == "CURRENT")
            ? QString("%1 %2:%3").arg(m_currentBook).arg(m_currentChapter).arg(m_currentVerse)
            : modName;
        emit personalNoteSelected(key);
    }
}

void LeftScreenWidget::onDocumentTreeContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_docTree->itemAt(pos);
    if (!item) return;
    QString kind = item->data(0, Qt::UserRole).toString();
    QString modName = item->data(0, Qt::UserRole + 1).toString();
    if (kind.isEmpty() || modName.isEmpty()) return;

    QString icon = (kind == "BIBLE") ? "📖" : (kind == "COMMENTARY" ? "📚" : (kind == "NOTE" ? "📝" : "🏛️"));
    QString label = (kind == "NOTE")
        ? ((modName == "CURRENT") ? "Notes Personnelles" : m_engine->toFrenchReference(modName))
        : modName;
    QMenu menu(this);
    const QStringList paneTitles = {
        "1️⃣ Fenêtre 1 (Centre - Gauche)",
        "2️⃣ Fenêtre 2 (Centre - Droite)",
        "3️⃣ Fenêtre 3 (Droite - Gauche)",
        "4️⃣ Fenêtre 4 (Droite - Droite)"
    };
    for (int p = 0; p < 4; ++p) {
        QAction* act = menu.addAction(QString("%1 Ouvrir « %2 » dans %3").arg(icon, label, paneTitles[p]));
        connect(act, &QAction::triggered, this, [this, p, kind, modName]() {
            if (kind == "BIBLE" && p < 2) {
                m_defaultTargetCol = p;
            }
            emit openDocumentInPaneRequested(p, kind, modName);
        });
    }
    menu.exec(m_docTree->viewport()->mapToGlobal(pos));
}

void LeftScreenWidget::syncCurrentReference(const QString& swordBook, int chapter, int verse) {
    m_updating = true;
    m_currentBook = swordBook;
    m_currentChapter = chapter;
    m_currentVerse = verse;

    int bIdx = m_bookCombo->findData(swordBook);
    if (bIdx >= 0) m_bookCombo->setCurrentIndex(bIdx);

    m_chapterSpin->setMaximum(m_engine->getChapterCount(swordBook));
    m_chapterSpin->setValue(chapter);

    m_verseSpin->setMaximum(m_engine->getVerseCount(swordBook, chapter));
    m_verseSpin->setValue(verse);
    m_updating = false;
}

void LeftScreenWidget::syncSelectedBibles(const QString&, const QString&) {
}

void LeftScreenWidget::onQuickRefSubmitted() {
    QString q = m_treeFilterEdit->text().trimmed();
    if (q.isEmpty()) return;
    QString bookSword;
    int ch = 1, v = 1;
    m_engine->parseUserReference(q, &ch, &v, &bookSword);
    m_treeFilterEdit->clear();
    emit referenceSelected(bookSword, ch, v);
}

void LeftScreenWidget::onBookComboChanged(int index) {
    if (m_updating || index < 0) return;
    QString bookSword = m_bookCombo->itemData(index).toString();
    emit referenceSelected(bookSword, 1, 1);
}

void LeftScreenWidget::onChapterSpinChanged(int chapter) {
    if (m_updating) return;
    emit referenceSelected(m_currentBook, chapter, 1);
}

void LeftScreenWidget::onVerseSpinChanged(int verse) {
    if (m_updating) return;
    emit referenceSelected(m_currentBook, m_currentChapter, verse);
}

void LeftScreenWidget::onSearchExecuted() {
    QString q = m_searchInput->text().trimmed();
    if (q.isEmpty()) return;
    QString bibleMod = m_searchBibleCombo->currentData().toString();
    if (bibleMod.isEmpty()) bibleMod = "FreLSG";

    m_searchStatusLabel->setText("⏳ Recherche...");
    QApplication::processEvents();

    auto hits = m_engine->searchBible(bibleMod, q, 0, m_currentBook, 200);
    m_searchResultsList->clear();
    for (const auto& h : hits) {
        auto* item = new QListWidgetItem(QString("%1 — %2").arg(h.frenchRef, h.plainText.left(100)));
        item->setData(Qt::UserRole, h.swordKey);
        item->setToolTip(h.plainText);
        m_searchResultsList->addItem(item);
    }
    m_searchStatusLabel->setText(QString("✓ %1 résultats.").arg(hits.size()));
}

void LeftScreenWidget::onSearchResultClicked(QListWidgetItem* item) {
    if (!item) return;
    QString key = item->data(Qt::UserRole).toString();
    QString b; int c = 1, v = 1;
    m_engine->splitSwordKey(key, b, c, v);
    emit referenceSelected(b, c, v);
}

// ============================================================================
// UniversalPaneWidget Implementation (Fenêtre universelle interchangeable 1..4)
// ============================================================================
UniversalPaneWidget::UniversalPaneWidget(int paneIndex, SwordEngine* engine, GeminiClient* gemini, QWidget* parent)
    : QWidget(parent)
    , m_paneIndex(paneIndex)
    , m_engine(engine)
    , m_gemini(gemini)
    , m_style(PaneStyle::preset("Noir Contraste Élevé", (paneIndex < 2) ? 18 : 16))
{
    setMinimumWidth(150);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 3, 4, 3);
    rootLayout->setSpacing(4);

    // ========================================================================
    // LIGNE 1 : Petit menu déroulant du type de fenêtre + Sélecteur du module + Roue dentée ⚙
    // ========================================================================
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(5);

    m_typeCombo = new QComboBox();
    m_typeCombo->setCursor(Qt::PointingHandCursor);
    m_typeCombo->setToolTip(
        QString("Choisir ce qu'affiche la Fenêtre %1 :\n"
                "• 📖 Bible (Français, Anglais, Espagnol, Grec, Hébreu)\n"
                "• 📚 Commentaires & TSK (synchronisé ou indépendant)\n"
                "• 🏛️ Dictionnaire Grec / Hébreu (Strong Auto ou lexique)\n"
                "• 🤖 IA Gemini & Sources (Chat Gemini ou Recherche dans les Sources)\n"
                "• 📝 Notes Personnelles (fiches d'étude indexées par verset + extraits IA)").arg(paneIndex + 1)
    );
    m_typeCombo->addItem("📖 Bible", "BIBLE");
    m_typeCombo->addItem("📚 Commentaires & TSK", "COMMENTARY");
    m_typeCombo->addItem("🏛️ Dictionnaire", "DICTIONARY");
    m_typeCombo->addItem("🤖 IA Gemini & Sources", "AI");
    m_typeCombo->addItem("📝 Notes Personnelles", "NOTES");
    m_typeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_typeCombo->setStyleSheet(
        "QComboBox { font-weight: bold; padding: 3px 8px; border: 1px solid #38BDF8; border-radius: 5px; }"
    );
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UniversalPaneWidget::onTypeComboChanged);

    m_headerModeStack = new QStackedWidget();
    m_headerModeStack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    // --- Header Page 0 : Contrôles BIBLE (Sélecteur de version + Bouton 🔗 Lié / 🔓 Délié) ---
    {
        auto* bibleHeader = new QWidget();
        auto* bhLayout = new QHBoxLayout(bibleHeader);
        bhLayout->setContentsMargins(0, 0, 0, 0);
        bhLayout->setSpacing(4);

        m_bibleCombo = new QComboBox();
        m_bibleCombo->setMaxVisibleItems(25);
        m_bibleCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_bibleCombo->setMinimumContentsLength(8);
        m_bibleCombo->setToolTip("Choisir la version de Bible affichée dans cette fenêtre");

        const auto bibles = m_engine->getBibleModules();
        for (const auto& b : bibles) {
            if (b.language == "fr") m_bibleCombo->addItem(QString("🇫🇷 %1 — %2").arg(b.name, b.description), b.name);
        }
        for (const auto& b : bibles) {
            if (b.language == "en") m_bibleCombo->addItem(QString("🇬🇧 %1 — %2").arg(b.name, b.description), b.name);
        }
        for (const auto& b : bibles) {
            if (b.language == "es") m_bibleCombo->addItem(QString("🇪🇸 %1 — %2").arg(b.name, b.description), b.name);
        }
        for (const auto& b : bibles) {
            if (b.language != "fr" && b.language != "en" && b.language != "es") {
                m_bibleCombo->addItem(QString("🏛️ [%1] %2 — %3").arg(b.language.toUpper(), b.name, b.description), b.name);
            }
        }
        connect(m_bibleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (m_updatingControls || idx < 0) return;
            m_bibleMod = m_bibleCombo->itemData(idx).toString();
            refreshContent(false);
            emit paneConfigChanged(m_paneIndex);
        });

        m_bibleLinkBtn = new QPushButton();
        m_bibleLinkBtn->setCursor(Qt::PointingHandCursor);
        connect(m_bibleLinkBtn, &QPushButton::clicked, this, [this]() {
            setLinked(!m_linked, true);
        });

        bhLayout->addWidget(m_bibleCombo, 1);
        bhLayout->addWidget(m_bibleLinkBtn);
        m_headerModeStack->addWidget(bibleHeader);
    }

    // --- Header Page 1 : Contrôles COMMENTAIRE (Sélecteur de commentaire + Bouton 🔗 Lié / 🔓 Délié) ---
    {
        auto* commHeader = new QWidget();
        auto* chLayout = new QHBoxLayout(commHeader);
        chLayout->setContentsMargins(0, 0, 0, 0);
        chLayout->setSpacing(4);

        m_commCombo = new QComboBox();
        m_commCombo->setMaxVisibleItems(25);
        m_commCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_commCombo->setMinimumContentsLength(8);
        m_commCombo->setToolTip("Choisir le Commentaire affiché dans cette fenêtre");
        m_commCombo->addItem("★ Tous les Commentaires Français + TSK", "ALL_FR");
        for (const auto& cm : m_engine->getCommentaryModules()) {
            QString flag = (cm.language == "fr") ? "🇫🇷 " : (cm.language == "en" ? "🇬🇧 " : "");
            m_commCombo->addItem(QString("%1%2 — %3").arg(flag, cm.name, cm.description), cm.name);
        }
        connect(m_commCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (m_updatingControls || idx < 0) return;
            m_commMod = m_commCombo->itemData(idx).toString();
            refreshContent(false);
            emit paneConfigChanged(m_paneIndex);
        });

        m_commLinkBtn = new QPushButton();
        m_commLinkBtn->setCursor(Qt::PointingHandCursor);
        connect(m_commLinkBtn, &QPushButton::clicked, this, [this]() {
            setLinked(!m_linked, true);
        });

        chLayout->addWidget(m_commCombo, 1);
        chLayout->addWidget(m_commLinkBtn);
        m_headerModeStack->addWidget(commHeader);
    }

    // --- Header Page 2 : Contrôles DICTIONNAIRE (Sélecteur de dictionnaire + Recherche Strong/Mot) ---
    {
        auto* dictHeader = new QWidget();
        auto* dhLayout = new QHBoxLayout(dictHeader);
        dhLayout->setContentsMargins(0, 0, 0, 0);
        dhLayout->setSpacing(4);

        m_dictCombo = new QComboBox();
        m_dictCombo->setMaxVisibleItems(25);
        m_dictCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_dictCombo->setMinimumContentsLength(7);
        m_dictCombo->setToolTip("Choisir le Dictionnaire affiché dans cette fenêtre");
        m_dictCombo->addItem("★ Grec/Hébreu Strong Auto", "AUTO_STRONG");
        for (const auto& lx : m_engine->getLexiconModules()) {
            QString flag = (lx.language == "fr") ? "🇫🇷 " : "";
            m_dictCombo->addItem(QString("%1%2 — %3").arg(flag, lx.name, lx.description), lx.name);
        }
        connect(m_dictCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (m_updatingControls || idx < 0) return;
            m_dictMod = m_dictCombo->itemData(idx).toString();
            refreshContent(false);
            emit paneConfigChanged(m_paneIndex);
        });

        m_dictSearchEdit = new QLineEdit();
        m_dictSearchEdit->setPlaceholderText("Strong (G3056, H1697) ou mot...");
        m_dictSearchEdit->setClearButtonEnabled(true);
        connect(m_dictSearchEdit, &QLineEdit::returnPressed, this, &UniversalPaneWidget::onDictManualSearch);

        dhLayout->addWidget(m_dictCombo, 1);
        dhLayout->addWidget(m_dictSearchEdit, 1);
        m_headerModeStack->addWidget(dictHeader);
    }

    // --- Header Page 3 : Contrôles IA GEMINI & SOURCES + MENU DÉROULANT DES DRAPEAUX (🇫🇷 🇪🇸 🇬🇧) ---
    {
        auto* aiHeader = new QWidget();
        auto* ahLayout = new QHBoxLayout(aiHeader);
        ahLayout->setContentsMargins(0, 0, 0, 0);
        ahLayout->setSpacing(4);

        m_modeChatBtn = new QPushButton("💬 Chat");
        m_modeChatBtn->setCursor(Qt::PointingHandCursor);
        m_modeChatBtn->setToolTip("Mode Chat Gemini (IA) : discuter librement avec l'IA comme dans Gemini");
        connect(m_modeChatBtn, &QPushButton::clicked, this, [this]() {
            setAiChatMode(true);
            emit paneConfigChanged(m_paneIndex);
        });

        m_modeSourcesBtn = new QPushButton("📚 Sources");
        m_modeSourcesBtn->setCursor(Qt::PointingHandCursor);
        m_modeSourcesBtn->setToolTip("Mode Recherche dans les Sources locales (Bibles, Grec/Hébreu Strong, Commentaires)");
        connect(m_modeSourcesBtn, &QPushButton::clicked, this, [this]() {
            setAiChatMode(false);
            emit paneConfigChanged(m_paneIndex);
        });

        m_aiLangCombo = new QComboBox();
        m_aiLangCombo->setCursor(Qt::PointingHandCursor);
        m_aiLangCombo->addItem("🇫🇷 Français", "fr");
        m_aiLangCombo->addItem("🇪🇸 Español", "es");
        m_aiLangCombo->addItem("🇬🇧 English", "en");
        m_aiLangCombo->setToolTip(
            "Choisir la langue dans laquelle Gemini rédige sa réponse :\n"
            "• 🇫🇷 Français (Louis Segond)\n"
            "• 🇪🇸 Español (Reina-Valera 1960 / LBLA — même si vous posez la question en français)\n"
            "• 🇬🇧 English"
        );
        m_aiLangCombo->setStyleSheet(
            "QComboBox { background-color: rgba(16, 185, 129, 0.22); color: #F8FAFC; "
            "border: 1px solid #10B981; border-radius: 5px; font-weight: bold; padding: 3px 8px; }"
            "QComboBox:hover { background-color: rgba(16, 185, 129, 0.35); }"
        );
        connect(m_aiLangCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (m_updatingControls || idx < 0) return;
            setAiResponseLanguage(m_aiLangCombo->itemData(idx).toString(), true);
        });

        m_topClearAiBtn = new QPushButton("🧹");
        m_topClearAiBtn->setCursor(Qt::PointingHandCursor);
        m_topClearAiBtn->setToolTip("Nettoyer la fenêtre IA et vider la zone de saisie");
        m_topClearAiBtn->setStyleSheet(
            "QPushButton { background-color: rgba(239, 68, 68, 0.18); color: #FCA5A5; "
            "border: 1px solid #DC2626; border-radius: 5px; font-weight: bold; padding: 3px 6px; }"
            "QPushButton:hover { background-color: rgba(239, 68, 68, 0.35); color: #FFFFFF; }"
        );
        connect(m_topClearAiBtn, &QPushButton::clicked, this, &UniversalPaneWidget::onClearAiWindow);

        ahLayout->addWidget(m_modeChatBtn);
        ahLayout->addWidget(m_modeSourcesBtn);
        ahLayout->addWidget(m_aiLangCombo);
        ahLayout->addWidget(m_topClearAiBtn);
        m_headerModeStack->addWidget(aiHeader);
    }

    // --- Header Page 4 : Contrôles NOTES PERSONNELLES (Sélecteur des notes + Outils d'annotation + 🔗 Lié / 🔓 Délié) ---
    {
        auto* notesHeader = new QWidget();
        auto* nhLayout = new QHBoxLayout(notesHeader);
        nhLayout->setContentsMargins(0, 0, 0, 0);
        nhLayout->setSpacing(4);

        m_notesListCombo = new QComboBox();
        m_notesListCombo->setMaxVisibleItems(25);
        m_notesListCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        m_notesListCombo->setMinimumContentsLength(8);
        m_notesListCombo->setToolTip("Choisir une note personnelle existante pour aller directement à son verset");
        connect(m_notesListCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (m_updatingControls || idx <= 0) return;
            QString key = m_notesListCombo->itemData(idx).toString();
            if (key.isEmpty()) return;
            QString b; int c = 1, v = 1;
            m_engine->splitSwordKey(key, b, c, v);
            loadPassage(b, c, v, true, true);
        });

        m_notesAddBlockBtn = new QPushButton("➕ Annotation");
        m_notesAddBlockBtn->setCursor(Qt::PointingHandCursor);
        m_notesAddBlockBtn->setToolTip("Ajouter un nouveau bloc d'annotation personnelle à la suite de cette fiche");
        m_notesAddBlockBtn->setStyleSheet(
            "QPushButton { background-color: rgba(2, 132, 199, 0.22); color: #38BDF8; "
            "border: 1px solid #0284C7; border-radius: 5px; font-weight: bold; padding: 3px 8px; }"
            "QPushButton:hover { background-color: rgba(2, 132, 199, 0.38); color: #FFFFFF; }"
        );
        connect(m_notesAddBlockBtn, &QPushButton::clicked, this, [this]() {
            QString key = swordKey();
            if (m_notesEditor && m_engine->hasPersonalNote(key)) {
                m_engine->saveEditedPersonalNoteHtml(key, m_notesEditor->toHtml());
            }
            m_engine->appendAnnotationBlockToPersonalNote(key, m_bibleMod);
            refreshPersonalNoteView(true);
            emit personalNoteModified(key);
            emit statusNotification(QString("✍️ Nouveau bloc d'annotation ajouté à la note de %1.").arg(m_engine->toFrenchReference(key)));
        });

        m_notesBoldBtn = new QPushButton("B");
        m_notesBoldBtn->setCursor(Qt::PointingHandCursor);
        m_notesBoldBtn->setToolTip("Mettre le texte sélectionné en Gras");
        m_notesBoldBtn->setStyleSheet(
            "QPushButton { background-color: #1E293B; color: #F8FAFC; border: 1px solid #475569; "
            "border-radius: 5px; font-weight: bold; padding: 3px 8px; }"
            "QPushButton:hover { background-color: #334155; }"
        );
        connect(m_notesBoldBtn, &QPushButton::clicked, this, [this]() {
            if (!m_notesEditor) return;
            QTextCursor cur = m_notesEditor->textCursor();
            QTextCharFormat fmt = cur.charFormat();
            fmt.setFontWeight(fmt.fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
            cur.mergeCharFormat(fmt);
            m_notesEditor->mergeCurrentCharFormat(fmt);
            m_notesEditor->setFocus();
        });

        m_notesHighlightBtn = new QPushButton("🖍️");
        m_notesHighlightBtn->setCursor(Qt::PointingHandCursor);
        m_notesHighlightBtn->setToolTip("Surligner en jaune/or le passage sélectionné dans votre note");
        m_notesHighlightBtn->setStyleSheet(
            "QPushButton { background-color: rgba(245, 158, 11, 0.22); color: #FBBF24; border: 1px solid #D97706; "
            "border-radius: 5px; font-weight: bold; padding: 3px 8px; }"
            "QPushButton:hover { background-color: rgba(245, 158, 11, 0.38); }"
        );
        connect(m_notesHighlightBtn, &QPushButton::clicked, this, [this]() {
            if (!m_notesEditor) return;
            QTextCursor cur = m_notesEditor->textCursor();
            QTextCharFormat fmt = cur.charFormat();
            if (fmt.background().style() != Qt::NoBrush && fmt.background().color() != Qt::transparent) {
                fmt.clearBackground();
            } else {
                fmt.setBackground(QColor("#854D0E"));
                fmt.setForeground(QColor("#FEF08A"));
            }
            cur.mergeCharFormat(fmt);
            m_notesEditor->setFocus();
        });

        m_notesCopyForWordBtn = new QPushButton("📋 Copier");
        m_notesCopyForWordBtn->setCursor(Qt::PointingHandCursor);
        m_notesCopyForWordBtn->setToolTip("Copier toute cette fiche d'étude (format propre sans fond noir) pour la coller directement dans votre traitement de texte (LibreOffice / Word)");
        m_notesCopyForWordBtn->setStyleSheet(
            "QPushButton { background-color: rgba(16, 185, 129, 0.20); color: #34D399; "
            "border: 1px solid #059669; border-radius: 5px; font-weight: bold; padding: 3px 8px; }"
            "QPushButton:hover { background-color: rgba(16, 185, 129, 0.36); color: #FFFFFF; }"
        );
        connect(m_notesCopyForWordBtn, &QPushButton::clicked, this, [this]() {
            if (!m_notesEditor) return;
            QString plain = m_notesEditor->toPlainText().trimmed();
            if (plain.isEmpty()) return;
            QString frRef = m_engine->toFrenchReference(swordKey());
            auto* mime = new QMimeData();
            mime->setText(plain);
            mime->setHtml(GeminiClient::markdownToWordProcessorHtml(plain, QString("📌 Fiche d'étude — %1").arg(frRef)));
            QApplication::clipboard()->setMimeData(mime);
            emit statusNotification(QString("📋 Fiche de %1 copiée — prête à coller (Ctrl+V) dans votre traitement de texte !").arg(frRef));
        });

        m_notesDeleteBtn = new QPushButton("🗑️");
        m_notesDeleteBtn->setCursor(Qt::PointingHandCursor);
        m_notesDeleteBtn->setToolTip("Effacer la note personnelle de ce verset");
        m_notesDeleteBtn->setStyleSheet(
            "QPushButton { background-color: rgba(239, 68, 68, 0.18); color: #FCA5A5; "
            "border: 1px solid #DC2626; border-radius: 5px; font-weight: bold; padding: 3px 8px; }"
            "QPushButton:hover { background-color: rgba(239, 68, 68, 0.35); color: #FFFFFF; }"
        );
        connect(m_notesDeleteBtn, &QPushButton::clicked, this, [this]() {
            QString key = swordKey();
            QString frRef = m_engine->toFrenchReference(key);
            m_engine->deletePersonalNote(key);
            refreshPersonalNoteView(false);
            emit personalNoteModified(key);
            emit statusNotification(QString("🗑️ Note personnelle de %1 supprimée.").arg(frRef));
        });

        m_notesLinkBtn = new QPushButton();
        m_notesLinkBtn->setCursor(Qt::PointingHandCursor);
        connect(m_notesLinkBtn, &QPushButton::clicked, this, [this]() {
            setLinked(!m_linked, true);
        });

        nhLayout->addWidget(m_notesListCombo, 1);
        nhLayout->addWidget(m_notesAddBlockBtn);
        nhLayout->addWidget(m_notesBoldBtn);
        nhLayout->addWidget(m_notesHighlightBtn);
        nhLayout->addWidget(m_notesCopyForWordBtn);
        nhLayout->addWidget(m_notesDeleteBtn);
        nhLayout->addWidget(m_notesLinkBtn);
        m_headerModeStack->addWidget(notesHeader);
    }

    auto* gearBtn = createGearButton(QString("⚙ Réglages de la Fenêtre %1 (taille des lettres, contraste, couleurs)").arg(paneIndex + 1));
    connect(gearBtn, &QToolButton::clicked, this, &UniversalPaneWidget::onOpenGearSettings);

    topRow->addWidget(m_typeCombo);
    topRow->addWidget(m_headerModeStack, 1);
    topRow->addWidget(gearBtn);
    rootLayout->addLayout(topRow);

    // ========================================================================
    // LIGNE 2 : Barre de navigation Livre / Chapitre / Verset (affichée en mode Bible, Commentaire et Notes)
    // ========================================================================
    m_navRowWidget = new QWidget();
    auto* navRow = new QHBoxLayout(m_navRowWidget);
    navRow->setContentsMargins(0, 0, 0, 0);
    navRow->setSpacing(4);

    m_refTitleLabel = new QLabel();
    m_refTitleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_refTitleLabel->setMinimumWidth(85);
    m_refTitleLabel->setStyleSheet("font-weight: bold; font-size: 10pt; padding: 1px 3px;");

    auto* prevBtn = new QToolButton();
    prevBtn->setText("◀");
    prevBtn->setToolTip("Chapitre précédent");
    prevBtn->setFixedSize(24, 24);
    connect(prevBtn, &QToolButton::clicked, this, &UniversalPaneWidget::onPrevChapter);

    m_bookCombo = new QComboBox();
    m_bookCombo->setMaxVisibleItems(28);
    m_bookCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_bookCombo->setMinimumContentsLength(6);
    m_bookCombo->setToolTip("Choisir le livre biblique");
    for (const auto& bk : m_engine->getBooks()) {
        m_bookCombo->addItem(bk.frenchName, bk.swordName);
    }

    m_chapterSpin = new QSpinBox();
    m_chapterSpin->setPrefix("Ch. ");
    m_chapterSpin->setRange(1, 150);

    m_verseSpin = new QSpinBox();
    m_verseSpin->setPrefix("V. ");
    m_verseSpin->setRange(1, 176);

    auto* nextBtn = new QToolButton();
    nextBtn->setText("▶");
    nextBtn->setToolTip("Chapitre suivant");
    nextBtn->setFixedSize(24, 24);
    connect(nextBtn, &QToolButton::clicked, this, &UniversalPaneWidget::onNextChapter);

    m_quickRefEdit = new QLineEdit();
    m_quickRefEdit->setPlaceholderText("Aller à (Jean 11:28)...");
    m_quickRefEdit->setClearButtonEnabled(true);
    m_quickRefEdit->setMaximumWidth(145);

    connect(m_bookCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (m_updatingControls || idx < 0) return;
        QString newBook = m_bookCombo->itemData(idx).toString();
        loadPassage(newBook, 1, 1, true, true);
    });
    connect(m_chapterSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ch) {
        if (m_updatingControls) return;
        loadPassage(m_book, ch, 1, true, true);
    });
    connect(m_verseSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_updatingControls) return;
        loadPassage(m_book, m_chapter, v, true, true);
    });
    connect(m_quickRefEdit, &QLineEdit::returnPressed, this, [this]() {
        QString q = m_quickRefEdit->text().trimmed();
        if (q.isEmpty()) return;
        QString bookSword;
        int ch = 1, v = 1;
        m_engine->parseUserReference(q, &ch, &v, &bookSword);
        m_quickRefEdit->clear();
        loadPassage(bookSword, ch, v, true, true);
    });

    navRow->addWidget(m_refTitleLabel, 1);
    navRow->addWidget(prevBtn);
    navRow->addWidget(m_bookCombo, 1);
    navRow->addWidget(m_chapterSpin);
    navRow->addWidget(m_verseSpin);
    navRow->addWidget(nextBtn);
    navRow->addWidget(m_quickRefEdit);
    rootLayout->addWidget(m_navRowWidget);

    // ========================================================================
    // CORPS PRINCIPAL : Page 0 = Browser, Page 1 = IA Gemini, Page 2 = Notes Personnelles
    // ========================================================================
    m_bodyStack = new QStackedWidget();

    // --- Page 0 : Browser standard (Bible, Commentaire, Dictionnaire) ---
    m_browser = new InteractiveTextBrowser(true);
    connect(m_browser, &QTextBrowser::anchorClicked, this, &UniversalPaneWidget::onBrowserAnchorClicked);
    connect(m_browser, &InteractiveTextBrowser::verseDropped,
            this, [this](const QString& book, int chapter, int verse) {
        loadPassage(book, chapter, verse, true, true);
        QString key = QString("%1 %2:%3").arg(book).arg(chapter).arg(verse);
        QString frRef = m_engine->toFrenchReference(key);
        emit statusNotification(
            QString("✓ Verset %1 déposé et affiché dans la Fenêtre %2.").arg(frRef).arg(m_paneIndex + 1)
        );
    });
    connect(m_browser, &InteractiveTextBrowser::selectionAiPreparedRequested,
            this, [this](int verse, const QString& selectedText, bool autoSend) {
        int v = (verse > 0) ? verse : m_verse;
        if (v != m_verse) {
            m_verse = v;
            syncHeaderControls();
        }
        QString key = QString("%1 %2:%3").arg(m_book).arg(m_chapter).arg(v);
        emit aiResearchRequested(m_paneIndex, key, m_bibleMod, selectedText, autoSend);
    });
    connect(m_browser, &InteractiveTextBrowser::selectionAiResearchRequested,
            this, [this](const QString& selectedText, bool autoSend) {
        QString srcLabel = (m_paneMode == "COMMENTARY") ? m_commMod : m_dictMod;
        emit aiResearchRequested(m_paneIndex, swordKey(), srcLabel, selectedText, autoSend);
    });
    connect(m_browser, &InteractiveTextBrowser::addSelectionToPersonalNoteRequested,
            this, [this](int verse, const QString& selectedText, bool openNotePane) {
        int v = (verse > 0) ? verse : m_verse;
        if (v != m_verse) {
            m_verse = v;
            syncHeaderControls();
        }
        QString key = QString("%1 %2:%3").arg(m_book).arg(m_chapter).arg(v);
        QString srcTitle;
        if (m_paneMode == "BIBLE") {
            srcTitle = QString("📖 Bible %1").arg(m_bibleMod);
        } else if (m_paneMode == "COMMENTARY") {
            srcTitle = QString("📚 Commentaire %1").arg((m_commMod == "ALL_FR") ? "Tous FR+TSK" : m_commMod);
        } else {
            srcTitle = QString("🏛️ Dictionnaire %1").arg(m_dictMod);
        }
        emit addExcerptToPersonalNoteRequested(m_paneIndex, key, srcTitle, selectedText, m_bibleMod, openNotePane);
    });
    connect(m_browser, &InteractiveTextBrowser::wordDictionaryRequested,
            this, [this](int verse, const QString& strong, const QString& morph, const QString& word, int wordIdx) {
        int v = (verse > 0) ? verse : m_verse;
        if (v != m_verse) {
            m_verse = v;
            syncHeaderControls();
            emit passageChanged(m_paneIndex, m_book, m_chapter, v);
        }
        QString key = QString("%1 %2:%3").arg(m_book).arg(m_chapter).arg(v);
        emit wordClickedInBible(m_paneIndex, key, strong, morph, word, wordIdx);
    });
    connect(m_browser, &InteractiveTextBrowser::verseActivated,
            this, [this](int verse) {
        if (verse <= 0) return;
        loadPassage(m_book, m_chapter, verse, true, true);
    });
    connect(m_browser, &InteractiveTextBrowser::fontSizeDeltaRequested,
            this, [this](int delta) {
        m_style.fontSizePt = std::clamp(m_style.fontSizePt + delta, 10, 32);
        refreshContent(false);
        emit styleChanged();
    });
    connect(m_browser, &InteractiveTextBrowser::statusNotification,
            this, &UniversalPaneWidget::statusNotification);
    connect(m_browser->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int val) {
        if (m_syncingScroll || m_paneMode != "BIBLE") return;
        QScrollBar* bar = m_browser->verticalScrollBar();
        int maxVal = bar->maximum();
        double ratio = (maxVal > 0) ? double(val) / double(maxVal) : 0.0;
        int topV = m_browser->topVisibleVerseNumber();
        emit bibleScrolled(m_paneIndex, ratio, topV);
    });

    m_bodyStack->addWidget(m_browser);

    // --- Page 1 : Vue IA Gemini & Sources ---
    auto* aiPage = new QWidget();
    auto* aiLayout = new QVBoxLayout(aiPage);
    aiLayout->setContentsMargins(0, 0, 0, 0);
    aiLayout->setSpacing(4);

    m_geminiStatusLabel = new QLabel("💬 Mode Chat Gemini actif — Choisissez le drapeau de langue (🇫🇷 FR, 🇪🇸 ES, 🇫🇷+🇪🇸, 🇬🇧 EN) en haut.");
    m_geminiStatusLabel->setStyleSheet("font-size: 9pt; color: #38BDF8;");
    m_geminiStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    aiLayout->addWidget(m_geminiStatusLabel);

    m_aiVerticalSplitter = new QSplitter(Qt::Vertical);
    m_aiVerticalSplitter->setHandleWidth(7);
    m_aiVerticalSplitter->setChildrenCollapsible(false);
    connect(m_aiVerticalSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        emit paneConfigChanged(m_paneIndex);
    });

    m_geminiBrowser = new InteractiveTextBrowser(false);
    m_geminiBrowser->setMinimumHeight(80);
    connect(m_geminiBrowser, &QTextBrowser::anchorClicked, this, &UniversalPaneWidget::onBrowserAnchorClicked);
    connect(m_geminiBrowser, &InteractiveTextBrowser::verseDropped,
            this, [this](const QString& book, int chapter, int verse) {
        handleVerseDroppedInAi(book, chapter, verse, m_bibleMod);
    });
    connect(m_geminiBrowser, &InteractiveTextBrowser::selectionAiResearchRequested,
            this, [this](const QString& sel, bool autoSend) {
        emit aiResearchRequested(m_paneIndex, swordKey(), "IA Gemini", sel, autoSend);
    });
    connect(m_geminiBrowser, &InteractiveTextBrowser::addSelectionToPersonalNoteRequested,
            this, [this](int, const QString& selectedText, bool openNotePane) {
        QString targetKey = m_preparedSwordKey.isEmpty() ? swordKey() : m_preparedSwordKey;
        QString srcTitle = m_aiChatMode
            ? QString("✨ IA Gemini (%1)").arg(GeminiClient::languageDisplayName(m_aiResponseLang))
            : "📚 Sources Locales";
        QString prefMod = m_preparedBibleMod.isEmpty() ? m_bibleMod : m_preparedBibleMod;
        emit addExcerptToPersonalNoteRequested(m_paneIndex, targetKey, srcTitle, selectedText, prefMod, openNotePane);
    });
    connect(m_geminiBrowser, &InteractiveTextBrowser::fontSizeDeltaRequested,
            this, [this](int delta) {
        m_style.fontSizePt = std::clamp(m_style.fontSizePt + delta, 10, 32);
        renderGeminiConversation();
        emit styleChanged();
    });
    connect(m_geminiBrowser, &InteractiveTextBrowser::statusNotification,
            this, &UniversalPaneWidget::statusNotification);
    m_aiVerticalSplitter->addWidget(m_geminiBrowser);

    auto* inputContainer = new QWidget();
    inputContainer->setMinimumHeight(95);
    auto* inputRow = new QHBoxLayout(inputContainer);
    inputRow->setContentsMargins(0, 4, 0, 0);
    inputRow->setSpacing(6);

    m_geminiInputEdit = new QPlainTextEdit();
    m_geminiInputEdit->setMinimumHeight(80);
    m_geminiInputEdit->setAcceptDrops(true);
    m_geminiInputEdit->installEventFilter(this);
    if (m_geminiInputEdit->viewport()) {
        m_geminiInputEdit->viewport()->installEventFilter(this);
    }

    auto* btnCol = new QVBoxLayout();
    btnCol->setSpacing(3);

    m_copyForWordBtn = new QPushButton("📋 Copier (Traitement de texte)");
    m_copyForWordBtn->setCursor(Qt::PointingHandCursor);
    m_copyForWordBtn->setToolTip(
        "Copier la dernière réponse de Gemini en format riche propre (sans fond noir, avec titres, gras et listes)\n"
        "pour la coller directement (Ctrl+V) dans votre traitement de texte (LibreOffice Writer, Word, Google Docs)"
    );
    m_copyForWordBtn->setStyleSheet(
        "QPushButton { background-color: rgba(245, 158, 11, 0.22); color: #FBBF24; "
        "border: 1px solid #D97706; border-radius: 5px; font-weight: bold; padding: 4px 8px; }"
        "QPushButton:hover { background-color: rgba(245, 158, 11, 0.38); color: #FFFFFF; }"
    );
    connect(m_copyForWordBtn, &QPushButton::clicked, this, &UniversalPaneWidget::onCopyLatestAiForWordProcessor);

    m_clearAiBtn = new QPushButton("🧹 Nettoyer");
    m_clearAiBtn->setCursor(Qt::PointingHandCursor);
    m_clearAiBtn->setStyleSheet(
        "QPushButton { background-color: rgba(239, 68, 68, 0.18); color: #FCA5A5; "
        "border: 1px solid #DC2626; border-radius: 5px; font-weight: bold; padding: 4px 8px; }"
        "QPushButton:hover { background-color: rgba(239, 68, 68, 0.35); color: #FFFFFF; }"
    );
    connect(m_clearAiBtn, &QPushButton::clicked, this, &UniversalPaneWidget::onClearAiWindow);

    m_micDictateBtn = new QPushButton("🎤 Dicter");
    m_micDictateBtn->setCursor(Qt::PointingHandCursor);
    m_micDictateBtn->setStyleSheet(
        "QPushButton { background-color: rgba(16, 185, 129, 0.20); color: #34D399; "
        "border: 1px solid #059669; border-radius: 5px; font-weight: bold; padding: 4px 8px; }"
        "QPushButton:hover { background-color: rgba(16, 185, 129, 0.35); }"
    );
    connect(m_micDictateBtn, &QPushButton::clicked, this, &UniversalPaneWidget::onToggleMicDictation);

    m_sendAiBtn = new QPushButton("🚀 Envoyer à Gemini");
    m_sendAiBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sendAiBtn, &QPushButton::clicked, this, &UniversalPaneWidget::onSendCustomAiQuestion);

    btnCol->addWidget(m_copyForWordBtn);
    btnCol->addWidget(m_clearAiBtn);
    btnCol->addWidget(m_micDictateBtn);
    btnCol->addWidget(m_sendAiBtn, 1);

    inputRow->addWidget(m_geminiInputEdit, 1);
    inputRow->addLayout(btnCol);

    m_aiVerticalSplitter->addWidget(inputContainer);
    m_aiVerticalSplitter->setStretchFactor(0, 3);
    m_aiVerticalSplitter->setStretchFactor(1, 1);
    m_aiVerticalSplitter->setSizes({480, 150});

    aiLayout->addWidget(m_aiVerticalSplitter, 1);
    m_bodyStack->addWidget(aiPage);

    // --- Page 2 : Vue NOTES PERSONNELLES (Éditeur riche indexé par verset) ---
    auto* notesPage = new QWidget();
    auto* notesLayout = new QVBoxLayout(notesPage);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->setSpacing(4);

    m_notesStatusLabel = new QLabel();
    m_notesStatusLabel->setStyleSheet("font-size: 9pt; color: #FBBF24;");
    m_notesStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    notesLayout->addWidget(m_notesStatusLabel);

    m_notesEditor = new QTextEdit();
    m_notesEditor->setAcceptRichText(true);
    connect(m_notesEditor, &QTextEdit::textChanged, this, [this]() {
        if (m_loadingNoteHtml) return;
        QString key = swordKey();
        if (m_notesEditor->document()->isEmpty() || m_notesEditor->toPlainText().trimmed().isEmpty()) {
            return;
        }
        m_engine->saveEditedPersonalNoteHtml(key, m_notesEditor->toHtml());
        if (m_notesStatusLabel) {
            m_notesStatusLabel->setText(
                QString("💾 Note de %1 enregistrée automatiquement — Vous pouvez continuer à écrire ou ajouter d'autres extraits depuis Gemini (Clic Droit).")
                    .arg(m_engine->toFrenchReference(key))
            );
        }
        populateNotesCombo();
        emit personalNoteModified(key);
    });
    notesLayout->addWidget(m_notesEditor, 1);
    m_bodyStack->addWidget(notesPage);

    rootLayout->addWidget(m_bodyStack, 1);

    // Connexions aux signaux de GeminiClient
    connect(m_gemini, &GeminiClient::requestStarted, this, [this](const QString& statusTxt) {
        if (m_paneMode == "AI") {
            m_geminiStatusLabel->setText("⏳ " + statusTxt);
            renderGeminiConversation();
        }
    });
    connect(m_gemini, &GeminiClient::responseReceived, this, [this](const QString&) {
        if (m_paneMode == "AI") {
            m_geminiStatusLabel->setText(
                QString("✓ Réponse reçue (%1) — Cliquez sur « 📋 Copier (Traitement de texte) » ou surlignez un extrait (Clic Droit → 📝 Ajouter à la note).")
                    .arg(GeminiClient::languageDisplayName(m_aiResponseLang))
            );
            renderGeminiConversation();
        }
    });
    connect(m_gemini, &GeminiClient::errorOccurred, this, [this](const QString& err) {
        if (m_paneMode == "AI") {
            m_geminiStatusLabel->setText("⚠️ " + err);
            renderGeminiConversation();
        }
    });

    updateLinkButtonUi();
    updateAiLanguageButtonsUi();
    updateAiModeSwitchUi();
    syncHeaderControls();
}

UniversalPaneWidget::~UniversalPaneWidget() {
    stopMicDictation();
}

QString UniversalPaneWidget::swordKey() const {
    return QString("%1 %2:%3").arg(m_book).arg(m_chapter).arg(m_verse);
}

void UniversalPaneWidget::onTypeComboChanged(int index) {
    if (m_updatingControls || index < 0) return;
    QString newMode = m_typeCombo->itemData(index).toString();
    setPaneMode(newMode, true);
}

void UniversalPaneWidget::setPaneMode(const QString& mode, bool emitSignal) {
    m_paneMode = mode;
    m_updatingControls = true;
    int tIdx = m_typeCombo->findData(mode);
    if (tIdx >= 0) m_typeCombo->setCurrentIndex(tIdx);
    m_updatingControls = false;

    if (m_paneMode == "BIBLE") {
        m_headerModeStack->setCurrentIndex(0);
        m_navRowWidget->show();
        m_bodyStack->setCurrentIndex(0);
        m_browser->setIsBibleView(true);
    } else if (m_paneMode == "COMMENTARY") {
        m_headerModeStack->setCurrentIndex(1);
        m_navRowWidget->show();
        m_bodyStack->setCurrentIndex(0);
        m_browser->setIsBibleView(false);
    } else if (m_paneMode == "DICTIONARY") {
        m_headerModeStack->setCurrentIndex(2);
        m_navRowWidget->hide();
        m_bodyStack->setCurrentIndex(0);
        m_browser->setIsBibleView(false);
    } else if (m_paneMode == "AI") {
        m_headerModeStack->setCurrentIndex(3);
        m_navRowWidget->hide();
        m_bodyStack->setCurrentIndex(1);
    } else if (m_paneMode == "NOTES") {
        m_headerModeStack->setCurrentIndex(4);
        m_navRowWidget->show();
        m_bodyStack->setCurrentIndex(2);
    }

    syncHeaderControls();
    refreshContent(true);

    if (emitSignal) {
        emit paneConfigChanged(m_paneIndex);
    }
}

void UniversalPaneWidget::setBibleModule(const QString& mod, bool switchToBibleMode) {
    if (!mod.isEmpty()) {
        m_bibleMod = mod;
        m_updatingControls = true;
        int idx = m_bibleCombo->findData(mod);
        if (idx >= 0) m_bibleCombo->setCurrentIndex(idx);
        m_updatingControls = false;
    }
    if (switchToBibleMode && m_paneMode != "BIBLE") {
        setPaneMode("BIBLE", true);
    } else if (m_paneMode == "BIBLE") {
        refreshContent(false);
        emit paneConfigChanged(m_paneIndex);
    }
}

void UniversalPaneWidget::setCommentaryModule(const QString& mod, bool switchToCommMode) {
    if (!mod.isEmpty()) {
        m_commMod = mod;
        m_updatingControls = true;
        int idx = m_commCombo->findData(mod);
        if (idx >= 0) m_commCombo->setCurrentIndex(idx);
        m_updatingControls = false;
    }
    if (switchToCommMode && m_paneMode != "COMMENTARY") {
        setPaneMode("COMMENTARY", true);
    } else if (m_paneMode == "COMMENTARY") {
        refreshContent(false);
        emit paneConfigChanged(m_paneIndex);
    }
}

void UniversalPaneWidget::setDictionaryModule(const QString& mod, bool switchToDictMode) {
    if (!mod.isEmpty()) {
        m_dictMod = mod;
        m_updatingControls = true;
        int idx = m_dictCombo->findData(mod);
        if (idx >= 0) m_dictCombo->setCurrentIndex(idx);
        m_updatingControls = false;
    }
    if (switchToDictMode && m_paneMode != "DICTIONARY") {
        setPaneMode("DICTIONARY", true);
    } else if (m_paneMode == "DICTIONARY") {
        refreshContent(false);
        emit paneConfigChanged(m_paneIndex);
    }
}

void UniversalPaneWidget::setAiChatMode(bool chatMode) {
    m_aiChatMode = chatMode;
    updateAiModeSwitchUi();
}

void UniversalPaneWidget::setAiResponseLanguage(const QString& langCode, bool emitSignal) {
    QString c = langCode.trimmed().toLower();
    if (c != "es" && c != "en") c = "fr";
    m_aiResponseLang = c;
    if (m_gemini) {
        m_gemini->setResponseLanguage(c);
    }
    updateAiLanguageButtonsUi();
    updateAiModeSwitchUi();

    if (emitSignal) {
        emit paneConfigChanged(m_paneIndex);
        emit statusNotification(
            QString("🌍 Langue de réponse IA réglée sur : %1 — vos prochaines réponses Gemini seront dans cette langue.")
                .arg(GeminiClient::languageDisplayName(m_aiResponseLang))
        );
    }
}

void UniversalPaneWidget::updateAiLanguageButtonsUi() {
    if (!m_aiLangCombo) return;
    bool prevUpd = m_updatingControls;
    m_updatingControls = true;
    int idx = m_aiLangCombo->findData(m_aiResponseLang);
    if (idx >= 0) {
        m_aiLangCombo->setCurrentIndex(idx);
    }
    m_updatingControls = prevUpd;
}

void UniversalPaneWidget::setLinked(bool linked, bool emitSignal) {
    m_linked = linked;
    updateLinkButtonUi();
    if (emitSignal) {
        emit paneConfigChanged(m_paneIndex);
        emit statusNotification(
            m_linked
                ? QString("🔗 Fenêtre %1 liée (synchronisée avec les autres fenêtres liées).").arg(m_paneIndex + 1)
                : QString("🔓 Fenêtre %1 déliée (indépendante).").arg(m_paneIndex + 1)
        );
    }
}

void UniversalPaneWidget::updateLinkButtonUi() {
    QString txt = m_linked ? "🔗 Lié" : "🔓 Délié";
    QString tip = m_linked
        ? "Cette fenêtre est LIÉE (synchronisée) :\n• Elle suit automatiquement le passage et le défilement des autres fenêtres liées.\nCliquez pour la DÉLIER."
        : "Cette fenêtre est DÉLIÉE (indépendante) :\n• Vous pouvez y lire un autre passage sans bouger les autres fenêtres.\nCliquez pour la RELIER.";
    QString qss = m_linked
        ? "QPushButton { background-color: rgba(16, 185, 129, 0.18); color: #34D399; border: 1px solid #059669; border-radius: 5px; font-weight: bold; padding: 3px 8px; }"
          "QPushButton:hover { background-color: rgba(16, 185, 129, 0.30); }"
        : "QPushButton { background-color: rgba(245, 158, 11, 0.18); color: #FBBF24; border: 1px solid #D97706; border-radius: 5px; font-weight: bold; padding: 3px 8px; }"
          "QPushButton:hover { background-color: rgba(245, 158, 11, 0.30); }";

    for (QPushButton* btn : {m_bibleLinkBtn, m_commLinkBtn, m_notesLinkBtn}) {
        if (!btn) continue;
        btn->setText(txt);
        btn->setToolTip(tip);
        btn->setStyleSheet(qss);
    }
}

void UniversalPaneWidget::updateAiModeSwitchUi() {
    if (!m_modeChatBtn || !m_modeSourcesBtn || !m_sendAiBtn || !m_geminiInputEdit) return;

    QString langBadge = (m_aiResponseLang == "es") ? "🇪🇸 ES"
                      : (m_aiResponseLang == "en" ? "🇬🇧 EN" : "🇫🇷 FR");

    if (m_aiChatMode) {
        m_modeChatBtn->setStyleSheet(
            "QPushButton { background-color: #0284C7; color: #FFFFFF; "
            "border: 2px solid #38BDF8; border-radius: 5px; font-weight: bold; padding: 3px 7px; }"
        );
        m_modeSourcesBtn->setStyleSheet(
            "QPushButton { background-color: #1E293B; color: #94A3B8; "
            "border: 1px solid #334155; border-radius: 5px; font-weight: normal; padding: 3px 7px; }"
            "QPushButton:hover { background-color: #334155; color: #E2E8F0; }"
        );
        m_sendAiBtn->setText(QString("🚀 Envoyer (%1)").arg(langBadge));
        m_sendAiBtn->setStyleSheet(
            "QPushButton { background-color: #0284C7; color: #FFFFFF; "
            "border: 1px solid #38BDF8; border-radius: 5px; font-weight: bold; padding: 5px 10px; }"
            "QPushButton:hover { background-color: #0369A1; }"
        );
        m_geminiInputEdit->setPlaceholderText(
            QString("💬 Chat Gemini [Réponse : %1] : écrivez en français ou en espagnol (ou dictez 🎤), "
                    "Gemini répondra dans la langue choisie dans le menu déroulant en haut...")
                .arg(GeminiClient::languageDisplayName(m_aiResponseLang))
        );
        if (!m_gemini->isPending()) {
            m_geminiStatusLabel->setText(
                QString("💬 Langue de réponse active : %1 — Choisissez 🇫🇷 Français, 🇪🇸 Español ou 🇬🇧 English dans le menu en haut.")
                    .arg(GeminiClient::languageDisplayName(m_aiResponseLang))
            );
        }
    } else {
        m_modeSourcesBtn->setStyleSheet(
            "QPushButton { background-color: #D97706; color: #FFFFFF; "
            "border: 2px solid #FBBF24; border-radius: 5px; font-weight: bold; padding: 3px 7px; }"
        );
        m_modeChatBtn->setStyleSheet(
            "QPushButton { background-color: #1E293B; color: #94A3B8; "
            "border: 1px solid #334155; border-radius: 5px; font-weight: normal; padding: 3px 7px; }"
            "QPushButton:hover { background-color: #334155; color: #E2E8F0; }"
        );
        m_sendAiBtn->setText("🔍 Chercher Sources");
        m_sendAiBtn->setStyleSheet(
            "QPushButton { background-color: #D97706; color: #FFFFFF; "
            "border: 1px solid #FBBF24; border-radius: 5px; font-weight: bold; padding: 5px 10px; }"
            "QPushButton:hover { background-color: #B45309; }"
        );
        m_geminiInputEdit->setPlaceholderText(
            "📚 Recherche dans les Sources : tapez un mot ou une expression à chercher dans vos Bibles et commentaires locaux..."
        );
        m_geminiStatusLabel->setText("📚 Mode Recherche dans les Sources locales actif — Cliquez sur « 💬 Chat » pour discuter avec Gemini.");
    }

    if (!m_aiChatMode && m_localFallbackCardHtml.isEmpty()) {
        m_localFallbackCardHtml = buildLocalExegesisFallbackHtml(m_lastSourcesQuery);
    }
    if (m_paneMode == "AI") {
        renderGeminiConversation();
    }
}

void UniversalPaneWidget::populateNotesCombo() {
    if (!m_notesListCombo) return;
    bool prevUpd = m_updatingControls;
    m_updatingControls = true;
    m_notesListCombo->clear();

    const auto notes = m_engine->getAllPersonalNotes();
    m_notesListCombo->addItem(QString("📌 Mes notes enregistrées (%1)...").arg(notes.size()), QString());
    QString curKey = swordKey();
    int matchIdx = 0;
    for (int i = 0; i < notes.size(); ++i) {
        const auto& n = notes[i];
        m_notesListCombo->addItem(
            QString("📝 %1 (%2 extrait%3)").arg(n.frenchRef).arg(n.excerptCount).arg(n.excerptCount > 1 ? "s" : ""),
            n.swordKey
        );
        if (n.swordKey == curKey) {
            matchIdx = i + 1;
        }
    }
    m_notesListCombo->setCurrentIndex(matchIdx);
    m_updatingControls = prevUpd;
}

void UniversalPaneWidget::syncHeaderControls() {
    m_updatingControls = true;

    int tIdx = m_typeCombo->findData(m_paneMode);
    if (tIdx >= 0) m_typeCombo->setCurrentIndex(tIdx);

    int bModIdx = m_bibleCombo->findData(m_bibleMod);
    if (bModIdx >= 0) m_bibleCombo->setCurrentIndex(bModIdx);

    int cModIdx = m_commCombo->findData(m_commMod);
    if (cModIdx >= 0) m_commCombo->setCurrentIndex(cModIdx);

    int dModIdx = m_dictCombo->findData(m_dictMod);
    if (dModIdx >= 0) m_dictCombo->setCurrentIndex(dModIdx);

    int bkIdx = m_bookCombo->findData(m_book);
    if (bkIdx >= 0) m_bookCombo->setCurrentIndex(bkIdx);

    m_chapterSpin->setMaximum(m_engine->getChapterCount(m_book));
    m_chapterSpin->setValue(m_chapter);

    m_verseSpin->setMaximum(m_engine->getVerseCount(m_book, m_chapter));
    m_verseSpin->setValue(m_verse);

    populateNotesCombo();

    QString frRef = m_engine->toFrenchReference(swordKey());
    if (m_paneMode == "BIBLE") {
        m_refTitleLabel->setText(QString("📖 %1 (%2)").arg(frRef, m_bibleMod));
    } else if (m_paneMode == "COMMENTARY") {
        QString cName = (m_commMod == "ALL_FR") ? "Tous FR+TSK" : m_commMod;
        m_refTitleLabel->setText(QString("📚 %1 (%2)").arg(frRef, cName));
    } else if (m_paneMode == "NOTES") {
        m_refTitleLabel->setText(QString("📝 Fiche %1").arg(frRef));
    } else {
        m_refTitleLabel->setText(frRef);
    }
    m_refTitleLabel->setToolTip(m_refTitleLabel->text());
    m_refTitleLabel->setStyleSheet(
        QString("font-weight: bold; font-size: 10pt; color: %1; padding: 1px 3px;").arg(m_style.accentColor)
    );

    m_updatingControls = false;
}

void UniversalPaneWidget::loadPassage(const QString& swordBook, int chapter, int verse, bool scrollToVerse, bool emitSignal) {
    int newCh = std::clamp(chapter, 1, m_engine->getChapterCount(swordBook));
    bool chapterChanged = (m_book != swordBook || m_chapter != newCh);
    if (chapterChanged) {
        m_browser->clearWordHighlight();
    }
    m_book = swordBook;
    m_chapter = newCh;
    m_verse = std::clamp(verse, 1, m_engine->getVerseCount(swordBook, m_chapter));

    syncHeaderControls();

    if (m_paneMode == "BIBLE") {
        refreshContent(scrollToVerse);
    } else if (m_paneMode == "COMMENTARY") {
        refreshContent(false);
    } else if (m_paneMode == "NOTES") {
        refreshPersonalNoteView(false);
    } else if (m_paneMode == "AI" && !m_aiChatMode && m_preparedSwordKey.isEmpty()) {
        m_localFallbackCardHtml = buildLocalExegesisFallbackHtml(m_lastSourcesQuery);
        renderGeminiConversation();
    } else if (m_paneMode == "AI") {
        renderGeminiConversation();
    }

    if (emitSignal) {
        emit passageChanged(m_paneIndex, m_book, m_chapter, m_verse);
    }
}

void UniversalPaneWidget::updateCommentaryVerseOnly(int verse) {
    if (verse <= 0 || verse == m_verse) return;
    m_verse = std::clamp(verse, 1, m_engine->getVerseCount(m_book, m_chapter));
    syncHeaderControls();
    if (m_paneMode == "COMMENTARY") {
        refreshContent(false);
    } else if (m_paneMode == "NOTES") {
        refreshPersonalNoteView(false);
    }
}

void UniversalPaneWidget::refreshPersonalNoteView(bool scrollToBottom) {
    if (!m_notesEditor || !m_notesStatusLabel) return;
    QString key = swordKey();
    QString frRef = m_engine->toFrenchReference(key);

    QString fontFam = m_style.useSerifFont ? "'Noto Serif',serif" : "'Noto Sans',sans-serif";
    m_notesEditor->setStyleSheet(
        QString("QTextEdit { background-color: %1; color: %2; font-family: %3; font-size: %4pt; "
                "border: 1px solid %5; border-radius: 6px; padding: 8px; }")
            .arg(m_style.bgColor, m_style.textColor, fontFam)
            .arg(m_style.fontSizePt)
            .arg(m_style.borderColor)
    );

    m_loadingNoteHtml = true;
    if (m_engine->hasPersonalNote(key)) {
        PersonalNote n = m_engine->getPersonalNote(key);
        m_notesEditor->setHtml(n.htmlBody);
        m_notesStatusLabel->setText(
            QString("📌 Fiche d'étude personnelle — %1 (%2 extrait%3 • MAJ %4) — Écrivez directement ou ajoutez d'autres extraits depuis Gemini (Clic Droit).")
                .arg(frRef)
                .arg(n.excerptCount)
                .arg(n.excerptCount > 1 ? "s" : "")
                .arg(n.updatedAt)
        );
    } else {
        m_notesEditor->setHtml(m_engine->buildInitialNoteHeaderHtml(key, m_bibleMod));
        m_notesStatusLabel->setText(
            QString("📝 Nouvelle fiche pour %1 — Sélectionnez des passages dans Gemini (Clic Droit → « 📝 Ajouter à la note ») ou cliquez sur « ➕ Annotation ».")
                .arg(frRef)
        );
    }
    m_loadingNoteHtml = false;
    populateNotesCombo();

    if (scrollToBottom) {
        QTimer::singleShot(25, this, [this]() {
            if (m_notesEditor && m_notesEditor->verticalScrollBar()) {
                m_notesEditor->verticalScrollBar()->setValue(m_notesEditor->verticalScrollBar()->maximum());
            }
        });
    }
}

double UniversalPaneWidget::scrollRatio() const {
    QScrollBar* bar = m_browser->verticalScrollBar();
    return (bar && bar->maximum() > 0) ? double(bar->value()) / double(bar->maximum()) : 0.0;
}

void UniversalPaneWidget::setScrollRatio(double ratio) {
    if (m_paneMode != "BIBLE") return;
    m_syncingScroll = true;
    QScrollBar* bar = m_browser->verticalScrollBar();
    if (bar && bar->maximum() > 0) {
        bar->setValue(int(ratio * bar->maximum()));
    }
    m_syncingScroll = false;
}

void UniversalPaneWidget::highlightWordInBible(int verse, const QString& resolvedStrong, const QString& word, int wordIdx, bool exactWord) {
    if (m_paneMode != "BIBLE") return;
    m_browser->highlightWord(verse, resolvedStrong, exactWord ? word : QString(), exactWord ? wordIdx : -1, exactWord);
}

void UniversalPaneWidget::clearWordHighlight() {
    m_browser->clearWordHighlight();
}

void UniversalPaneWidget::showWordDefinition(
    const QString& key,
    const QString& strongCode,
    const QString& morphCode,
    const QString& clickedWord,
    int wordIndex
) {
    QString b; int c = 1, v = 1;
    m_engine->splitSwordKey(key, b, c, v);
    m_book = b;
    m_chapter = c;
    m_verse = v;

    WordToken tk = m_engine->resolveClickedWordToToken(key, clickedWord, strongCode, morphCode, wordIndex);
    m_activeStrongCode = tk.strongCode;
    m_activeMorphCode = tk.morphCode;
    m_activeClickedWord = clickedWord.isEmpty() ? tk.surfaceText.trimmed() : clickedWord;
    m_dictMod = "AUTO_STRONG";

    syncHeaderControls();
    if (m_paneMode == "DICTIONARY") {
        refreshContent(false);
    }
}

void UniversalPaneWidget::reloadModuleLists() {
    bool prevUpd = m_updatingControls;
    m_updatingControls = true;

    if (m_bibleCombo) {
        m_bibleCombo->clear();
        const auto bibles = m_engine->getBibleModules();
        for (const auto& b : bibles) {
            if (b.language == "fr") m_bibleCombo->addItem(QString("🇫🇷 %1 — %2").arg(b.name, b.description), b.name);
        }
        for (const auto& b : bibles) {
            if (b.language == "en") m_bibleCombo->addItem(QString("🇬🇧 %1 — %2").arg(b.name, b.description), b.name);
        }
        for (const auto& b : bibles) {
            if (b.language == "es") m_bibleCombo->addItem(QString("🇪🇸 %1 — %2").arg(b.name, b.description), b.name);
        }
        for (const auto& b : bibles) {
            if (b.language != "fr" && b.language != "en" && b.language != "es") {
                m_bibleCombo->addItem(QString("🏛️ [%1] %2 — %3").arg(b.language.toUpper(), b.name, b.description), b.name);
            }
        }
    }

    if (m_commCombo) {
        m_commCombo->clear();
        m_commCombo->addItem("★ Tous les Commentaires Français + TSK", "ALL_FR");
        for (const auto& cm : m_engine->getCommentaryModules()) {
            QString flag = (cm.language == "fr") ? "🇫🇷 " : (cm.language == "en" ? "🇬🇧 " : (cm.language == "es" ? "🇪🇸 " : ""));
            m_commCombo->addItem(QString("%1%2 — %3").arg(flag, cm.name, cm.description), cm.name);
        }
    }

    if (m_dictCombo) {
        m_dictCombo->clear();
        m_dictCombo->addItem("★ Grec/Hébreu Strong Auto", "AUTO_STRONG");
        for (const auto& lx : m_engine->getLexiconModules()) {
            QString flag = (lx.language == "fr") ? "🇫🇷 " : (lx.language == "es" ? "🇪🇸 " : "");
            m_dictCombo->addItem(QString("%1%2 — %3").arg(flag, lx.name, lx.description), lx.name);
        }
    }

    m_updatingControls = prevUpd;
    syncHeaderControls();
    refreshContent(false);
}

void UniversalPaneWidget::refreshContent(bool scrollToVerse) {
    syncHeaderControls();

    const BookInfo* bInfo = m_engine->findBookBySwordName(m_book);
    QString frBookName = bInfo ? bInfo->frenchName : m_book;

    if (m_paneMode == "BIBLE") {
        m_browser->setIsBibleView(true);
        m_browser->setPassageContext(m_book, m_chapter, m_bibleMod, frBookName, m_verse);
        int savedScroll = m_browser->verticalScrollBar()->value();

        QString html = m_engine->renderChapterHtml(m_bibleMod, m_book, m_chapter, m_verse, m_style);
        m_syncingScroll = true;
        m_browser->setHtml(html);
        m_browser->reapplyWordHighlight();
        m_syncingScroll = false;

        if (scrollToVerse) {
            QTimer::singleShot(15, this, [this]() {
                scrollBibleToActiveVerse();
            });
        } else {
            m_syncingScroll = true;
            m_browser->verticalScrollBar()->setValue(savedScroll);
            m_syncingScroll = false;
        }
    } else if (m_paneMode == "COMMENTARY") {
        m_browser->setIsBibleView(false);
        m_browser->setPassageContext(m_book, m_chapter, m_commMod, frBookName, m_verse);
        bool allFr = (m_commMod == "ALL_FR");
        m_browser->setHtml(m_engine->renderCommentaryHtml(
            allFr ? QString() : m_commMod,
            swordKey(),
            allFr,
            m_style
        ));
    } else if (m_paneMode == "DICTIONARY") {
        m_browser->setIsBibleView(false);
        m_browser->setPassageContext(m_book, m_chapter, m_dictMod, frBookName, m_verse);
        if (m_dictMod == "AUTO_STRONG" || m_dictMod == "Fr_Greek" ||
            m_dictMod == "Fr_Hebreu" || m_dictMod == "StrongsGreek" ||
            m_dictMod == "StrongsHebrew" || m_dictMod == "BDBGlosses_Strongs" ||
            m_dictMod == "MLStrong" || m_dictMod == "Strong Zodiates (NT)") {
            m_browser->setHtml(m_engine->lookupStrongAndMorphHtml(
                swordKey(),
                m_activeStrongCode,
                m_activeMorphCode,
                m_activeClickedWord,
                m_style
            ));
        } else {
            QString k = m_dictSearchEdit->text().trimmed();
            if (k.isEmpty()) k = m_activeClickedWord;
            m_browser->setHtml(m_engine->lookupDictionaryKeyHtml(m_dictMod, k, m_style));
        }
    } else if (m_paneMode == "AI") {
        renderGeminiConversation();
    } else if (m_paneMode == "NOTES") {
        refreshPersonalNoteView(false);
    }
}

bool UniversalPaneWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_geminiInputEdit || (m_geminiInputEdit && watched == m_geminiInputEdit->viewport())) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData() && de->mimeData()->hasFormat("application/x-bible-verse-ref")) {
                de->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData() && de->mimeData()->hasFormat("application/x-bible-verse-ref")) {
                QString payload = QString::fromUtf8(de->mimeData()->data("application/x-bible-verse-ref"));
                QStringList parts = payload.split('|');
                if (parts.size() >= 3) {
                    QString book = parts[0];
                    int ch = parts[1].toInt();
                    int v = parts[2].toInt();
                    QString srcMod = (parts.size() >= 4) ? parts[3] : m_bibleMod;
                    if (!book.isEmpty() && ch > 0 && v > 0) {
                        handleVerseDroppedInAi(book, ch, v, srcMod);
                        de->acceptProposedAction();
                        return true;
                    }
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void UniversalPaneWidget::handleVerseDroppedInAi(const QString& book, int chapter, int verse, const QString& sourceModule) {
    QString key = QString("%1 %2:%3").arg(book).arg(chapter).arg(verse);
    QString frRef = m_engine->toFrenchReference(key);
    QString mod = sourceModule.isEmpty() ? m_bibleMod : sourceModule;
    QString verseTxt = m_engine->getCleanVerseText(mod, key);
    if (verseTxt.isEmpty()) verseTxt = m_engine->getCleanVerseText("FreSegond", key);
    if (verseTxt.isEmpty()) verseTxt = m_engine->getCleanVerseText("FreLSG", key);

    m_sourcesCleared = false;
    m_book = book;
    m_chapter = chapter;
    m_verse = verse;
    m_preparedSwordKey = key;
    m_preparedBibleMod = mod;
    m_preparedExcerptText = verseTxt;

    if (!m_aiChatMode) {
        m_geminiInputEdit->setPlainText(frRef);
        QTextCursor cur = m_geminiInputEdit->textCursor();
        cur.movePosition(QTextCursor::End);
        m_geminiInputEdit->setTextCursor(cur);
        m_lastSourcesQuery.clear();
        m_localFallbackCardHtml = buildLocalExegesisFallbackHtml(QString());
        m_geminiStatusLabel->setText(
            QString("✓ Verset %1 (%2) déposé — Dossier complet des sources locales affiché ci-dessus.")
                .arg(frRef, mod)
        );
        renderGeminiConversation();
        emit statusNotification(QString("📚 Sources locales affichées pour %1.").arg(frRef));
    } else {
        QString ctx;
        QStringList activeBibles;
        emit requestStudyContext(ctx, activeBibles);
        preparePassageForAiQuestion(key, mod, verseTxt, activeBibles, false);
        emit statusNotification(QString("🤖 Verset %1 prêt dans le Chat Gemini — posez votre question.").arg(frRef));
    }
}

void UniversalPaneWidget::scrollBibleToActiveVerse() {
    if (m_paneMode != "BIBLE" || !isVisible()) return;
    m_syncingScroll = true;
    m_browser->scrollToAnchor(QString("v%1").arg(m_verse));
    m_syncingScroll = false;
}

void UniversalPaneWidget::onPrevChapter() {
    QString targetBook = m_book;
    int targetCh = m_chapter;
    if (targetCh > 1) {
        targetCh -= 1;
    } else if (const BookInfo* b = m_engine->findBookBySwordName(targetBook)) {
        if (b->index > 1) {
            const BookInfo& prevBook = m_engine->getBooks().at(b->index - 2);
            targetBook = prevBook.swordName;
            targetCh = prevBook.chapterCount;
        }
    }
    loadPassage(targetBook, targetCh, 1, true, true);
}

void UniversalPaneWidget::onNextChapter() {
    QString targetBook = m_book;
    int targetCh = m_chapter;
    int maxCh = m_engine->getChapterCount(targetBook);
    if (targetCh < maxCh) {
        targetCh += 1;
    } else if (const BookInfo* b = m_engine->findBookBySwordName(targetBook)) {
        if (b->index < m_engine->getBooks().size()) {
            const BookInfo& nextBook = m_engine->getBooks().at(b->index);
            targetBook = nextBook.swordName;
            targetCh = 1;
        }
    }
    loadPassage(targetBook, targetCh, 1, true, true);
}

void UniversalPaneWidget::onDictManualSearch() {
    QString q = m_dictSearchEdit->text().trimmed();
    if (q.isEmpty()) return;

    static const QRegularExpression rxStrong(R"(^[GHgh]?\d+$)");
    if (rxStrong.match(q).hasMatch()) {
        QString code = q.toUpper();
        if (!code.startsWith('G') && !code.startsWith('H')) {
            const BookInfo* bi = m_engine->findBookBySwordName(m_book);
            bool isOT = bi ? (bi->testament == 1) : false;
            code = (isOT ? "H" : "G") + code;
        }
        m_activeStrongCode = code;
        m_activeMorphCode.clear();
        m_activeClickedWord.clear();
        m_dictMod = "AUTO_STRONG";
        syncHeaderControls();
        refreshContent(false);
    } else if (m_dictMod != "AUTO_STRONG") {
        m_browser->setHtml(m_engine->lookupDictionaryKeyHtml(m_dictMod, q, m_style));
    } else {
        m_browser->setHtml(m_engine->lookupDictionaryKeyHtml("Fr_Emmaus", q.toUpper(), m_style));
    }
}

void UniversalPaneWidget::onBrowserAnchorClicked(const QUrl& url) {
    if (url.scheme() == "app" && url.host() == "word") {
        QUrlQuery q(url);
        QString sc = q.queryItemValue("strong");
        QString mc = q.queryItemValue("morph");
        QString w = q.queryItemValue("w");
        if (!sc.isEmpty()) {
            emit wordClickedInBible(m_paneIndex, swordKey(), sc, mc, w, -1);
        }
    } else if (url.scheme() == "app" && url.host() == "ref") {
        QString ref = QUrl::fromPercentEncoding(url.path().mid(1).toUtf8()).replace('+', ' ');
        QString b; int c = 1, v = 1;
        QString key = m_engine->parseUserReference(ref, &c, &v, &b);
        emit navigateToReferenceRequested(m_paneIndex, key);
    } else if (url.scheme() == "app" && url.host() == "note") {
        int v = url.path().mid(1).toInt();
        if (v > 0) {
            m_verse = v;
            syncHeaderControls();
            QString key = QString("%1 %2:%3").arg(m_book).arg(m_chapter).arg(v);
            emit openPersonalNoteForVerseRequested(m_paneIndex, key);
        }
    } else if (url.scheme() == "app" && url.host() == "copy-turn") {
        int turnIdx = url.path().mid(1).toInt();
        copyTurnToWordProcessorClipboard(turnIdx);
    } else if (url.scheme() == "app" && url.host() == "translate-turn") {
        int turnIdx = url.path().mid(1).toInt();
        QUrlQuery q(url);
        QString targetLang = q.queryItemValue("lang").trimmed().toLower();
        const auto& hist = m_gemini->history();
        if (turnIdx >= 0 && turnIdx < hist.size() && !m_gemini->isPending()) {
            QString originalText = hist[turnIdx].text;
            setAiResponseLanguage(targetLang, true);
            QString prompt;
            QString displayLabel;
            if (targetLang == "es") {
                displayLabel = "🇪🇸 Traduire / Adapter cette étude en Español (Reina-Valera / LBLA) pour ma prédication";
                prompt = QString(
                    "Traduce y adapta íntegramente al ESPAÑOL el siguiente estudio bíblico y exegético "
                    "(usando la terminología bíblica en español de Reina-Valera 1960 / LBLA, conservando "
                    "todos los títulos, listas, negritas y palabras griegas/hebreas Strong, listo para pegar "
                    "en mi procesador de textos para predicar en español):\n\n%1"
                ).arg(originalText);
            } else if (targetLang == "fr_es") {
                displayLabel = "🇫🇷+🇪🇸 Présenter cette étude en version Bilingue (Français + Español)";
                prompt = QString(
                    "Présente l'étude biblique suivante en version BILINGUE complète : "
                    "1) Section en FRANÇAIS, suivie de 2) Section équivalente en ESPAÑOL (Reina-Valera 1960 / LBLA), "
                    "prête à être collée dans mon traitement de texte :\n\n%1"
                ).arg(originalText);
            } else {
                displayLabel = "🇫🇷 Traduire / Adapter cette étude en Français (Louis Segond)";
                prompt = QString(
                    "Traduis et adapte intégralement en FRANÇAIS l'étude biblique et exégétique suivante "
                    "(avec la terminologie biblique française Louis Segond, en conservant tous les titres, "
                    "listes, gras et mots grecs/hébreux Strong, prête à être collée dans mon traitement de texte) :\n\n%1"
                ).arg(originalText);
            }
            QString ctx;
            QStringList activeBibles;
            emit requestStudyContext(ctx, activeBibles);
            m_gemini->sendPrompt(prompt, ctx, displayLabel);
            renderGeminiConversation();
        }
    }
}

void UniversalPaneWidget::onCopyLatestAiForWordProcessor() {
    // 1. Si l'utilisateur a sélectionné un passage à la souris dans la fenêtre IA, copier uniquement cette sélection !
    if (m_geminiBrowser && m_geminiBrowser->textCursor().hasSelection()) {
        QString selPlain = m_geminiBrowser->textCursor().selectedText();
        selPlain.replace(QChar(0x2029), '\n');
        QString targetKey = m_preparedSwordKey.isEmpty() ? swordKey() : m_preparedSwordKey;
        QString frRef = m_engine->toFrenchReference(targetKey);
        QString titleHdr = QString("%1 — Extrait (%2)")
            .arg(frRef, GeminiClient::languageDisplayName(m_aiResponseLang));
        QString cleanHtml = GeminiClient::markdownToWordProcessorHtml(selPlain, titleHdr);
        auto* mime = new QMimeData();
        mime->setHtml(cleanHtml);
        mime->setText(QString("%1\n\n%2").arg(titleHdr, selPlain.trimmed()));
        QApplication::clipboard()->setMimeData(mime);
        m_geminiStatusLabel->setText("📋 Sélection copiée (format Traitement de texte propre sur fond blanc) — faites Ctrl+V dans LibreOffice / Word !");
        emit statusNotification("📋 Sélection copiée pour votre traitement de texte (Ctrl+V).");
        return;
    }

    // 2. Sinon, en mode Chat Gemini, copier la dernière réponse de Gemini
    const auto& hist = m_gemini->history();
    if (m_aiChatMode && !hist.isEmpty()) {
        for (int i = hist.size() - 1; i >= 0; --i) {
            if (hist[i].role == "model") {
                copyTurnToWordProcessorClipboard(i);
                return;
            }
        }
    }

    // 3. Sinon (mode Sources locales ou aucune réponse IA), copier le texte visible de la fenêtre
    QString plain = m_geminiBrowser ? m_geminiBrowser->toPlainText().trimmed() : QString();
    if (plain.isEmpty()) {
        emit statusNotification("⚠️ Aucune réponse à copier pour l'instant.");
        return;
    }
    QString targetKey = m_preparedSwordKey.isEmpty() ? swordKey() : m_preparedSwordKey;
    QString frRef = m_engine->toFrenchReference(targetKey);
    QString cleanHtml = GeminiClient::markdownToWordProcessorHtml(plain, QString("Étude Biblique — %1").arg(frRef));
    auto* mime = new QMimeData();
    mime->setHtml(cleanHtml);
    mime->setText(plain);
    QApplication::clipboard()->setMimeData(mime);
    m_geminiStatusLabel->setText("📋 Contenu copié pour votre traitement de texte — faites Ctrl+V dans LibreOffice / Word !");
    emit statusNotification("📋 Contenu copié pour votre traitement de texte (Ctrl+V).");
}

void UniversalPaneWidget::copyTurnToWordProcessorClipboard(int turnIndex) {
    const auto& hist = m_gemini->history();
    if (turnIndex < 0 || turnIndex >= hist.size()) return;

    const ChatTurn& turn = hist[turnIndex];
    QString targetKey = m_preparedSwordKey.isEmpty() ? swordKey() : m_preparedSwordKey;
    QString frRef = m_engine->toFrenchReference(targetKey);
    QString turnLang = turn.langCode.isEmpty() ? m_aiResponseLang : turn.langCode;
    QString titleHeader = QString("Étude Biblique — %1 (%2)")
        .arg(frRef, GeminiClient::languageDisplayName(turnLang));

    QString wordHtml = GeminiClient::markdownToWordProcessorHtml(turn.text, titleHeader);

    // Créer également une version texte brut propre (sans symboles Markdown ### ou **) pour les éditeurs texte simple
    QString cleanPlain = turn.text;
    cleanPlain.replace(QRegularExpression(R"(\*\*([^*]+)\*\*)"), "\\1");
    cleanPlain.replace(QRegularExpression(R"(^#{1,4}\s+)", QRegularExpression::MultilineOption), "");

    auto* mime = new QMimeData();
    mime->setHtml(wordHtml);
    mime->setText(QString("%1\n%2\n\n%3")
                      .arg(titleHeader, QString(titleHeader.size(), QChar('=')), cleanPlain.trimmed()));
    QApplication::clipboard()->setMimeData(mime);

    QString msg = QString("📋 Réponse Gemini (%1) copiée avec mise en page Traitement de texte (titres, gras, listes sur fond blanc) — faites Ctrl+V dans LibreOffice / Word !")
                      .arg(GeminiClient::languageDisplayName(turnLang));
    m_geminiStatusLabel->setText(msg);
    emit statusNotification(msg);
}

void UniversalPaneWidget::preparePassageForAiQuestion(
    const QString& key,
    const QString& sourceLabel,
    const QString& excerptOrVerseText,
    const QStringList& activeBibles,
    bool autoSend
) {
    if (m_paneMode != "AI") {
        setPaneMode("AI", true);
    }

    QString b; int c = 1, v = 1;
    m_engine->splitSwordKey(key, b, c, v);
    m_book = b;
    m_chapter = c;
    m_verse = v;

    m_sourcesCleared = false;
    m_preparedSwordKey = key;
    m_preparedBibleMod = sourceLabel;
    m_preparedExcerptText = excerptOrVerseText;
    m_preparedActiveBibles = activeBibles;

    QString frRef = m_engine->toFrenchReference(key);
    QString langLabel;
    for (const auto& bm : m_engine->getBibleModules()) {
        if (bm.name == sourceLabel) {
            if (bm.language == "fr") langLabel = "Français";
            else if (bm.language == "en") langLabel = "Anglais";
            else if (bm.language == "es") langLabel = "Espagnol";
            else langLabel = bm.language.toUpper();
            break;
        }
    }
    QString modDesc = m_engine->getModuleDescription(sourceLabel);
    QString headerLine;
    if (!langLabel.isEmpty() && !modDesc.isEmpty()) {
        headerLine = QString("📖 %1 — %2 [%3 — %4]").arg(frRef, sourceLabel, langLabel, modDesc);
    } else if (!langLabel.isEmpty()) {
        headerLine = QString("📖 %1 — %2 (%3)").arg(frRef, sourceLabel, langLabel);
    } else {
        headerLine = QString("📖 %1 — Source : %2").arg(frRef, sourceLabel);
    }

    QString prefilledBox = QString("%1\n« %2 »\n\nMa question : ")
        .arg(headerLine, excerptOrVerseText.trimmed());

    m_geminiInputEdit->setPlainText(prefilledBox);
    QTextCursor cur = m_geminiInputEdit->textCursor();
    cur.movePosition(QTextCursor::End);
    m_geminiInputEdit->setTextCursor(cur);
    m_geminiInputEdit->setFocus();

    if (m_aiChatMode) {
        m_localFallbackCardHtml.clear();
        m_geminiStatusLabel->setText(
            QString("✓ Sélection (%1 — %2) prête ci-dessous [Langue de réponse : %3]. Écrivez/dictez (🎤) votre question puis cliquez sur « 🚀 Envoyer ».")
                .arg(frRef, sourceLabel, GeminiClient::languageDisplayName(m_aiResponseLang))
        );
    } else {
        m_localFallbackCardHtml = buildLocalExegesisFallbackHtml(QString());
        m_geminiStatusLabel->setText(
            QString("✓ Sources locales affichées pour %1 (%2). Cliquez sur « 💬 Chat » en haut pour discuter avec l'IA.")
                .arg(frRef, sourceLabel)
        );
    }
    renderGeminiConversation();

    if (autoSend) {
        onSendCustomAiQuestion();
    }
}

void UniversalPaneWidget::notifyGeminiUpdated() {
    if (m_paneMode == "AI") {
        renderGeminiConversation();
    }
}

void UniversalPaneWidget::onSendCustomAiQuestion() {
    if ((m_dictationProcess && m_dictationProcess->state() != QProcess::NotRunning) ||
        !m_lastPartialDictation.isEmpty()) {
        stopMicDictation();
    }

    m_sourcesCleared = false;
    QString q = m_geminiInputEdit->toPlainText().trimmed();
    if (!m_aiChatMode) {
        // Si l'utilisateur a glissé ou tapé une référence biblique (en français ou en anglais, ex: "Jean 3:16" ou "John 3:16"),
        // basculer directement l'étude des sources sur ce verset et afficher la référence en français !
        QString refKey, refBook;
        int refCh = 1, refV = 1;
        QString afterQuestion = q;
        int idxQ = afterQuestion.indexOf("Ma question :");
        if (idxQ >= 0) {
            afterQuestion = afterQuestion.mid(idxQ + QString("Ma question :").size()).trimmed();
        }

        if (m_engine->tryParseStrictReference(q, refKey, &refBook, &refCh, &refV) &&
            (idxQ < 0 || afterQuestion.isEmpty())) {
            m_book = refBook;
            m_chapter = refCh;
            m_verse = refV;
            m_preparedSwordKey = refKey;
            if (m_preparedBibleMod.isEmpty()) m_preparedBibleMod = m_bibleMod;
            m_preparedExcerptText = m_engine->getCleanVerseText(m_preparedBibleMod, refKey);
            if (m_preparedExcerptText.isEmpty()) {
                m_preparedExcerptText = m_engine->getCleanVerseText("FreSegond", refKey);
            }
            QString frRef = m_engine->toFrenchReference(refKey);
            if (!q.contains('\n')) {
                m_geminiInputEdit->setPlainText(frRef);
            }
            m_lastSourcesQuery.clear();
            m_localFallbackCardHtml = buildLocalExegesisFallbackHtml(QString());
            m_geminiStatusLabel->setText(QString("✓ Sources locales complètes affichées pour %1.").arg(frRef));
            renderGeminiConversation();
            return;
        }

        // Et si l'utilisateur a écrit une référence après "Ma question :"
        if (!afterQuestion.isEmpty() && m_engine->tryParseStrictReference(afterQuestion, refKey, &refBook, &refCh, &refV)) {
            m_book = refBook;
            m_chapter = refCh;
            m_verse = refV;
            m_preparedSwordKey = refKey;
            if (m_preparedBibleMod.isEmpty()) m_preparedBibleMod = m_bibleMod;
            m_preparedExcerptText = m_engine->getCleanVerseText(m_preparedBibleMod, refKey);
            QString frRef = m_engine->toFrenchReference(refKey);
            m_lastSourcesQuery.clear();
            m_localFallbackCardHtml = buildLocalExegesisFallbackHtml(QString());
            m_geminiStatusLabel->setText(QString("✓ Sources locales complètes affichées pour %1.").arg(frRef));
            renderGeminiConversation();
            return;
        }

        m_lastSourcesQuery = q;
        m_localFallbackCardHtml = buildLocalExegesisFallbackHtml(q);
        m_geminiStatusLabel->setText("✓ Recherche dans les sources locales mise à jour.");
        renderGeminiConversation();
        return;
    }

    if (q.isEmpty()) return;
    if (q.endsWith("Ma question :") || q.endsWith("Ma question :")) {
        if (m_aiResponseLang == "es") {
            q += " Explica este pasaje en detalle en ESPAÑOL (sentido teológico, contexto histórico, estudio de las palabras originales griegas o hebreas Strong y aplicación pastoral para predicar).";
        } else if (m_aiResponseLang == "fr_es") {
            q += " Explique ce passage en détail en version BILINGUE Français + Español (sens théologique, contexte historique, mots originaux grecs/hébreux Strong et points de prédication dans les deux langues).";
        } else if (m_aiResponseLang == "en") {
            q += " Explain this passage in detail in ENGLISH (theological meaning, historical context, and original Greek/Hebrew Strong's word study).";
        } else {
            q += " Explique ce passage en détail (sens théologique, contexte historique et étude des mots originaux grecs ou hébreux).";
        }
    }

    m_gemini->setResponseLanguage(m_aiResponseLang);

    QString ctx;
    QStringList activeBibles;
    emit requestStudyContext(ctx, activeBibles);
    m_preparedActiveBibles = activeBibles;

    m_geminiInputEdit->clear();
    m_gemini->sendPrompt(q, ctx, q);
    renderGeminiConversation();
}

void UniversalPaneWidget::onClearAiWindow() {
    stopMicDictation();
    m_gemini->clearHistory();
    m_localFallbackCardHtml.clear();
    m_lastSourcesQuery.clear();
    m_preparedSwordKey.clear();
    m_preparedBibleMod.clear();
    m_preparedExcerptText.clear();
    m_geminiInputEdit->clear();
    m_sourcesCleared = true;
    if (m_aiChatMode) {
        m_geminiStatusLabel->setText("✓ Chat Gemini nettoyé — posez une nouvelle question ou sélectionnez un texte (Clic Droit → 🤖 Rechercher dans l'IA).");
    } else {
        m_geminiStatusLabel->setText("✓ Fenêtre des sources locales nettoyée — glissez un verset ou lancez une nouvelle recherche.");
    }
    renderGeminiConversation();
    emit statusNotification("🧹 Fenêtre IA / Sources nettoyée pour une nouvelle recherche.");
}

void UniversalPaneWidget::onToggleMicDictation() {
    if (m_dictationProcess && m_dictationProcess->state() != QProcess::NotRunning) {
        stopMicDictation();
        m_geminiStatusLabel->setText("✓ Dictée vocale terminée. Vous pouvez compléter ou cliquer sur « 🚀 Envoyer ».");
        return;
    }

    m_lastPartialDictation.clear();
    const QString appDir = QCoreApplication::applicationDirPath();
    QString pythonBin = "python3";
    const QStringList pyCandidates = {
        appDir + "/.venv/bin/python",
        appDir + "/../.venv/bin/python",
        QDir::homePath() + "/.gemini/antigravity/scratch/Bible_Tool/.venv/bin/python"
    };
    for (const QString& cand : pyCandidates) {
        if (QFileInfo::exists(cand)) {
            pythonBin = cand;
            break;
        }
    }

    QString scriptPath = appDir + "/scripts/live_dictation.py";
    const QStringList scriptCandidates = {
        appDir + "/scripts/live_dictation.py",
        appDir + "/../scripts/live_dictation.py",
        appDir + "/../share/ai-bible/scripts/live_dictation.py",
        QDir::homePath() + "/.gemini/antigravity/scratch/Bible_Tool/scripts/live_dictation.py"
    };
    for (const QString& cand : scriptCandidates) {
        if (QFileInfo::exists(cand)) {
            scriptPath = cand;
            break;
        }
    }

    m_dictationProcess = new QProcess(this);
    connect(m_dictationProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        while (m_dictationProcess && m_dictationProcess->canReadLine()) {
            QString line = QString::fromUtf8(m_dictationProcess->readLine()).trimmed();
            if (line == "READY") {
                m_geminiStatusLabel->setText("🎙️ Parlez maintenant dans le micro... Vous pouvez cliquer directement sur « 🚀 Envoyer » dès que vous avez fini.");
            } else if (line.startsWith("PARTIAL:")) {
                QString p = line.mid(8).trimmed();
                if (!p.isEmpty()) {
                    m_lastPartialDictation = p;
                    m_geminiStatusLabel->setText(QString("🎙️ Écoute en direct : « %1 ... »").arg(p));
                }
            } else if (line.startsWith("FINAL:")) {
                QString finalTxt = line.mid(6).trimmed();
                m_lastPartialDictation.clear();
                if (!finalTxt.isEmpty()) {
                    QTextCursor cur = m_geminiInputEdit->textCursor();
                    cur.movePosition(QTextCursor::End);
                    QString existing = m_geminiInputEdit->toPlainText();
                    if (!existing.isEmpty() && !existing.endsWith(' ') && !existing.endsWith('\n')) {
                        cur.insertText(" ");
                    }
                    cur.insertText(finalTxt + " ");
                    m_geminiInputEdit->setTextCursor(cur);
                    m_geminiInputEdit->ensureCursorVisible();
                    m_geminiStatusLabel->setText(QString("🎙️ Dicté : « %1 » (continuez à parler ou cliquez sur 🚀 Envoyer)").arg(finalTxt));
                }
            }
        }
    });

    connect(m_dictationProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        if (m_micDictateBtn) {
            m_micDictateBtn->setText("🎤 Dicter");
            m_micDictateBtn->setStyleSheet(
                "QPushButton { background-color: rgba(16, 185, 129, 0.20); color: #34D399; "
                "border: 1px solid #059669; border-radius: 5px; font-weight: bold; padding: 5px 10px; }"
                "QPushButton:hover { background-color: rgba(16, 185, 129, 0.35); }"
            );
        }
    });

    m_micDictateBtn->setText("🔴 Arrêter");
    m_micDictateBtn->setStyleSheet(
        "QPushButton { background-color: #DC2626; color: #FFFFFF; "
        "border: 1px solid #F87171; border-radius: 5px; font-weight: bold; padding: 5px 10px; }"
        "QPushButton:hover { background-color: #B91C1C; }"
    );
    m_geminiStatusLabel->setText("⏳ Activation du microphone...");
    m_dictationProcess->start(pythonBin, {scriptPath});
}

void UniversalPaneWidget::stopMicDictation() {
    // 1. S'il reste des paroles en cours de dictée ("PARTIAL:") non encore validées par un silence Vosk,
    // les insérer immédiatement dans la zone de saisie pour ne perdre aucun mot quand on clique directement sur "Envoyer" !
    if (!m_lastPartialDictation.trimmed().isEmpty() && m_geminiInputEdit) {
        QTextCursor cur = m_geminiInputEdit->textCursor();
        cur.movePosition(QTextCursor::End);
        QString existing = m_geminiInputEdit->toPlainText();
        if (!existing.isEmpty() && !existing.endsWith(' ') && !existing.endsWith('\n')) {
            cur.insertText(" ");
        }
        cur.insertText(m_lastPartialDictation.trimmed() + " ");
        m_geminiInputEdit->setTextCursor(cur);
        m_geminiInputEdit->ensureCursorVisible();
        m_lastPartialDictation.clear();
    }

    // 2. Remettre immédiatement le bouton "🎤 Dicter" dans son état normal
    if (m_micDictateBtn) {
        m_micDictateBtn->setText("🎤 Dicter");
        m_micDictateBtn->setStyleSheet(
            "QPushButton { background-color: rgba(16, 185, 129, 0.20); color: #34D399; "
            "border: 1px solid #059669; border-radius: 5px; font-weight: bold; padding: 5px 10px; }"
            "QPushButton:hover { background-color: rgba(16, 185, 129, 0.35); }"
        );
    }

    // 3. Arrêter le processus de dictée de façon instantanée et non-bloquante (sans bloquer l'interface avec waitForFinished)
    if (!m_dictationProcess) return;
    QProcess* proc = m_dictationProcess;
    m_dictationProcess = nullptr;
    proc->disconnect(this);
    if (proc->state() != QProcess::NotRunning) {
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                proc, &QObject::deleteLater);
        proc->kill();
    } else {
        proc->deleteLater();
    }
}

QString UniversalPaneWidget::buildLocalExegesisFallbackHtml(const QString& userQuestion) const {
    QString key = m_preparedSwordKey.isEmpty() ? swordKey() : m_preparedSwordKey;

    // Si userQuestion est une référence biblique directe (FR ou EN), utiliser cette référence comme clé d'étude
    QString parsedRefKey;
    if (!userQuestion.trimmed().isEmpty() && !userQuestion.contains("Ma question :") &&
        m_engine->tryParseStrictReference(userQuestion, parsedRefKey)) {
        key = parsedRefKey;
    }

    QString frRef = m_engine->toFrenchReference(key);

    QString html = QString(R"(
        <div style="background:%1; border:2px solid #F59E0B; border-radius:8px; padding:12px; margin-bottom:12px;">
            <div style="font-size:1.1em; font-weight:700; color:#FBBF24; margin-bottom:6px;">
                📚 Recherche dans les Sources Locales — %2
            </div>
    )").arg(m_style.cardBgColor, frRef.toHtmlEscaped());

    if (!m_preparedExcerptText.trimmed().isEmpty()) {
        html += QString(R"(
            <div style="background:%1; border-left:3px solid %2; padding:8px 10px; margin-bottom:10px;">
                <b style="color:%2;">📌 Texte sélectionné (%3) :</b><br>« %4 »
            </div>
        )").arg(m_style.bgColor, m_style.accentColor,
                m_preparedBibleMod.toHtmlEscaped(),
                m_preparedExcerptText.trimmed().toHtmlEscaped().replace("\n", "<br>"));
    }

    QStringList mods = m_preparedActiveBibles;
    if (!m_preparedBibleMod.isEmpty() && !mods.contains(m_preparedBibleMod)) {
        mods.prepend(m_preparedBibleMod);
    }
    for (const char* stdMod : {"FreSegond", "FreLSG", "LBLA", "KJV"}) {
        if (!mods.contains(stdMod)) mods.push_back(stdMod);
    }

    html += "<div style='margin-bottom:10px;'><b>📖 Comparaison des versions installées (%1) :</b><ul style='margin:4px 0 6px 18px;'>";
    html = html.arg(frRef.toHtmlEscaped());
    int shownMods = 0;
    for (const QString& m : mods) {
        if (m.isEmpty()) continue;
        QString t = m_engine->getCleanVerseText(m, key);
        if (!t.isEmpty()) {
            html += QString("<li><b>%1</b> : « %2 »</li>").arg(m.toHtmlEscaped(), t.toHtmlEscaped());
            if (++shownMods >= 4) break;
        }
    }
    html += "</ul></div>";

    QString origMod;
    QString origTxt = m_engine->getOriginalVerseText(key, &origMod);
    if (!origTxt.isEmpty()) {
        html += QString("<div style='margin-bottom:10px;'><b>🏛️ Texte Original (%1) :</b><br>"
                        "<span style='font-size:1.2em;'>%2</span></div>")
                    .arg(origMod.toHtmlEscaped(), origTxt.toHtmlEscaped());
    }

    auto tokens = m_engine->getVerseWordTokens(key, "FreLSG");
    if (!tokens.isEmpty()) {
        html += "<div style='margin-bottom:10px;'><b>🔍 Mots originaux (Grec / Hébreu Strong) du verset :</b><ul style='margin:4px 0 6px 18px;'>";
        int count = 0;
        for (const auto& tk : tokens) {
            if (tk.strongCode.isEmpty()) continue;
            html += QString("<li><b>%1</b> → <a href='app://word?strong=%2&morph=%3&w=%4' style='color:%5; font-weight:bold;'>%2</a> "
                            "(<b>%6</b> <i>%7</i>) : %8</li>")
                        .arg(tk.surfaceText.trimmed().toHtmlEscaped(),
                             tk.strongCode.toHtmlEscaped(),
                             tk.morphCode.toHtmlEscaped(),
                             tk.surfaceText.trimmed().toHtmlEscaped(),
                             m_style.accentColor,
                             tk.originalUnicode.toHtmlEscaped(),
                             tk.transliteration.toHtmlEscaped(),
                             tk.shortFrenchGloss.toHtmlEscaped());
            if (++count >= 14) break;
        }
        html += "</ul></div>";
    }

    int commCount = 0;
    for (const auto& cm : m_engine->getCommentaryModules()) {
        QString ctxt = m_engine->getPlainCommentaryText(cm.name, key);
        if (!ctxt.isEmpty()) {
            html += QString("<div style='margin-top:8px; border-top:1px solid %1; padding-top:6px;'>"
                            "<b>📝 Commentaire %2 (%3) :</b><br>%4</div>")
                        .arg(m_style.borderColor, cm.name.toHtmlEscaped(),
                             cm.language.toUpper().toHtmlEscaped(),
                             ctxt.left(800).toHtmlEscaped());
            if (++commCount >= 2) break;
        }
    }

    // Afficher également les références croisées / passages parallèles TSK pour ce verset
    QString tskHtml = m_engine->renderTskHtml(key, "FreSegond", m_style);
    if (!tskHtml.isEmpty()) {
        html += QString("<div style='margin-top:10px; border-top:1px solid %1; padding-top:8px;'>%2</div>")
                    .arg(m_style.borderColor, tskHtml);
    }

    // Nettoyage de la requête utilisateur pour une éventuelle recherche de mots-clés
    QString cleanSearch = userQuestion.trimmed();
    int idxQuestion = cleanSearch.indexOf("Ma question :");
    if (idxQuestion >= 0) {
        cleanSearch = cleanSearch.mid(idxQuestion + QString("Ma question :").size()).trimmed();
    } else {
        // Retirer les lignes d'en-tête automatiques ("📖 ..." et "« ... »")
        QStringList keptLines;
        for (const QString& ln : cleanSearch.split('\n')) {
            QString tln = ln.trimmed();
            if (tln.startsWith("📖") || tln.startsWith("«")) continue;
            if (!tln.isEmpty()) keptLines.push_back(tln);
        }
        cleanSearch = keptLines.join(" ").trimmed();
    }

    // Ne lancer une recherche de mots dans la Bible que si cleanSearch n'est PAS simplement une référence biblique
    QString dummyRef;
    if (!cleanSearch.isEmpty() && cleanSearch.size() <= 120 && !m_engine->tryParseStrictReference(cleanSearch, dummyRef)) {
        static const QSet<QString> stopWords = {
            "quel", "quelle", "quels", "quelles", "que", "quoi", "qui", "comment", "pourquoi",
            "dans", "pour", "avec", "sans", "sous", "vers", "chez", "entre", "mais", "donc",
            "veut", "dire", "signifie", "signification", "sens", "verset", "passage", "chapitre",
            "texte", "bible", "cette", "cet", "ces", "celui", "celle", "ceux", "elles", "vous",
            "nous", "leur", "leurs", "etre", "avoir", "faire", "plus", "moins", "tres", "bien",
            "explique", "expliquer", "parle", "parler", "mot", "mots"
        };
        QString cleanedPunct = cleanSearch;
        cleanedPunct.replace(QRegularExpression("[^\\p{L}\\p{N}\\s]"), " ");
        QStringList rawWords = cleanedPunct.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        QString effectiveQuery = cleanSearch;
        if (rawWords.size() > 2) {
            QStringList keywords;
            for (const QString& w : rawWords) {
                if (w.size() >= 4 && !stopWords.contains(w.toLower())) {
                    keywords.push_back(w);
                }
            }
            if (!keywords.isEmpty()) {
                effectiveQuery = keywords.join(" ");
            }
        }

        // Recherche rapide en une seule passe sur FreSegond (texte pur sans balises XML lourdes)
        auto hits = m_engine->searchBible("FreSegond", effectiveQuery, 0, QString(), 15);

        html += QString("<div style='margin-top:10px; border-top:1px solid %1; padding-top:8px;'>"
                        "<b style='color:#FBBF24;'>🔎 Versets trouvés dans la Bible pour « %2 » (%3 résultats) :</b>")
                    .arg(m_style.borderColor, effectiveQuery.toHtmlEscaped())
                    .arg(hits.size());
        if (!hits.isEmpty()) {
            html += "<ul style='margin:4px 0 6px 18px;'>";
            for (const auto& h : hits) {
                QString encKey = QString::fromUtf8(QUrl::toPercentEncoding(h.swordKey));
                html += QString("<li><a href='app://ref/%1' style='color:%2; font-weight:bold;'>%3</a> : %4</li>")
                            .arg(encKey, m_style.accentColor, h.frenchRef.toHtmlEscaped(), h.plainText.left(180).toHtmlEscaped());
            }
            html += "</ul>";
        }
        html += "</div>";
    }

    html += "</div>";
    return html;
}

void UniversalPaneWidget::renderGeminiConversation() {
    QString targetKey = m_preparedSwordKey.isEmpty() ? swordKey() : m_preparedSwordKey;
    QString tb = m_book;
    int tc = m_chapter;
    int tv = m_verse;
    m_engine->splitSwordKey(targetKey, tb, tc, tv);
    const BookInfo* bInfo = m_engine->findBookBySwordName(tb);
    QString frBookName = bInfo ? bInfo->frenchName : tb;
    QString activeFrRef = m_engine->toFrenchReference(targetKey);
    m_geminiBrowser->setPassageContext(tb, tc, m_aiChatMode ? "IA Gemini" : "Sources Locales", frBookName, tv);

    QString fontFam = m_style.useSerifFont ? "'Noto Serif',serif" : "'Noto Sans',sans-serif";
    QString weight = m_style.highContrastBold ? "600" : "400";
    double lineH = double(m_style.lineSpacingPct) / 100.0;

    QString html = QString(R"(<!DOCTYPE html><html><head><meta charset="utf-8"><style>
        body {
            background-color: %1;
            color: %2;
            font-family: %3;
            font-size: %4pt;
            font-weight: %5;
            line-height: %6;
            margin: 8px;
        }
        a.action-pill {
            display: inline-block;
            text-decoration: none;
            font-weight: bold;
            font-size: 0.88em;
            padding: 3px 9px;
            border-radius: 5px;
            margin-right: 6px;
        }
    </style></head><body>)")
        .arg(m_style.bgColor, m_style.textColor, fontFam)
        .arg(m_style.fontSizePt)
        .arg(weight)
        .arg(lineH, 0, 'f', 2);

    if (!m_aiChatMode) {
        if (m_sourcesCleared) {
            html += QString(R"(
                <div style="background:%1; border:2px dashed #F59E0B; border-radius:8px; padding:14px; margin-bottom:10px;">
                    <div style="font-size:1.1em; font-weight:700; color:#FBBF24; margin-bottom:8px;">
                        🧹 Fenêtre des Sources Locales nettoyée
                    </div>
                    <div>
                        Prêt pour une nouvelle recherche dans vos sources :
                    </div>
                    <ul style="margin:6px 0 6px 20px;">
                        <li><b>Glissez-déposez un numéro de verset</b> depuis une Bible vers cette fenêtre pour afficher ses versions, son Grec/Hébreu Strong, ses commentaires et ses références TSK.</li>
                        <li>Ou <b>tapez une référence en français</b> (ex: <i>Jean 3:16</i>, <i>Romains 8:28</i>) ou <b>un mot / code Strong</b> (ex: <i>alliance</i>, <i>G3056</i>) ci-dessous puis cliquez sur <b>« 🔍 Chercher Sources »</b>.</li>
                        <li>Ou <b>sélectionnez un passage</b> + <b>Clic Droit → « 🤖 Rechercher dans l'IA »</b>.</li>
                    </ul>
                </div>
            )").arg(m_style.cardBgColor);
        } else {
            if (m_localFallbackCardHtml.isEmpty()) {
                m_localFallbackCardHtml = buildLocalExegesisFallbackHtml(m_lastSourcesQuery);
            }
            html += m_localFallbackCardHtml;
        }
    } else {
        const auto& hist = m_gemini->history();
        QString activeLangDisp = GeminiClient::languageDisplayName(m_aiResponseLang);
        if (hist.isEmpty() && !m_gemini->isPending()) {
            html += QString(R"(
                <div style="background:%1; border:2px solid %2; border-radius:8px; padding:14px; margin-bottom:10px;">
                    <div style="font-size:1.1em; font-weight:700; color:%3; margin-bottom:8px;">
                        💬 Fenêtre de Chat Gemini — Langue active : %5
                    </div>
                    <div style="margin-bottom:8px;">
                        Vous êtes dans le mode <b>💬 Chat Gemini (IA)</b>. Vous pouvez :
                    </div>
                    <ul style="margin:4px 0 8px 20px;">
                        <li><b>Choisir la langue de réponse avec le menu déroulant des drapeaux en haut</b> : <b>🇫🇷 Français</b>, <b>🇪🇸 Español</b> (pour étudier et prêcher en espagnol) ou <b>🇬🇧 English</b>.</li>
                        <li><b>Coller directement dans votre traitement de texte (LibreOffice / Word)</b> : cliquez sur <b>« 📋 Copier (Traitement de texte) »</b> pour copier la réponse avec une mise en page propre sur fond blanc (titres, gras, listes).</li>
                        <li><b>Sélectionner n'importe quelle partie intéressante d'une réponse de Gemini</b> → <b>Clic Droit → « 📝 Ajouter la sélection à la note de %4 »</b> ou <b>« 📋 Copier la sélection pour Traitement de texte »</b>.</li>
                    </ul>
                </div>
            )").arg(m_style.cardBgColor, m_style.accentColor, m_style.accentColor, activeFrRef.toHtmlEscaped(), activeLangDisp.toHtmlEscaped());

            if (!m_preparedExcerptText.trimmed().isEmpty()) {
                QString frRef = m_engine->toFrenchReference(m_preparedSwordKey);
                html += QString(R"(
                    <div style="background:%1; border-left:4px solid %2; border-radius:6px; padding:10px 12px; margin-bottom:10px;">
                        <b style="color:%2;">📌 Passage prêt dans la boîte de question (%3 — %4) [Réponse : %6] :</b><br>
                        « %5 »<br>
                        <span style="color:#94A3B8; font-size:0.9em;">👉 Choisissez la langue (🇫🇷 Français / 🇪🇸 Español) dans le menu en haut, écrivez ou dictez (🎤) votre question puis cliquez sur <b>« 🚀 Envoyer »</b>.</span>
                    </div>
                )").arg(m_style.cardBgColor, m_style.accentColor,
                        frRef.toHtmlEscaped(), m_preparedBibleMod.toHtmlEscaped(),
                        m_preparedExcerptText.trimmed().toHtmlEscaped().replace("\n", "<br>"),
                        activeLangDisp.toHtmlEscaped());
            }
        } else {
            bool isDark = QColor(m_style.bgColor).lightness() < 128;
            html += QString(R"(
                <div style="background:rgba(245, 158, 11, 0.12); border:1px solid #D97706; border-radius:6px; padding:6px 10px; margin-bottom:10px; font-size:0.9em; color:#FBBF24;">
                    💡 <b>Astuce Prédication & Notes (%1) :</b> Utilisez le bouton <b>« 📋 Copier pour Traitement de texte »</b> sous chaque réponse (ou <b>Clic Droit</b> sur une sélection) pour coller directement dans LibreOffice / Word sur fond blanc, ou <b>« 🇪🇸 Traduire en Español » / « 🇫🇷 Traduire en Français »</b> en 1 clic !
                </div>
            )").arg(activeFrRef.toHtmlEscaped());

            for (int i = 0; i < hist.size(); ++i) {
                const auto& turn = hist[i];
                bool isUser = (turn.role == "user_input");
                QString shownUser = turn.displayLabel.isEmpty() ? turn.text : turn.displayLabel;
                QString bodyHtml = isUser
                    ? QString("<div style='white-space:pre-wrap;'>%1</div>").arg(shownUser.toHtmlEscaped().replace("\n", "<br>"))
                    : GeminiClient::markdownToHtml(turn.text, isDark);

                QString borderCol = isUser ? "#0284C7" : m_style.accentColor;
                QString turnLang = turn.langCode.isEmpty() ? m_aiResponseLang : turn.langCode;
                QString langDisp = GeminiClient::languageDisplayName(turnLang);
                QString headerTitle = isUser
                    ? "👤 Vous"
                    : QString("✨ Gemini (%1 — %2)").arg(activeFrRef.toHtmlEscaped(), langDisp.toHtmlEscaped());

                QString actionFooter;
                if (!isUser) {
                    QString translateLinks;
                    if (turnLang != "es") {
                        translateLinks += QString(
                            "<a class='action-pill' href='app://translate-turn/%1?lang=es' "
                            "style='background:rgba(245,158,11,0.18); color:#FBBF24; border:1px solid #D97706;'>"
                            "🇪🇸 Traduire cette réponse en Español</a>"
                        ).arg(i);
                    }
                    if (turnLang != "fr") {
                        translateLinks += QString(
                            "<a class='action-pill' href='app://translate-turn/%1?lang=fr' "
                            "style='background:rgba(56,189,248,0.18); color:#38BDF8; border:1px solid #0284C7;'>"
                            "🇫🇷 Traduire cette réponse en Français</a>"
                        ).arg(i);
                    }

                    actionFooter = QString(R"(
                        <div style="margin-top:10px; padding-top:8px; border-top:1px dashed %1;">
                            <a class="action-pill" href="app://copy-turn/%2"
                               style="background:rgba(14,165,233,0.22); color:#7DD3FC; border:1px solid #0284C7;">
                                📋 Copier pour Traitement de texte (Word / LibreOffice)
                            </a>
                            %3
                        </div>
                    )").arg(m_style.borderColor).arg(i).arg(translateLinks);
                }

                html += QString(R"(
                    <div style="background:%1; border:1px solid %2; border-left:4px solid %3; border-radius:8px; padding:10px 14px; margin-bottom:12px;">
                        <div style="font-weight:700; color:%3; margin-bottom:6px;">%4</div>
                        <div>%5</div>
                        %6
                    </div>
                )").arg(m_style.cardBgColor, m_style.borderColor, borderCol,
                        headerTitle, bodyHtml, actionFooter);
            }

            if (m_gemini->isPending()) {
                html += QString(R"(
                    <div style="background:%1; border:2px dashed %2; border-radius:8px; padding:12px; margin-bottom:10px;">
                        <b style="color:%2;">⏳ Gemini réfléchit et rédige la réponse (%3)...</b>
                    </div>
                )").arg(m_style.cardBgColor, m_style.accentColor, activeLangDisp.toHtmlEscaped());
            }
        }
    }

    html += "</body></html>";
    m_geminiBrowser->setHtml(html);
    if (m_aiChatMode && m_gemini->isPending()) {
        m_geminiBrowser->verticalScrollBar()->setValue(m_geminiBrowser->verticalScrollBar()->maximum());
    }
}

QList<int> UniversalPaneWidget::aiSplitterSizes() const {
    return m_aiVerticalSplitter ? m_aiVerticalSplitter->sizes() : QList<int>{480, 150};
}

void UniversalPaneWidget::setAiSplitterSizes(const QList<int>& sizes) {
    if (m_aiVerticalSplitter && sizes.size() == 2 && (sizes[0] + sizes[1] > 50)) {
        m_aiVerticalSplitter->setSizes(sizes);
    }
}

void UniversalPaneWidget::setPaneStyle(const PaneStyle& style) {
    m_style = style;
    refreshContent(false);
}

void UniversalPaneWidget::onOpenGearSettings() {
    auto* extraBox = new QGroupBox("Paramètres IA Gemini");
    auto* extraForm = new QFormLayout(extraBox);

    auto* modelCombo = new QComboBox();
    modelCombo->addItem("⚡ Gemini 3.8 Flash (API ou Gemma 4 Auto)", "gemini-3.8-flash");
    modelCombo->addItem("🧠 Gemini 4 Argon", "gemini-4-argon");
    modelCombo->addItem("🚀 Gemini 3.5 Flash-Lite", "gemini-3.5-flash-lite");
    modelCombo->addItem("🌟 Google Gemma 4 31B Cloud (Sans clé API)", "gemma4:31b-cloud");
    modelCombo->addItem("💻 Google Gemma 4 Local (Hors-ligne)", "gemma4:latest");
    int mIdx = modelCombo->findData(m_gemini->model());
    if (mIdx >= 0) modelCombo->setCurrentIndex(mIdx);
    connect(modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), extraBox, [this, modelCombo](int idx) {
        if (idx >= 0) {
            m_gemini->setModel(modelCombo->itemData(idx).toString());
            emit apiKeyUpdated(m_gemini->apiKey(), m_gemini->model());
        }
    });
    extraForm->addRow("Modèle IA :", modelCombo);

    auto* keyEdit = new QLineEdit(m_gemini->apiKey());
    keyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    keyEdit->setPlaceholderText("Optionnel : Clé GEMINI_API_KEY (AIzaSy...)");
    connect(keyEdit, &QLineEdit::editingFinished, extraBox, [this, keyEdit]() {
        m_gemini->setApiKey(keyEdit->text().trimmed());
        emit apiKeyUpdated(m_gemini->apiKey(), m_gemini->model());
    });
    extraForm->addRow("Clé API Gemini :", keyEdit);

    openModuleSettingsDialog(
        this,
        QString("Fenêtre %1").arg(m_paneIndex + 1),
        m_style,
        [this](const PaneStyle& s) {
            setPaneStyle(s);
            emit styleChanged();
        },
        [this](const PaneStyle& s) {
            emit applyStyleToAllRequested(s);
        },
        extraBox
    );
}

QJsonObject UniversalPaneWidget::toJson() const {
    QJsonObject o;
    o["paneMode"] = m_paneMode;
    o["bibleMod"] = m_bibleMod;
    o["commMod"] = m_commMod;
    o["dictMod"] = m_dictMod;
    o["aiChatMode"] = m_aiChatMode;
    o["aiResponseLang"] = m_aiResponseLang;
    o["linked"] = m_linked;
    o["book"] = m_book;
    o["chapter"] = m_chapter;
    o["verse"] = m_verse;
    o["style"] = m_style.toJson();

    QJsonArray aiSizes;
    for (int s : aiSplitterSizes()) aiSizes.append(s);
    o["aiSplitterSizes"] = aiSizes;
    return o;
}

void UniversalPaneWidget::fromJson(const QJsonObject& obj) {
    if (obj.contains("bibleMod")) setBibleModule(obj.value("bibleMod").toString("FreSegond"), false);
    if (obj.contains("commMod")) setCommentaryModule(obj.value("commMod").toString("ALL_FR"), false);
    if (obj.contains("dictMod")) setDictionaryModule(obj.value("dictMod").toString("AUTO_STRONG"), false);
    if (obj.contains("aiResponseLang")) setAiResponseLanguage(obj.value("aiResponseLang").toString("fr"), false);
    if (obj.contains("aiChatMode")) setAiChatMode(obj.value("aiChatMode").toBool(true));
    if (obj.contains("linked")) setLinked(obj.value("linked").toBool(true), false);

    QString bk = obj.value("book").toString(m_book);
    int ch = obj.value("chapter").toInt(m_chapter);
    int v = obj.value("verse").toInt(m_verse);
    m_book = bk;
    m_chapter = std::clamp(ch, 1, m_engine->getChapterCount(bk));
    m_verse = std::clamp(v, 1, m_engine->getVerseCount(bk, m_chapter));

    if (obj.contains("style") && obj.value("style").isObject()) {
        m_style = PaneStyle::fromJson(obj.value("style").toObject(), m_style.fontSizePt);
    }

    if (obj.contains("aiSplitterSizes") && obj.value("aiSplitterSizes").isArray()) {
        QList<int> sz;
        for (const auto& val : obj.value("aiSplitterSizes").toArray()) sz.push_back(val.toInt());
        setAiSplitterSizes(sz);
    }

    QString mode = obj.value("paneMode").toString(m_paneMode);
    setPaneMode(mode, false);
}

// ============================================================================
// CenterScreenWidget Implementation (Fenêtres 1 et 2 interchangeables au Centre)
// ============================================================================
CenterScreenWidget::CenterScreenWidget(SwordEngine* engine, GeminiClient* gemini, QWidget* parent)
    : QWidget(parent), m_engine(engine), m_gemini(gemini)
{
    setMinimumWidth(260);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    m_toggleSidebarBtn = new QPushButton("📂 Bibliothèque");
    m_toggleSidebarBtn->setCursor(Qt::PointingHandCursor);
    m_toggleSidebarBtn->setToolTip("Afficher / Masquer le panneau Bibliothèque & Navigation à gauche");
    connect(m_toggleSidebarBtn, &QPushButton::clicked, this, &CenterScreenWidget::toggleLeftSidebarRequested);

    m_linkToggleBtn = new QPushButton();
    m_linkToggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_linkToggleBtn, &QPushButton::clicked, this, &CenterScreenWidget::onToggleLinkColumns);

    m_layoutPresetBtn = new QPushButton("📐 Mise en page (1/2 Écrans & Colonnes)");
    m_layoutPresetBtn->setCursor(Qt::PointingHandCursor);
    m_layoutPresetBtn->setToolTip("Choisir le mode 1 ou 2 écrans, le nombre de colonnes (2, 3, 4 ou Mixte) et la disposition visuelle");
    m_layoutPresetBtn->setStyleSheet(
        "QPushButton { background-color: rgba(2, 132, 199, 0.22); color: #38BDF8; "
        "border: 1px solid #0284C7; border-radius: 5px; font-weight: bold; padding: 4px 12px; }"
        "QPushButton:hover { background-color: rgba(2, 132, 199, 0.36); }"
    );
    connect(m_layoutPresetBtn, &QPushButton::clicked, this, &CenterScreenWidget::openLayoutSelectorRequested);

    m_syncStatusLabel = new QLabel();
    m_syncStatusLabel->setStyleSheet("color: #94A3B8; font-size: 9.2pt;");
    m_syncStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* gearBtn = createGearButton("⚙ Réglages globaux d'apparence (police, contraste, couleurs)");
    connect(gearBtn, &QToolButton::clicked, this, &CenterScreenWidget::onOpenGearSettings);

    topBar->addWidget(m_toggleSidebarBtn);
    topBar->addWidget(m_linkToggleBtn);
    topBar->addWidget(m_layoutPresetBtn);
    topBar->addWidget(m_syncStatusLabel, 1);
    topBar->addWidget(gearBtn);
    rootLayout->addLayout(topBar);

    m_columnsSplitter = new QSplitter(Qt::Horizontal);
    m_columnsSplitter->setHandleWidth(6);
    m_columnsSplitter->setChildrenCollapsible(false);
    connect(m_columnsSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        emit columnSplitterMoved();
    });

    m_panes[0] = new UniversalPaneWidget(0, m_engine, m_gemini, this);
    m_panes[1] = new UniversalPaneWidget(1, m_engine, m_gemini, this);

    m_panes[0]->setBibleModule("FreSegond", false);
    m_panes[0]->setPaneMode("BIBLE", false);

    m_panes[1]->setBibleModule("LBLA", false);
    m_panes[1]->setPaneMode("BIBLE", false);

    m_columnsSplitter->addWidget(m_panes[0]);
    m_columnsSplitter->addWidget(m_panes[1]);
    m_columnsSplitter->setSizes({480, 480});

    rootLayout->addWidget(m_columnsSplitter, 1);
    setLeftSidebarVisibleState(true);
    updateLinkButtonUi();
}

void CenterScreenWidget::setLeftSidebarVisibleState(bool visible) {
    if (!m_toggleSidebarBtn) return;
    if (visible) {
        m_toggleSidebarBtn->setText("📂 Bibliothèque");
        m_toggleSidebarBtn->setStyleSheet(
            "QPushButton { background-color: #1E293B; color: #CBD5E1; border: 1px solid #334155; "
            "border-radius: 5px; font-weight: bold; padding: 4px 10px; }"
            "QPushButton:hover { background-color: #334155; }"
        );
    } else {
        m_toggleSidebarBtn->setText("📂 Afficher Bibliothèque");
        m_toggleSidebarBtn->setStyleSheet(
            "QPushButton { background-color: rgba(245, 158, 11, 0.20); color: #FBBF24; border: 1px solid #D97706; "
            "border-radius: 5px; font-weight: bold; padding: 4px 10px; }"
            "QPushButton:hover { background-color: rgba(245, 158, 11, 0.34); }"
        );
    }
}

void CenterScreenWidget::setSecondPaneVisible(bool visible) {
    if (m_panes[1]) {
        m_panes[1]->setVisible(visible);
        if (visible && m_columnsSplitter) {
            QList<int> sz = m_columnsSplitter->sizes();
            if (sz.size() == 2 && sz[1] < 40) {
                int half = std::max(300, (sz[0] + sz[1]) / 2);
                m_columnsSplitter->setSizes({half, half});
            }
        }
    }
}

bool CenterScreenWidget::isSecondPaneVisible() const {
    return m_panes[1] ? !m_panes[1]->isHidden() : false;
}

void CenterScreenWidget::updateLinkButtonUi() {
    if (!m_linkToggleBtn || !m_syncStatusLabel) return;
    if (m_columnsLinked) {
        m_linkToggleBtn->setText("🔗 Fenêtres Liées");
        m_linkToggleBtn->setStyleSheet(
            "QPushButton { background-color: rgba(16, 185, 129, 0.18); color: #34D399; "
            "border: 1px solid #059669; border-radius: 5px; font-weight: bold; padding: 4px 12px; }"
            "QPushButton:hover { background-color: rgba(16, 185, 129, 0.30); }"
        );
        m_syncStatusLabel->setText("Menu déroulant en haut de chaque fenêtre = choisir Bible, Commentaires, Dictionnaire, IA ou Notes");
    } else {
        m_linkToggleBtn->setText("🔓 Fenêtres Déliées");
        m_linkToggleBtn->setStyleSheet(
            "QPushButton { background-color: rgba(245, 158, 11, 0.18); color: #FBBF24; "
            "border: 1px solid #D97706; border-radius: 5px; font-weight: bold; padding: 4px 12px; }"
            "QPushButton:hover { background-color: rgba(245, 158, 11, 0.30); }"
        );
        m_syncStatusLabel->setText("Mode indépendant : chaque fenêtre peut afficher un passage ou un outil différent");
    }
}

void CenterScreenWidget::onToggleLinkColumns() {
    setColumnsLinked(!m_columnsLinked);
}

void CenterScreenWidget::setColumnsLinked(bool linked) {
    m_columnsLinked = linked;
    m_panes[0]->setLinked(linked, false);
    m_panes[1]->setLinked(linked, false);
    updateLinkButtonUi();
    emit linkStateChanged(m_columnsLinked);
}

QList<int> CenterScreenWidget::splitterSizes() const {
    return m_columnsSplitter ? m_columnsSplitter->sizes() : QList<int>{};
}

void CenterScreenWidget::setSplitterSizes(const QList<int>& sizes) {
    if (m_columnsSplitter && sizes.size() == 2 && (sizes[0] + sizes[1] > 50)) {
        m_columnsSplitter->setSizes(sizes);
    }
}

void CenterScreenWidget::setPaneStyle(const PaneStyle& style) {
    m_panes[0]->setPaneStyle(style);
    m_panes[1]->setPaneStyle(style);
}

void CenterScreenWidget::onOpenGearSettings() {
    PaneStyle st = m_panes[0]->paneStyle();
    openModuleSettingsDialog(
        this,
        "Fenêtres Centrales (1 & 2)",
        st,
        [this](const PaneStyle& s) {
            setPaneStyle(s);
            emit styleChanged();
        },
        [this](const PaneStyle& s) {
            emit applyStyleToAllRequested(s);
        }
    );
}

// ============================================================================
// RightScreenWidget Implementation (Fenêtres 3 et 4 interchangeables à Droite)
// ============================================================================
RightScreenWidget::RightScreenWidget(SwordEngine* engine, GeminiClient* gemini, QWidget* parent)
    : QWidget(parent), m_engine(engine), m_gemini(gemini)
{
    setMinimumWidth(260);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(0);

    m_rightVerticalSplitter = new QSplitter(Qt::Vertical);
    m_rightVerticalSplitter->setHandleWidth(8);
    m_rightVerticalSplitter->setChildrenCollapsible(false);
    connect(m_rightVerticalSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        emit rightSplitterMoved();
    });

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setHandleWidth(6);
    m_mainSplitter->setChildrenCollapsible(false);
    connect(m_mainSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        emit rightSplitterMoved();
    });

    m_panes[0] = new UniversalPaneWidget(2, m_engine, m_gemini, this);
    m_panes[1] = new UniversalPaneWidget(3, m_engine, m_gemini, this);

    m_panes[0]->setDictionaryModule("AUTO_STRONG", false);
    m_panes[0]->setPaneMode("DICTIONARY", false);

    m_panes[1]->setCommentaryModule("ALL_FR", false);
    m_panes[1]->setPaneMode("AI", false);

    m_mainSplitter->addWidget(m_panes[0]);
    m_mainSplitter->addWidget(m_panes[1]);
    m_mainSplitter->setSizes({480, 520});

    m_bottomSpacerWidget = new QWidget();
    m_bottomSpacerWidget->setMinimumHeight(10);
    auto* bottomSpacerLayout = new QVBoxLayout(m_bottomSpacerWidget);
    bottomSpacerLayout->setContentsMargins(6, 2, 6, 2);
    auto* bottomHintLabel = new QLabel("↕ Glissez la barre horizontale juste au-dessus vers le haut ou vers le bas pour ajuster la hauteur de l'écran de droite");
    bottomHintLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    bottomHintLabel->setStyleSheet("color: #64748B; font-size: 8.5pt;");
    bottomHintLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bottomSpacerLayout->addWidget(bottomHintLabel);
    bottomSpacerLayout->addStretch(1);

    m_rightVerticalSplitter->addWidget(m_mainSplitter);
    m_rightVerticalSplitter->addWidget(m_bottomSpacerWidget);
    m_rightVerticalSplitter->setStretchFactor(0, 4);
    m_rightVerticalSplitter->setStretchFactor(1, 1);
    m_rightVerticalSplitter->setSizes({760, 210});

    rootLayout->addWidget(m_rightVerticalSplitter, 1);
}

void RightScreenWidget::setOrientation(Qt::Orientation orientation) {
    if (m_mainSplitter && m_mainSplitter->orientation() != orientation) {
        m_mainSplitter->setOrientation(orientation);
    }
}

Qt::Orientation RightScreenWidget::orientation() const {
    return m_mainSplitter ? m_mainSplitter->orientation() : Qt::Horizontal;
}

void RightScreenWidget::setSecondPaneVisible(bool visible) {
    if (m_panes[1]) {
        m_panes[1]->setVisible(visible);
        if (visible && m_mainSplitter) {
            QList<int> sz = m_mainSplitter->sizes();
            if (sz.size() == 2 && sz[1] < 40) {
                int half = std::max(280, (sz[0] + sz[1]) / 2);
                m_mainSplitter->setSizes({half, half});
            }
        }
    }
}

bool RightScreenWidget::isSecondPaneVisible() const {
    return m_panes[1] ? !m_panes[1]->isHidden() : false;
}

void RightScreenWidget::setBottomHeightAdjusterVisible(bool visible) {
    if (m_bottomSpacerWidget) {
        m_bottomSpacerWidget->setVisible(visible);
    }
}

bool RightScreenWidget::isBottomHeightAdjusterVisible() const {
    return m_bottomSpacerWidget ? !m_bottomSpacerWidget->isHidden() : false;
}

QList<int> RightScreenWidget::splitterSizes() const {
    return m_mainSplitter ? m_mainSplitter->sizes() : QList<int>{};
}

void RightScreenWidget::setSplitterSizes(const QList<int>& sizes) {
    if (m_mainSplitter && sizes.size() == 2 && (sizes[0] + sizes[1] > 50)) {
        m_mainSplitter->setSizes(sizes);
    }
}

QList<int> RightScreenWidget::verticalSplitterSizes() const {
    return m_rightVerticalSplitter ? m_rightVerticalSplitter->sizes() : QList<int>{};
}

void RightScreenWidget::setVerticalSplitterSizes(const QList<int>& sizes) {
    if (m_rightVerticalSplitter && sizes.size() == 2 && (sizes[0] + sizes[1] > 50)) {
        m_rightVerticalSplitter->setSizes(sizes);
    }
}



