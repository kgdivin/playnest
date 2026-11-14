#include "Net.h"

#include <QCoreApplication>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QSslSocket>

namespace {
QNetworkRequest makeRequest(const QUrl& url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Playnest/%1 (lanceur d'emulateurs)").arg(QCoreApplication::applicationVersion()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    // HTTP/1.1 : plus robuste que l'implémentation HTTP/2 de Qt 6.4 derrière certains proxys.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    if (url.host() == QLatin1String("api.github.com"))
        req.setRawHeader("Accept", "application/vnd.github+json");
    return req;
}
} // namespace

Net& Net::instance() {
    static Net n;
    return n;
}

Net::Net() {
    m_nam = new QNetworkAccessManager(this);
    m_tls = QSslSocket::supportsSsl();
    if (!m_tls)
        qInfo("TLS indisponible dans Qt : utilisation de curl pour les téléchargements HTTPS.");
}

void Net::get(const QUrl& url, QObject* context, DataCallback done, qint64 maxBytes) {
    if (!m_tls && url.scheme() == QLatin1String("https")) {
        curlGet(url, context, std::move(done));
        return;
    }
    QNetworkReply* reply = m_nam->get(makeRequest(url));
    QPointer<QObject> ctx(context);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 rec, qint64) {
        if (rec > maxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, ctx, done] {
        reply->deleteLater();
        if (!ctx)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            done(false, {}, reply->errorString());
            return;
        }
        done(true, reply->readAll(), {});
    });
}

void Net::download(const QUrl& url, const QString& filePath, QObject* context, ProgressCallback progress,
                   FileCallback done) {
    if (!m_tls && url.scheme() == QLatin1String("https")) {
        curlDownload(url, filePath, context, std::move(done));
        return;
    }
    auto* file = new QSaveFile(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        const QString err = file->errorString();
        delete file;
        done(false, err);
        return;
    }
    QNetworkRequest req = makeRequest(url);
    req.setTransferTimeout(120000);
    QNetworkReply* reply = m_nam->get(req);
    QPointer<QObject> ctx(context);
    connect(reply, &QNetworkReply::readyRead, reply, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, reply, [ctx, progress](qint64 rec, qint64 total) {
        if (ctx && progress)
            progress(rec, total);
    });
    connect(reply, &QNetworkReply::finished, this, [reply, file, ctx, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = reply->error() == QNetworkReply::NoError && status < 400;
        QString err = reply->errorString();
        if (ok) {
            file->write(reply->readAll());
            ok = file->commit();
            if (!ok)
                err = file->errorString();
        } else {
            file->cancelWriting();
            file->commit();
        }
        delete file;
        if (ctx)
            done(ok, ok ? QString() : err);
    });
}

void Net::curlGet(const QUrl& url, QObject* context, DataCallback done) {
    auto* p = new QProcess(this);
    QPointer<QObject> ctx(context);
    connect(p, &QProcess::finished, this, [p, ctx, done](int code, QProcess::ExitStatus) {
        p->deleteLater();
        if (!ctx)
            return;
        if (code != 0)
            done(false, {}, QString::fromLocal8Bit(p->readAllStandardError()).trimmed());
        else
            done(true, p->readAllStandardOutput(), {});
    });
    connect(p, &QProcess::errorOccurred, this, [p, ctx, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            if (ctx)
                done(false, {}, QStringLiteral("curl introuvable"));
        }
    });
    p->start(QStringLiteral("curl"), {QStringLiteral("-fsSL"), QStringLiteral("--max-time"), QStringLiteral("30"),
                                      QStringLiteral("-A"), QStringLiteral("Playnest"), url.toString(QUrl::FullyEncoded)});
}

void Net::curlDownload(const QUrl& url, const QString& filePath, QObject* context, FileCallback done) {
    auto* p = new QProcess(this);
    QPointer<QObject> ctx(context);
    connect(p, &QProcess::finished, this, [p, ctx, done](int code, QProcess::ExitStatus) {
        p->deleteLater();
        if (ctx)
            done(code == 0, code == 0 ? QString() : QString::fromLocal8Bit(p->readAllStandardError()).trimmed());
    });
    connect(p, &QProcess::errorOccurred, this, [p, ctx, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            if (ctx)
                done(false, QStringLiteral("curl introuvable"));
        }
    });
    p->start(QStringLiteral("curl"), {QStringLiteral("-fsSL"), QStringLiteral("-A"), QStringLiteral("Playnest"),
                                      QStringLiteral("-o"), filePath, url.toString(QUrl::FullyEncoded)});
}
