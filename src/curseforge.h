#pragma once

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>
#include <QUrlQuery>
#include <functional>

// A thin async client for the CurseForge Core API (https://docs.curseforge.com).
// Every call reports back through a callback with either a result or an error
// message that can be shown to the user as is.
class CurseForge : public QObject {
    Q_OBJECT

public:
    using JsonCallback = std::function<void(const QJsonDocument &doc, const QString &error)>;
    using DataCallback = std::function<void(const QByteArray &data, const QString &error)>;

    explicit CurseForge(QObject *parent = nullptr);

    void setApiKey(const QString &key) { m_key = key; }

    void get(const QString &path, const QUrlQuery &query, JsonCallback done);
    void post(const QString &path, const QJsonDocument &body, JsonCallback done);

    // Fetches a file from the CDN. The API key is deliberately not sent here,
    // since downloads go to a different host.
    void download(const QUrl &url, DataCallback done);

private:
    QNetworkRequest apiRequest(const QString &path, const QUrlQuery &query) const;
    void handleJson(QNetworkReply *reply, JsonCallback done);
    bool missingKey(const JsonCallback &done);

    QNetworkAccessManager m_network;
    QString m_key;
};
