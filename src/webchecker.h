#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include <functional>

// Reads an addon's web page to see which version it offers. Nothing is
// downloaded or installed: the window only offers to open the page.
//
// WoWInterface pages and GitHub releases are read as plain pages. CurseForge
// puts a bot check in front of its site, so its pages aren't read at all.
// Other sites get a best guess from the first "Version: x.y" on the page.
class WebChecker : public QObject {
    Q_OBJECT

public:
    struct Result {
        QString version;  // empty when no version was found
        QString error;    // why there's no version, or empty
    };
    using Callback = std::function<void(const Result &)>;

    explicit WebChecker(QObject *parent = nullptr);

    // Calls done once, later, on this object's thread.
    void check(const QUrl &page, Callback done);

    // Building blocks, public for the tests.
    static bool isCurseForge(const QUrl &page);
    static bool isGitHub(const QUrl &page);
    // "owner/repo" from any github.com/owner/repo/... address.
    static QString gitHubRepo(const QUrl &page);
    // The version on a WoWInterface or other addon page.
    static QString versionFromPage(const QByteArray &html);
    // The tag in a /releases/tag/<tag> address or a tags.atom feed.
    static QString versionFromGitHub(const QByteArray &textOrUrl);
    // The page an addon's .toc points at, from X-Website, X-WoWI-ID or
    // X-Curse-Project-ID, in that order.
    static QString pageFromToc(const QString &website, const QString &wowiId, const QString &curseId);

private:
    QNetworkRequest request(const QUrl &url) const;
    void get(const QUrl &url, bool follow, std::function<void(int status, const QUrl &location, const QByteArray &body, const QString &error)> done);
    void checkGitHub(const QString &repo, Callback done);

    QNetworkAccessManager m_network;
};
