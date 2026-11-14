#include "core/AppInfo.h"
#include "core/Lang.h"
#include "core/Paths.h"
#include "core/Registry.h"
#include "core/Settings.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/WelcomeDialog.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QMutex>
#include <QPainter>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>

namespace {

QFile* g_log = nullptr;
QMutex g_logMutex;

void logHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    const char* level = "INFO";
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    default: break;
    }
    QMutexLocker lock(&g_logMutex);
    const QString line = QStringLiteral("%1 [%2] %3\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate), level, msg);
    if (g_log) {
        g_log->write(line.toUtf8());
        g_log->flush();
    }
    fputs(line.toLocal8Bit().constData(), stderr);
}

// Icône de la fenêtre : le nid aux couleurs des consoles.
QIcon appIcon() {
    QIcon ic;
    for (int sz : {16, 24, 32, 48, 64, 128, 256})
        ic.addPixmap(Theme::logoPixmap(sz));
    return ic;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(AppInfo::kName));
    QApplication::setOrganizationName(QString::fromLatin1(AppInfo::kOrganization));
    QApplication::setApplicationVersion(QStringLiteral(PLAYNEST_VERSION));

    Paths::init();
    QFile log(Paths::logFile());
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        g_log = &log;
    qInstallMessageHandler(logHandler);
    qInfo("Playnest %s, données : %s%s", PLAYNEST_VERSION, qUtf8Printable(Paths::dataDir()), Paths::isPortable() ? " (portable)" : "");

    Settings::instance().init();
    Lang::init(); // avant le registre : les notes des émulateurs sont traduites au chargement
    Registry::instance().load();
    Theme::apply(app);
    app.setWindowIcon(appIcon());

    // Outil de build : --export-logo <dossier> écrit le logo en PNG (pour l'icône .ico et le README).
    if (const int i = QCoreApplication::arguments().indexOf(QStringLiteral("--export-logo")); i > 0 && i + 1 < QCoreApplication::arguments().size()) {
        const QString dir = QCoreApplication::arguments().at(i + 1);
        QDir().mkpath(dir);
        for (int sz : {16, 20, 24, 32, 40, 48, 64, 128, 256, 512})
            Theme::logoPixmap(sz).save(QStringLiteral("%1/logo-%2.png").arg(dir).arg(sz));
        return 0;
    }

    // Une seule instance à la fois : évite que deux fenêtres écrivent la bibliothèque en même temps.
    QLockFile lock(Paths::lockFile());
    lock.setStaleLockTime(0);
    // Après « Redémarrer » (changement de langue), l'ancienne fenêtre met un instant à se fermer.
    if (!lock.tryLock(QCoreApplication::arguments().contains(QStringLiteral("--restarted")) ? 8000 : 200)) {
        QMessageBox::information(nullptr, QString::fromLatin1(AppInfo::kName),
                                 QObject::tr("Playnest est déjà ouvert. Utilise la fenêtre existante."));
        return 0;
    }

    // Premier lancement : langue et dossier de jeux, avant d'ouvrir la fenêtre principale.
    if (WelcomeDialog::needed() || qEnvironmentVariableIsSet("PLAYNEST_FORCE_WELCOME")) {
        const QString before = Lang::current();
        WelcomeDialog welcome;
        if (const QString shot = qEnvironmentVariable("PLAYNEST_WELCOME_SHOT"); !shot.isEmpty()) { // aide au développement
            const int page = qEnvironmentVariableIntValue("PLAYNEST_WELCOME_PAGE");
            QTimer::singleShot(600, &welcome, [&welcome, page] {
                for (int i = 0; i < page; ++i)
                    if (auto* next = welcome.findChild<QPushButton*>(QStringLiteral("welcomeNext")))
                        next->click();
            });
            QTimer::singleShot(1500, &welcome, [&welcome, shot] {
                welcome.grab().save(shot);
                QCoreApplication::exit(0);
            });
            welcome.show();
            return app.exec();
        }
        if (welcome.exec() != QDialog::Accepted)
            return 0; // fenêtre fermée : on reposera la question au prochain lancement
        if (Lang::current() != before)
            Registry::instance().load();
    }

    MainWindow w;
    w.show();
    w.startup();

    // Aide au développement : PLAYNEST_SCREENSHOT=fichier.png capture la fenêtre puis quitte.
    const QString shot = qEnvironmentVariable("PLAYNEST_SCREENSHOT");
    if (!shot.isEmpty() && qEnvironmentVariableIsSet("PLAYNEST_SCREENSHOT_PAGE")) {
        const int page = qEnvironmentVariableIntValue("PLAYNEST_SCREENSHOT_PAGE");
        const QStringList sz = qEnvironmentVariable("PLAYNEST_SCREENSHOT_SIZE").split(QLatin1Char('x'));
        if (sz.size() == 2)
            w.resize(sz[0].toInt(), sz[1].toInt());
        QTimer::singleShot(1500, &w, [&w, page] { w.showPage(page); });
        for (const QString& what : qEnvironmentVariable("PLAYNEST_SCREENSHOT_SHOW").split(QLatin1Char(';'), Qt::SkipEmptyParts))
            QTimer::singleShot(1900, &w, [&w, what] { w.debugShow(what); });
        QTimer::singleShot(qMax(3500, qEnvironmentVariableIntValue("PLAYNEST_SCREENSHOT_DELAY")), &w, [&w, shot] {
            QWidget* target = QApplication::activeModalWidget() ? QApplication::activeModalWidget() : &w;
            target->grab().save(shot);
            QApplication::exit(0);
        });
    } else if (!shot.isEmpty()) {
        QTimer::singleShot(qEnvironmentVariableIntValue("PLAYNEST_SCREENSHOT_DELAY") > 0 ? qEnvironmentVariableIntValue("PLAYNEST_SCREENSHOT_DELAY") : 2500,
                           &w, [&w, shot] {
                               w.grab().save(shot);
                               QApplication::quit();
                           });
    }
    const int rc = app.exec();
    qInstallMessageHandler(nullptr);
    g_log = nullptr;
    return rc;
}
