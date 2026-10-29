#include "LayoutSelectorDialog.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

// ============================================================================
// Carte Visuelle de Mise en Page (avec dessin schématique des colonnes/fenêtres)
// ============================================================================
class LayoutCardWidget : public QFrame {
    Q_OBJECT
public:
    LayoutCardWidget(
        const QString& presetId,
        const QString& title,
        const QString& badgeText,
        const QString& subtitle,
        int recommendedScreens,
        QWidget* parent = nullptr
    )
        : QFrame(parent)
        , m_presetId(presetId)
        , m_title(title)
        , m_badgeText(badgeText)
        , m_subtitle(subtitle)
        , m_recommendedScreens(recommendedScreens)
    {
        setCursor(Qt::PointingHandCursor);
        setMinimumSize(210, 215);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    QString presetId() const { return m_presetId; }
    int recommendedScreens() const { return m_recommendedScreens; }

    void setSelected(bool sel) {
        if (m_selected != sel) {
            m_selected = sel;
            update();
        }
    }
    bool isSelected() const { return m_selected; }

    void setShowLeftSidebar(bool show) {
        if (m_showLeft != show) {
            m_showLeft = show;
            update();
        }
    }

    void setHighlightedForScreenMode(bool highlighted) {
        if (m_highlightedScreen != highlighted) {
            m_highlightedScreen = highlighted;
            update();
        }
    }

signals:
    void clicked(const QString& presetId);

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked(m_presetId);
        }
        QFrame::mousePressEvent(event);
    }

    void enterEvent(QEnterEvent* event) override {
        m_hovered = true;
        update();
        QFrame::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        m_hovered = false;
        update();
        QFrame::leaveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        QRectF cardRect = rect().adjusted(2, 2, -2, -2);

        // Couleur de fond et bordure de la carte
        QColor bgCol = m_selected ? QColor("#0F2942") : (m_hovered ? QColor("#1E293B") : QColor("#131C2E"));
        QColor borderCol = m_selected ? QColor("#38BDF8") : (m_hovered ? QColor("#64748B") : QColor("#334155"));
        double borderWidth = m_selected ? 2.5 : 1.2;

        QPainterPath path;
        path.addRoundedRect(cardRect, 10, 10);
        p.fillPath(path, bgCol);
        p.setPen(QPen(borderCol, borderWidth));
        p.drawPath(path);

        // Badge en haut à droite (ex: "★ 1 Écran" ou "★ 2 Écrans")
        QRectF badgeRect(cardRect.right() - 96, cardRect.top() + 8, 86, 20);
        QPainterPath badgePath;
        badgePath.addRoundedRect(badgeRect, 5, 5);
        QColor badgeBg = m_highlightedScreen
            ? QColor(16, 185, 129, 55)
            : QColor(51, 65, 85, 140);
        QColor badgeFg = m_highlightedScreen
            ? QColor("#34D399")
            : QColor("#94A3B8");
        p.fillPath(badgePath, badgeBg);
        p.setPen(QPen(badgeFg, 1.0));
        QFont badgeFont = font();
        badgeFont.setPointSizeF(7.8);
        badgeFont.setBold(true);
        p.setFont(badgeFont);
        p.drawText(badgeRect, Qt::AlignCenter, m_badgeText);

        // Indicateur radio à gauche en haut
        QRectF radioRect(cardRect.left() + 10, cardRect.top() + 10, 16, 16);
        p.setPen(QPen(m_selected ? QColor("#38BDF8") : QColor("#64748B"), 1.8));
        p.setBrush(m_selected ? QColor("#0284C7") : Qt::NoBrush);
        p.drawEllipse(radioRect);
        if (m_selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(radioRect.adjusted(4.5, 4.5, -4.5, -4.5));
        }

        // Zone du schéma miniature de l'écran / des colonnes
        QRectF previewArea(cardRect.left() + 12, cardRect.top() + 34, cardRect.width() - 24, 96);
        drawSchematicPreview(p, previewArea);

        // Titre de la disposition
        QFont titleFont = font();
        titleFont.setPointSizeF(9.8);
        titleFont.setBold(true);
        p.setFont(titleFont);
        p.setPen(m_selected ? QColor("#F8FAFC") : QColor("#E2E8F0"));
        QRectF titleRect(cardRect.left() + 10, previewArea.bottom() + 8, cardRect.width() - 20, 22);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, m_title);

        // Sous-titre descriptif
        QFont subFont = font();
        subFont.setPointSizeF(8.3);
        subFont.setBold(false);
        p.setFont(subFont);
        p.setPen(QColor("#94A3B8"));
        QRectF subRect(cardRect.left() + 10, titleRect.bottom() + 2, cardRect.width() - 20, cardRect.bottom() - titleRect.bottom() - 6);
        p.drawText(subRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_subtitle);
    }

