#include "Launcher.h"

#include "Emulators.h"
#include "Library.h"
#include "Registry.h"
#include "Settings.h"

#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QDesktopServices>

Launcher::Launcher(QObject* parent) : QObject(parent) {}

Launcher::~Launcher() {
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->setParent(nullptr); // l'émulateur continue même si Playnest se ferme
    }
}

QStringList Launcher::buildArguments(const EmulatorDef& emu, const Game& g, const QString& exePath) {
    const bool fs = Settings::instance().launchFullscreen();
    QStringList tmpl;
    const QString custom = Settings::instance().customArgs(emu.id, fs);
    if (!custom.isEmpty())
        tmpl = QProcess::splitCommand(custom);
    else
        tmpl = fs ? emu.args : emu.argsWindowed;
    if (tmpl.isEmpty())
        tmpl = QStringList{QStringLiteral("{rom}")};

    const QFileInfo rom(g.path);
    const QString romPath = QDir::toNativeSeparators(rom.absoluteFilePath());
    const QString romDir = QDir::toNativeSeparators(rom.absolutePath());
    const QString emuDir = QDir::toNativeSeparators(QFileInfo(exePath).absolutePath());
    // {system} : nom de la console tel que l'émulateur l'attend (ares : « Famicom », « Nintendo 64 »…).
    const SystemInfo* sys = Registry::instance().system(g.system());
    const QString systemName = emu.systemNames.value(g.system(), sys ? sys->name : g.system());
    QStringList out;
    for (QString a : tmpl) {
        a.replace(QStringLiteral("{system}"), systemName);
        a.replace(QStringLiteral("{rom}"), romPath);
        a.replace(QStringLiteral("{romdir}"), romDir);
        a.replace(QStringLiteral("{romname}"), rom.completeBaseName());
        a.replace(QStringLiteral("{emudir}"), emuDir);
        out << a;
    }
    return out;
}

bool Launcher::launch(const Game& g, const QString& emulatorId, QString* error) {
    if (isRunning()) {
        if (error)
            *error = tr("« %1 » est déjà en cours. Ferme l'émulateur avant de lancer un autre jeu.").arg(m_title);
        return false;
    }
    if (!QFileInfo::exists(g.path)) {
        if (error)
            *error = tr("Le fichier du jeu est introuvable :\n%1").arg(QDir::toNativeSeparators(g.path));
        return false;
    }
    if (g.system() == QLatin1String("pc")) // fan game, jeu PC : pas d'émulateur
        return launchDirect(g, error);
    const EmulatorDef* emu = Registry::instance().emulator(emulatorId);
    if (!emu) {
        if (error)
            *error = tr("Aucun émulateur n'est configuré pour ce système.");
        return false;
    }
    const QString exe = EmulatorManager::instance().exePath(emu->id);
    if (exe.isEmpty()) {
        if (error)
            *error = tr("%1 n'est pas installé ou introuvable.").arg(emu->name);
        return false;
    }

    const QStringList args = buildArguments(*emu, g, exe);
    delete m_proc;
    m_proc = new QProcess(this);
    m_proc->setProgram(exe);
    m_proc->setArguments(args);
    m_proc->setWorkingDirectory(QFileInfo(exe).absolutePath());
#ifdef Q_OS_LINUX
    if (exe.endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive)) {
        // Une AppImage se monte avec FUSE. Sans lui (Ubuntu récent sans libfuse2), elle sait se décompresser
        // elle-même avant de démarrer : un peu plus lent, mais ça marche partout.
        const bool fuse = !QStandardPaths::findExecutable(QStringLiteral("fusermount")).isEmpty() ||
                          !QStandardPaths::findExecutable(QStringLiteral("fusermount3")).isEmpty();
        const bool fuse2 = QFileInfo::exists(QStringLiteral("/usr/lib/x86_64-linux-gnu/libfuse.so.2")) ||
                           QFileInfo::exists(QStringLiteral("/usr/lib64/libfuse.so.2")) || QFileInfo::exists(QStringLiteral("/usr/lib/libfuse.so.2"));
        if (!fuse || !fuse2) {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN"), QStringLiteral("1"));
            m_proc->setProcessEnvironment(env);
        }
    }
