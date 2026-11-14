#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

// Accès réseau minimal : QNetworkAccessManager si TLS est disponible,
// sinon repli sur curl (présent par défaut sur Windows 10/11).
class Net : public QObject {
    Q_OBJECT
public:
    static Net& instance();

    using DataCallback = std::function<void(bool ok, const QByteArray& data, const QString& error)>;
    using ProgressCallback = std::function<void(qint64 received, qint64 total)>;
    using FileCallback = std::function<void(bool ok, const QString& error)>;

    // Requête GET en mémoire (taille limitée à maxBytes).
    void get(const QUrl& url, QObject* context, DataCallback done, qint64 maxBytes = 32 * 1024 * 1024);
    // Téléchargement vers un fichier.
    void download(const QUrl& url, const QString& filePath, QObject* context, ProgressCallback progress,
                  FileCallback done);

    bool tlsAvailable() const { return m_tls; }

private:
    Net();
    void curlGet(const QUrl& url, QObject* context, DataCallback done);
    void curlDownload(const QUrl& url, const QString& filePath, QObject* context, FileCallback done);
    QNetworkAccessManager* m_nam = nullptr;
    bool m_tls = false;
};