private:
    void drawBlock(QPainter& p, const QRectF& r, const QColor& fill, const QColor& border, const QString& label) {
        QPainterPath bp;
        bp.addRoundedRect(r, 4, 4);
        p.fillPath(bp, fill);
        p.setPen(QPen(border, 1.2));
        p.drawPath(bp);

        QFont f = font();
        f.setPointSizeF(7.8);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor("#F8FAFC"));
        p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, label);
    }

    void drawSchematicPreview(QPainter& p, const QRectF& area) {
        // Cadre d'écran(s)
        if (m_presetId == "4_COLS") {
            // Dessiner 2 écrans côte à côte pour illustrer le mode Double Écran / 4 Colonnes
            double gap = 6.0;
            double w1 = (area.width() - gap) * 0.54;
            double w2 = (area.width() - gap) * 0.46;
            QRectF mon1(area.left(), area.top(), w1, area.height());
            QRectF mon2(mon1.right() + gap, area.top(), w2, area.height());

            p.setPen(QPen(QColor("#475569"), 1.2, Qt::DashLine));
            p.setBrush(QColor("#090D16"));
            p.drawRoundedRect(mon1, 5, 5);
            p.drawRoundedRect(mon2, 5, 5);

            QRectF in1 = mon1.adjusted(4, 4, -4, -4);
            QRectF in2 = mon2.adjusted(4, 4, -4, -4);

            double x = in1.left();
            if (m_showLeft) {
                double lw = in1.width() * 0.24;
                drawBlock(p, QRectF(x, in1.top(), lw - 2, in1.height()),
                          QColor("#1E293B"), QColor("#475569"), "📂");
                x += lw + 1;
            }
            double rem1 = in1.right() - x;
            double colW1 = (rem1 - 3) / 2.0;
            drawBlock(p, QRectF(x, in1.top(), colW1, in1.height()),
                      QColor(2, 132, 199, 90), QColor("#38BDF8"), "①\nBible 1");
            drawBlock(p, QRectF(x + colW1 + 3, in1.top(), colW1, in1.height()),
                      QColor(2, 132, 199, 70), QColor("#38BDF8"), "②\nBible 2");

            double colW2 = (in2.width() - 3) / 2.0;
            drawBlock(p, QRectF(in2.left(), in2.top(), colW2, in2.height()),
                      QColor(217, 119, 6, 85), QColor("#F59E0B"), "③\nDico");
            drawBlock(p, QRectF(in2.left() + colW2 + 3, in2.top(), colW2, in2.height()),
                      QColor(16, 185, 129, 85), QColor("#10B981"), "④\nIA/Notes");
            return;
        }

        // Cadre moniteur unique (ou principal)
        p.setPen(QPen(QColor("#475569"), 1.2));
        p.setBrush(QColor("#090D16"));
        p.drawRoundedRect(area, 6, 6);

        QRectF inner = area.adjusted(5, 5, -5, -5);
        double x = inner.left();
        if (m_showLeft) {
            double lw = inner.width() * 0.17;
            drawBlock(p, QRectF(x, inner.top(), lw - 3, inner.height()),
                      QColor("#1E293B"), QColor("#475569"), "📂");
            x += lw;
        }
        double remW = inner.right() - x;

        if (m_presetId == "2_COLS") {
            double cw = (remW - 4) / 2.0;
            drawBlock(p, QRectF(x, inner.top(), cw, inner.height()),
                      QColor(2, 132, 199, 90), QColor("#38BDF8"), "①\nBible");
            drawBlock(p, QRectF(x + cw + 4, inner.top(), cw, inner.height()),
                      QColor(16, 185, 129, 85), QColor("#10B981"), "②\nIA / Notes");
        } else if (m_presetId == "3_MIXED") {
            double cw1 = (remW - 4) * 0.54;
            double cw2 = (remW - 4) * 0.46;
            double halfH = (inner.height() - 4) / 2.0;
            drawBlock(p, QRectF(x, inner.top(), cw1, inner.height()),
                      QColor(2, 132, 199, 90), QColor("#38BDF8"), "①\nGrande Bible");
            drawBlock(p, QRectF(x + cw1 + 4, inner.top(), cw2, halfH),
                      QColor(217, 119, 6, 85), QColor("#F59E0B"), "③ Haut : Dico");
            drawBlock(p, QRectF(x + cw1 + 4, inner.top() + halfH + 4, cw2, halfH),
                      QColor(16, 185, 129, 85), QColor("#10B981"), "④ Bas : IA/Notes");
        } else if (m_presetId == "4_MIXED") {
            double cwCenter = (remW - 6) * 0.60;
            double cwRight = (remW - 6) * 0.40;
            double bW = (cwCenter - 3) / 2.0;
            double halfH = (inner.height() - 4) / 2.0;
            drawBlock(p, QRectF(x, inner.top(), bW, inner.height()),
                      QColor(2, 132, 199, 90), QColor("#38BDF8"), "①\nBible 1");
            drawBlock(p, QRectF(x + bW + 3, inner.top(), bW, inner.height()),
                      QColor(2, 132, 199, 70), QColor("#38BDF8"), "②\nBible 2");
            drawBlock(p, QRectF(x + cwCenter + 6, inner.top(), cwRight, halfH),
                      QColor(217, 119, 6, 85), QColor("#F59E0B"), "③ Haut : Dico");
            drawBlock(p, QRectF(x + cwCenter + 6, inner.top() + halfH + 4, cwRight, halfH),
                      QColor(16, 185, 129, 85), QColor("#10B981"), "④ Bas : IA");
        } else if (m_presetId == "3_COLS") {
            double cw = (remW - 8) / 3.0;
            drawBlock(p, QRectF(x, inner.top(), cw, inner.height()),
                      QColor(2, 132, 199, 90), QColor("#38BDF8"), "①\nBible 1");
            drawBlock(p, QRectF(x + cw + 4, inner.top(), cw, inner.height()),
                      QColor(217, 119, 6, 85), QColor("#F59E0B"), "②\nBible/Comm");
            drawBlock(p, QRectF(x + (cw + 4) * 2, inner.top(), cw, inner.height()),
                      QColor(16, 185, 129, 85), QColor("#10B981"), "③\nIA / Notes");
        }
    }

    QString m_presetId;
    QString m_title;
    QString m_badgeText;
    QString m_subtitle;
    int m_recommendedScreens = 1;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_showLeft = true;
    bool m_highlightedScreen = false;
};

