#pragma once

#include <QObject>
#include <QSet>
#include <QString>

// Outils et composants système dont les émulateurs ont besoin.
//  - vcredist : runtime Microsoft Visual C++ 2015-2022 x64 (Eden, Lichii, Azahar, Dolphin, Cemu…)
//  - 7zip     : extraction des archives .7z (Dolphin)
//  - vulkan   : API graphique utilisée par les émulateurs récents (fournie par le pilote GPU)
class Tools : public QObject {
    Q_OBJECT
public:
    static Tools& instance();

    struct Status {
        bool ok = false;
        bool installable = false;
        QString detail;
    };
    Status status(const QString& id) const;
    QString label(const QString& id) const;

    QString sevenZipPath() const;   // thread-safe (lecture disque)
    void checkVulkanAsync();
    bool isBusy(const QString& id) const { return m_busy.contains(id); }
    void install(const QString& id);

signals:
    void changed();
    void installProgress(const QString& id, const QString& stage, qint64 received, qint64 total);
    void installFinished(const QString& id, bool ok, const QString& message);

private:
    Tools() = default;
    Status vcredistStatus() const;
    void finish(const QString& id, bool ok, const QString& msg);

    QSet<QString> m_busy;
    bool m_vulkanChecked = false;
    bool m_vulkanOk = false;
    QString m_vulkanDetail;
};
