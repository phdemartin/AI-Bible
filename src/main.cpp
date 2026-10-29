#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("BibleStudyStudio");
    app.setApplicationDisplayName("AI Bible — Logos & Lumière");
    app.setOrganizationName("LogosLumiere");

    const QString appDir = QCoreApplication::applicationDirPath();
    if (qEnvironmentVariableIsEmpty("CURL_CA_BUNDLE") && QFileInfo::exists(appDir + "/ca-bundle.crt")) {
        qputenv("CURL_CA_BUNDLE", (appDir + "/ca-bundle.crt").toUtf8());
    }

    const QStringList iconCandidates = {
        appDir + "/ai_bible_icon.svg",
        appDir + "/../share/icons/hicolor/scalable/apps/ai_bible_icon.svg",
        appDir + "/../ai_bible_icon.svg",
        "/usr/share/icons/hicolor/scalable/apps/ai_bible_icon.svg"
    };
    for (const QString& iconPath : iconCandidates) {
        if (QFileInfo::exists(iconPath)) {
            app.setWindowIcon(QIcon(iconPath));
            break;
        }
    }

    MainWindow win;
    win.show();

    return app.exec();
}


