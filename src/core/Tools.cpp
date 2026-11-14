#include "Tools.h"

#include "Net.h"
#include "Paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLibrary>
#include <QProcessEnvironment>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <cstdint>
#include <cstring>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

// --- Vulkan : vérification sans en-têtes ni bibliothèque à l'édition de liens -------------
struct VkInstanceCreateInfoMin {
    uint32_t sType;
    const void* pNext;
    uint32_t flags;
    const void* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};
#ifdef Q_OS_WIN
#define VKCALL __stdcall
#else
#define VKCALL
#endif
using PFN_GetInstanceProcAddr = void*(VKCALL*)(void*, const char*);
using PFN_EnumerateInstanceVersion = int32_t(VKCALL*)(uint32_t*);
using PFN_CreateInstance = int32_t(VKCALL*)(const VkInstanceCreateInfoMin*, const void*, void**);
using PFN_DestroyInstance = void(VKCALL*)(void*, const void*);
using PFN_EnumeratePhysicalDevices = int32_t(VKCALL*)(void*, uint32_t*, void**);
using PFN_GetPhysicalDeviceProperties = void(VKCALL*)(void*, void*);

struct VulkanResult {
    bool ok = false;
    QString detail;
};

QString vkVersion(uint32_t v) { return QStringLiteral("%1.%2").arg(v >> 22).arg((v >> 12) & 0x3FF); }

VulkanResult probeVulkan() {
    VulkanResult r;
#ifdef Q_OS_WIN
    QLibrary lib(QStringLiteral("vulkan-1"));
#else
    QLibrary lib(QStringLiteral("vulkan"), 1);
#endif
    if (!lib.load()) {
        r.detail = QObject::tr("Vulkan introuvable : installe ou mets à jour le pilote de ta carte graphique.");
        return r;
    }
    auto gipa = reinterpret_cast<PFN_GetInstanceProcAddr>(lib.resolve("vkGetInstanceProcAddr"));
    if (!gipa) {
        r.detail = QObject::tr("Bibliothèque Vulkan invalide.");
        return r;
    }
    uint32_t apiVersion = 1u << 22;
    if (auto ev = reinterpret_cast<PFN_EnumerateInstanceVersion>(gipa(nullptr, "vkEnumerateInstanceVersion")))
        ev(&apiVersion);
    auto create = reinterpret_cast<PFN_CreateInstance>(gipa(nullptr, "vkCreateInstance"));
    void* instance = nullptr;
    VkInstanceCreateInfoMin info{};
    info.sType = 1; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    QString bestName;
    uint32_t bestApi = 0;
    int bestScore = -1;
    if (create && create(&info, nullptr, &instance) == 0 && instance) {
        auto destroy = reinterpret_cast<PFN_DestroyInstance>(gipa(instance, "vkDestroyInstance"));
        auto enumDev = reinterpret_cast<PFN_EnumeratePhysicalDevices>(gipa(instance, "vkEnumeratePhysicalDevices"));
        auto props = reinterpret_cast<PFN_GetPhysicalDeviceProperties>(gipa(instance, "vkGetPhysicalDeviceProperties"));
        if (enumDev && props) {
            uint32_t n = 0;
            enumDev(instance, &n, nullptr);
            QVector<void*> devs(int(qMin<uint32_t>(n, 16)));
            n = uint32_t(devs.size());
            if (n && enumDev(instance, &n, devs.data()) >= 0) {
                for (uint32_t i = 0; i < n; ++i) {
                    alignas(16) unsigned char buf[4096];
                    std::memset(buf, 0, sizeof(buf));
                    props(devs[int(i)], buf);
                    uint32_t api = 0, type = 0;
                    std::memcpy(&api, buf, 4);
                    std::memcpy(&type, buf + 16, 4);
                    const QString name = QString::fromUtf8(reinterpret_cast<const char*>(buf + 20));
                    const int score = type == 2 ? 3 : type == 1 ? 2 : type == 3 ? 1 : 0; // dédiée > intégrée > virtuelle
                    if (score > bestScore) {
                        bestScore = score;
                        bestName = name;
                        bestApi = api;
                    }
                }
            }
        }
        if (destroy)
            destroy(instance, nullptr);
    }
    if (bestName.isEmpty()) {
        r.ok = false;
        r.detail = QObject::tr("Vulkan %1 est installé mais aucune carte graphique compatible n'a été trouvée.").arg(vkVersion(apiVersion));
        return r;
    }
    const uint32_t api = bestApi ? bestApi : apiVersion;
    r.ok = (api >> 22) > 1 || ((api >> 12) & 0x3FF) >= 1;
    r.detail = r.ok ? QObject::tr("Vulkan %1, %2").arg(vkVersion(api), bestName)
                    : QObject::tr("Vulkan %1 (%2) : trop ancien, mets à jour le pilote graphique.").arg(vkVersion(api), bestName);
    return r;
}

