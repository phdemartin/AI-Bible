#include "SwordEngine.h"

#include <swmgr.h>
#include <swmodule.h>
#include <rawcom.h>
#include <versekey.h>
#include <listkey.h>
#include <markupfiltmgr.h>
#include <gbfhtmlhref.h>
#include <latin1utf8.h>

#include <QRegularExpression>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QSet>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QLocale>
#include <algorithm>

using namespace sword;

// ============================================================================
// PaneStyle Presets & Serialization
// ============================================================================
PaneStyle PaneStyle::preset(const QString& name, int keepFontSize) {
    PaneStyle s;
    s.fontSizePt = keepFontSize;
    s.presetName = name;

    if (name == "Noir Contraste Élevé") {
        s.bgColor = "#000000";
        s.textColor = "#FFFFFF";
        s.cardBgColor = "#0B0F17";
        s.accentColor = "#38BDF8";
        s.borderColor = "#475569";
        s.highContrastBold = true;
    } else if (name == "Noir & Ambre (Confort Nocturne)") {
        s.bgColor = "#080808";
        s.textColor = "#FDE68A";
        s.cardBgColor = "#14120E";
        s.accentColor = "#F59E0B";
        s.borderColor = "#453823";
        s.highContrastBold = false;
    } else if (name == "Clair Papier (Sépia)") {
        s.bgColor = "#FAF6EE";
        s.textColor = "#1E242B";
        s.cardBgColor = "#F1E9D8";
        s.accentColor = "#B45309";
        s.borderColor = "#D6CBB5";
        s.highContrastBold = false;
    } else if (name == "Blanc Contraste Élevé") {
        s.bgColor = "#FFFFFF";
        s.textColor = "#000000";
        s.cardBgColor = "#F1F5F9";
        s.accentColor = "#0369A1";
        s.borderColor = "#94A3B8";
        s.highContrastBold = true;
    } else if (name == "Vert Repos Visuel") {
        s.bgColor = "#0E1D17";
        s.textColor = "#E6F4EA";
        s.cardBgColor = "#162C23";
        s.accentColor = "#34D399";
        s.borderColor = "#284E3F";
        s.highContrastBold = false;
    } else {
        // Default: "Sombre Doux"
        s.presetName = "Sombre Doux";
        s.bgColor = "#141821";
        s.textColor = "#E8ECF2";
        s.cardBgColor = "#1E293B";
        s.accentColor = "#38BDF8";
        s.borderColor = "#334155";
        s.highContrastBold = false;
    }
    return s;
}

QJsonObject PaneStyle::toJson() const {
    QJsonObject o;
    o["fontSizePt"] = fontSizePt;
    o["lineSpacingPct"] = lineSpacingPct;
    o["highContrastBold"] = highContrastBold;
    o["useSerifFont"] = useSerifFont;
    o["presetName"] = presetName;
    o["bgColor"] = bgColor;
    o["textColor"] = textColor;
    o["cardBgColor"] = cardBgColor;
    o["accentColor"] = accentColor;
    o["borderColor"] = borderColor;
    return o;
}

PaneStyle PaneStyle::fromJson(const QJsonObject& o, int defaultFontSize) {
    PaneStyle s = PaneStyle::preset(o.value("presetName").toString("Sombre Doux"), defaultFontSize);
    s.fontSizePt = std::clamp(o.value("fontSizePt").toInt(defaultFontSize), 9, 36);
    s.lineSpacingPct = std::clamp(o.value("lineSpacingPct").toInt(165), 110, 260);
    s.highContrastBold = o.value("highContrastBold").toBool(s.highContrastBold);
    s.useSerifFont = o.value("useSerifFont").toBool(true);
    if (o.contains("bgColor")) s.bgColor = o.value("bgColor").toString(s.bgColor);
    if (o.contains("textColor")) s.textColor = o.value("textColor").toString(s.textColor);
    if (o.contains("cardBgColor")) s.cardBgColor = o.value("cardBgColor").toString(s.cardBgColor);
    if (o.contains("accentColor")) s.accentColor = o.value("accentColor").toString(s.accentColor);
    if (o.contains("borderColor")) s.borderColor = o.value("borderColor").toString(s.borderColor);
    return s;
}

namespace {

QString normalizeForMatch(const QString& s) {
    QString out = s.trimmed().toLower();
    out.replace(QRegularExpression("[éèêë]"), "e");
    out.replace(QRegularExpression("[àâäá]"), "a");
    out.replace(QRegularExpression("[îïí]"), "i");
    out.replace(QRegularExpression("[ôöó]"), "o");
    out.replace(QRegularExpression("[ùûüú]"), "u");
    out.replace("ç", "c");
    out.replace("ñ", "n");
    out.replace("œ", "oe");
    out.replace(QRegularExpression("[^a-z0-9]"), "");
    return out;
}

QString buildBodyCss(const PaneStyle& style) {
    QString fontFam = style.useSerifFont
        ? "\"Noto Serif\", \"DejaVu Serif\", \"Georgia\", serif"
        : "\"Noto Sans\", \"Inter\", \"DejaVu Sans\", sans-serif";
    QString weight = style.highContrastBold ? "600" : "400";
    double lineH = double(style.lineSpacingPct) / 100.0;
    return QString(
        "background-color: %1; color: %2; font-family: %3; font-size: %4pt; font-weight: %5; line-height: %6;"
    ).arg(style.bgColor, style.textColor, fontFam)
     .arg(style.fontSizePt)
     .arg(weight)
     .arg(lineH, 0, 'f', 2);
}

} // namespace

SwordEngine::SwordEngine(const QString& swordPath)
    : m_swordPath(swordPath.isEmpty() ? (QDir::homePath() + "/.sword") : swordPath)
{
    QDir().mkpath(m_swordPath + "/mods.d");
    QDir().mkpath(m_swordPath + "/modules");
    initBooks();
    reloadModules();
    loadPersonalNotes();
}

SwordEngine::~SwordEngine() = default;

void SwordEngine::reloadModules() {
    m_mgr = std::make_unique<SWMgr>(
        m_swordPath.toUtf8().constData(),
        true,
        new MarkupFilterMgr(FMT_HTMLHREF, ENC_UTF8)
    );
    m_mgr->setGlobalOption("Strong's Numbers", "On");
    m_mgr->setGlobalOption("Morphological Tags", "On");
    m_mgr->setGlobalOption("Footnotes", "On");
    m_mgr->setGlobalOption("Headings", "On");

    m_plainMgr = std::make_unique<SWMgr>(
        m_swordPath.toUtf8().constData(),
        true,
        new MarkupFilterMgr(FMT_PLAIN, ENC_UTF8)
    );
    m_plainMgr->setGlobalOption("Strong's Numbers", "Off");
    m_plainMgr->setGlobalOption("Morphological Tags", "Off");
    m_plainMgr->setGlobalOption("Footnotes", "Off");

    // Load direct French TSK if ~/.sword/modules/comments/rawcom/Fr_tsk/ exists
    QString frTskPath = m_swordPath + "/modules/comments/rawcom/Fr_tsk/";
    if (QFileInfo::exists(frTskPath + "nt") || QFileInfo::exists(frTskPath + "ot")) {
        m_frTskDirect = std::make_unique<RawCom>(
            frTskPath.toUtf8().constData(),
            "Fr_Tsk",
            "Trésor de la Connaissance Biblique (TSK en Français)"
        );
        m_frTskDirect->addRenderFilter(new GBFHTMLHREF());
        m_frTskDirect->addEncodingFilter(new Latin1UTF8());
    }

    initModules();
}

void SwordEngine::initBooks() {
    struct RawBookDef {
        int testament;
        const char* swordName;
        const char* osisName;
        const char* frenchName;
        const char* frenchAbbrev;
        const char* category;
        int chapters;
    };

    static const RawBookDef defs[] = {
        // Ancien Testament (39)
        {1, "Genesis", "Gen", "Genèse", "Gn", "Pentateuque", 50},
        {1, "Exodus", "Exod", "Exode", "Ex", "Pentateuque", 40},
        {1, "Leviticus", "Lev", "Lévitique", "Lv", "Pentateuque", 27},
        {1, "Numbers", "Num", "Nombres", "Nb", "Pentateuque", 36},
        {1, "Deuteronomy", "Deut", "Deutéronome", "Dt", "Pentateuque", 34},
        {1, "Joshua", "Josh", "Josué", "Jos", "Historiques", 24},
        {1, "Judges", "Judg", "Juges", "Jg", "Historiques", 21},
        {1, "Ruth", "Ruth", "Ruth", "Rt", "Historiques", 4},
        {1, "I Samuel", "1Sam", "1 Samuel", "1S", "Historiques", 31},
        {1, "II Samuel", "2Sam", "2 Samuel", "2S", "Historiques", 24},
        {1, "I Kings", "1Kgs", "1 Rois", "1R", "Historiques", 22},
        {1, "II Kings", "2Kgs", "2 Rois", "2R", "Historiques", 25},
        {1, "I Chronicles", "1Chr", "1 Chroniques", "1Ch", "Historiques", 29},
        {1, "II Chronicles", "2Chr", "2 Chroniques", "2Ch", "Historiques", 36},
        {1, "Ezra", "Ezra", "Esdras", "Esd", "Historiques", 10},
        {1, "Nehemiah", "Neh", "Néhémie", "Né", "Historiques", 13},
        {1, "Esther", "Esth", "Esther", "Est", "Historiques", 10},
        {1, "Job", "Job", "Job", "Jb", "Poétiques", 42},
        {1, "Psalms", "Ps", "Psaumes", "Ps", "Poétiques", 150},
        {1, "Proverbs", "Prov", "Proverbes", "Pr", "Poétiques", 31},
        {1, "Ecclesiastes", "Eccl", "Ecclésiaste", "Ec", "Poétiques", 12},
        {1, "Song of Solomon", "Song", "Cantique des Cantiques", "Ct", "Poétiques", 8},
        {1, "Isaiah", "Isa", "Ésaïe", "És", "Prophètes", 66},
        {1, "Jeremiah", "Jer", "Jérémie", "Jr", "Prophètes", 52},
        {1, "Lamentations", "Lam", "Lamentations", "Lm", "Prophètes", 5},
        {1, "Ezekiel", "Ezek", "Ézéchiel", "Éz", "Prophètes", 48},
        {1, "Daniel", "Dan", "Daniel", "Dn", "Prophètes", 12},
        {1, "Hosea", "Hos", "Osée", "Os", "Prophètes", 14},
        {1, "Joel", "Joel", "Joël", "Jl", "Prophètes", 3},
        {1, "Amos", "Amos", "Amos", "Am", "Prophètes", 9},
        {1, "Obadiah", "Obad", "Abdias", "Ab", "Prophètes", 1},
        {1, "Jonah", "Jonah", "Jonas", "Jon", "Prophètes", 4},
        {1, "Micah", "Mic", "Michée", "Mi", "Prophètes", 7},
        {1, "Nahum", "Nah", "Nahum", "Na", "Prophètes", 3},
        {1, "Habakkuk", "Hab", "Habacuc", "Ha", "Prophètes", 3},
        {1, "Zephaniah", "Zeph", "Sophonie", "So", "Prophètes", 3},
        {1, "Haggai", "Hag", "Aggée", "Ag", "Prophètes", 2},
        {1, "Zechariah", "Zech", "Zacharie", "Za", "Prophètes", 14},
        {1, "Malachi", "Mal", "Malachie", "Ml", "Prophètes", 4},
        // Nouveau Testament (27)
        {2, "Matthew", "Matt", "Matthieu", "Mt", "Évangiles & Actes", 28},
        {2, "Mark", "Mark", "Marc", "Mc", "Évangiles & Actes", 16},
        {2, "Luke", "Luke", "Luc", "Lc", "Évangiles & Actes", 24},
        {2, "John", "John", "Jean", "Jn", "Évangiles & Actes", 21},
        {2, "Acts", "Acts", "Actes", "Ac", "Évangiles & Actes", 28},
        {2, "Romans", "Rom", "Romains", "Rm", "Épîtres", 16},
        {2, "I Corinthians", "1Cor", "1 Corinthiens", "1Co", "Épîtres", 16},
        {2, "II Corinthians", "2Cor", "2 Corinthiens", "2Co", "Épîtres", 13},
        {2, "Galatians", "Gal", "Galates", "Ga", "Épîtres", 6},
        {2, "Ephesians", "Eph", "Éphésiens", "Ép", "Épîtres", 6},
        {2, "Philippians", "Phil", "Philippiens", "Ph", "Épîtres", 4},
        {2, "Colossians", "Col", "Colossiens", "Col", "Épîtres", 4},
        {2, "I Thessalonians", "1Thes", "1 Thessaloniciens", "1Th", "Épîtres", 5},
        {2, "II Thessalonians", "2Thes", "2 Thessaloniciens", "2Th", "Épîtres", 3},
        {2, "I Timothy", "1Tim", "1 Timothée", "1Tm", "Épîtres", 6},
        {2, "II Timothy", "2Tim", "2 Timothée", "2Tm", "Épîtres", 4},
        {2, "Titus", "Titus", "Tite", "Tt", "Épîtres", 3},
        {2, "Philemon", "Phlm", "Philémon", "Phm", "Épîtres", 1},
        {2, "Hebrews", "Heb", "Hébreux", "Hé", "Épîtres", 13},
        {2, "James", "Jas", "Jacques", "Jc", "Épîtres", 5},
        {2, "I Peter", "1Pet", "1 Pierre", "1P", "Épîtres", 5},
        {2, "II Peter", "2Pet", "2 Pierre", "2P", "Épîtres", 3},
        {2, "I John", "1John", "1 Jean", "1Jn", "Épîtres", 5},
        {2, "II John", "2John", "2 Jean", "2Jn", "Épîtres", 1},
        {2, "III John", "3John", "3 Jean", "3Jn", "Épîtres", 1},
        {2, "Jude", "Jude", "Jude", "Jd", "Épîtres", 1},
        {2, "Revelation of John", "Rev", "Apocalypse", "Ap", "Apocalypse", 22}
    };

    m_books.clear();
    int idx = 1;
    for (const auto& d : defs) {
        BookInfo b;
        b.index = idx++;
        b.testament = d.testament;
        b.swordName = QString::fromUtf8(d.swordName);
        b.osisName = QString::fromUtf8(d.osisName);
        b.frenchName = QString::fromUtf8(d.frenchName);
        b.frenchAbbrev = QString::fromUtf8(d.frenchAbbrev);
        b.category = QString::fromUtf8(d.category);
        b.chapterCount = d.chapters;
        m_books.push_back(b);
    }
}

