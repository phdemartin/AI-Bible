#include "GeminiClient.h"
#include "SwordEngine.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QTextStream>

static const char* kSystemPrompt =
    "Tu es un assistant expert en exégèse biblique, théologie, histoire ancienne et philologie des langues bibliques "
    "(Hébreu massorétique, Araméen, Grec Koïné du Nouveau Testament et de la Septante). "
    "Réponds toujours en français clair, structuré, pédagogique et précis. "
    "Appuie tes analyses sur les textes originaux (mots grecs/hébreux en caractères originaux + translittération + numéros Strong quand pertinent), "
    "les comparaisons de traductions (Louis Segond, Darby, Semeur, TOB, Jérusalem, LBLA, KJV, etc.), "
    "et les passages parallèles.";

GeminiClient::GeminiClient(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
    m_apiKey = qEnvironmentVariable("GEMINI_API_KEY");
    if (m_apiKey.isEmpty()) {
        m_apiKey = qEnvironmentVariable("GOOGLE_API_KEY");
    }
    if (m_apiKey.isEmpty()) {
        QFile envFile(QDir::homePath() + "/.env");
        if (envFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&envFile);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.startsWith("GEMINI_API_KEY=") || line.startsWith("GOOGLE_API_KEY=")) {
                    int eq = line.indexOf('=');
                    QString val = line.mid(eq + 1).trimmed();
                    if ((val.startsWith('"') && val.endsWith('"')) || (val.startsWith('\'') && val.endsWith('\''))) {
                        val = val.mid(1, val.size() - 2);
                    }
                    if (!val.isEmpty()) {
                        m_apiKey = val;
                        break;
                    }
                }
            }
        }
    }
}

void GeminiClient::setApiKey(const QString& key) {
    m_apiKey = key.trimmed();
}

void GeminiClient::setModel(const QString& modelName) {
    if (!modelName.trimmed().isEmpty()) {
        m_model = modelName.trimmed();
    }
}

void GeminiClient::setResponseLanguage(const QString& langCode) {
    QString c = langCode.trimmed().toLower();
    if (!c.isEmpty() && c != "auto") {
        m_responseLang = c;
    } else if (m_responseLang.isEmpty()) {
        m_responseLang = SwordEngine::detectSystemLanguageCode();
    }
}

QString GeminiClient::languageDisplayName(const QString& langCode) {
    return SwordEngine::languageFlagAndName(langCode);
}

