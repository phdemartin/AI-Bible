#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QHash>
#include <QJsonObject>
#include <memory>

namespace sword {
class SWMgr;
class SWModule;
class RawCom;
}

// Style visuel configurable par la petite roue dentée (⚙) de chaque module
struct PaneStyle {
    int fontSizePt = 15;
    int lineSpacingPct = 165;
    bool highContrastBold = false;
    bool useSerifFont = true;
    QString presetName = "Sombre Doux";
    QString bgColor = "#141821";
    QString textColor = "#E8ECF2";
    QString cardBgColor = "#1E293B";
    QString accentColor = "#38BDF8";
    QString borderColor = "#334155";

    static PaneStyle preset(const QString& name, int keepFontSize = 15);
    QJsonObject toJson() const;
    static PaneStyle fromJson(const QJsonObject& obj, int defaultFontSize = 15);
};

struct ModuleInfo {
    QString name;
    QString description;
    QString type;       // "Biblical Texts", "Commentaries", "Lexicons / Dictionaries"
    QString language;   // "fr", "en", "es", "grc", "he", etc.
    bool hasStrongs = false;
    bool hasMorph = false;
};

struct BookInfo {
    int index = 0;          // 1..66
    int testament = 1;      // 1 = AT, 2 = NT
    QString swordName;      // e.g. "Genesis", "John"
    QString osisName;       // e.g. "Gen", "John"
    QString frenchName;     // e.g. "Genèse", "Jean"
    QString frenchAbbrev;   // e.g. "Gn", "Jn"
    QString category;
    int chapterCount = 1;
};

struct WordToken {
    QString surfaceText;
    QString strongCode;
    QString morphCode;
    QString morphClass;
    QString originalUnicode;
    QString transliteration;
    QString pronunciation;
    QString shortFrenchGloss;
};

struct StrongSummary {
    QString code;
    QString originalUnicode;
    QString transliteration;
    QString pronunciation;
    QString shortFrenchGloss;
};

struct TskRefItem {
    QString swordKey;
    QString rangeKey;
    QString frenchLabel;
    QString versePreviewText;
};

struct TskTopicGroup {
    QString sourceModule;
    QString topicPhrase;
    QString summaryNote;
    QVector<TskRefItem> refs;
};

struct SearchHit {
    QString swordKey;
    QString frenchRef;
    QString snippetHtml;
    QString plainText;
};

struct PersonalNote {
    QString swordKey;       // e.g. "John 11:28"
    QString frenchRef;      // e.g. "Jean 11:28"
    QString bibleModule;    // e.g. "FreSegond"
    QString verseText;      // Texte du verset en en-tête
    QString htmlBody;       // Contenu HTML structuré (blocs d'extraits + annotations)
    int excerptCount = 0;   // Nombre d'extraits ajoutés
    QString updatedAt;      // Date/heure de mise à jour
};

class SwordEngine {
public:
    explicit SwordEngine(const QString& swordPath = QString());
    ~SwordEngine();

    // Module discovery & hot-reload after installing/removing SWORD modules
    void reloadModules();
    QString swordPath() const { return m_swordPath; }
    sword::SWMgr* rawMgr() const { return m_mgr.get(); }
    QVector<ModuleInfo> getBibleModules() const { return m_bibles; }
    QVector<ModuleInfo> getCommentaryModules() const { return m_commentaries; }
    QVector<ModuleInfo> getLexiconModules() const { return m_lexicons; }
    QString getModuleDescription(const QString& modName) const;

    // Books & Navigation
    const QVector<BookInfo>& getBooks() const { return m_books; }
    const BookInfo* findBookBySwordName(const QString& swordName) const;
    const BookInfo* findBookByAnyName(const QString& query) const;
    int getChapterCount(const QString& swordBookName) const;
    int getVerseCount(const QString& swordBookName, int chapter) const;

    // Reference conversion ("Jean 1:1" <-> "John 1:1")
    QString parseUserReference(const QString& input, int* outChapter = nullptr, int* outVerse = nullptr, QString* outBookSword = nullptr) const;
    bool tryParseStrictReference(const QString& input, QString& outSwordKey, QString* outBookSword = nullptr, int* outChapter = nullptr, int* outVerse = nullptr) const;
    QString toFrenchReference(const QString& swordKey) const;
    void splitSwordKey(const QString& swordKey, QString& outBookSword, int& outChapter, int& outVerse) const;

