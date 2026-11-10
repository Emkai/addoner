#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

// Everything addoner does to the disk: addon zips found in the downloads
// folder, what was installed from them, and local addons linked into AddOns.
// The window and the command line both drive this class, and tests construct
// it with temporary config and data directories.
class AddonManager : public QObject {
    Q_OBJECT

public:
    explicit AddonManager(const QString &configDir = {}, const QString &dataDir = {}, QObject *parent = nullptr);

    // Settings
    QString addonsDir() const { return m_addonsDir; }
    QString localDir() const { return m_localDir; }
    QString downloadsDir() const { return m_downloadsDir; }
    bool addonsDirExists() const;
    // Returns an error message, or empty.
    QString saveSettings(const QString &addonsDir, const QString &localDir, const QString &downloadsDir);

    // Addon zips in the downloads folder, newest first. Zips without an addon
    // in them (photos, invoices) are left out. Each has a status: "new",
    // "installed" (same version), "update", "older" (AddOns has a newer
    // version) or "linked" (a local addon has that folder name).
    QJsonArray downloads() const;
    // Installs or updates from a zip, replacing the folders the previous
    // install of that addon put there. Sets *name to the addon's title.
    QString install(const QString &zipPath, QString *name = nullptr);
    // Installs the newest update zip of every addon that has one.
    QString updateAll(QStringList *names);
    QString uninstall(const QString &folder);

    // Main folder name -> record, for addons installed from zips.
    QJsonObject installedAddons() const;
    // Main folder name -> zip path of the newest update for it.
    QHash<QString, QString> updates() const;
    QStringList otherFolders() const;

    // Local addons, linked into AddOns so edits show up after /reload.
    QJsonArray localAddons() const;
    QString link(const QString &name);
    QString unlink(const QString &name);
    QString createAddon(const QString &name, const QString &title, int interfaceVersion);
    int guessInterface() const;

    // Building blocks, public for the tests.
    struct ZipInfo {
        QStringList folders;  // addon folders as they'll be named in AddOns
        QString main;         // the folder the zip is named after
        QString title;
        QString version;
    };
    ZipInfo inspectZip(const QString &zipPath) const;
    QString installZip(const QString &zipPath, const QStringList &replace, QStringList *folders);
    static int compareVersions(const QString &a, const QString &b);
    static int interfaceFromVersion(const QString &version);
    static bool isLink(const QString &path);
    static QString tarProgram();

signals:
    void changed();

private:
    struct CachedZip {
        qint64 size = -1;
        QDateTime modified;
        ZipInfo info;
    };

    void loadSettings();
    void setInstalled(const QJsonObject &addons);
    QString localAddonPath(const QString &name, QString *error) const;

    QString m_configFile;
    QString m_stateFile;
    QString m_addonsDir;
    QString m_localDir;
    QString m_downloadsDir;
    QJsonObject m_state;  // AddOns path -> { main folder -> record }
    // Looking inside a zip runs tar, so each one is read once until it changes.
    mutable QHash<QString, CachedZip> m_zipCache;
};