#ifdef Q_OS_WIN
// Lance un programme avec élévation (UAC) et attend sa fin. Renvoie le code de sortie (-1 si refus/échec).
int runElevated(const QString& exe, const QString& params) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    const std::wstring file = QDir::toNativeSeparators(exe).toStdWString();
    const std::wstring args = params.toStdWString();
    sei.lpFile = file.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei) || !sei.hProcess)
        return -1;
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return int(code);
}
#endif

} // namespace

Tools& Tools::instance() {
    static Tools t;
    return t;
}

QString Tools::label(const QString& id) const {
    if (id == QLatin1String("vcredist")) return tr("Runtime Visual C++ 2015-2022");
    if (id == QLatin1String("7zip")) return tr("7-Zip (archives .7z)");
    if (id == QLatin1String("vulkan")) return tr("Vulkan et carte graphique");
    return id;
}

QString Tools::sevenZipPath() const {
    const QString local = Paths::toolsDir() + QStringLiteral("/7zr.exe");
    if (QFileInfo::exists(local))
        return local;
#ifdef Q_OS_WIN
    const auto env = QProcessEnvironment::systemEnvironment();
    for (const char* var : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        const QString p = env.value(QString::fromLatin1(var)) + QStringLiteral("/7-Zip/7z.exe");
        if (QFileInfo::exists(p))
            return p;
    }
#else
    for (const char* p : {"/usr/bin/7z", "/usr/bin/7za", "/usr/bin/7zr", "/usr/bin/7zz"})
        if (QFileInfo::exists(QString::fromLatin1(p)))
            return QString::fromLatin1(p);
#endif
    return {};
}

Tools::Status Tools::vcredistStatus() const {
    Status s;
#ifdef Q_OS_WIN
    s.installable = true;
    QString version;
    bool installed = false;
    for (const char* key : {"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
                            "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64"}) {
        QSettings reg(QString::fromLatin1(key), QSettings::Registry64Format);
        if (reg.value(QStringLiteral("Installed")).toInt() == 1) {
            installed = true;
            version = reg.value(QStringLiteral("Version")).toString();
            break;
        }
    }
    if (!installed) {
        s.detail = tr("Non installé. Indispensable pour Eden, Lichii, Azahar, Citra, Dolphin et Cemu.");
        return s;
    }
    // Les émulateurs compilés avec Visual Studio 2022 17.10+ plantent avec un runtime antérieur à 14.40.
    const QStringList parts = version.mid(version.startsWith(QLatin1Char('v')) ? 1 : 0).split(QLatin1Char('.'));
    const int major = parts.value(0).toInt(), minor = parts.value(1).toInt();
    if (major < 14 || (major == 14 && minor < 40)) {
        s.detail = tr("Version %1 trop ancienne : certains émulateurs récents plantent au démarrage. Mets-le à jour.").arg(version);
        return s;
    }
    s.ok = true;
    s.detail = tr("Installé (%1).").arg(version);
#else
    s.ok = true;
    s.detail = tr("Non nécessaire sous Linux.");
#endif
    return s;
}

