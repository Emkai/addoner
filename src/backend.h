#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "manager.h"
#include "webchecker.h"

// The window's view of the AddonManager: lists QML can show and actions it
// can call. The lists are rebuilt when the manager changes or when any of
// the watched folders (downloads, AddOns, local addons) changes on disk.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(QVariantList downloads READ downloads NOTIFY listsChanged)
    Q_PROPERTY(QVariantList installed READ installed NOTIFY listsChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY listsChanged)
    Q_PROPERTY(int webUpdateCount READ webUpdateCount NOTIFY listsChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(QStringList otherFolders READ otherFolders NOTIFY listsChanged)
    Q_PROPERTY(QVariantList localAddons READ localAddons NOTIFY listsChanged)
    Q_PROPERTY(QString addonsDir READ addonsDir NOTIFY settingsChanged)
    Q_PROPERTY(bool addonsDirExists READ addonsDirExists NOTIFY settingsChanged)
    Q_PROPERTY(QString localDir READ localDir NOTIFY settingsChanged)
    Q_PROPERTY(QString downloadsDir READ downloadsDir NOTIFY settingsChanged)

public:
    explicit Backend(AddonManager *manager, QObject *parent = nullptr);

    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QVariantList downloads() const { return m_downloads; }
    QVariantList installed() const { return m_installed; }
    int updateCount() const { return m_updates.size(); }
    int webUpdateCount() const { return m_webUpdateCount; }
    bool checking() const { return m_pending > 0; }
    QStringList otherFolders() const { return m_manager->otherFolders(); }
    QVariantList localAddons() const { return m_manager->localAddons().toVariantList(); }
    QString addonsDir() const { return m_manager->addonsDir(); }
    bool addonsDirExists() const { return m_manager->addonsDirExists(); }
    QString localDir() const { return m_manager->localDir(); }
    QString downloadsDir() const { return m_manager->downloadsDir(); }

    Q_INVOKABLE void install(const QString &zipPath);
    Q_INVOKABLE void uninstall(const QString &folder);
    Q_INVOKABLE void deleteZip(const QString &zipPath);
    Q_INVOKABLE void deleteInstalledZips();
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void link(const QString &name);
    Q_INVOKABLE void unlink(const QString &name);
    Q_INVOKABLE QString createAddon(const QString &name, const QString &title, const QString &interfaceVersion);
    Q_INVOKABLE int suggestedInterface() const { return m_manager->guessInterface(); }
    Q_INVOKABLE void openFolder(const QString &path);
    // Reads every installed addon's page for its newest version.
    Q_INVOKABLE void checkWeb();
    Q_INVOKABLE QString setPage(const QString &main, const QString &url);
    Q_INVOKABLE void openPage(const QString &url);
    Q_INVOKABLE QString saveSettings(const QString &addonsDir, const QString &localDir, const QString &downloadsDir);
    Q_INVOKABLE void clearMessage();

signals:
    void statusChanged();
    void listsChanged();
    void settingsChanged();
    void checkingChanged();

private:
    void refresh();
    void report(const QString &error, const QString &okStatus = {});
    void watchFolders();
    void checkPage(const QString &main, const QString &page);

    AddonManager *m_manager;
    QString m_status;
    QString m_error;
    QVariantList m_downloads;
    QVariantList m_installed;
    QHash<QString, QString> m_updates;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;

    WebChecker m_web;
    QHash<QString, WebChecker::Result> m_webResults;  // main folder -> what its page said
    QHash<QString, QString> m_webPages;               // main folder -> the page that was read
    int m_pending = 0;
    int m_webUpdateCount = 0;
};