void SwordEngine::initModules() {
    m_bibles.clear();
    m_commentaries.clear();
    m_lexicons.clear();

    if (!m_mgr) return;

    static const QMap<QString, QString> friendlyDescriptions = {
        {"FreLSG", "Louis Segond 1910 (avec Strong)"},
        {"FreJND", "Bible Darby Française (avec Strong)"},
        {"Fr_Semeur", "Bible du Semeur"},
        {"Fr_NEG", "Nouvelle Édition de Genève 1979 (NEG)"},
        {"Fr_JER", "Bible de Jérusalem"},
        {"Fr_TOB", "Traduction Œcuménique de la Bible (TOB)"},
        {"FR_BFC", "Bible en Français Courant (BFC)"},
        {"Fr_COL", "Bible à la Colombe"},
        {"Fr_Darby", "Bible J.N. Darby (Français)"},
        {"Fr_OST", "Bible Ostervald"},
        {"Fr_MAR", "Bible David Martin 1744"},
        {"Fr_BDP", "Bible des Peuples"},
        {"Fr_CRAMPON", "Bible Augustin Crampon"},
        {"Fr_fillon", "Bible Louis-Claude Fillion"},
        {"Fr_LXX", "La Septante (Traduction Française)"},
        {"KJV", "King James Version (avec Strong)"},
        {"UKJV", "Updated King James Version"},
        {"MKJV", "Modern King James Version"},
        {"Eng AMP", "Amplified Bible (English)"},
        {"SpaRV60", "Reina-Valera 1960 (Español)"},
        {"SpaRV", "Reina-Valera 1909 (Español)"},
        {"LBLA", "La Biblia de las Américas (Español)"},
        {"Es_LBLA", "La Biblia de las Américas (LBLA)"},
        {"NBLA", "Nueva Biblia de las Américas (Español)"},
        {"Es_DHH", "Dios Habla Hoy (Español)"},
        {"Es_NRVA", "Nueva Reina-Valera 1990 (Español)"},
        {"Fr_Annotee", "Bible Annotée de Neuchâtel"},
        {"Fr_Scofield", "Notes de la Bible Scofield (Français)"},
        {"Fr_Mhcc", "Commentaire de Matthew Henry (Français)"},
        {"Fr_Pnt", "Commentaire du Nouveau Testament (Français)"},
        {"Fr_Tsk", "Trésor de la Connaissance Biblique (TSK Français)"},
        {"TSK", "Treasury of Scripture Knowledge (Références Parallèles)"},
        {"Fr_Greek", "Dictionnaire Strong Grec (Français)"},
        {"Fr_Hebreu", "Dictionnaire Strong Hébreu (Français)"},
        {"Fr_Emmaus", "Dictionnaire Biblique Emmaüs (Français)"},
        {"FreGBM", "Dictionnaire Biblique Westphal / GBM (Français)"},
        {"StrongsGreek", "Strong's Greek Dictionary"},
        {"StrongsHebrew", "Strong's Hebrew Dictionary"},
        {"BDBGlosses_Strongs", "Lexique Hébreu Brown-Driver-Briggs (BDB)"},
        {"MLStrong", "Lexique Grec Liddell-Scott"},
        {"Strong Zodiates (NT)", "Étude Lexicale Grecque Spiros Zodhiates (NT)"}
    };

    for (auto const& [modName, mod] : m_mgr->Modules) {
        if (!mod) continue;
        ModuleInfo info;
        info.name = QString::fromUtf8(modName.c_str());
        info.description = QString::fromUtf8(mod->getDescription() ? mod->getDescription() : "").trimmed();
        if (const char* descFr = mod->getConfigEntry("Description_fr")) {
            QString dfr = QString::fromUtf8(descFr).trimmed();
            if (!dfr.isEmpty()) info.description = dfr;
        }
        if (friendlyDescriptions.contains(info.name)) {
            info.description = friendlyDescriptions.value(info.name);
        } else if (info.description.isEmpty()) {
            info.description = info.name;
        }

        info.type = QString::fromUtf8(mod->getType() ? mod->getType() : "");
        info.language = QString::fromUtf8(mod->getLanguage() ? mod->getLanguage() : "").toLower();

        if (info.name.startsWith("Fr_", Qt::CaseInsensitive) || info.name.startsWith("Fre", Qt::CaseInsensitive)) {
            info.language = "fr";
        } else if (info.name.startsWith("Es_", Qt::CaseInsensitive) || info.name.startsWith("Spa", Qt::CaseInsensitive) ||
                   info.name == "LBLA" || info.name == "NBLA") {
            info.language = "es";
        } else if (info.name == "KJV" || info.name == "UKJV" || info.name == "MKJV" || info.name.startsWith("Eng", Qt::CaseInsensitive)) {
            info.language = "en";
        }

        ConfigEntMap::const_iterator it = mod->getConfig().begin();
        for (; it != mod->getConfig().end(); ++it) {
            QString k = QString::fromUtf8(it->first.c_str());
            QString v = QString::fromUtf8(it->second.c_str());
            if (k == "GlobalOptionFilter" || k == "Feature") {
                if (v.contains("Strong", Qt::CaseInsensitive)) info.hasStrongs = true;
                if (v.contains("Morph", Qt::CaseInsensitive)) info.hasMorph = true;
            }
        }
        if (info.name == "FreLSG" || info.name == "FreJND" || info.name == "KJV" || info.name == "Byz" || info.name == "LXX") {
            info.hasStrongs = true;
        }

        if (info.type == "Biblical Texts") {
            m_bibles.push_back(info);
        } else if (info.type == "Commentaries") {
            m_commentaries.push_back(info);
        } else if (info.type == "Lexicons / Dictionaries") {
            m_lexicons.push_back(info);
        }
    }

    auto langPriority = [](const ModuleInfo& m) -> int {
        if (m.name == "FreLSG") return 0;
        if (m.name == "FreJND") return 1;
        if (m.name == "Fr_Semeur" || m.name == "Fr_NEG" || m.name == "Fr_JER" || m.name == "Fr_TOB") return 2;
        if (m.language == "fr") return 3;
        if (m.name == "KJV") return 4;
        if (m.language == "en") return 5;
        if (m.name == "SpaRV60" || m.name == "LBLA" || m.name == "NBLA" || m.name == "SpaRV") return 6;
        if (m.language == "es") return 7;
        if (m.language == "grc" || m.language == "he" || m.name == "Byz" || m.name == "BHS" || m.name == "Aleppo" || m.name == "LXX") return 8;
        return 9;
    };

    auto sorter = [&](const ModuleInfo& a, const ModuleInfo& b) {
        int pa = langPriority(a);
        int pb = langPriority(b);
        if (pa != pb) return pa < pb;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    };

    std::sort(m_bibles.begin(), m_bibles.end(), sorter);
    std::sort(m_commentaries.begin(), m_commentaries.end(), sorter);
    std::sort(m_lexicons.begin(), m_lexicons.end(), sorter);
}

QString SwordEngine::getModuleDescription(const QString& modName) const {
    for (const auto& m : m_bibles) if (m.name == modName) return m.description;
    for (const auto& m : m_commentaries) if (m.name == modName) return m.description;
    for (const auto& m : m_lexicons) if (m.name == modName) return m.description;
    return modName;
}

QString SwordEngine::getModuleLanguage(const QString& modName) const {
    for (const auto& m : m_bibles) if (m.name == modName) return m.language;
    for (const auto& m : m_commentaries) if (m.name == modName) return m.language;
    for (const auto& m : m_lexicons) if (m.name == modName) return m.language;
    if (modName == "ALL_FR") return "fr";
    return QString();
}

bool SwordEngine::isAncientBiblicalLanguage(const QString& langCode) {
    QString c = langCode.trimmed().toLower();
    return (c == "grc" || c == "he" || c == "hbo" || c == "la" || c == "syc" ||
            c == "cop" || c == "arc" || c == "egy" || c == "got" || c == "cu" || c == "gez");
}

QString SwordEngine::detectSystemLanguageCode() {
    QString sys = QLocale::system().name().section('_', 0, 0).section('-', 0, 0).trimmed().toLower();
    if (sys.isEmpty() || sys == "c" || sys == "posix") {
        QString envLang = qEnvironmentVariable("LANG").section('.', 0, 0).section('_', 0, 0).trimmed().toLower();
        if (!envLang.isEmpty() && envLang != "c" && envLang != "posix") {
            sys = envLang;
        } else {
            sys = "en";
        }
    }
    return sys;
}

QString SwordEngine::uiTr(const QString& fr, const QString& en, const QString& es) {
    static const QString sysLang = detectSystemLanguageCode();
    if (sysLang == "fr") return fr;
    if (sysLang == "es") return es.isEmpty() ? en : es;
    return en;
}

QString SwordEngine::localizedBookName(const BookInfo& b) {
    static const QString sysLang = detectSystemLanguageCode();
    if (sysLang == "fr") return b.frenchName;
    if (sysLang == "es") {
        static const QMap<QString, QString> esBooks = {
            {"Genesis", "Génesis"}, {"Exodus", "Éxodo"}, {"Leviticus", "Levítico"}, {"Numbers", "Números"},
            {"Deuteronomy", "Deuteronomio"}, {"Joshua", "Josué"}, {"Judges", "Jueces"}, {"Ruth", "Rut"},
            {"I Samuel", "1 Samuel"}, {"II Samuel", "2 Samuel"}, {"I Kings", "1 Reyes"}, {"II Kings", "2 Reyes"},
            {"I Chronicles", "1 Crónicas"}, {"II Chronicles", "2 Crónicas"}, {"Ezra", "Esdras"}, {"Nehemiah", "Nehemías"},
            {"Esther", "Ester"}, {"Job", "Job"}, {"Psalms", "Salmos"}, {"Proverbs", "Proverbios"},
            {"Ecclesiastes", "Eclesiastés"}, {"Song of Solomon", "Cantares"}, {"Isaiah", "Isaías"}, {"Jeremiah", "Jeremías"},
            {"Lamentations", "Lamentaciones"}, {"Ezekiel", "Ezequiel"}, {"Daniel", "Daniel"}, {"Hosea", "Oseas"},
            {"Joel", "Joel"}, {"Amos", "Amós"}, {"Obadiah", "Abdías"}, {"Jonah", "Jonás"},
            {"Micah", "Miqueas"}, {"Nahum", "Nahúm"}, {"Habakkuk", "Habacuc"}, {"Zephaniah", "Sofonías"},
            {"Haggai", "Hageo"}, {"Zechariah", "Zacarías"}, {"Malachi", "Malaquías"}, {"Matthew", "Mateo"},
            {"Mark", "Marcos"}, {"Luke", "Lucas"}, {"John", "Juan"}, {"Acts", "Hechos"},
            {"Romans", "Romanos"}, {"I Corinthians", "1 Corintios"}, {"II Corinthians", "2 Corintios"}, {"Galatians", "Gálatas"},
            {"Ephesians", "Efesios"}, {"Philippians", "Filipenses"}, {"Colossians", "Colosenses"},
            {"I Thessalonians", "1 Tesalonicenses"}, {"II Thessalonians", "2 Tesalonicenses"},
            {"I Timothy", "1 Timoteo"}, {"II Timothy", "2 Timoteo"}, {"Titus", "Tito"}, {"Philemon", "Filemón"},
            {"Hebrews", "Hebreos"}, {"James", "Santiago"}, {"I Peter", "1 Pedro"}, {"II Peter", "2 Pedro"},
            {"I John", "1 Juan"}, {"II John", "2 Juan"}, {"III John", "3 Juan"}, {"Jude", "Judas"},
            {"Revelation of John", "Apocalipsis"}
        };
        return esBooks.value(b.swordName, b.swordName);
    }
    if (b.swordName == "Revelation of John") return "Revelation";
    if (b.swordName.startsWith("III ")) return "3 " + b.swordName.mid(4);
    if (b.swordName.startsWith("II ")) return "2 " + b.swordName.mid(3);
    if (b.swordName.startsWith("I ")) return "1 " + b.swordName.mid(2);
    return b.swordName;
}