// ============================================================================
// LayoutSelectorDialog Implementation
// ============================================================================
LayoutSelectorDialog::LayoutSelectorDialog(
    const WorkspaceLayoutConfig& currentConfig,
    bool isFirstStartup,
    QWidget* parent
)
    : QDialog(parent)
    , m_config(currentConfig)
    , m_isFirstStartup(isFirstStartup)
{
    setWindowTitle(isFirstStartup
        ? "Bienvenue dans AI Bible — Choix de la Mise en Page & des Écrans"
        : "📐 Configuration de la Mise en Page & des Écrans");
    setMinimumSize(1060, 690);
    resize(1120, 720);

    setStyleSheet(
        "QDialog { background-color: #0B1120; color: #E2E8F0; } "
        "QLabel { color: #E2E8F0; } "
        "QCheckBox { color: #E2E8F0; font-size: 9.8pt; spacing: 8px; } "
        "QComboBox { background-color: #1E293B; color: #F8FAFC; border: 1px solid #334155; "
        "            border-radius: 5px; padding: 4px 8px; font-weight: bold; } "
    );

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 16, 18, 16);
    rootLayout->setSpacing(12);

    // 1. En-tête
    auto* headerFrame = new QFrame();
    headerFrame->setStyleSheet(
        "QFrame { background-color: #111827; border: 1px solid #1E293B; border-radius: 8px; }"
    );
    auto* headerLayout = new QVBoxLayout(headerFrame);
    headerLayout->setContentsMargins(14, 10, 14, 10);
    headerLayout->setSpacing(4);

    auto* titleLbl = new QLabel(isFirstStartup
        ? "✨ Bienvenue ! Choisissez votre configuration d'écran (1 ou 2 écrans) et votre disposition de travail"
        : "📐 Choisir la disposition des colonnes et le mode d'affichage (1 ou 2 écrans)");
    titleLbl->setStyleSheet("font-size: 13pt; font-weight: bold; color: #38BDF8; border: none;");

    auto* descLbl = new QLabel(
        "Chaque fenêtre (①, ②, ③, ④) reste 100 % interchangeable à tout moment via son menu déroulant "
        "(📖 Bible, 📚 Commentaires, 🏛️ Dictionnaire Grec/Hébreu, 🤖 IA Gemini & Sources, 📝 Notes Personnelles)."
    );
    descLbl->setWordWrap(true);
    descLbl->setStyleSheet("font-size: 9.5pt; color: #94A3B8; border: none;");

    headerLayout->addWidget(titleLbl);
    headerLayout->addWidget(descLbl);
    rootLayout->addWidget(headerFrame);

    // 2. Étape 1 : Choix 1 Écran vs 2 Écrans
    auto* screenFrame = new QFrame();
    screenFrame->setStyleSheet(
        "QFrame { background-color: #131C2E; border: 1px solid #1E293B; border-radius: 8px; }"
    );
    auto* screenLayout = new QVBoxLayout(screenFrame);
    screenLayout->setContentsMargins(14, 10, 14, 10);
    screenLayout->setSpacing(8);

    auto* screenTopRow = new QHBoxLayout();
    auto* step1Title = new QLabel("🖥️ 1. Nombre d'écrans utilisés :");
    step1Title->setStyleSheet("font-size: 10.5pt; font-weight: bold; color: #F8FAFC; border: none;");

    m_oneScreenBtn = new QPushButton("🖥️  Mode 1 Écran (Portable / Écran Unique)");
    m_oneScreenBtn->setCursor(Qt::PointingHandCursor);
    m_oneScreenBtn->setMinimumHeight(36);
    connect(m_oneScreenBtn, &QPushButton::clicked, this, [this]() { onScreenModeSelected(1); });

    m_twoScreensBtn = new QPushButton("🖥️🖥️  Mode 2 Écrans (Bureau Étendu sur 2 Moniteurs)");
    m_twoScreensBtn->setCursor(Qt::PointingHandCursor);
    m_twoScreensBtn->setMinimumHeight(36);
    connect(m_twoScreensBtn, &QPushButton::clicked, this, [this]() { onScreenModeSelected(2); });

    m_screenDetectedLabel = new QLabel();
    m_screenDetectedLabel->setStyleSheet("font-size: 9pt; color: #94A3B8; border: none;");

    screenTopRow->addWidget(step1Title);
    screenTopRow->addSpacing(8);
    screenTopRow->addWidget(m_oneScreenBtn);
    screenTopRow->addWidget(m_twoScreensBtn);
    screenTopRow->addSpacing(10);
    screenTopRow->addWidget(m_screenDetectedLabel, 1);
    screenLayout->addLayout(screenTopRow);

    auto* screenOptsRow = new QHBoxLayout();
    m_showLeftSidebarCheck = new QCheckBox("📂 Afficher le panneau Bibliothèque & Navigation à gauche");
    m_showLeftSidebarCheck->setChecked(m_config.showLeftSidebar);
    connect(m_showLeftSidebarCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_config.showLeftSidebar = checked;
        for (auto* c : m_cards) c->setShowLeftSidebar(checked);
    });

    m_rightHeightAdjustCheck = new QCheckBox(
        "↕ Barre d'ajustement de hauteur sur l'écran de droite (utile si le 2e écran a une hauteur différente, ex. 900p vs 1080p)"
    );
    m_rightHeightAdjustCheck->setChecked(m_config.rightHeightAdjuster);
    connect(m_rightHeightAdjustCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_config.rightHeightAdjuster = checked;
    });

    screenOptsRow->addWidget(m_showLeftSidebarCheck);
    screenOptsRow->addSpacing(20);
    screenOptsRow->addWidget(m_rightHeightAdjustCheck, 1);
    screenLayout->addLayout(screenOptsRow);

    rootLayout->addWidget(screenFrame);

    // 3. Étape 2 : Cartes Visuelles de Mise en Page
    auto* step2Label = new QLabel("📐 2. Choisissez votre disposition visuelle (cliquez sur une vignette) :");
    step2Label->setStyleSheet("font-size: 10.5pt; font-weight: bold; color: #F8FAFC;");
    rootLayout->addWidget(step2Label);

    auto* cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(10);

    auto addCard = [&](const QString& id, const QString& title, const QString& badge, const QString& sub, int recScreens) {
        auto* card = new LayoutCardWidget(id, title, badge, sub, recScreens, this);
        card->setShowLeftSidebar(m_config.showLeftSidebar);
        connect(card, &LayoutCardWidget::clicked, this, &LayoutSelectorDialog::onPresetSelected);
        m_cards.push_back(card);
        cardsRow->addWidget(card, 1);
    };

    addCard(
        "2_COLS",
        "2 Colonnes (Compact)",
        "1 Écran",
        "2 grandes colonnes côte à côte : idéal sur portable pour lire la Bible à gauche et l'IA / Notes à droite.",
        1
    );
    addCard(
        "3_MIXED",
        "3 Fenêtres Mixtes ★",
        "★ 1 Écran",
        "1 grande colonne Bible au centre + 2 fenêtres empilées Haut/Bas à droite (Dictionnaire en haut, IA/Notes en bas).",
        1
    );
    addCard(
        "4_MIXED",
        "4 Fenêtres Mixtes",
        "1 Écran Large",
        "2 Bibles côte à côte au centre + 2 fenêtres empilées Haut/Bas à droite (Dictionnaire + IA Gemini).",
        1
    );
    addCard(
        "3_COLS",
        "3 Colonnes Côte à Côte",
        "1 ou 2 Écrans",
        "3 colonnes verticales de pleine hauteur côte à côte (ex: Bible 1, Bible 2 ou Commentaires, et IA/Notes).",
        1
    );
    addCard(
        "4_COLS",
        "4 Colonnes Studio ★",
        "★ 2 Écrans",
        "Écran 1 : Bibliothèque + 2 colonnes centrales. Écran 2 : 2 grandes colonnes d'outils côte à côte.",
        2
    );

    rootLayout->addLayout(cardsRow, 1);

    // 4. Étape 3 : Contenu initial de chaque fenêtre active
    auto* rolesFrame = new QFrame();
    rolesFrame->setStyleSheet(
        "QFrame { background-color: #131C2E; border: 1px solid #1E293B; border-radius: 8px; }"
    );
    auto* rolesLayout = new QVBoxLayout(rolesFrame);
    rolesLayout->setContentsMargins(14, 10, 14, 10);
    rolesLayout->setSpacing(6);

    auto* rolesTitle = new QLabel("🎛️ 3. Outil affiché dans chaque fenêtre active (modifiable à tout moment en 1 clic) :");
    rolesTitle->setStyleSheet("font-size: 10pt; font-weight: bold; color: #38BDF8; border: none;");
    rolesLayout->addWidget(rolesTitle);

    auto* rolesRow = new QHBoxLayout();
    rolesRow->setSpacing(12);

    const QStringList defaultLabels = {
        "Fenêtre ① (Centre Gauche) :",
        "Fenêtre ② (Centre Droite) :",
        "Fenêtre ③ (Droite 1 / Haut) :",
        "Fenêtre ④ (Droite 2 / Bas) :"
    };

    for (int i = 0; i < 4; ++i) {
        m_paneRolesWidgets[i] = new QWidget();
        auto* box = new QVBoxLayout(m_paneRolesWidgets[i]);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(3);

        m_paneRoleLabels[i] = new QLabel(defaultLabels[i]);
        m_paneRoleLabels[i]->setStyleSheet("font-size: 9pt; font-weight: bold; color: #CBD5E1; border: none;");

        m_paneRoleCombos[i] = new QComboBox();
        m_paneRoleCombos[i]->addItem("📖 Bible", "BIBLE");
        m_paneRoleCombos[i]->addItem("📚 Commentaires & TSK", "COMMENTARY");
        m_paneRoleCombos[i]->addItem("🏛️ Dictionnaire Grec/Hébreu", "DICTIONARY");
        m_paneRoleCombos[i]->addItem("🤖 IA Gemini & Sources", "AI");
        m_paneRoleCombos[i]->addItem("📝 Notes Personnelles", "NOTES");

        int idx = m_paneRoleCombos[i]->findData(m_config.paneModes[i]);
        if (idx >= 0) m_paneRoleCombos[i]->setCurrentIndex(idx);

        connect(m_paneRoleCombos[i], QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, i](int) {
            m_config.paneModes[i] = m_paneRoleCombos[i]->currentData().toString();
        });

        box->addWidget(m_paneRoleLabels[i]);
        box->addWidget(m_paneRoleCombos[i]);
        rolesRow->addWidget(m_paneRolesWidgets[i], 1);
    }
    rolesLayout->addLayout(rolesRow);
    rootLayout->addWidget(rolesFrame);

    // 5. Barre inférieure : Télécharger des modules + Afficher au démarrage + Boutons Valider / Annuler
    auto* bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(12);

    auto* dlModulesBtn = new QPushButton("📥 Télécharger / Gérer des Modules (Bibles, Langues...)");
    dlModulesBtn->setCursor(Qt::PointingHandCursor);
    dlModulesBtn->setStyleSheet(
        "QPushButton { background-color: #1E293B; color: #38BDF8; border: 1px solid #0284C7; "
        "border-radius: 6px; font-weight: bold; padding: 7px 14px; } "
        "QPushButton:hover { background-color: #0F2942; }"
    );
    connect(dlModulesBtn, &QPushButton::clicked, this, &LayoutSelectorDialog::openModuleInstallerRequested);

    m_showOnStartupCheck = new QCheckBox("Proposer ce choix de mise en page à chaque démarrage");
    m_showOnStartupCheck->setChecked(m_config.showOnStartup);
    connect(m_showOnStartupCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_config.showOnStartup = checked;
    });

    bottomRow->addWidget(dlModulesBtn);
    bottomRow->addWidget(m_showOnStartupCheck);
    bottomRow->addStretch(1);

    if (!isFirstStartup) {
        auto* cancelBtn = new QPushButton("Annuler");
        cancelBtn->setCursor(Qt::PointingHandCursor);
        cancelBtn->setStyleSheet(
            "QPushButton { background-color: #1E293B; color: #CBD5E1; border: 1px solid #475569; "
            "border-radius: 6px; padding: 8px 18px; font-weight: bold; } "
            "QPushButton:hover { background-color: #334155; }"
        );
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        bottomRow->addWidget(cancelBtn);
    }

    auto* applyBtn = new QPushButton("✓ Appliquer cette mise en page");
    applyBtn->setCursor(Qt::PointingHandCursor);
    applyBtn->setDefault(true);
    applyBtn->setStyleSheet(
        "QPushButton { background-color: #0284C7; color: #FFFFFF; border: 1px solid #38BDF8; "
        "border-radius: 6px; padding: 8px 22px; font-size: 10.5pt; font-weight: bold; } "
        "QPushButton:hover { background-color: #0369A1; }"
    );
    connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomRow->addWidget(applyBtn);

    rootLayout->addLayout(bottomRow);

    updateUiState();
}