QString GeminiClient::buildSystemInstruction() const {
    if (m_responseLang == "es") {
        return QStringLiteral(
            "Eres un asistente experto en exégesis bíblica, teología, homilética (predicación), historia antigua y filología de las lenguas bíblicas "
            "(Hebreo masorético, Arameo, Griego Koiné del Nuevo Testamento y de la Septuaginta). "
            "IMPORTANTE: Aunque la pregunta o el contexto estén en francés o en otro idioma, RESPONDE SIEMPRE ÍNTEGRAMENTE EN ESPAÑOL claro, estructurado, pastoral y preciso, listo para ser usado en un estudio bíblico o predicación en español. "
            "Cita las referencias bíblicas en español (ej. Juan 11:28, Romanos 8:28) y apóyate en versiones en español (Reina-Valera, LBLA - La Biblia de las Américas) además de los textos originales griegos/hebreos (con código Strong)."
        );
    } else if (m_responseLang == "fr_es") {
        return QStringLiteral(
            "Tu es un assistant expert en exégèse biblique, théologie, homilétique (prédication) et langues bibliques (Hébreu, Araméen, Grec Koïné). "
            "IMPORTANT : L'utilisateur étudie et prêche en FRANÇAIS et en ESPAGNOL. "
            "Rédige TOUJOURS ta réponse en DEUX PARTIES claires et prêtes à copier dans un traitement de texte :\n"
            "1. 🇫🇷 **VERSION FRANÇAISE** (exégèse complète, mots grecs/hébreux Strong, application homilétique en français)\n"
            "2. 🇪🇸 **VERSIÓN EN ESPAÑOL** (la misma exégesis, estudio de palabras y aplicación homilética íntegramente en español, con referencias en español como LBLA / Reina-Valera)."
        );
    } else if (m_responseLang == "en") {
        return QStringLiteral(
            "You are an expert assistant in biblical exegesis, theology, homiletics, ancient history, and biblical languages "
            "(Masoretic Hebrew, Aramaic, Koine Greek). "
            "IMPORTANT: Regardless of the language of the prompt or context, ALWAYS RESPOND ENTIRELY IN CLEAR, STRUCTURED ENGLISH, "
            "citing original Greek/Hebrew words (with Strong's numbers) and English Bible translations (KJV, ESV, NASB)."
        );
    } else if (m_responseLang == "fr") {
        return QString::fromUtf8(kSystemPrompt);
    }

    QString langName = SwordEngine::languageNativeName(m_responseLang);
    return QString(
        "You are an expert assistant in biblical exegesis, theology, homiletics, ancient history, and biblical languages "
        "(Masoretic Hebrew, Aramaic, Koine Greek). "
        "IMPORTANT: Regardless of the language of the user prompt or study context, ALWAYS RESPOND ENTIRELY IN %1 (ISO code: %2), "
        "using clear, structured, pastoral, and scholarly prose ready to be pasted into a word processor, citing original Greek/Hebrew words (with Strong's numbers) "
        "and Bible book names in %1."
    ).arg(langName, m_responseLang);
}

QString GeminiClient::buildLanguageDirective() const {
    if (m_responseLang == "es") {
        return QStringLiteral(
            "\n\n[INSTRUCCIÓN DE IDIOMA OBLIGATORIA : Responde ÍNTEGRAMENTE en ESPAÑOL (castellano), con formato claro y listo para pegar en un procesador de textos para predicación o estudio bíblico en español. Usa nombres de libros bíblicos en español.]"
        );
    } else if (m_responseLang == "fr_es") {
        return QStringLiteral(
            "\n\n[INSTRUCTION DE LANGUE OBLIGATOIRE : Réponds en BILINGUE FRANÇAIS + ESPAGNOL : présente d'abord la section « 🇫🇷 EN FRANÇAIS », puis la section « 🇪🇸 EN ESPAÑOL », chacune prête à être copiée dans un traitement de texte pour prêcher dans les deux langues.]"
        );
    } else if (m_responseLang == "en") {
        return QStringLiteral(
            "\n\n[MANDATORY LANGUAGE INSTRUCTION: Respond ENTIRELY in ENGLISH, clearly formatted and ready to paste into a word processor.]"
        );
    } else if (m_responseLang == "fr") {
        return QStringLiteral(
            "\n\n[INSTRUCTION DE LANGUE OBLIGATOIRE : Réponds INTÉGRALEMENT en FRANÇAIS, bien structuré et prêt à être collé dans un traitement de texte.]"
        );
    }

    QString langName = SwordEngine::languageNativeName(m_responseLang);
    return QString(
        "\n\n[MANDATORY LANGUAGE INSTRUCTION: Respond ENTIRELY in %1 (%2), clearly structured and ready to paste into a word processor.]"
    ).arg(langName, m_responseLang);
}

void GeminiClient::clearHistory() {
    m_history.clear();
    m_pending = false;
}