QString SwordEngine::languageFlag(const QString& langCode) {
    QString c = langCode.trimmed().toLower().section('-', 0, 0).section('_', 0, 0);
    static const QMap<QString, QString> flags = {
        {"fr", "🇫🇷"}, {"en", "🇬🇧"}, {"es", "🇪🇸"}, {"de", "🇩🇪"}, {"pt", "🇵🇹"},
        {"it", "🇮🇹"}, {"nl", "🇳🇱"}, {"ru", "🇷🇺"}, {"uk", "🇺🇦"}, {"pl", "🇵🇱"},
        {"ro", "🇷🇴"}, {"cs", "🇨🇿"}, {"sk", "🇸🇰"}, {"hu", "🇭🇺"}, {"bg", "🇧🇬"},
        {"sr", "🇷🇸"}, {"hr", "🇭🇷"}, {"el", "🇬🇷"}, {"grc", "🏛️"}, {"he", "🇮🇱"},
        {"hbo", "🕎"}, {"la", "🏛️"}, {"ar", "🇸🇦"}, {"fa", "🇮🇷"}, {"tr", "🇹🇷"},
        {"zh", "🇨🇳"}, {"ja", "🇯🇵"}, {"ko", "🇰🇷"}, {"vi", "🇻🇳"}, {"th", "🇹🇭"},
        {"id", "🇮🇩"}, {"ms", "🇲🇾"}, {"tl", "🇵🇭"}, {"hi", "🇮🇳"}, {"ta", "🇮🇳"},
        {"sw", "🇰🇪"}, {"af", "🇿🇦"}, {"sv", "🇸🇪"}, {"no", "🇳🇴"}, {"nb", "🇳🇴"},
        {"da", "🇩🇰"}, {"fi", "🇫🇮"}, {"eo", "🌍"}
    };
    return flags.value(c, "🌐");
}

QString SwordEngine::languageNativeName(const QString& langCode) {
    QString c = langCode.trimmed().toLower().section('-', 0, 0).section('_', 0, 0);
    static const QMap<QString, QString> names = {
        {"fr", "Français"}, {"en", "English"}, {"es", "Español"}, {"de", "Deutsch"},
        {"pt", "Português"}, {"it", "Italiano"}, {"nl", "Nederlands"}, {"ru", "Русский"},
        {"uk", "Українська"}, {"pl", "Polski"}, {"ro", "Română"}, {"cs", "Čeština"},
        {"sk", "Slovenčina"}, {"hu", "Magyar"}, {"bg", "Български"}, {"sr", "Српски"},
        {"hr", "Hrvatski"}, {"el", "Ελληνικά"}, {"grc", "Grec Ancien (Koïné)"},
        {"he", "Hébreu / עברית"}, {"hbo", "Hébreu Biblique"}, {"la", "Latin"},
        {"ar", "العربية (Arabe)"}, {"fa", "فارسی (Persan)"}, {"tr", "Türkçe"},
        {"zh", "中文 (Chinois)"}, {"ja", "日本語 (Japonais)"}, {"ko", "한국어 (Coréen)"},
        {"vi", "Tiếng Việt"}, {"th", "ไทย"}, {"id", "Bahasa Indonesia"},
        {"tl", "Tagalog"}, {"hi", "हिन्दी (Hindi)"}, {"sw", "Kiswahili"},
        {"af", "Afrikaans"}, {"sv", "Svenska"}, {"no", "Norsk"}, {"nb", "Norsk"},
        {"da", "Dansk"}, {"fi", "Suomi"}, {"eo", "Esperanto"}
    };
    if (names.contains(c)) return names.value(c);
    return c.toUpper();
}

QString SwordEngine::languageFlagAndName(const QString& langCode) {
    QString c = langCode.trimmed().toLower();
    if (c == "auto") return uiTr("🔄 Auto (selon la Bible active)", "🔄 Auto (from active Bible)", "🔄 Auto (según la Biblia activa)");
    if (c == "fr_es") return "🇫🇷+🇪🇸 Français & Español";
    return QString("%1 %2").arg(languageFlag(c), languageNativeName(c));
}

QStringList SwordEngine::getInstalledStudyLanguages() const {
    QStringList result;
    auto addLangIfModern = [&](const QString& rawCode) {
        QString c = rawCode.trimmed().toLower().section('-', 0, 0).section('_', 0, 0);
        if (c.isEmpty() || c == "und" || isAncientBiblicalLanguage(c)) return;
        if (!result.contains(c)) {
            result.push_back(c);
        }
    };

    // 1. Parcourir les langues de toutes les Bibles installées
    for (const auto& b : m_bibles) {
        addLangIfModern(b.language);
    }
    // 2. Parcourir les langues des Commentaires installés
    for (const auto& cm : m_commentaries) {
        addLangIfModern(cm.language);
    }
    // 3. Ajouter aussi la langue du système d'exploitation (ex: "en" si l'OS est en anglais)
    addLangIfModern(detectSystemLanguageCode());

    // 4. Si aucun module moderne n'est encore installé, proposer au moins la langue système + FR / EN / ES
    if (result.isEmpty()) {
        result = {"fr", "en", "es"};
    }
    return result;
}

const BookInfo* SwordEngine::findBookBySwordName(const QString& swordName) const {
    for (const auto& b : m_books) {
        if (b.swordName.compare(swordName, Qt::CaseInsensitive) == 0 ||
            b.osisName.compare(swordName, Qt::CaseInsensitive) == 0) {
            return &b;
        }
    }
    return nullptr;
}

const BookInfo* SwordEngine::findBookByAnyName(const QString& query) const {
    QString q = normalizeForMatch(query);
    if (q.isEmpty()) return nullptr;

    static const QMap<QString, QString> aliases = {
        {"jn", "John"}, {"jean", "John"}, {"joh", "John"}, {"juan", "John"},
        {"1jn", "I John"}, {"1jean", "I John"}, {"1jo", "I John"}, {"1juan", "I John"},
        {"2jn", "II John"}, {"2jean", "II John"}, {"2jo", "II John"}, {"2juan", "II John"},
        {"3jn", "III John"}, {"3jean", "III John"}, {"3jo", "III John"}, {"3juan", "III John"},
        {"gn", "Genesis"}, {"gen", "Genesis"}, {"ge", "Genesis"}, {"genese", "Genesis"},
        {"ex", "Exodus"}, {"exo", "Exodus"}, {"exode", "Exodus"}, {"exodo", "Exodus"},
        {"lv", "Leviticus"}, {"lev", "Leviticus"}, {"levitique", "Leviticus"}, {"levitico", "Leviticus"},
        {"nb", "Numbers"}, {"nom", "Numbers"}, {"nombres", "Numbers"}, {"numeros", "Numbers"},
        {"dt", "Deuteronomy"}, {"deut", "Deuteronomy"}, {"deuteronome", "Deuteronomy"}, {"deuteronomio", "Deuteronomy"},
        {"ps", "Psalms"}, {"psa", "Psalms"}, {"psaume", "Psalms"}, {"psaumes", "Psalms"}, {"sal", "Psalms"}, {"salmos", "Psalms"},
        {"pr", "Proverbs"}, {"prov", "Proverbs"}, {"proverbes", "Proverbs"}, {"proverbios", "Proverbs"},
        {"es", "Isaiah"}, {"esa", "Isaiah"}, {"esaie", "Isaiah"}, {"isa", "Isaiah"}, {"isaie", "Isaiah"}, {"isaias", "Isaiah"},
        {"mt", "Matthew"}, {"mat", "Matthew"}, {"matt", "Matthew"}, {"matthieu", "Matthew"}, {"mateo", "Matthew"},
        {"mc", "Mark"}, {"mr", "Mark"}, {"marc", "Mark"}, {"marcos", "Mark"},
        {"lc", "Luke"}, {"lu", "Luke"}, {"luc", "Luke"}, {"lucas", "Luke"},
        {"ac", "Acts"}, {"act", "Acts"}, {"actes", "Acts"}, {"hech", "Acts"}, {"hechos", "Acts"},
        {"rm", "Romans"}, {"ro", "Romans"}, {"rom", "Romans"}, {"romains", "Romans"}, {"romanos", "Romans"},
        {"1co", "I Corinthians"}, {"1cor", "I Corinthians"}, {"1corinthiens", "I Corinthians"}, {"1corintios", "I Corinthians"},
        {"2co", "II Corinthians"}, {"2cor", "II Corinthians"}, {"2corinthiens", "II Corinthians"}, {"2corintios", "II Corinthians"},
        {"ga", "Galatians"}, {"gal", "Galatians"}, {"galates", "Galatians"}, {"galatas", "Galatians"},
        {"ep", "Ephesians"}, {"eph", "Ephesians"}, {"ephesiens", "Ephesians"}, {"ef", "Ephesians"}, {"efesios", "Ephesians"},
        {"ph", "Philippians"}, {"php", "Philippians"}, {"phil", "Philippians"}, {"philippiens", "Philippians"}, {"fil", "Philippians"}, {"filipenses", "Philippians"},
        {"col", "Colossians"}, {"colossiens", "Colossians"}, {"colosenses", "Colossians"},
        {"he", "Hebrews"}, {"heb", "Hebrews"}, {"hebreux", "Hebrews"}, {"hebreos", "Hebrews"},
        {"jc", "James"}, {"jac", "James"}, {"jacques", "James"}, {"stg", "James"}, {"santiago", "James"},
        {"1p", "I Peter"}, {"1pi", "I Peter"}, {"1pe", "I Peter"}, {"1pierre", "I Peter"}, {"1pedro", "I Peter"},
        {"2p", "II Peter"}, {"2pi", "II Peter"}, {"2pe", "II Peter"}, {"2pierre", "II Peter"}, {"2pedro", "II Peter"},
        {"ap", "Revelation of John"}, {"apo", "Revelation of John"}, {"apoc", "Revelation of John"},
        {"apocalypse", "Revelation of John"}, {"apocalipsis", "Revelation of John"}, {"re", "Revelation of John"}, {"rev", "Revelation of John"}
    };

    if (aliases.contains(q)) {
        return findBookBySwordName(aliases.value(q));
    }

    for (const auto& b : m_books) {
        if (normalizeForMatch(b.frenchName) == q ||
            normalizeForMatch(b.swordName) == q ||
            normalizeForMatch(b.osisName) == q ||
            normalizeForMatch(b.frenchAbbrev) == q) {
            return &b;
        }
    }
    for (const auto& b : m_books) {
        if (normalizeForMatch(b.frenchName).startsWith(q) ||
            normalizeForMatch(b.swordName).startsWith(q)) {
            return &b;
        }
    }
    return nullptr;
}

int SwordEngine::getChapterCount(const QString& swordBookName) const {
    if (const BookInfo* b = findBookBySwordName(swordBookName)) {
        return b->chapterCount;
    }
    VerseKey vk(QString("%1 1:1").arg(swordBookName).toUtf8().constData());
    return vk.getChapterMax();
}

int SwordEngine::getVerseCount(const QString& swordBookName, int chapter) const {
    VerseKey vk(QString("%1 %2:1").arg(swordBookName).arg(chapter).toUtf8().constData());
    int vmax = vk.getVerseMax();
    return vmax > 0 ? vmax : 30;
}

QString SwordEngine::parseUserReference(const QString& input, int* outChapter, int* outVerse, QString* outBookSword) const {
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        if (outBookSword) *outBookSword = "John";
        if (outChapter) *outChapter = 1;
        if (outVerse) *outVerse = 1;
        return "John 1:1";
    }

    static const QRegularExpression rx(R"(^\s*([1-3I]{0,3}\s*[^\d:.]+?)\s*(\d+)?(?:\s*[:.,v]\s*(\d+))?(?:\s*-\s*\d+)?\s*$)");
    QRegularExpressionMatch m = rx.match(trimmed);
    if (m.hasMatch()) {
        QString bookPart = m.captured(1).trimmed();
        int chap = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
        int verse = m.captured(3).isEmpty() ? 1 : m.captured(3).toInt();
        if (const BookInfo* b = findBookByAnyName(bookPart)) {
            chap = std::clamp(chap, 1, b->chapterCount);
            int vmax = getVerseCount(b->swordName, chap);
            verse = std::clamp(verse, 1, vmax);
            if (outBookSword) *outBookSword = b->swordName;
            if (outChapter) *outChapter = chap;
            if (outVerse) *outVerse = verse;
            return QString("%1 %2:%3").arg(b->swordName).arg(chap).arg(verse);
        }
    }

    VerseKey vk(trimmed.toUtf8().constData());
    QString bname = QString::fromUtf8(vk.getBookName());
    int chap = vk.getChapter() > 0 ? vk.getChapter() : 1;
    int verse = vk.getVerse() > 0 ? vk.getVerse() : 1;
    if (outBookSword) *outBookSword = bname;
    if (outChapter) *outChapter = chap;
    if (outVerse) *outVerse = verse;
    return QString("%1 %2:%3").arg(bname).arg(chap).arg(verse);
}