void LayoutSelectorDialog::onScreenModeSelected(int screenMode) {
    m_config.screenMode = screenMode;
    if (screenMode == 1) {
        m_rightHeightAdjustCheck->setChecked(false);
        m_config.rightHeightAdjuster = false;
        if (m_config.presetId == "4_COLS") {
            onPresetSelected("3_MIXED");
            return;
        }
    } else if (screenMode == 2) {
        const auto screens = QGuiApplication::screens();
        if (screens.size() >= 2 && screens[0]->geometry().height() != screens[1]->geometry().height()) {
            m_rightHeightAdjustCheck->setChecked(true);
            m_config.rightHeightAdjuster = true;
        }
        onPresetSelected("4_COLS");
        return;
    }
    updateUiState();
}

void LayoutSelectorDialog::onPresetSelected(const QString& presetId) {
    m_config.presetId = presetId;

    // Adapter intelligemment les rôles suggérés des fenêtres selon la disposition choisie
    auto setComboRole = [this](int paneIdx, const QString& mode) {
        m_config.paneModes[paneIdx] = mode;
        if (m_paneRoleCombos[paneIdx]) {
            int idx = m_paneRoleCombos[paneIdx]->findData(mode);
            if (idx >= 0) m_paneRoleCombos[paneIdx]->setCurrentIndex(idx);
        }
    };

    if (presetId == "2_COLS") {
        setComboRole(0, "BIBLE");
        if (m_config.paneModes[1] == "BIBLE") {
            setComboRole(1, "AI");
        }
    } else if (presetId == "3_MIXED") {
        setComboRole(0, "BIBLE");
        setComboRole(2, "DICTIONARY");
        setComboRole(3, "AI");
    } else if (presetId == "4_MIXED") {
        setComboRole(0, "BIBLE");
        setComboRole(1, "BIBLE");
        setComboRole(2, "DICTIONARY");
        setComboRole(3, "AI");
    } else if (presetId == "3_COLS") {
        setComboRole(0, "BIBLE");
        if (m_config.paneModes[1].isEmpty()) setComboRole(1, "BIBLE");
        setComboRole(2, "AI");
    } else if (presetId == "4_COLS") {
        setComboRole(0, "BIBLE");
        setComboRole(1, "BIBLE");
        setComboRole(2, "DICTIONARY");
        setComboRole(3, "AI");
    }

    updateUiState();
}