Tools::Status Tools::status(const QString& id) const {
    if (id == QLatin1String("vcredist"))
        return vcredistStatus();
    if (id == QLatin1String("7zip")) {
        Status s;
        const QString p = sevenZipPath();
        s.ok = !p.isEmpty();
        s.installable = true;
        s.detail = s.ok ? tr("Disponible (%1).").arg(QDir::toNativeSeparators(p))
                        : tr("Non installé. Nécessaire pour installer Dolphin et mGBA ; Playnest le télécharge automatiquement si besoin.");
        return s;
    }
    if (id == QLatin1String("vulkan")) {
        Status s;
        s.ok = m_vulkanOk;
        s.detail = m_vulkanChecked ? m_vulkanDetail : tr("Vérification…");
        return s;
    }
    return {};
}

void Tools::checkVulkanAsync() {
    auto* w = new QFutureWatcher<VulkanResult>(this);
    connect(w, &QFutureWatcher<VulkanResult>::finished, this, [this, w] {
        const VulkanResult r = w->result();
        w->deleteLater();
        m_vulkanChecked = true;
        m_vulkanOk = r.ok;
        m_vulkanDetail = r.detail;
        qInfo("Vulkan : %s", qUtf8Printable(r.detail));
        emit changed();
    });
    w->setFuture(QtConcurrent::run(probeVulkan));
}

void Tools::finish(const QString& id, bool ok, const QString& msg) {
    m_busy.remove(id);
    emit installFinished(id, ok, msg);
    emit changed();
}

void Tools::install(const QString& id) {
    if (m_busy.contains(id))
        return;
    if (id == QLatin1String("7zip")) {
        m_busy.insert(id);
        const QString dest = Paths::toolsDir() + QStringLiteral("/7zr.exe");
        emit installProgress(id, tr("Téléchargement de 7-Zip…"), 0, 0);
        Net::instance().download(
            QUrl(QStringLiteral("https://www.7-zip.org/a/7zr.exe")), dest, this,
            [this, id](qint64 r, qint64 t) { emit installProgress(id, tr("Téléchargement de 7-Zip…"), r, t); },
            [this, id, dest](bool ok, const QString& err) {
                if (!ok || QFileInfo(dest).size() < 100 * 1024) {
                    QFile::remove(dest);
                    finish(id, false, tr("Échec du téléchargement de 7-Zip : %1").arg(err));
                    return;
                }
                finish(id, true, tr("7-Zip est prêt."));
            });
        return;
    }
    if (id == QLatin1String("vcredist")) {
#ifdef Q_OS_WIN
        m_busy.insert(id);
        const QString dest = Paths::downloadsDir() + QStringLiteral("/vc_redist.x64.exe");
        emit installProgress(id, tr("Téléchargement du runtime Visual C++…"), 0, 0);
        Net::instance().download(
            QUrl(QStringLiteral("https://aka.ms/vs/17/release/vc_redist.x64.exe")), dest, this,
            [this, id](qint64 r, qint64 t) { emit installProgress(id, tr("Téléchargement du runtime Visual C++…"), r, t); },
            [this, id, dest](bool ok, const QString& err) {
                if (!ok) {
                    finish(id, false, tr("Échec du téléchargement : %1").arg(err));
                    return;
                }
                emit installProgress(id, tr("Installation (accepte la demande d'autorisation de Windows)…"), 0, 0);
                auto* w = new QFutureWatcher<int>(this);
                connect(w, &QFutureWatcher<int>::finished, this, [this, w, id, dest] {
                    const int code = w->result();
                    w->deleteLater();
                    QFile::remove(dest);
                    // 0 = OK, 3010 = OK (redémarrage conseillé), 1638 = une version plus récente est déjà là
                    if (code == 0 || code == 3010 || code == 1638)
                        finish(id, true, code == 3010 ? tr("Runtime Visual C++ installé. Redémarre Windows pour terminer.")
                                                      : tr("Runtime Visual C++ installé."));
                    else if (code == -1)
                        finish(id, false, tr("Installation annulée (autorisation refusée)."));
                    else
                        finish(id, false, tr("L'installation a échoué (code %1).").arg(code));
                });
                w->setFuture(QtConcurrent::run([dest] { return runElevated(dest, QStringLiteral("/install /passive /norestart")); }));
            });
#else
        emit installFinished(id, true, tr("Non nécessaire sous Linux."));
#endif
        return;
    }
}