bool SwordEngine::tryParseStrictReference(const QString& input, QString& outSwordKey, QString* outBookSword, int* outChapter, int* outVerse) const {
    QString candidate = input.trimmed();
    if (candidate.isEmpty()) return false;

    // Si le texte comporte plusieurs lignes ou un préfixe "📖 Jean 3:16 — ...", extraire la référence
    QString firstLine = candidate.section('\n', 0, 0).trimmed();
    if (firstLine.startsWith("📖")) {
        firstLine = firstLine.mid(QString("📖").size()).trimmed();
    }
    if (firstLine.contains("—")) {
        firstLine = firstLine.section("—", 0, 0).trimmed();
    }

    static const QRegularExpression rxStrict(R"(^\s*([1-3I]{0,3}\s*[^\d:.]+?)\s+(\d+)(?:\s*[:.,v]\s*(\d+))?(?:\s*-\s*\d+)?\s*$)");
    QRegularExpressionMatch m = rxStrict.match(firstLine);
    if (!m.hasMatch()) return false;

    QString bookPart = m.captured(1).trimmed();
    if (bookPart.size() < 2) return false;

    const BookInfo* b = findBookByAnyName(bookPart);
    if (!b) return false;

    int chap = m.captured(2).toInt();
    int verse = m.captured(3).isEmpty() ? 1 : m.captured(3).toInt();
    chap = std::clamp(chap, 1, b->chapterCount);
    int vmax = getVerseCount(b->swordName, chap);
    verse = std::clamp(verse, 1, vmax);

    if (outBookSword) *outBookSword = b->swordName;
    if (outChapter) *outChapter = chap;
    if (outVerse) *outVerse = verse;
    outSwordKey = QString("%1 %2:%3").arg(b->swordName).arg(chap).arg(verse);
    return true;
}

void SwordEngine::splitSwordKey(const QString& swordKey, QString& outBookSword, int& outChapter, int& outVerse) const {
    parseUserReference(swordKey, &outChapter, &outVerse, &outBookSword);
}

QString SwordEngine::toFrenchReference(const QString& swordKey) const {
    QString bookSword;
    int chap = 1, verse = 1;
    splitSwordKey(swordKey, bookSword, chap, verse);
    if (const BookInfo* b = findBookBySwordName(bookSword)) {
        return QString("%1 %2:%3").arg(localizedBookName(*b)).arg(chap).arg(verse);
    }
    return swordKey;
}

QString SwordEngine::normalizeStrongKey5(const QString& numPart) const {
    QString digits = numPart;
    digits.remove(QRegularExpression("[^0-9]"));
    int val = digits.toInt();
    return QString("%1").arg(val, 5, 10, QChar('0'));
}

QString SwordEngine::normalizeStrongKey4(const QString& numPart) const {
    QString digits = numPart;
    digits.remove(QRegularExpression("[^0-9]"));
    int val = digits.toInt();
    if (val < 1000) {
        return QString("%1").arg(val, 4, 10, QChar('0'));
    }
    return QString::number(val);
}

StrongSummary SwordEngine::getStrongSummary(const QString& rawStrongCode) {
    QString clean = rawStrongCode.trimmed().toUpper();
    bool isHebrew = clean.startsWith('H');
    bool isGreek = clean.startsWith('G');
    QString digits = clean;
    digits.remove(QRegularExpression("[^0-9]"));
    if (digits.isEmpty()) return {};

    int num = digits.toInt();
    if (!isHebrew && !isGreek) {
        isGreek = true;
    }
    QString canonicalCode = QString("%1%2").arg(isHebrew ? "H" : "G").arg(num);

    if (m_strongCache.contains(canonicalCode)) {
        return m_strongCache.value(canonicalCode);
    }

    StrongSummary sum;
    sum.code = canonicalCode;

    QString key5 = normalizeStrongKey5(digits);
    if (isGreek) {
        if (SWModule* sg = m_mgr->getModule("StrongsGreek")) {
            sg->setKey(key5.toUtf8().constData());
            QString html = QString::fromUtf8(sg->renderText().c_str());
            static const QRegularExpression rxGreek(R"(<b>\d+</b>\s*<b>([^<]+)</b>\s*<b>([^<,]+),?</b>\s*<i>([^<]+)</i>)");
            auto m = rxGreek.match(html);
            if (m.hasMatch()) {
                sum.originalUnicode = m.captured(1).trimmed();
                sum.transliteration = m.captured(2).trimmed();
                sum.pronunciation = m.captured(3).trimmed();
            }
        }
        if (SWModule* frg = m_mgr->getModule("Fr_Greek")) {
            frg->setKey(key5.toUtf8().constData());
            QString txt = QString::fromUtf8(frg->renderText().c_str());
            if (QString::fromUtf8(frg->getKeyText()) == key5 && !txt.isEmpty()) {
                txt.replace(QRegularExpression("<[^>]+>"), " ");
                static const QRegularExpression rxLsg(R"(LSG\s*-\s*([^;:\n\r]+))");
                auto m = rxLsg.match(txt);
                if (m.hasMatch()) {
                    sum.shortFrenchGloss = m.captured(1).simplified();
                } else {
                    static const QRegularExpression rxDef(R"(1\)\s*([^\n\r]+))");
                    auto m2 = rxDef.match(txt);
                    if (m2.hasMatch()) sum.shortFrenchGloss = m2.captured(1).simplified().left(90);
                }
            }
        }
    } else {
        if (SWModule* bdb = m_mgr->getModule("BDBGlosses_Strongs")) {
            QString bdbKey = QString("H%1").arg(normalizeStrongKey4(digits));
            bdb->setKey(bdbKey.toUtf8().constData());
            QString html = QString::fromUtf8(bdb->renderText().c_str());
            if (QString::fromUtf8(bdb->getKeyText()).compare(bdbKey, Qt::CaseInsensitive) == 0 && !html.isEmpty()) {
                static const QRegularExpression rxBdb(R"(</b>H\d+\s+([^\s<]+)\s*<i>([^<]+)</i>)");
                auto m = rxBdb.match(html);
                if (m.hasMatch()) {
                    sum.originalUnicode = m.captured(1).trimmed();
                }
            }
        }
        if (SWModule* sh = m_mgr->getModule("StrongsHebrew")) {
            sh->setKey(key5.toUtf8().constData());
            QString html = QString::fromUtf8(sh->renderText().c_str());
            static const QRegularExpression rxHeb(R"(<b>\d+</b>\s*<b>([^<]+)</b>\s*(?:<br\s*/?>)?\s*<b>([^<]+)</b>\s*<i>([^<]+)</i>)");
            auto m = rxHeb.match(html);
            if (m.hasMatch()) {
                if (sum.originalUnicode.isEmpty()) {
                    sum.originalUnicode = m.captured(1).trimmed();
                }
                QString tr = m.captured(2);
                tr.replace(QRegularExpression("<[^>]+>"), "");
                sum.transliteration = tr.trimmed();
                sum.pronunciation = m.captured(3).trimmed();
            }
        }
        if (SWModule* frh = m_mgr->getModule("Fr_Hebreu")) {
            frh->setKey(key5.toUtf8().constData());
            QString txt = QString::fromUtf8(frh->renderText().c_str());
            if (QString::fromUtf8(frh->getKeyText()) == key5 && !txt.isEmpty()) {
                txt.replace(QRegularExpression("<[^>]+>"), " ");
                static const QRegularExpression rxLsg(R"(LSG\s*-\s*([^;:\n\r]+))");
                auto m = rxLsg.match(txt);
                if (m.hasMatch()) {
                    sum.shortFrenchGloss = m.captured(1).simplified();
                } else {
                    static const QRegularExpression rxDef(R"(1\)\s*([^\n\r]+))");
                    auto m2 = rxDef.match(txt);
                    if (m2.hasMatch()) sum.shortFrenchGloss = m2.captured(1).simplified().left(90);
                }
            }
        }
    }

    m_strongCache.insert(canonicalCode, sum);
    return sum;
}

QString SwordEngine::cleanRawEntryToPlain(const QString& htmlOrRaw) const {
    QString s = htmlOrRaw;
    s.replace(QRegularExpression(R"RX(<small>\s*<em class="(?:strongs|morph)">.*?</em>\s*</small>)RX", QRegularExpression::DotMatchesEverythingOption), "");
    s.replace(QRegularExpression(R"RX(<a[^>]*>\s*<small>\s*<sup[^>]*>.*?</sup>\s*</small>\s*</a>)RX", QRegularExpression::DotMatchesEverythingOption), "");
    s.replace(QRegularExpression(R"(<W[A-Za-z0-9]+>)"), "");
    s.replace(QRegularExpression(R"(D<font[^>]*>IEU</font>)"), "DIEU");
    s.replace(QRegularExpression(R"(É<font[^>]*>TERNEL</font>)"), "ÉTERNEL");
    s.replace(QRegularExpression(R"(L<font[^>]*>ORD</font>)"), "LORD");
    s.replace(QRegularExpression(R"(<[^>]+>)"), "");
    s.replace("&nbsp;", " ");
    s.replace("&lt;", "<");
    s.replace("&gt;", ">");
    s.replace("&amp;", "&");
    s.replace("&quot;", "\"");
    s = s.simplified();
    s.replace(" ,", ",");
    s.replace(" .", ".");
    return s.trimmed();
}

QString SwordEngine::getCleanVerseText(const QString& moduleName, const QString& swordKey) const {
    if (!m_mgr) return {};
    SWModule* mod = m_mgr->getModule(moduleName.toUtf8().constData());
    if (!mod) return {};
    mod->setKey(swordKey.toUtf8().constData());
    QString rendered = QString::fromUtf8(mod->renderText().c_str());
    return cleanRawEntryToPlain(rendered);
}