void GeminiClient::sendViaOllama(const QString& ollamaModel, bool allowLocalFallback) {
    QJsonArray messages;
    {
        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = buildSystemInstruction();
        messages.append(sysMsg);
    }
    for (const auto& turn : m_history) {
        QJsonObject msg;
        msg["role"] = (turn.role == "user_input") ? "user" : "assistant";
        msg["content"] = turn.text;
        messages.append(msg);
    }

    QJsonObject payload;
    payload["model"] = ollamaModel;
    payload["messages"] = messages;
    payload["stream"] = false;

    QNetworkRequest req(QUrl("http://127.0.0.1:11434/api/chat"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_pending = true;
    emit requestStarted(QString("💬 Chat IA (%1 — %2) réfléchit...").arg(ollamaModel, languageDisplayName(m_responseLang)));

    QNetworkReply* reply = m_net->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, ollamaModel, allowLocalFallback]() {
        reply->deleteLater();
        QByteArray rawData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(rawData);

        if (reply->error() != QNetworkReply::NoError) {
            if (allowLocalFallback && ollamaModel != "gemma4:latest") {
                sendViaOllama("gemma4:latest", false);
                return;
            }
            m_pending = false;
            if (!m_history.isEmpty() && m_history.last().role == "user_input") {
                m_history.removeLast();
            }
            emit errorOccurred(QString("Erreur IA (%1) : %2").arg(ollamaModel, reply->errorString()));
            return;
        }

        QString extractedText;
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            if (root.contains("message") && root.value("message").isObject()) {
                extractedText = root.value("message").toObject().value("content").toString();
            } else if (root.contains("response") && root.value("response").isString()) {
                extractedText = root.value("response").toString();
            }
        }
        if (extractedText.trimmed().isEmpty()) {
            if (allowLocalFallback && ollamaModel != "gemma4:latest") {
                sendViaOllama("gemma4:latest", false);
                return;
            }
            extractedText = QString::fromUtf8(rawData);
        }

        m_pending = false;
        ChatTurn modelTurn;
        modelTurn.role = "model_output";
        modelTurn.text = extractedText;
        modelTurn.displayLabel = extractedText;
        modelTurn.langCode = m_responseLang;
        m_history.push_back(modelTurn);

        emit responseReceived(extractedText);
    });
}

void GeminiClient::sendPrompt(const QString& userPrompt, const QString& studyContext, const QString& displayTitle) {
    QString fullInput = userPrompt + buildLanguageDirective();
    if (!studyContext.trimmed().isEmpty()) {
        fullInput = studyContext + "\n\n=== QUESTION / DEMANDE ===\n" + userPrompt + buildLanguageDirective();
    }

    ChatTurn userTurn;
    userTurn.role = "user_input";
    userTurn.text = fullInput;
    userTurn.displayLabel = displayTitle.isEmpty() ? userPrompt : displayTitle;
    userTurn.langCode = m_responseLang;
    m_history.push_back(userTurn);

    bool isExplicitOllamaModel = !m_model.startsWith("gemini-");
    if (m_apiKey.isEmpty() || isExplicitOllamaModel) {
        QString targetOllama = isExplicitOllamaModel ? m_model : QStringLiteral("gemma4:31b-cloud");
        sendViaOllama(targetOllama, true);
        return;
    }

    QJsonArray stepsArray;
    for (const auto& turn : m_history) {
        QJsonObject textPart;
        textPart["type"] = "text";
        textPart["text"] = turn.text;

        QJsonArray contentArr;
        contentArr.append(textPart);

        QJsonObject stepObj;
        stepObj["type"] = turn.role;
        stepObj["content"] = contentArr;
        stepsArray.append(stepObj);
    }

    QJsonObject payload;
    payload["model"] = m_model;
    payload["system_instruction"] = buildSystemInstruction();
    payload["input"] = stepsArray;

    QNetworkRequest req(QUrl("https://generativelanguage.googleapis.com/v1/interactions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());

    m_pending = true;
    emit requestStarted(QString("💬 Interrogation de %1 (%2) en cours...").arg(m_model, languageDisplayName(m_responseLang)));

    QNetworkReply* reply = m_net->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QByteArray rawData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(rawData);

        if (reply->error() != QNetworkReply::NoError) {
            // Bascule transparente vers Google Gemma 4 31B si la clé API échoue
            sendViaOllama("gemma4:31b-cloud", true);
            return;
        }

        QString extractedText;
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            if (root.contains("output_text") && root.value("output_text").isString()) {
                extractedText = root.value("output_text").toString();
            } else if (root.contains("steps") && root.value("steps").isArray()) {
                QJsonArray steps = root.value("steps").toArray();
                QStringList textRuns;
                for (const QJsonValue& sv : steps) {
                    QJsonObject step = sv.toObject();
                    if (step.value("type").toString() == "model_output") {
                        QJsonArray content = step.value("content").toArray();
                        for (const QJsonValue& cv : content) {
                            QJsonObject cobj = cv.toObject();
                            if (cobj.value("type").toString() == "text") {
                                textRuns.push_back(cobj.value("text").toString());
                            }
                        }
                    }
                }
                extractedText = textRuns.join("\n");
            }
        }

        if (extractedText.trimmed().isEmpty()) {
            sendViaOllama("gemma4:31b-cloud", true);
            return;
        }

        m_pending = false;
        ChatTurn modelTurn;
        modelTurn.role = "model_output";
        modelTurn.text = extractedText;
        modelTurn.displayLabel = extractedText;
        modelTurn.langCode = m_responseLang;
        m_history.push_back(modelTurn);

        emit responseReceived(extractedText);
    });
}

