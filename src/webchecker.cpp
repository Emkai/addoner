#include "webchecker.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>

WebChecker::WebChecker(QObject *parent) : QObject(parent) {}

bool WebChecker::isCurseForge(const QUrl &page) {
    const QString host = page.host().toLower();
    return host == "curseforge.com" || host.endsWith(".curseforge.com");
}

bool WebChecker::isGitHub(const QUrl &page) {
    const QString host = page.host().toLower();
    return (host == "github.com" || host == "www.github.com") && !gitHubRepo(page).isEmpty();
}

QString WebChecker::gitHubRepo(const QUrl &page) {
    const QStringList parts = page.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return {};
    QString repo = parts[1];
    if (repo.endsWith(".git"))
        repo.chop(4);
    return parts[0] + "/" + repo;
}

QString WebChecker::versionFromPage(const QByteArray &html) {
    const QString page = QString::fromUtf8(html);
    // WoWInterface: <div id="version">Version: 12.1.9</div>
    static const QRegularExpression wowi(R"(<div id="version">\s*Version:\s*([^<]+?)\s*</div>)");
    if (const auto match = wowi.match(page); match.hasMatch())
        return match.captured(1);
    // Anything else: the first "Version: 1.2.3" in the page's text.
    QString text = page;
    static const QRegularExpression tags("<[^>]*>");
    text.replace(tags, " ");
    static const QRegularExpression generic(R"(\bVersion\s*:?\s*(v?\d+(?:\.\d+)+(?:-[0-9A-Za-z.]+)?))",
                                            QRegularExpression::CaseInsensitiveOption);
    if (const auto match = generic.match(text); match.hasMatch())
        return match.captured(1);
    return {};
}

QString WebChecker::versionFromGitHub(const QByteArray &textOrUrl) {
    static const QRegularExpression tag(R"(/releases/tag/([^"'<>\s?#]+))");
    const auto match = tag.match(QString::fromUtf8(textOrUrl));
    return match.hasMatch() ? QUrl::fromPercentEncoding(match.captured(1).toUtf8()) : QString();
}

QString WebChecker::pageFromToc(const QString &website, const QString &wowiId, const QString &curseId) {
    const QUrl url = QUrl::fromUserInput(website.trimmed());
    if (!website.trimmed().isEmpty() && url.isValid() && (url.scheme() == "http" || url.scheme() == "https"))
        return url.toString();
    static const QRegularExpression digits("^\\d+$");
    if (digits.match(wowiId.trimmed()).hasMatch())
        return "https://www.wowinterface.com/downloads/info" + wowiId.trimmed() + ".html";
    if (digits.match(curseId.trimmed()).hasMatch())
        return "https://www.curseforge.com/projects/" + curseId.trimmed();
    return {};
}

QNetworkRequest WebChecker::request(const QUrl &url) const {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "addoner/" + QCoreApplication::applicationVersion());
    request.setTransferTimeout(30000);
    return request;
}

void WebChecker::get(const QUrl &url, bool follow,
                     std::function<void(int, const QUrl &, const QByteArray &, const QString &)> done) {
    QNetworkRequest req = request(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     follow ? QNetworkRequest::NoLessSafeRedirectPolicy : QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = m_network.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl location = reply->url().resolved(reply->header(QNetworkRequest::LocationHeader).toUrl());
        // A redirect we didn't follow isn't a failure.
        const bool redirect = status >= 300 && status < 400;
        QString error;
        if (!redirect && reply->error() != QNetworkReply::NoError)
            error = status > 0 ? tr("the site answered %1").arg(status) : reply->errorString();
        done(status, location, redirect ? QByteArray() : reply->readAll(), error);
    });
}

void WebChecker::check(const QUrl &page, Callback done) {
    if (!page.isValid() || (page.scheme() != "http" && page.scheme() != "https")) {
        QTimer::singleShot(0, this, [done] { done({{}, tr("not a web address")}); });
        return;
    }
    if (isCurseForge(page)) {
        QTimer::singleShot(0, this, [done] { done({{}, tr("CurseForge doesn't let programs read its pages")}); });
        return;
    }
    if (isGitHub(page))
        return checkGitHub(gitHubRepo(page), std::move(done));

    get(page, true, [done](int, const QUrl &, const QByteArray &body, const QString &error) {
        if (!error.isEmpty())
            return done({{}, error});
        const QString version = versionFromPage(body);
        done({version, version.isEmpty() ? tr("no version on the page") : QString()});
    });
}

// /releases/latest redirects to the newest release's tag. Repositories
// without releases redirect to /releases instead; their newest tag comes
// from the tags feed.
void WebChecker::checkGitHub(const QString &repo, Callback done) {
    const QString base = "https://github.com/" + repo;
    get(QUrl(base + "/releases/latest"), false,
        [this, base, done](int, const QUrl &location, const QByteArray &, const QString &error) {
            if (!error.isEmpty())
                return done({{}, error});
            const QString version = versionFromGitHub(location.toString().toUtf8());
            if (!version.isEmpty())
                return done({version, {}});
            get(QUrl(base + "/tags.atom"), true, [done](int, const QUrl &, const QByteArray &body, const QString &error) {
                if (!error.isEmpty())
                    return done({{}, error});
                const QString tag = versionFromGitHub(body);
                done({tag, tag.isEmpty() ? tr("no releases or tags yet") : QString()});
            });
        });
}
