#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QNetworkAccessManager>

struct ChatTurn {
    QString role; // "user_input" or "model_output"
    QString text;
    QString displayLabel;
    QString langCode = "fr"; // "fr", "es", "fr_es", "en"
};

class GeminiClient : public QObject {
    Q_OBJECT
public:
    explicit GeminiClient(QObject* parent = nullptr);

    void setApiKey(const QString& key);
    QString apiKey() const { return m_apiKey; }

    void setModel(const QString& modelName);
    QString model() const { return m_model; }

    void setResponseLanguage(const QString& langCode);
    QString responseLanguage() const { return m_responseLang; }
    static QString languageDisplayName(const QString& langCode);

    void clearHistory();
    const QVector<ChatTurn>& history() const { return m_history; }

    void sendPrompt(const QString& userPrompt, const QString& studyContext, const QString& displayTitle = QString());
    bool isPending() const { return m_pending; }

    static QString markdownToHtml(const QString& md, bool darkTheme);
    static QString markdownToWordProcessorHtml(const QString& md, const QString& titleHeader = QString());

signals:
    void requestStarted(const QString& statusText);
    void responseReceived(const QString& modelText);
    void errorOccurred(const QString& errorMessage);

private:
    QString buildSystemInstruction() const;
    QString buildLanguageDirective() const;
    void sendViaOllama(const QString& ollamaModel, bool allowLocalFallback);

    QNetworkAccessManager* m_net;
    QString m_apiKey;
    QString m_model = "gemini-3.8-flash";
    QString m_responseLang = "fr"; // "fr" (🇫🇷 Français), "es" (🇪🇸 Español), "fr_es" (🇫🇷+🇪🇸 Bilingue), "en" (🇬🇧 English)
    QVector<ChatTurn> m_history;
    bool m_pending = false;
};