#endif
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    m_output.clear();
    connect(m_proc, &QProcess::readyRead, this, [this] {
        m_output += m_proc->readAll();
        if (m_output.size() > 64 * 1024)
            m_output = m_output.right(32 * 1024);
    });
    connect(m_proc, &QProcess::finished, this, &Launcher::onFinished);

    qInfo("Lancement : %s %s", qUtf8Printable(exe), qUtf8Printable(args.join(QLatin1Char(' '))));
    m_proc->start();
    if (!m_proc->waitForStarted(8000)) {
        if (error)
            *error = tr("Impossible de démarrer %1 :\n%2").arg(emu->name, m_proc->errorString());
        delete m_proc;
        m_proc = nullptr;
        return false;
    }
    m_gamePath = g.path;
    m_title = g.displayTitle();
    m_emuName = emu->name;
    m_timer.start();
    EmulatorManager::instance().setRunningExe(exe);
    emit started(g.path);
    return true;
}

bool Launcher::launchDirect(const Game& g, QString* error) {
    const QString exe = QDir::toNativeSeparators(QFileInfo(g.path).absoluteFilePath());
    QString program = exe;
    QStringList arguments;
#ifndef Q_OS_WIN
    if (exe.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) {
        // Un jeu Windows (fan game) passe par Wine sous Linux et macOS.
        program = QStandardPaths::findExecutable(QStringLiteral("wine"));
        if (program.isEmpty())
            program = QStandardPaths::findExecutable(QStringLiteral("wine64"));
        if (program.isEmpty()) {
            if (error)
                *error = tr("Ce jeu est un programme Windows : installe Wine pour le lancer sur ce système.");
            return false;
        }
        arguments << exe;
    }
#endif
    delete m_proc;
    m_proc = new QProcess(this);
    m_proc->setProgram(program);
    m_proc->setArguments(arguments);
    m_proc->setWorkingDirectory(QFileInfo(exe).absolutePath()); // les fan games lisent leurs fichiers à côté de l'exécutable
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    m_output.clear();
    connect(m_proc, &QProcess::readyRead, this, [this] {
        m_output += m_proc->readAll();
        if (m_output.size() > 64 * 1024)
            m_output = m_output.right(32 * 1024);
    });
    connect(m_proc, &QProcess::finished, this, &Launcher::onFinished);
    qInfo("Lancement direct : %s", qUtf8Printable(exe));
    m_proc->start();
    if (!m_proc->waitForStarted(8000)) {
        const QString why = m_proc->errorString();
        delete m_proc;
        m_proc = nullptr;
        // Programme qui demande les droits administrateur : on le laisse s'ouvrir comme un double-clic.
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(exe))) {
            emit started(g.path);
            emit finished(g.path, 0, 0, false, QString());
            return true;
        }
        if (error)
            *error = tr("Impossible de démarrer « %1 » :\n%2").arg(g.displayTitle(), why);
        return false;
    }
    m_gamePath = g.path;
    m_title = g.displayTitle();
    m_emuName = g.displayTitle();
    m_timer.start();
    emit started(g.path);
    return true;
}

bool Launcher::isRunning() const { return m_proc && m_proc->state() != QProcess::NotRunning; }

qint64 Launcher::elapsedSeconds() const { return m_timer.isValid() ? m_timer.elapsed() / 1000 : 0; }

void Launcher::onFinished(int exitCode, QProcess::ExitStatus status) {
    const qint64 secs = elapsedSeconds();
    m_output += m_proc->readAll();
    const bool crashed = (status == QProcess::CrashExit || exitCode != 0) && secs < 15;
    QStringList lines = QString::fromLocal8Bit(m_output).split(QLatin1Char('\n'));
    if (lines.size() > 40)
        lines = lines.mid(lines.size() - 40);
    const QString tail = lines.join(QLatin1Char('\n')).trimmed();
    const QString path = m_gamePath;
    m_gamePath.clear();
    m_timer.invalidate();
    m_proc->deleteLater();
    m_proc = nullptr;
    EmulatorManager::instance().setRunningExe(QString());
    emit finished(path, exitCode, secs, crashed, tail);
}