QString SwordEngine::renderChapterHtml(
    const QString& moduleName,
    const QString& swordBook,
    int chapter,
    int activeVerse,
    const PaneStyle& style
) {
    if (!m_mgr || moduleName.isEmpty()) return {};
    SWModule* mod = m_mgr->getModule(moduleName.toUtf8().constData());
    if (!mod) return {};

    const BookInfo* bookInfo = findBookBySwordName(swordBook);
    bool isOT = bookInfo ? (bookInfo->testament == 1) : true;
    QString prefixChar = isOT ? "H" : "G";
    if (moduleName == "LXX" || moduleName == "Byz") {
        prefixChar = "G";
    }

    QString html;
    html += QString(R"(<!DOCTYPE html><html><head><meta charset="utf-8"><style>
        body {
            %1
            margin: 10px 14px;
        }
        .verse-row {
            padding: 6px 10px;
            margin-bottom: 4px;
            border-radius: 6px;
            border-left: 3px solid transparent;
        }
        .verse-active {
            background-color: %2;
            border-left: 4px solid %3;
        }
        .vnum {
            font-family: "Inter", "Noto Sans", sans-serif;
            font-weight: 700;
            font-size: 0.85em;
            color: %3;
            text-decoration: none;
            margin-right: 6px;
        }
        .wlink {
            color: %4;
            text-decoration: none;
        }
    </style></head><body>)")
        .arg(buildBodyCss(style), style.cardBgColor, style.accentColor, style.textColor);

    static const QRegularExpression rxWordStrong(
        R"RX(([^\s<>]+)?(\s*<small><em class="strongs">&lt;<a href="passagestudy\.jsp\?action=showStrongs&type=([^&"]+)&value=([^"]+)"[^>]*>[^<]+</a>&gt;</em></small>)+(\s*<small><em class="(?:strongs|morph)">\(<a href="passagestudy\.jsp\?action=show(?:Strongs|Morph)&type=([^&"]+)&value=([^"]+)"[^>]*>([^<]+)</a>\)</em></small>)*)RX"
    );
    static const QRegularExpression rxSingleStrong(R"RX(type=(Greek|Hebrew)&value=([0-9]+))RX");
    static const QRegularExpression rxSingleMorph(R"RX(\(<a href="passagestudy\.jsp\?action=show(?:Strongs|Morph)&type=([^&"]+)&value=([^"]+)"[^>]*>([^<]+)</a>\))RX");
    static const QRegularExpression rxPlainWord(R"(([\p{L}\p{M}0-9'’-]+))");

    auto wrapPlainWords = [&](const QString& plainSegment, int verseNum, int& wordIdxCounter) -> QString {
        QString out;
        int last = 0;
        auto wit = rxPlainWord.globalMatch(plainSegment);
        while (wit.hasNext()) {
            auto wm = wit.next();
            out += plainSegment.mid(last, wm.capturedStart() - last).toHtmlEscaped();
            last = wm.capturedEnd();
            QString w = wm.captured(1);
            QUrl u("app://word");
            QUrlQuery q;
            q.addQueryItem("v", QString::number(verseNum));
            q.addQueryItem("idx", QString::number(wordIdxCounter++));
            q.addQueryItem("w", w);
            u.setQuery(q);
            out += QString(R"(<a class="wlink" href="%1">%2</a>)").arg(u.toString(), w.toHtmlEscaped());
        }
        out += plainSegment.mid(last).toHtmlEscaped();
        return out;
    };

    int vmax = getVerseCount(swordBook, chapter);
    for (int v = 1; v <= vmax; ++v) {
        QString vkey = QString("%1 %2:%3").arg(swordBook).arg(chapter).arg(v);
        mod->setKey(vkey.toUtf8().constData());

        QString rowClass = (v == activeVerse) ? "verse-row verse-active" : "verse-row";
        QString noteBadge = hasPersonalNote(vkey)
            ? QString(R"(<a href="app://note/%1" title="Ouvrir votre note personnelle sur ce verset" style="text-decoration:none; font-size:0.85em; margin-right:5px;">📝</a>)").arg(v)
            : QString();
        html += QString(R"(<div class="%1" id="v%2"><a name="v%2"></a><a class="vnum" href="app://verse/%2">%2</a>%3 )")
            .arg(rowClass).arg(v).arg(noteBadge);

        QString rawRendered = QString::fromUtf8(mod->renderText().c_str());
        rawRendered.replace(QRegularExpression(R"(D<font[^>]*>IEU</font>)"), "DIEU");
        rawRendered.replace(QRegularExpression(R"(É<font[^>]*>TERNEL</font>)"), "ÉTERNEL");
        rawRendered.replace(QRegularExpression(R"(L<font[^>]*>ORD</font>)"), "LORD");
        rawRendered.replace(QRegularExpression(R"RX(<a href="passagestudy\.jsp\?action=showNote[^>]*>.*?</a>)RX", QRegularExpression::DotMatchesEverythingOption), "");

        int wordIdx = 0;
        if (rawRendered.contains("action=showStrongs")) {
            QString transformed;
            int lastPos = 0;
            auto it = rxWordStrong.globalMatch(rawRendered);
            while (it.hasNext()) {
                auto m = it.next();
                QString between = cleanRawEntryToPlain(rawRendered.mid(lastPos, m.capturedStart() - lastPos));
                if (!between.isEmpty()) {
                    transformed += wrapPlainWords(between, v, wordIdx) + " ";
                }
                lastPos = m.capturedEnd();

                QString wordPart = cleanRawEntryToPlain(m.captured(1));
                QString fullChunk = m.captured(0);

                QStringList strongCodes;
                auto sit = rxSingleStrong.globalMatch(fullChunk);
                while (sit.hasNext()) {
                    auto sm = sit.next();
                    QString langType = sm.captured(1);
                    QString val = sm.captured(2);
                    int num = val.toInt();
                    if (num > 0) {
                        QString sc = QString("%1%2").arg(langType.startsWith("Heb", Qt::CaseInsensitive) ? "H" : "G").arg(num);
                        if (!strongCodes.contains(sc)) strongCodes.push_back(sc);
                    }
                }

                QString morphCode;
                auto mit = rxSingleMorph.globalMatch(fullChunk);
                if (mit.hasNext()) {
                    auto mm = mit.next();
                    morphCode = mm.captured(3).trimmed();
                    if (morphCode.toInt() > 0) {
                        morphCode = prefixChar + morphCode;
                    }
                }

                QString primaryStrong = strongCodes.isEmpty() ? QString() : strongCodes.first();
                if (!wordPart.isEmpty()) {
                    QUrl url("app://word");
                    QUrlQuery q;
                    q.addQueryItem("v", QString::number(v));
                    q.addQueryItem("idx", QString::number(wordIdx++));
                    if (!primaryStrong.isEmpty()) q.addQueryItem("strong", primaryStrong);
                    if (!morphCode.isEmpty()) q.addQueryItem("morph", morphCode);
                    q.addQueryItem("w", wordPart);
                    url.setQuery(q);
                    transformed += QString(R"(<a class="wlink" href="%1">%2</a> )").arg(url.toString(), wordPart.toHtmlEscaped());
                }
            }
            QString tail = cleanRawEntryToPlain(rawRendered.mid(lastPos));
            if (!tail.isEmpty()) {
                transformed += wrapPlainWords(tail, v, wordIdx);
            }
            html += transformed.trimmed();
        } else {
            QString cleanTxt = cleanRawEntryToPlain(rawRendered);
            html += wrapPlainWords(cleanTxt, v, wordIdx);
        }

        html += "</div>\n";
    }

    html += "</body></html>";
    return html;
}

QString SwordEngine::getOriginalVerseText(const QString& swordKey, QString* outModuleUsed) const {
    QString bookSword;
    int ch = 1, v = 1;
    splitSwordKey(swordKey, bookSword, ch, v);
    const BookInfo* b = findBookBySwordName(bookSword);
    bool isOT = b ? (b->testament == 1) : true;

    if (isOT) {
        for (const char* modName : {"BHS", "Aleppo"}) {
            QString txt = getCleanVerseText(modName, swordKey);
            if (!txt.isEmpty()) {
                if (outModuleUsed) *outModuleUsed = QString("%1 (Hébreu)").arg(modName);
                return txt;
            }
        }
    } else {
        QString txt = getCleanVerseText("Byz", swordKey);
        if (!txt.isEmpty()) {
            if (outModuleUsed) *outModuleUsed = "Byz (Grec)";
            return txt;
        }
    }
    return {};
}

QVector<WordToken> SwordEngine::getVerseWordTokens(const QString& swordKey, const QString& preferredBible) {
    QVector<WordToken> result;
    if (!m_mgr) return result;

    QString bookSword;
    int ch = 1, v = 1;
    splitSwordKey(swordKey, bookSword, ch, v);
    const BookInfo* b = findBookBySwordName(bookSword);
    bool isOT = b ? (b->testament == 1) : true;

    QStringList candidates = {preferredBible, "FreLSG", "FreJND", "KJV", isOT ? "LXX" : "Byz"};
    candidates.removeDuplicates();

    for (const QString& modName : candidates) {
        if (modName.isEmpty()) continue;
        SWModule* mod = m_mgr->getModule(modName.toUtf8().constData());
        if (!mod) continue;

        mod->setKey(swordKey.toUtf8().constData());
        mod->renderText();
        auto& attrs = mod->getEntryAttributes();
        if (attrs["Word"].empty()) continue;

        for (auto const& [wnum, wmap] : attrs["Word"]) {
            WordToken tk;
            auto itText = wmap.find("Text");
            if (itText != wmap.end()) {
                tk.surfaceText = QString::fromUtf8(itText->second.c_str());
            }
            auto itLemma = wmap.find("Lemma");
            if (itLemma != wmap.end()) {
                QString rawLemma = QString::fromUtf8(itLemma->second.c_str());
                static const QRegularExpression rxSH(R"(\b([GHgh]?\d+)\b)");
                auto m = rxSH.match(rawLemma);
                if (m.hasMatch()) {
                    QString code = m.captured(1).toUpper();
                    if (!code.startsWith('G') && !code.startsWith('H')) {
                        code = (isOT && modName != "LXX" ? "H" : "G") + QString::number(code.toInt());
                    } else {
                        QString prefix = code.left(1);
                        int num = code.mid(1).toInt();
                        code = prefix + QString::number(num);
                    }
                    tk.strongCode = code;
                }
            }
            auto itMorph = wmap.find("Morph");
            if (itMorph != wmap.end()) {
                tk.morphCode = QString::fromUtf8(itMorph->second.c_str()).trimmed();
            }
            auto itMorphClass = wmap.find("MorphClass");
            if (itMorphClass != wmap.end()) {
                tk.morphClass = QString::fromUtf8(itMorphClass->second.c_str()).trimmed();
            }

            if (!tk.strongCode.isEmpty()) {
                StrongSummary sum = getStrongSummary(tk.strongCode);
                tk.originalUnicode = sum.originalUnicode;
                tk.transliteration = sum.transliteration;
                tk.pronunciation = sum.pronunciation;
                tk.shortFrenchGloss = sum.shortFrenchGloss;
                result.push_back(tk);
            }
        }
        if (!result.isEmpty()) break;
    }

    return result;
}

WordToken SwordEngine::resolveClickedWordToToken(
    const QString& swordKey,
    const QString& clickedWord,
    const QString& explicitStrongCode,
    const QString& explicitMorphCode,
    int wordIndexInVerse
) {
    if (!explicitStrongCode.trimmed().isEmpty()) {
        WordToken tk;
        tk.surfaceText = clickedWord;
        tk.strongCode = explicitStrongCode.trimmed().toUpper();
        tk.morphCode = explicitMorphCode.trimmed();
        StrongSummary sum = getStrongSummary(tk.strongCode);
        tk.originalUnicode = sum.originalUnicode;
        tk.transliteration = sum.transliteration;
        tk.pronunciation = sum.pronunciation;
        tk.shortFrenchGloss = sum.shortFrenchGloss;
        return tk;
    }

    auto tokens = getVerseWordTokens(swordKey, "FreLSG");
    if (tokens.isEmpty()) {
        tokens = getVerseWordTokens(swordKey, "KJV");
    }
    if (tokens.isEmpty()) {
        WordToken emptyTk;
        emptyTk.surfaceText = clickedWord;
        return emptyTk;
    }

    QString normClicked = normalizeForMatch(clickedWord);
    if (normClicked.size() >= 3) {
        for (const auto& tk : tokens) {
            QString normSurf = normalizeForMatch(tk.surfaceText);
            QString normGloss = normalizeForMatch(tk.shortFrenchGloss);
            if ((!normSurf.isEmpty() && (normSurf.contains(normClicked) || normClicked.contains(normSurf))) ||
                (!normGloss.isEmpty() && normGloss.contains(normClicked))) {
                WordToken matched = tk;
                if (!clickedWord.isEmpty()) matched.surfaceText = clickedWord;
                return matched;
            }
        }
    }

    int idx = std::clamp(wordIndexInVerse, 0, int(tokens.size()) - 1);
    WordToken chosen = tokens.at(idx);
    if (!clickedWord.isEmpty()) {
        chosen.surfaceText = clickedWord;
    }
    return chosen;
}

QString SwordEngine::lookupStrongAndMorphHtml(
    const QString& swordKey,
    const QString& strongCode,
    const QString& morphCode,
    const QString& clickedWord,
    const PaneStyle& style
) {
    QString html = QString(R"(<!DOCTYPE html><html><head><meta charset="utf-8"><style>
        body { %1 margin: 8px 10px; }
        a { color: %2; text-decoration: none; }
    </style></head><body>)").arg(buildBodyCss(style), style.accentColor);

    // 0. Barre des mots originaux du verset actif
    auto verseTokens = getVerseWordTokens(swordKey, "FreLSG");
    if (!verseTokens.isEmpty()) {
        html += QString(R"(
            <div style="background:%1; border:1px solid %2; border-radius:8px; padding:8px 10px; margin-bottom:10px;">
                <div style="font-size:0.86em; font-weight:700; color:%3; margin-bottom:6px;">
                    🏛️ Mots du verset (%4) :
                </div>
                <div>
        )").arg(style.cardBgColor, style.borderColor, style.accentColor, toFrenchReference(swordKey).toHtmlEscaped());

        for (int i = 0; i < verseTokens.size(); ++i) {
            const auto& tk = verseTokens.at(i);
            if (tk.strongCode.isEmpty()) continue;
            bool isActive = (tk.strongCode.compare(strongCode, Qt::CaseInsensitive) == 0);
            QUrl u("app://word");
            QUrlQuery q;
            q.addQueryItem("strong", tk.strongCode);
            if (!tk.morphCode.isEmpty()) q.addQueryItem("morph", tk.morphCode);
            if (!tk.surfaceText.isEmpty()) q.addQueryItem("w", tk.surfaceText.trimmed());
            u.setQuery(q);

            QString label = tk.surfaceText.trimmed();
            if (label.isEmpty()) label = tk.strongCode;
            if (!tk.originalUnicode.isEmpty()) {
                label += QString(" (%1)").arg(tk.originalUnicode);
            }
            html += QString(R"(<a href="%1" style="display:inline-block; background:%2; color:%3; border:1px solid %4; border-radius:5px; padding:3px 8px; margin:2px 4px 2px 0; text-decoration:none; font-size:0.9em; font-weight:%5;">%6</a>)")
                .arg(u.toString(),
                     isActive ? style.accentColor : style.bgColor,
                     isActive ? "#FFFFFF" : style.textColor,
                     style.borderColor,
                     isActive ? "700" : "500",
                     label.toHtmlEscaped());
        }
        html += "</div></div>";
    }

    QString clean = strongCode.trimmed().toUpper();
    bool isHebrew = clean.startsWith('H');
    QString digits = clean;
    digits.remove(QRegularExpression("[^0-9]"));
    if (digits.isEmpty() && !verseTokens.isEmpty()) {
        clean = verseTokens.first().strongCode;
        isHebrew = clean.startsWith('H');
        digits = clean;
        digits.remove(QRegularExpression("[^0-9]"));
    }
    if (digits.isEmpty()) {
        html += "<p>Cliquez sur n'importe quel mot dans la Bible au centre pour afficher sa signification grecque ou hébraïque.</p></body></html>";
        return html;
    }

    int num = digits.toInt();
    QString canonical = QString("%1%2").arg(isHebrew ? "H" : "G").arg(num);
    QString key5 = normalizeStrongKey5(digits);
    QString key4 = normalizeStrongKey4(digits);

    StrongSummary sum = getStrongSummary(canonical);

    // En-tête du mot original Grec / Hébreu
    html += QString(R"(
        <div style="background:%1; border:1px solid %2; border-radius:8px; padding:12px 14px; margin-bottom:12px;">
            <div style="font-size:0.95em; color:%3; font-weight:700;">
                %4 — %5 %6
            </div>
            <div style="font-size:1.85em; font-weight:700; margin:6px 0; font-family:'Noto Serif','SBL Hebrew','Gentium Plus',serif;">
                %7
            </div>
            <div style="font-size:0.95em;">
                <b>Translittération :</b> <i>%8</i> &nbsp;|&nbsp; <b>Prononciation :</b> <code>%9</code>
            </div>
            %10
        </div>
    )")
        .arg(style.cardBgColor, style.borderColor, style.accentColor, canonical,
             isHebrew ? "Hébreu / Araméen (AT)" : "Grec Koïné (NT)",
             clickedWord.isEmpty() ? "" : QString(" — « %1 »").arg(clickedWord.toHtmlEscaped()),
             sum.originalUnicode.isEmpty() ? canonical : sum.originalUnicode.toHtmlEscaped(),
             sum.transliteration.isEmpty() ? "—" : sum.transliteration.toHtmlEscaped(),
             sum.pronunciation.isEmpty() ? "—" : sum.pronunciation.toHtmlEscaped(),
             sum.shortFrenchGloss.isEmpty() ? "" : QString("<div style='margin-top:6px;'><b>Sens principal (LSG) :</b> %1</div>").arg(sum.shortFrenchGloss.toHtmlEscaped()));

    auto appendSection = [&](const QString& title, const QString& modBadge, const QString& bodyHtml) {
        if (bodyHtml.trimmed().isEmpty()) return;
        html += QString(R"(
            <div style="background:%1; border:1px solid %2; border-radius:8px; padding:10px 12px; margin-bottom:10px;">
                <div style="font-weight:700; color:%3; border-bottom:1px solid %2; padding-bottom:4px; margin-bottom:8px;">
                    %4 <span style="font-size:0.8em; font-weight:normal; opacity:0.8;">[%5]</span>
                </div>
                <div>%6</div>
            </div>
        )").arg(style.cardBgColor, style.borderColor, style.accentColor, title, modBadge, bodyHtml);
    };

    // 1. Dictionnaire Strong en Français (Fr_Hebreu ou Fr_Greek)
    QString frModName = isHebrew ? "Fr_Hebreu" : "Fr_Greek";
    if (SWModule* frMod = m_mgr->getModule(frModName.toUtf8().constData())) {
        frMod->setKey(key5.toUtf8().constData());
        QString entry = QString::fromUtf8(frMod->renderText().c_str());
        if (QString::fromUtf8(frMod->getKeyText()) == key5 && !entry.trimmed().isEmpty()) {
            entry.replace(QRegularExpression(R"(Voir définition\s+(\d+))"),
                          QString(R"(Voir définition <a href="app://word?strong=%1\1">%1\1</a>)").arg(isHebrew ? "H" : "G"));
            appendSection("🇫🇷 Dictionnaire Strong en Français", frModName, entry);
        }
    }

    // 2. Morphologie (si présente)
    if (!morphCode.trimmed().isEmpty()) {
        QString mc = morphCode.trimmed();
        QString morphDigits = mc;
        morphDigits.remove(QRegularExpression("[^0-9]"));
        if (!morphDigits.isEmpty() && morphDigits.toInt() >= 5000) {
            QString mKey5 = normalizeStrongKey5(morphDigits);
            if (SWModule* frMod = m_mgr->getModule(frModName.toUtf8().constData())) {
                frMod->setKey(mKey5.toUtf8().constData());
                QString mEntry = QString::fromUtf8(frMod->renderText().c_str());
                if (QString::fromUtf8(frMod->getKeyText()) == mKey5 && !mEntry.trimmed().isEmpty()) {
                    appendSection("🔤 Conjugaison / Morphologie (Français)",
                                  QString("%1 : %2").arg(frModName, mc),
                                  mEntry);
                }
            }
        }
        for (const char* mdict : {"Robinson", "Packard", "OSHM"}) {
            if (SWModule* rm = m_mgr->getModule(mdict)) {
                QString cleanKey = mc;
                if (cleanKey.contains(':')) cleanKey = cleanKey.section(':', -1);
                rm->setKey(cleanKey.toUtf8().constData());
                QString rEntry = QString::fromUtf8(rm->renderText().c_str());
                if (QString::fromUtf8(rm->getKeyText()).compare(cleanKey, Qt::CaseInsensitive) == 0 && !rEntry.trimmed().isEmpty()) {
                    appendSection("🔤 Analyse Grammaticale", QString("%1 : %2").arg(mdict, cleanKey), rEntry);
                }
            }
        }
    }

    // 3. Zodhiates (NT)
    if (!isHebrew) {
        if (SWModule* zod = m_mgr->getModule("Strong Zodiates (NT)")) {
            zod->setKey(key5.toUtf8().constData());
            QString ztxt = QString::fromUtf8(zod->renderText().c_str());
            if (QString::fromUtf8(zod->getKeyText()) == key5 && !ztxt.trimmed().isEmpty()) {
                appendSection("📘 Étude Lexicale Spiros Zodhiates (NT)", "Strong Zodiates (NT)", ztxt);
            }
        }
    }

    // 4. BDB / Strongs / MLStrong
    if (isHebrew) {
        if (SWModule* bdb = m_mgr->getModule("BDBGlosses_Strongs")) {
            QString bdbKey = "H" + key4;
            bdb->setKey(bdbKey.toUtf8().constData());
            QString bdbTxt = QString::fromUtf8(bdb->renderText().c_str());
            if (QString::fromUtf8(bdb->getKeyText()).compare(bdbKey, Qt::CaseInsensitive) == 0 && !bdbTxt.trimmed().isEmpty()) {
                appendSection("🏛️ Lexique Hébreu Brown-Driver-Briggs (BDB)", "BDBGlosses_Strongs", bdbTxt);
            }
        }
        if (SWModule* sh = m_mgr->getModule("StrongsHebrew")) {
            sh->setKey(key5.toUtf8().constData());
            QString shTxt = QString::fromUtf8(sh->renderText().c_str());
            if (QString::fromUtf8(sh->getKeyText()) == key5 && !shTxt.trimmed().isEmpty()) {
                appendSection("📖 Strong's Hebrew Dictionary", "StrongsHebrew", shTxt);
            }
        }
    } else {
        if (SWModule* sg = m_mgr->getModule("StrongsGreek")) {
            sg->setKey(key5.toUtf8().constData());
            QString sgTxt = QString::fromUtf8(sg->renderText().c_str());
            if (QString::fromUtf8(sg->getKeyText()) == key5 && !sgTxt.trimmed().isEmpty()) {
                appendSection("📖 Strong's Greek Dictionary", "StrongsGreek", sgTxt);
            }
        }
        if (SWModule* mls = m_mgr->getModule("MLStrong")) {
            mls->setKey(key5.toUtf8().constData());
            QString mlsTxt = QString::fromUtf8(mls->renderText().c_str());
            if (QString::fromUtf8(mls->getKeyText()) == key5 && !mlsTxt.trimmed().isEmpty()) {
                appendSection("🏛️ Liddell & Scott Greek Lexicon", "MLStrong", mlsTxt);
            }
        }
    }

    html += "</body></html>";
    return html;
}

QString SwordEngine::lookupDictionaryKeyHtml(const QString& dictModule, const QString& keyText, const PaneStyle& style) {
    if (!m_mgr) return {};
    SWModule* mod = m_mgr->getModule(dictModule.toUtf8().constData());
    if (!mod) return "<p>Module dictionnaire introuvable.</p>";

    QString q = keyText.trimmed();
    if (!q.isEmpty()) {
        mod->setKey(q.toUtf8().constData());
    }
    QString body = QString::fromUtf8(mod->renderText().c_str());
    QString actualKey = QString::fromUtf8(mod->getKeyText());
    body = linkifyScriptureRefsInHtml(body, "John 1:1");

    return QString(R"(<!DOCTYPE html><html><head><meta charset="utf-8"><style>
        body { %1 margin: 8px 10px; }
        a { color: %2; text-decoration: none; }
    </style></head><body>
        <div style="background:%3; border:1px solid %4; border-radius:8px; padding:12px;">
            <h3 style="color:%2; margin-top:0;">[%5] %6</h3>
            <div>%7</div>
        </div>
    </body></html>)").arg(buildBodyCss(style), style.accentColor, style.cardBgColor, style.borderColor,
                          dictModule.toHtmlEscaped(), actualKey.toHtmlEscaped(), body);
}

QStringList SwordEngine::getDictionarySampleKeys(const QString& dictModule, const QString& prefixFilter, int maxCount) {
    QStringList keys;
    if (!m_mgr) return keys;
    SWModule* mod = m_mgr->getModule(dictModule.toUtf8().constData());
    if (!mod) return keys;

    QString pf = prefixFilter.trimmed().toUpper();
    if (pf.isEmpty()) {
        mod->setPosition(TOP);
    } else {
        mod->setKey(pf.toUtf8().constData());
    }

    for (int i = 0; i < maxCount && !mod->popError(); ++i) {
        mod->renderText();
        QString k = QString::fromUtf8(mod->getKeyText());
        if (!k.isEmpty()) keys.push_back(k);
        (*mod)++;
    }
    keys.removeDuplicates();
    return keys;
}

QString SwordEngine::linkifyScriptureRefsInHtml(const QString& html, const QString& contextKey) const {
    QString out = html;
    static const QRegularExpression rxHref(R"(href="passagestudy\.jsp\?action=showRef&type=scripRef&value=([^"&]+)[^"]*")");
    out.replace(rxHref, R"(href="app://ref/\1")");

    static const QRegularExpression rxHashRef(R"(#([1-3]?[A-Za-z]+\s+\d+(?::\d+(?:-\d+)?)?))");
    out.replace(rxHashRef, R"(<a href="app://ref/\1">#\1</a>)");
    Q_UNUSED(contextKey);
    return out;
}

QString SwordEngine::renderCommentaryHtml(const QString& commModule, const QString& swordKey, bool allFrenchCommentaries, const PaneStyle& style) {
    if (!m_mgr) return {};
    if (commModule == "Fr_Tsk" || commModule == "TSK") {
        return QString(R"(<!DOCTYPE html><html><head><meta charset="utf-8"><style>
            body { %1 margin: 8px 10px; }
            a { color: %2; text-decoration: none; }
        </style></head><body>%3</body></html>)")
            .arg(buildBodyCss(style), style.accentColor, renderTskHtml(swordKey, "FreLSG", style));
    }

    QString html = QString(R"(<!DOCTYPE html><html><head><meta charset="utf-8"><style>
        body { %1 margin: 8px 10px; }
        a { color: %2; text-decoration: none; }
    </style></head><body>)").arg(buildBodyCss(style), style.accentColor);

    QStringList modsToQuery;
    if (allFrenchCommentaries) {
        for (const auto& cm : m_commentaries) {
            if (cm.language == "fr" && cm.name != "Fr_Tsk") {
                modsToQuery.push_back(cm.name);
            }
        }
    } else if (!commModule.isEmpty()) {
        modsToQuery.push_back(commModule);
    }

    int foundCount = 0;
    for (const QString& mName : modsToQuery) {
        SWModule* mod = (mName == "Fr_Tsk" && m_frTskDirect) ? m_frTskDirect.get() : m_mgr->getModule(mName.toUtf8().constData());
        if (!mod) continue;
        mod->setKey(swordKey.toUtf8().constData());
        QString entry = QString::fromUtf8(mod->renderText().c_str()).trimmed();
        if (entry.isEmpty()) continue;

        foundCount++;
        entry = linkifyScriptureRefsInHtml(entry, swordKey);
        QString desc = getModuleDescription(mName);
        html += QString(R"(
            <div style="background:%1; border:1px solid %2; border-radius:8px; padding:10px 14px; margin-bottom:12px;">
                <div style="font-weight:700; color:%3; margin-bottom:6px; border-bottom:1px solid %2; padding-bottom:4px;">
                    📚 %4 — <span style="font-weight:normal;">%5 (%6)</span>
                </div>
                <div>%7</div>
            </div>
        )").arg(style.cardBgColor, style.borderColor, style.accentColor, mName.toHtmlEscaped(), desc.toHtmlEscaped(), toFrenchReference(swordKey).toHtmlEscaped(), entry);
    }

    QString tskSection = renderTskHtml(swordKey, "FreLSG", style);
    if (!tskSection.isEmpty()) {
        html += "<hr style='border:none; border-top:1px solid " + style.borderColor + "; margin:14px 0;'/>";
        html += tskSection;
    }

    if (foundCount == 0 && tskSection.isEmpty()) {
        html += QString("<p style='opacity:0.75;'><i>Aucune note de commentaire pour %1 dans ce module.</i></p>")
            .arg(toFrenchReference(swordKey).toHtmlEscaped());
    }

    html += "</body></html>";
    return html;
}

QString SwordEngine::getPlainCommentaryText(const QString& commModule, const QString& swordKey) const {
    if (!m_mgr || commModule.isEmpty()) return {};
    SWModule* mod = (commModule == "Fr_Tsk" && m_frTskDirect) ? m_frTskDirect.get() : m_mgr->getModule(commModule.toUtf8().constData());
    if (!mod) return {};
    mod->setKey(swordKey.toUtf8().constData());
    return cleanRawEntryToPlain(QString::fromUtf8(mod->renderText().c_str()));
}

QVector<TskTopicGroup> SwordEngine::getTskForVerse(const QString& swordKey, const QString& previewBible) const {
    QVector<TskTopicGroup> groups;
    if (!m_mgr) return groups;

    QString effBible = previewBible;
    if (effBible.isEmpty() || !m_mgr->getModule(effBible.toUtf8().constData())) {
        if (m_mgr->getModule("FreSegond")) effBible = "FreSegond";
        else if (m_mgr->getModule("FreLSG")) effBible = "FreLSG";
        else if (m_mgr->getModule("KJV")) effBible = "KJV";
        else if (!m_bibles.isEmpty()) effBible = m_bibles.first().name;
    }

    VerseKey vk(swordKey.toUtf8().constData());

    auto addParsedListKey = [&](ListKey& lk, QVector<TskRefItem>& outRefs) {
        for (int i = 0; i < lk.getCount(); ++i) {
            SWKey* elem = lk.getElement(i);
            if (!elem) continue;
            QString rangeTxt = QString::fromUtf8(elem->getRangeText());
            QString shortTxt = QString::fromUtf8(elem->getShortText());
            QString baseKey = rangeTxt.contains('-') ? rangeTxt.section('-', 0, 0).trimmed() : rangeTxt;
            if (baseKey.isEmpty()) baseKey = shortTxt;

            TskRefItem item;
            item.swordKey = baseKey;
            item.rangeKey = rangeTxt;
            item.frenchLabel = toFrenchReference(baseKey);
            if (rangeTxt.contains('-')) {
                QString endKey = rangeTxt.section('-', 1, 1).trimmed();
                QString endB; int endC = 1, endV = 1;
                splitSwordKey(endKey, endB, endC, endV);
                item.frenchLabel += QString("-%1").arg(endV);
            }
            item.versePreviewText = getCleanVerseText(effBible, baseKey);
            outRefs.push_back(item);
        }
    };

    if (m_frTskDirect) {
        m_frTskDirect->setKey(swordKey.toUtf8().constData());
        QString frHtml = QString::fromUtf8(m_frTskDirect->renderText().c_str());
        if (!frHtml.trimmed().isEmpty()) {
            QStringList lines = frHtml.split(QRegularExpression("<br\\s*/?>"), Qt::SkipEmptyParts);
            TskTopicGroup currentGroup;
            currentGroup.sourceModule = "Fr_Tsk";
            currentGroup.topicPhrase = uiTr("Références parallèles", "Cross-References", "Referencias paralelas");

            for (QString line : lines) {
                QString cleanLine = line.trimmed();
                if (cleanLine.isEmpty()) continue;
                if (cleanLine.startsWith('*')) {
                    if (!currentGroup.refs.isEmpty() || !currentGroup.summaryNote.isEmpty()) {
                        groups.push_back(currentGroup);
                    }
                    currentGroup = TskTopicGroup{};
                    currentGroup.sourceModule = "Fr_Tsk";
                    currentGroup.topicPhrase = cleanLine.mid(1).trimmed();
                } else if (cleanLine.startsWith('#')) {
                    QString refListStr = cleanLine.mid(1).trimmed();
                    refListStr.replace(' ', "; ");
                    ListKey lk = vk.parseVerseList(refListStr.toUtf8().constData(), swordKey.toUtf8().constData(), true);
                    addParsedListKey(lk, currentGroup.refs);
                } else {
                    if (!currentGroup.summaryNote.isEmpty()) currentGroup.summaryNote += " ";
                    currentGroup.summaryNote += cleanLine;
                }
            }
            if (!currentGroup.refs.isEmpty() || !currentGroup.summaryNote.isEmpty()) {
                groups.push_back(currentGroup);
            }
        }
    }

    if (SWModule* tskEn = m_mgr->getModule("TSK")) {
        tskEn->setKey(swordKey.toUtf8().constData());
        QString enHtml = QString::fromUtf8(tskEn->renderText().c_str());
        if (!enHtml.trimmed().isEmpty()) {
            TskTopicGroup enGroup;
            enGroup.sourceModule = "TSK";
            enGroup.topicPhrase = uiTr("Passages parallèles complémentaires (TSK)",
                                       "Additional Cross-References (TSK)",
                                       "Pasajes paralelos complementarios (TSK)");

            static const QRegularExpression rxVal(R"(action=showRef&type=scripRef&value=([^"&]+))");
            auto it = rxVal.globalMatch(enHtml);
            QSet<QString> seenKeys;
            for (const auto& g : groups) {
                for (const auto& r : g.refs) seenKeys.insert(r.swordKey);
            }

            while (it.hasNext() && enGroup.refs.size() < 25) {
                auto m = it.next();
                QString rawRef = QUrl::fromPercentEncoding(m.captured(1).toUtf8()).replace('+', ' ');
                ListKey lk = vk.parseVerseList(rawRef.toUtf8().constData(), swordKey.toUtf8().constData(), true);
                for (int i = 0; i < lk.getCount(); ++i) {
                    SWKey* elem = lk.getElement(i);
                    if (!elem) continue;
                    QString baseKey = QString::fromUtf8(elem->getRangeText()).section('-', 0, 0).trimmed();
                    if (seenKeys.contains(baseKey)) continue;
                    seenKeys.insert(baseKey);

                    TskRefItem item;
                    item.swordKey = baseKey;
                    item.rangeKey = QString::fromUtf8(elem->getRangeText());
                    item.frenchLabel = toFrenchReference(baseKey);
                    item.versePreviewText = getCleanVerseText(effBible, baseKey);
                    enGroup.refs.push_back(item);
                }
            }
            if (!enGroup.refs.isEmpty()) {
                groups.push_back(enGroup);
            }
        }
    }

    return groups;
}

QString SwordEngine::renderTskHtml(const QString& swordKey, const QString& previewBible, const PaneStyle& style) const {
    auto groups = getTskForVerse(swordKey, previewBible);
    if (groups.isEmpty()) return {};

    QString titleText = uiTr("🔗 Versets Parallèles TSK (%1)",
                             "🔗 TSK Cross-References (%1)",
                             "🔗 Versículos Paralelos TSK (%1)")
                        .arg(toFrenchReference(swordKey).toHtmlEscaped());
    QString hintText = uiTr("💡 Clic gauche : ouvrir dans la fenêtre par défaut  •  Clic droit : choisir dans quelle fenêtre ouvrir (Fenêtre 1, 2, 3, 4...)",
                            "💡 Left-click: open in default window  •  Right-click: choose which window to open in (Window 1, 2, 3, 4...)",
                            "💡 Clic izquierdo: abrir en ventana por defecto  •  Clic derecho: elegir en qué ventana abrir (Ventana 1, 2, 3, 4...)");

    QString html = QString(R"(<div><div style="margin-bottom:3px; font-weight:700; color:%1;">%2</div><div style="font-size:0.84em; opacity:0.80; margin-bottom:8px;">%3</div>)")
        .arg(style.accentColor, titleText, hintText);

    for (const auto& g : groups) {
        html += QString(R"(<div style="background:%1; border:1px solid %2; border-radius:8px; padding:10px 12px; margin-bottom:10px;">)")
            .arg(style.cardBgColor, style.borderColor);
        html += QString(R"(<div style="font-weight:700; color:%1; margin-bottom:6px;">✦ %2 <span style="font-size:0.8em; font-weight:normal; opacity:0.75;">[%3]</span></div>)")
            .arg(style.accentColor, g.topicPhrase.toHtmlEscaped(), g.sourceModule.toHtmlEscaped());

        if (!g.summaryNote.isEmpty()) {
            html += QString(R"(<div style="font-size:0.9em; opacity:0.9; margin-bottom:8px; font-style:italic;">%1</div>)")
                .arg(g.summaryNote.toHtmlEscaped());
        }

        for (const auto& r : g.refs) {
            QUrl u("app://ref/" + r.swordKey);
            QString previewPart = r.versePreviewText.isEmpty()
                ? QString()
                : QString(" <span>— %1</span>").arg(r.versePreviewText.toHtmlEscaped());
            html += QString(R"(
                <div style="border-left:3px solid %1; padding-left:8px; margin:6px 0;">
                    <a href="%2" style="color:%1; font-weight:700; text-decoration:none;">%3</a>%4
                </div>
            )").arg(style.accentColor, u.toString(), r.frenchLabel.toHtmlEscaped(), previewPart);
        }
        html += "</div>";
    }

    html += "</div>";
    return html;
}

QVector<SearchHit> SwordEngine::searchBible(
    const QString& bibleModule,
    const QString& query,
    int scopeMode,
    const QString& currentBookSword,
    int maxResults
) {
    QVector<SearchHit> hits;
    QString q = query.trimmed();
    if (!m_mgr || q.isEmpty() || bibleModule.isEmpty()) return hits;

    static const QRegularExpression rxStrongQuery(R"(^([GHgh])(\d+)$)");
    auto sm = rxStrongQuery.match(q);
    bool isStrongSearch = sm.hasMatch();
    QString strongNumberStr;
    bool strongIsHeb = false;
    if (isStrongSearch) {
        strongIsHeb = sm.captured(1).toUpper() == "H";
        strongNumberStr = QString::number(sm.captured(2).toInt());
    }

    SWModule* searchMod = isStrongSearch ? m_mgr->getModule(bibleModule.toUtf8().constData())
                                         : m_plainMgr->getModule(bibleModule.toUtf8().constData());
    if (!searchMod) return hits;

    QStringList queryWords = q.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    for (const auto& book : m_books) {
        if (scopeMode == 1 && book.testament != 1) continue;
        if (scopeMode == 2 && book.testament != 2) continue;
        if (scopeMode == 3 && book.swordName != currentBookSword) continue;
        if (isStrongSearch && strongIsHeb && book.testament != 1) continue;
        if (isStrongSearch && !strongIsHeb && book.testament != 2 && bibleModule != "LXX") continue;

        for (int ch = 1; ch <= book.chapterCount; ++ch) {
            int vmax = getVerseCount(book.swordName, ch);
            for (int v = 1; v <= vmax; ++v) {
                QString key = QString("%1 %2:%3").arg(book.swordName).arg(ch).arg(v);
                searchMod->setKey(key.toUtf8().constData());
                QString raw = QString::fromUtf8(searchMod->getRawEntry());
                if (raw.isEmpty()) continue;

                bool matched = false;
                if (isStrongSearch) {
                    QRegularExpression rxTag(QString(R"((?:<W[GH]0*%1>|strong:[GH]0*%1\b))").arg(strongNumberStr));
                    matched = rxTag.match(raw).hasMatch();
                } else {
                    QString strip = QString::fromUtf8(searchMod->stripText());
                    matched = true;
                    for (const QString& qw : queryWords) {
                        if (!strip.contains(qw, Qt::CaseInsensitive) &&
                            !normalizeForMatch(strip).contains(normalizeForMatch(qw))) {
                            matched = false;
                            break;
                        }
                    }
                }

                if (matched) {
                    SearchHit hit;
                    hit.swordKey = key;
                    hit.frenchRef = QString("%1 %2:%3").arg(book.frenchName).arg(ch).arg(v);
                    hit.plainText = getCleanVerseText(bibleModule, key);
                    QString hl = hit.plainText.toHtmlEscaped();
                    if (!isStrongSearch) {
                        for (const QString& qw : queryWords) {
                            if (qw.size() >= 2) {
                                QRegularExpression rxHl(QString("(%1)").arg(QRegularExpression::escape(qw)),
                                                        QRegularExpression::CaseInsensitiveOption);
                                hl.replace(rxHl, "<b style='color:#D97706;'>\\1</b>");
                            }
                        }
                    }
                    hit.snippetHtml = hl;
                    hits.push_back(hit);
                    if (hits.size() >= maxResults) return hits;
                }
            }
        }
    }

    return hits;
}

QString SwordEngine::buildGeminiStudyContext(
    const QString& swordKey,
    const QStringList& openBibles,
    const QString& activeStrongCode,
    const QString& clickedWord,
    const QString& activeCommModule
) {
    QStringList ctx;
    ctx.push_back(QString("=== PASSAGE BIBLIQUE ÉTUDIÉ : %1 (%2) ===")
        .arg(toFrenchReference(swordKey), swordKey));

    ctx.push_back("\n[TRADUCTIONS OUVERTES]");
    for (const QString& bmod : openBibles) {
        if (bmod.isEmpty()) continue;
        QString txt = getCleanVerseText(bmod, swordKey);
        if (!txt.isEmpty()) {
            ctx.push_back(QString("- %1 (%2) : « %3 »").arg(bmod, getModuleDescription(bmod), txt));
        }
    }

    QString origMod;
    QString origTxt = getOriginalVerseText(swordKey, &origMod);
    if (!origTxt.isEmpty()) {
        ctx.push_back(QString("\n[TEXTE ORIGINAL - %1]\n%2").arg(origMod, origTxt));
    }

    auto tokens = getVerseWordTokens(swordKey, "FreLSG");
    if (!tokens.isEmpty()) {
        ctx.push_back("\n[MOTS ORIGINAUX DU VERSET (STRONG & MORPHOLOGIE)]");
        for (const auto& tk : tokens) {
            if (tk.strongCode.isEmpty()) continue;
            ctx.push_back(QString("- « %1 » -> %2 (%3 / %4) Morph:%5 : %6")
                .arg(tk.surfaceText.trimmed(), tk.strongCode, tk.originalUnicode,
                     tk.transliteration, tk.morphCode.isEmpty() ? "—" : tk.morphCode, tk.shortFrenchGloss));
        }
    }

    if (!activeStrongCode.isEmpty() || !clickedWord.isEmpty()) {
        StrongSummary s = getStrongSummary(activeStrongCode);
        ctx.push_back(QString("\n[MOT CIBLÉ PAR LE CLIC DE L'UTILISATEUR : « %1 » — Strong %2 — %3 (%4) : %5]")
            .arg(clickedWord, s.code, s.originalUnicode, s.transliteration, s.shortFrenchGloss));
    }

    if (!activeCommModule.isEmpty()) {
        QString commTxt = getPlainCommentaryText(activeCommModule, swordKey);
        if (!commTxt.isEmpty()) {
            ctx.push_back(QString("\n[EXTRAIT DU COMMENTAIRE %1]\n%2")
                .arg(activeCommModule, commTxt.left(1000)));
        }
    }

    return ctx.join("\n");
}

// ============================================================================
// NOTES PERSONNELLES INDEXÉES PAR VERSET
// ============================================================================
QString SwordEngine::personalNotesFilePath() const {
    QString dir = QDir::homePath() + "/.config/LogosLumiere/BibleStudyStudio";
    QDir().mkpath(dir);
    return dir + "/personal_notes.json";
}

void SwordEngine::loadPersonalNotes() {
    m_personalNotes.clear();
    QFile f(personalNotesFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    QJsonArray arr = root.value("notes").toArray();
    for (const auto& val : arr) {
        if (!val.isObject()) continue;
        QJsonObject o = val.toObject();
        PersonalNote n;
        n.swordKey = o.value("swordKey").toString();
        n.frenchRef = o.value("frenchRef").toString();
        n.bibleModule = o.value("bibleModule").toString("FreSegond");
        n.verseText = o.value("verseText").toString();
        n.htmlBody = o.value("htmlBody").toString();
        n.excerptCount = o.value("excerptCount").toInt(0);
        n.updatedAt = o.value("updatedAt").toString();
        if (!n.swordKey.isEmpty() && !n.htmlBody.trimmed().isEmpty()) {
            m_personalNotes.insert(n.swordKey, n);
        }
    }
}

void SwordEngine::savePersonalNotes() const {
    QJsonArray arr;
    for (auto it = m_personalNotes.constBegin(); it != m_personalNotes.constEnd(); ++it) {
        const PersonalNote& n = it.value();
        if (n.swordKey.isEmpty() || n.htmlBody.trimmed().isEmpty()) continue;
        QJsonObject o;
        o["swordKey"] = n.swordKey;
        o["frenchRef"] = n.frenchRef;
        o["bibleModule"] = n.bibleModule;
        o["verseText"] = n.verseText;
        o["htmlBody"] = n.htmlBody;
        o["excerptCount"] = n.excerptCount;
        o["updatedAt"] = n.updatedAt;
        arr.append(o);
    }

    QJsonObject root;
    root["notes"] = arr;
    QFile f(personalNotesFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

bool SwordEngine::hasPersonalNote(const QString& swordKey) const {
    QString b; int c = 1, v = 1;
    splitSwordKey(swordKey, b, c, v);
    QString normKey = QString("%1 %2:%3").arg(b).arg(c).arg(v);
    return m_personalNotes.contains(normKey) && !m_personalNotes.value(normKey).htmlBody.trimmed().isEmpty();
}

PersonalNote SwordEngine::getPersonalNote(const QString& swordKey) const {
    QString b; int c = 1, v = 1;
    splitSwordKey(swordKey, b, c, v);
    QString normKey = QString("%1 %2:%3").arg(b).arg(c).arg(v);
    return m_personalNotes.value(normKey);
}

QVector<PersonalNote> SwordEngine::getAllPersonalNotes() const {
    QVector<PersonalNote> list;
    list.reserve(m_personalNotes.size());
    for (auto it = m_personalNotes.constBegin(); it != m_personalNotes.constEnd(); ++it) {
        if (!it.value().htmlBody.trimmed().isEmpty()) {
            list.push_back(it.value());
        }
    }
    std::sort(list.begin(), list.end(), [this](const PersonalNote& a, const PersonalNote& b) {
        QString ba, bb; int ca = 1, va = 1, cb = 1, vb = 1;
        splitSwordKey(a.swordKey, ba, ca, va);
        splitSwordKey(b.swordKey, bb, cb, vb);
        const BookInfo* ia = findBookBySwordName(ba);
        const BookInfo* ib = findBookBySwordName(bb);
        int idxA = ia ? ia->index : 999;
        int idxB = ib ? ib->index : 999;
        if (idxA != idxB) return idxA < idxB;
        if (ca != cb) return ca < cb;
        return va < vb;
    });
    return list;
}

QString SwordEngine::buildInitialNoteHeaderHtml(const QString& swordKey, const QString& preferredBibleMod) const {
    QString b; int c = 1, v = 1;
    splitSwordKey(swordKey, b, c, v);
    QString normKey = QString("%1 %2:%3").arg(b).arg(c).arg(v);
    QString frRef = toFrenchReference(normKey);

    QString mod = preferredBibleMod.isEmpty() ? "FreSegond" : preferredBibleMod;
    QString vtxt = getCleanVerseText(mod, normKey);
    if (vtxt.isEmpty()) {
        mod = "FreSegond";
        vtxt = getCleanVerseText(mod, normKey);
    }
    if (vtxt.isEmpty()) {
        mod = "FreLSG";
        vtxt = getCleanVerseText(mod, normKey);
    }

    QString origMod;
    QString origTxt = getOriginalVerseText(normKey, &origMod);
    QString origLine;
    if (!origTxt.isEmpty()) {
        origLine = QString("<div style='margin-top:6px; font-size:10.5pt; color:#94A3B8;'>🏛️ <b>%1 :</b> %2</div>")
            .arg(origMod.toHtmlEscaped(), origTxt.toHtmlEscaped());
    }

    return QString(R"(
        <table width="100%" cellspacing="0" cellpadding="12" bgcolor="#1E293B" style="border: 2px solid #38BDF8; border-radius: 8px; margin-bottom: 12px;">
            <tr><td>
                <div style="font-size: 14pt; font-weight: bold; color: #38BDF8;">
                    📌 FICHE D'ÉTUDE PERSONNELLE — %1
                    <span style="font-size: 9.5pt; font-weight: normal; color: #94A3B8;">(%2)</span>
                </div>
                <div style="margin-top: 8px; font-size: 11.5pt; font-style: italic; color: #F8FAFC;">
                    « %3 »
                </div>
                %4
            </td></tr>
        </table>
    )").arg(frRef.toHtmlEscaped(), mod.toHtmlEscaped(), vtxt.toHtmlEscaped(), origLine);
}

static void appendHtmlBeforeBodyEnd(QString& targetHtml, const QString& snippetHtml) {
    int bodyEndIdx = targetHtml.lastIndexOf("</body>", -1, Qt::CaseInsensitive);
    if (bodyEndIdx >= 0) {
        targetHtml.insert(bodyEndIdx, snippetHtml);
    } else {
        targetHtml += snippetHtml;
    }
}

void SwordEngine::appendExcerptToPersonalNote(
    const QString& swordKey,
    const QString& sourceTitle,
    const QString& excerptText,
    const QString& preferredBibleMod
) {
    QString b; int c = 1, v = 1;
    splitSwordKey(swordKey, b, c, v);
    QString normKey = QString("%1 %2:%3").arg(b).arg(c).arg(v);
    QString frRef = toFrenchReference(normKey);
    QString nowStr = QDateTime::currentDateTime().toString("dd/MM/yyyy à HH:mm");

    PersonalNote note = m_personalNotes.value(normKey);
    if (note.swordKey.isEmpty() || note.htmlBody.trimmed().isEmpty()) {
        note.swordKey = normKey;
        note.frenchRef = frRef;
        note.bibleModule = preferredBibleMod.isEmpty() ? "FreSegond" : preferredBibleMod;
        note.verseText = getCleanVerseText(note.bibleModule, normKey);
        if (note.verseText.isEmpty()) note.verseText = getCleanVerseText("FreSegond", normKey);
        note.htmlBody = buildInitialNoteHeaderHtml(normKey, note.bibleModule);
        note.excerptCount = 0;
    }

    note.excerptCount += 1;
    note.updatedAt = nowStr;

    // Formatage très lisible de l'extrait sélectionné (préserve les paragraphes)
    QString formattedExcerpt = excerptText.trimmed().toHtmlEscaped();
    formattedExcerpt.replace("\n\n", "</p><p style='margin:6px 0;'>");
    formattedExcerpt.replace("\n", "<br>");

    QString blockHtml = QString(R"(
        <table width="100%" cellspacing="0" cellpadding="11" bgcolor="#162032" style="border: 1px solid #334155; border-left: 4px solid #F59E0B; margin-top: 10px; margin-bottom: 10px;">
            <tr><td>
                <div style="font-size: 11pt; font-weight: bold; color: #FBBF24; margin-bottom: 6px;">
                    ✦ Extrait #%1 — %2
                    <span style="font-size: 9pt; font-weight: normal; color: #94A3B8;">(ajouté le %3)</span>
                </div>
                <div style="color: #E8ECF2; margin-bottom: 10px; line-height: 1.55;">
                    <p style="margin: 4px 0;">%4</p>
                </div>
                <hr style="border: 0; border-top: 1px solid #334155; margin: 6px 0;">
                <div style="color: #34D399; font-weight: bold; margin-top: 4px;">
                    ✍️ Mon annotation : <span style="font-weight: normal; color: #F8FAFC;">Écrivez vos remarques personnelles ici...</span>
                </div>
            </td></tr>
        </table>
        <p><br></p>
    )").arg(note.excerptCount)
       .arg(sourceTitle.toHtmlEscaped())
       .arg(nowStr.toHtmlEscaped())
       .arg(formattedExcerpt);

    appendHtmlBeforeBodyEnd(note.htmlBody, blockHtml);
    m_personalNotes.insert(normKey, note);
    savePersonalNotes();
}

void SwordEngine::appendAnnotationBlockToPersonalNote(
    const QString& swordKey,
    const QString& preferredBibleMod
) {
    QString b; int c = 1, v = 1;
    splitSwordKey(swordKey, b, c, v);
    QString normKey = QString("%1 %2:%3").arg(b).arg(c).arg(v);
    QString frRef = toFrenchReference(normKey);
    QString nowStr = QDateTime::currentDateTime().toString("dd/MM/yyyy à HH:mm");

    PersonalNote note = m_personalNotes.value(normKey);
    if (note.swordKey.isEmpty() || note.htmlBody.trimmed().isEmpty()) {
        note.swordKey = normKey;
        note.frenchRef = frRef;
        note.bibleModule = preferredBibleMod.isEmpty() ? "FreSegond" : preferredBibleMod;
        note.verseText = getCleanVerseText(note.bibleModule, normKey);
        if (note.verseText.isEmpty()) note.verseText = getCleanVerseText("FreSegond", normKey);
        note.htmlBody = buildInitialNoteHeaderHtml(normKey, note.bibleModule);
        note.excerptCount = 0;
    }

    note.updatedAt = nowStr;

    QString blockHtml = QString(R"(
        <table width="100%" cellspacing="0" cellpadding="10" bgcolor="#132328" style="border: 1px solid #059669; border-left: 4px solid #10B981; margin-top: 10px; margin-bottom: 10px;">
            <tr><td>
                <div style="font-size: 11pt; font-weight: bold; color: #34D399; margin-bottom: 6px;">
                    ✍️ Note & Réflexion personnelle <span style="font-size: 9pt; font-weight: normal; color: #94A3B8;">(%1)</span>
                </div>
                <div style="color: #F8FAFC;">
                    Écrivez votre note ici...
                </div>
            </td></tr>
        </table>
        <p><br></p>
    )").arg(nowStr.toHtmlEscaped());

    appendHtmlBeforeBodyEnd(note.htmlBody, blockHtml);
    m_personalNotes.insert(normKey, note);
    savePersonalNotes();
}

void SwordEngine::saveEditedPersonalNoteHtml(
    const QString& swordKey,
    const QString& editedHtml,
    const QString& preferredBibleMod
) {
    QString b; int c = 1, v = 1;
    splitSwordKey(swordKey, b, c, v);
    QString normKey = QString("%1 %2:%3").arg(b).arg(c).arg(v);
    QString frRef = toFrenchReference(normKey);

    PersonalNote note = m_personalNotes.value(normKey);
    note.swordKey = normKey;
    note.frenchRef = frRef;
    if (note.bibleModule.isEmpty()) {
        note.bibleModule = preferredBibleMod.isEmpty() ? "FreSegond" : preferredBibleMod;
    }
    if (note.verseText.isEmpty()) {
        note.verseText = getCleanVerseText(note.bibleModule, normKey);
    }
    note.htmlBody = editedHtml;
    note.updatedAt = QDateTime::currentDateTime().toString("dd/MM/yyyy à HH:mm");
    m_personalNotes.insert(normKey, note);
    savePersonalNotes();
}

void SwordEngine::deletePersonalNote(const QString& swordKey) {
    QString b; int c = 1, v = 1;
    splitSwordKey(swordKey, b, c, v);
    QString normKey = QString("%1 %2:%3").arg(b).arg(c).arg(v);
    m_personalNotes.remove(normKey);
    savePersonalNotes();
}