QString GeminiClient::markdownToHtml(const QString& md, bool darkTheme) {
    QString accent = darkTheme ? "#38BDF8" : "#B45309";
    QString codeBg = darkTheme ? "#0F172A" : "#F1F5F9";
    QString tableBorder = darkTheme ? "#334155" : "#CBD5E1";
    QString tableHeaderBg = darkTheme ? "#1E293B" : "#E2E8F0";
    QString linesOut;

    const QStringList lines = md.split('\n');
    bool inUl = false;
    bool inOl = false;
    bool inTable = false;
    bool isTableFirstRow = false;

    auto closeListsAndTables = [&]() {
        if (inUl) { linesOut += "</ul>"; inUl = false; }
        if (inOl) { linesOut += "</ol>"; inOl = false; }
        if (inTable) { linesOut += "</table>"; inTable = false; }
    };

    auto formatInline = [&](QString s) -> QString {
        s = s.toHtmlEscaped();
        s.replace(QRegularExpression(R"(\*\*([^*]+)\*\*)"), "<b>\\1</b>");
        s.replace(QRegularExpression(R"(\*([^*]+)\*)"), "<i>\\1</i>");
        s.replace(QRegularExpression(R"(`([^`]+)`)"),
                  QString("<code style='background:%1; padding:1px 4px; border-radius:3px;'>\\1</code>").arg(codeBg));
        return s;
    };

    static const QRegularExpression rxOrdered(R"(^\d+\.\s+(.+)$)");
    static const QRegularExpression rxTableSep(R"(^\|[\s:\-\|]+\|$)");

    for (QString rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            closeListsAndTables();
            linesOut += "<div style='height:6px;'></div>";
            continue;
        }

        if (line == "---" || line == "***" || line == "___") {
            closeListsAndTables();
            linesOut += QString("<hr style='border:none; border-top:1px solid %1; margin:10px 0;'>").arg(tableBorder);
            continue;
        }

        // Table rows: | col1 | col2 |
        if (line.startsWith('|') && line.endsWith('|') && line.size() > 2) {
            if (inUl) { linesOut += "</ul>"; inUl = false; }
            if (inOl) { linesOut += "</ol>"; inOl = false; }
            if (rxTableSep.match(line).hasMatch()) {
                continue;
            }
            if (!inTable) {
                linesOut += QString("<table style='border-collapse:collapse; width:100%; margin:8px 0; border:1px solid %1;'>").arg(tableBorder);
                inTable = true;
                isTableFirstRow = true;
            }
            QString inner = line.mid(1, line.size() - 2);
            QStringList cells = inner.split('|');
            linesOut += "<tr>";
            for (const QString& cell : cells) {
                if (isTableFirstRow) {
                    linesOut += QString("<th style='border:1px solid %1; background:%2; padding:6px 8px; text-align:left; color:%3;'>%4</th>")
                                    .arg(tableBorder, tableHeaderBg, accent, formatInline(cell.trimmed()));
                } else {
                    linesOut += QString("<td style='border:1px solid %1; padding:6px 8px;'>%2</td>")
                                    .arg(tableBorder, formatInline(cell.trimmed()));
                }
            }
            linesOut += "</tr>";
            isTableFirstRow = false;
            continue;
        }

        if (inTable) {
            linesOut += "</table>";
            inTable = false;
        }

        if (line.startsWith("#### ")) {
            closeListsAndTables();
            linesOut += QString("<div style='font-weight:700; color:%1; margin:10px 0 4px 0; font-size:1.05em;'>%2</div>")
                            .arg(accent, formatInline(line.mid(5)));
        } else if (line.startsWith("### ")) {
            closeListsAndTables();
            linesOut += QString("<h4 style='color:%1; margin:10px 0 4px 0;'>%2</h4>").arg(accent, formatInline(line.mid(4)));
        } else if (line.startsWith("## ")) {
            closeListsAndTables();
            linesOut += QString("<h3 style='color:%1; margin:12px 0 6px 0;'>%2</h3>").arg(accent, formatInline(line.mid(3)));
        } else if (line.startsWith("# ")) {
            closeListsAndTables();
            linesOut += QString("<h2 style='color:%1; margin:12px 0 6px 0;'>%2</h2>").arg(accent, formatInline(line.mid(2)));
        } else if (line.startsWith("- ") || line.startsWith("* ") || line.startsWith("• ")) {
            if (inOl) { linesOut += "</ol>"; inOl = false; }
            if (!inUl) {
                linesOut += "<ul style='margin:4px 0 6px 20px; padding:0;'>";
                inUl = true;
            }
            QString itemTxt = line.mid(2).trimmed();
            linesOut += QString("<li style='margin-bottom:4px;'>%1</li>").arg(formatInline(itemTxt));
        } else if (auto m = rxOrdered.match(line); m.hasMatch()) {
            if (inUl) { linesOut += "</ul>"; inUl = false; }
            if (!inOl) {
                linesOut += "<ol style='margin:4px 0 6px 22px; padding:0;'>";
                inOl = true;
            }
            linesOut += QString("<li style='margin-bottom:4px;'>%1</li>").arg(formatInline(m.captured(1)));
        } else if (line.startsWith("> ")) {
            closeListsAndTables();
            linesOut += QString("<blockquote style='border-left:3px solid %1; padding-left:10px; margin:6px 0; font-style:italic;'>%2</blockquote>")
                .arg(accent, formatInline(line.mid(2)));
        } else {
            closeListsAndTables();
            linesOut += QString("<div style='margin-bottom:5px;'>%1</div>").arg(formatInline(rawLine));
        }
    }
    closeListsAndTables();
    return linesOut;
}

QString GeminiClient::markdownToWordProcessorHtml(const QString& md, const QString& titleHeader) {
    QString body = markdownToHtml(md, false);
    QString headHtml;
    if (!titleHeader.trimmed().isEmpty()) {
        headHtml = QString(
            "<h2 style='color:#1E3A8A; border-bottom:2px solid #93C5FD; padding-bottom:4px; margin-bottom:10px;'>%1</h2>"
        ).arg(titleHeader.toHtmlEscaped());
    }
    return QString(
        "<!DOCTYPE html><html><head><meta charset='utf-8'></head>"
        "<body style=\"font-family:'Noto Serif','Liberation Serif','Times New Roman',serif; font-size:12pt; color:#111827; background-color:transparent; line-height:1.45;\">"
        "%1%2"
        "</body></html>"
    ).arg(headHtml, body);
}

