#pragma once

#include <QObject>
#include <QString>

// Mise à jour de Playnest lui-même depuis les releases GitHub du dépôt configuré.
// L'exécutable en cours est renommé en .old (autorisé par Windows), la nouvelle
// version est copiée à sa place puis relancée. Le dossier data/ n'est jamais touché.
class SelfUpdate : public QObject {
    Q_OBJECT
public:
    static SelfUpdate& instance();

    QString repo() const;
    void setRepo(const QString& repo);
    void cleanupOldFiles();           // au démarrage : supprime les .old
    void check(bool force);
    void apply();
    bool available() const { return !m_url.isEmpty(); }
    QString latestVersion() const { return m_version; }
    QString notes() const { return m_notes; }
    bool busy() const { return m_busy; }

signals:
    void checked(bool available, const QString& version, const QString& error);
    void progress(const QString& stage, qint64 received, qint64 total);
    void failed(const QString& message);

private:
    SelfUpdate() = default;
    QString m_version, m_url, m_asset, m_notes, m_page;
    bool m_busy = false;
};
