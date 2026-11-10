#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>

#include "curseforge.h"

// Everything addoner does to the disk and to CurseForge. The window and the
// command line both drive this class, and tests construct it with temporary
// config and data directories.
class AddonManager : public QObject {
    Q_OBJECT

public:
    using SearchCallback = std::function<void(const QJsonArray &mods, int total, const QString &error)>;
    using InstallCallback = std::function<void(const QStringList &names, const QString &error)>;
    using DoneCallback = std::function<void(const QString &error)>;

    explicit AddonManager(const QString &configDir = {}, const QString &dataDir = {}, QObject *parent = nullptr);

    // Settings
    bool hasApiKey() const { return !m_apiKey.isEmpty(); }
    QString addonsDir() const { return m_addonsDir; }
    QString localDir() const { return m_localDir; }
    bool addonsDirExists() const;
    // An empty key keeps the saved one. Returns an error message, or empty.
    QString saveSettings(const QString &apiKey, const QString &addonsDir, const QString &localDir);

    // CurseForge addons
    void search(const QString &query, int index, SearchCallback done);
    void install(int modId, InstallCallback done);
    void updateAll(InstallCallback done);
    void checkUpdates(DoneCallback done);
    QString uninstall(int modId);

    QJsonObject installedMods() const;  // modId (as text) -> record
    bool isInstalled(int modId) const;
    bool hasUpdate(int modId) const;
    QList<int> modsWithUpdates() const;
    QStringList otherFolders() const;

    // Local addons, linked into AddOns so edits show up after /reload.
    QJsonArray localAddons() const;
    QString link(const QString &name);
    QString unlink(const QString &name);
    QString createAddon(const QString &name, const QString &title, int interfaceVersion);
    int guessInterface() const;

    // Building blocks, public for the tests.
    QString installZip(const QString &zipPath, const QStringList &replace, QStringList *folders);
    void recordInstall(int modId, const QJsonObject &record);
    static QJsonObject bestFile(const QJsonObject &mod);
    static QUrl downloadUrl(const QJsonObject &file);
    static int interfaceFromVersion(const QString &version);
    static bool isLink(const QString &path);
    static QString tarProgram();

signals:
    void changed();

private:
    struct InstallJob;
    using FileCallback = std::function<void(const QJsonObject &file, const QString &name, const QString &error)>;

    void runJob(const std::shared_ptr<InstallJob> &job);
    void installOne(int modId, FileCallback done);
    void loadSettings();
    void setMods(const QJsonObject &mods);
    QString localAddonPath(const QString &name, QString *error) const;

    QString m_configFile;
    QString m_stateFile;
    QString m_apiKey;
    QString m_addonsDir;
    QString m_localDir;
    QJsonObject m_state;       // AddOns path -> { modId -> record }
    QHash<int, int> m_latest;  // modId -> newest fileId CurseForge offers
    CurseForge m_curseforge;
};
