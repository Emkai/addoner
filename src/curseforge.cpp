#include "curseforge.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QTimer>

static const char *ApiBase = "https://api.curseforge.com";

CurseForge::CurseForge(QObject *parent) : QObject(parent) {}

QNetworkRequest CurseForge::apiRequest(const QString &path, const QUrlQuery &query) const {
    QUrl url(QString(ApiBase) + path);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setRawHeader("x-api-key", m_key.toUtf8());
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, "addoner/" + QCoreApplication::applicationVersion());
    request.setTransferTimeout(30000);
    return request;
}

bool CurseForge::missingKey(const JsonCallback &done) {
    if (!m_key.isEmpty())
        return false;
    // Reported on the next turn of the event loop so callers always get their
    // answer asynchronously, the same as a real request.
    const QString error = tr("No CurseForge API key yet. Add one under Settings "
                             "(it's free at console.curseforge.com).");
    QTimer::singleShot(0, this, [done, error] { done({}, error); });
    return true;
}

void CurseForge::get(const QString &path, const QUrlQuery &query, JsonCallback done) {
    if (missingKey(done))
        return;
    handleJson(m_network.get(apiRequest(path, query)), std::move(done));
}

void CurseForge::post(const QString &path, const QJsonDocument &body, JsonCallback done) {
    if (missingKey(done))
        return;
    QNetworkRequest request = apiRequest(path, {});
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    handleJson(m_network.post(request, body.toJson(QJsonDocument::Compact)), std::move(done));
}

void CurseForge::handleJson(QNetworkReply *reply, JsonCallback done) {
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 403)
            return done({}, tr("CurseForge rejected the API key. Keys start with $2a$10$ and are "
                                "about 60 characters; copy yours from API keys at console.curseforge.com."));
        if (status == 404)
            return done({}, tr("CurseForge doesn't know that addon."));
        if (reply->error() != QNetworkReply::NoError)
            return done({}, tr("CurseForge request failed: %1").arg(reply->errorString()));
        QJsonParseError parse;
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parse);
        if (parse.error != QJsonParseError::NoError)
            return done({}, tr("CurseForge sent a reply that isn't JSON."));
        done(doc, {});
    });
}

void CurseForge::download(const QUrl &url, DataCallback done) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "addoner/" + QCoreApplication::applicationVersion());
    request.setTransferTimeout(120000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return done({}, reply->errorString());
        done(reply->readAll(), {});
    });
}