void LayoutSelectorDialog::updateUiState() {
    const auto screens = QGuiApplication::screens();
    if (screens.size() >= 2) {
        QSize s0 = screens[0]->size();
        QSize s1 = screens[1]->size();
        m_screenDetectedLabel->setText(
            QString("✓ %1 écrans détectés : Écran 1 (%2×%3) + Écran 2 (%4×%5)")
                .arg(screens.size())
                .arg(s0.width()).arg(s0.height())
                .arg(s1.width()).arg(s1.height())
        );
    } else if (!screens.isEmpty()) {
        QSize s0 = screens[0]->size();
        m_screenDetectedLabel->setText(
            QString("ℹ️ 1 écran détecté (%1×%2) — vous pouvez quand même tester tous les modes")
                .arg(s0.width()).arg(s0.height())
        );
    }

    QString activeBtnStyle =
        "QPushButton { background-color: #0284C7; color: #FFFFFF; border: 2px solid #38BDF8; "
        "border-radius: 6px; font-weight: bold; padding: 6px 14px; }";
    QString inactiveBtnStyle =
        "QPushButton { background-color: #1E293B; color: #94A3B8; border: 1px solid #334155; "
        "border-radius: 6px; font-weight: bold; padding: 6px 14px; } "
        "QPushButton:hover { background-color: #334155; color: #E2E8F0; }";

    m_oneScreenBtn->setStyleSheet(m_config.screenMode == 1 ? activeBtnStyle : inactiveBtnStyle);
    m_twoScreensBtn->setStyleSheet(m_config.screenMode == 2 ? activeBtnStyle : inactiveBtnStyle);
    m_rightHeightAdjustCheck->setEnabled(m_config.screenMode == 2);

    for (auto* card : m_cards) {
        card->setSelected(card->presetId() == m_config.presetId);
        card->setHighlightedForScreenMode(card->recommendedScreens() == m_config.screenMode);
    }

    // Mettre à jour la visibilité et les intitulés des sélecteurs de fenêtres actives
    bool p0 = true;
    bool p1 = (m_config.presetId != "3_MIXED");
    bool p2 = (m_config.presetId != "2_COLS");
    bool p3 = (m_config.presetId == "3_MIXED" || m_config.presetId == "4_MIXED" || m_config.presetId == "4_COLS");

    m_paneRolesWidgets[0]->setVisible(p0);
    m_paneRolesWidgets[1]->setVisible(p1);
    m_paneRolesWidgets[2]->setVisible(p2);
    m_paneRolesWidgets[3]->setVisible(p3);

    if (m_config.presetId == "2_COLS") {
        m_paneRoleLabels[0]->setText("Fenêtre ① (Colonne Gauche) :");
        m_paneRoleLabels[1]->setText("Fenêtre ② (Colonne Droite) :");
    } else if (m_config.presetId == "3_MIXED") {
        m_paneRoleLabels[0]->setText("Fenêtre ① (Grande Colonne Centrale) :");
        m_paneRoleLabels[2]->setText("Fenêtre ③ (Droite — En Haut) :");
        m_paneRoleLabels[3]->setText("Fenêtre ④ (Droite — En Bas) :");
    } else if (m_config.presetId == "4_MIXED") {
        m_paneRoleLabels[0]->setText("Fenêtre ① (Centre Colonne 1) :");
        m_paneRoleLabels[1]->setText("Fenêtre ② (Centre Colonne 2) :");
        m_paneRoleLabels[2]->setText("Fenêtre ③ (Droite — En Haut) :");
        m_paneRoleLabels[3]->setText("Fenêtre ④ (Droite — En Bas) :");
    } else if (m_config.presetId == "3_COLS") {
        m_paneRoleLabels[0]->setText("Fenêtre ① (Colonne 1) :");
        m_paneRoleLabels[1]->setText("Fenêtre ② (Colonne 2) :");
        m_paneRoleLabels[2]->setText("Fenêtre ③ (Colonne 3) :");
    } else {
        m_paneRoleLabels[0]->setText("Fenêtre ① (Écran 1 — Col. 1) :");
        m_paneRoleLabels[1]->setText("Fenêtre ② (Écran 1 — Col. 2) :");
        m_paneRoleLabels[2]->setText("Fenêtre ③ (Écran 2 — Col. 3) :");
        m_paneRoleLabels[3]->setText("Fenêtre ④ (Écran 2 — Col. 4) :");
    }
}

#include "LayoutSelectorDialog.moc"