    // Clean Bible chapter rendering with full PaneStyle (font size, contrast, colors)
    QString renderChapterHtml(
        const QString& moduleName,
        const QString& swordBook,
        int chapter,
        int activeVerse,
        const PaneStyle& style
    );

    // Clean plain text extraction
    QString getCleanVerseText(const QString& moduleName, const QString& swordKey) const;

    // Original Greek/Hebrew & Word-by-Word analysis for active verse
    QString getOriginalVerseText(const QString& swordKey, QString* outModuleUsed = nullptr) const;
    QVector<WordToken> getVerseWordTokens(const QString& swordKey, const QString& preferredBible = "FreLSG");
    StrongSummary getStrongSummary(const QString& rawStrongCode);

    // Find best matching Strong token when user clicks a word in any Bible (FR, EN, or ES)
    WordToken resolveClickedWordToToken(
        const QString& swordKey,
        const QString& clickedWord,
        const QString& explicitStrongCode,
        const QString& explicitMorphCode,
        int wordIndexInVerse
    );

    // Rich Greek / Hebrew Lexicon HTML (styled with PaneStyle)
    QString lookupStrongAndMorphHtml(
        const QString& swordKey,
        const QString& strongCode,
        const QString& morphCode,
        const QString& clickedWord,
        const PaneStyle& style
    );
    QString lookupDictionaryKeyHtml(const QString& dictModule, const QString& keyText, const PaneStyle& style);
    QStringList getDictionarySampleKeys(const QString& dictModule, const QString& prefixFilter, int maxCount = 80);

    // Commentaries & TSK (styled with PaneStyle)
    QString renderCommentaryHtml(const QString& commModule, const QString& swordKey, bool allFrenchCommentaries, const PaneStyle& style);
    QString getPlainCommentaryText(const QString& commModule, const QString& swordKey) const;
    QVector<TskTopicGroup> getTskForVerse(const QString& swordKey, const QString& previewBible = "FreLSG") const;
    QString renderTskHtml(const QString& swordKey, const QString& previewBible, const PaneStyle& style) const;

    // Fast Bible & Strong Search
    QVector<SearchHit> searchBible(
        const QString& bibleModule,
        const QString& query,
        int scopeMode,
        const QString& currentBookSword,
        int maxResults = 250
    );

    // Context builder for Gemini AI
    QString buildGeminiStudyContext(
        const QString& swordKey,
        const QStringList& openBibles,
        const QString& activeStrongCode,
        const QString& clickedWord,
        const QString& activeCommModule
    );

    // Personal Study Notes indexed by verse ("John 11:28" -> "Jean 11:28")
    void loadPersonalNotes();
    void savePersonalNotes() const;
    QString personalNotesFilePath() const;
    bool hasPersonalNote(const QString& swordKey) const;
    PersonalNote getPersonalNote(const QString& swordKey) const;
    QVector<PersonalNote> getAllPersonalNotes() const;
    void appendExcerptToPersonalNote(
        const QString& swordKey,
        const QString& sourceTitle,
        const QString& excerptText,
        const QString& preferredBibleMod = "FreSegond"
    );
    void appendAnnotationBlockToPersonalNote(
        const QString& swordKey,
        const QString& preferredBibleMod = "FreSegond"
    );
    void saveEditedPersonalNoteHtml(
        const QString& swordKey,
        const QString& editedHtml,
        const QString& preferredBibleMod = "FreSegond"
    );
    void deletePersonalNote(const QString& swordKey);
    QString buildInitialNoteHeaderHtml(const QString& swordKey, const QString& preferredBibleMod = "FreSegond") const;

private:
    void initBooks();
    void initModules();
    QString normalizeStrongKey5(const QString& numPart) const;
    QString normalizeStrongKey4(const QString& numPart) const;
    QString cleanRawEntryToPlain(const QString& rawOrStrip) const;
    QString linkifyScriptureRefsInHtml(const QString& html, const QString& contextKey) const;

    QString m_swordPath;
    std::unique_ptr<sword::SWMgr> m_mgr;
    std::unique_ptr<sword::SWMgr> m_plainMgr;
    std::unique_ptr<sword::RawCom> m_frTskDirect;

    QVector<BookInfo> m_books;
    QVector<ModuleInfo> m_bibles;
    QVector<ModuleInfo> m_commentaries;
    QVector<ModuleInfo> m_lexicons;
    QHash<QString, StrongSummary> m_strongCache;
    QMap<QString, PersonalNote> m_personalNotes;
};
