#pragma once

#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "manager.h"

// The window's view of the AddonManager: lists QML can show and actions it
// can call. Only one install or update runs at a time (busy); searching is
// separate so browsing keeps working while something downloads.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY resultsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int resultsTotal READ resultsTotal NOTIFY resultsChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList installed READ installed NOTIFY installedChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY installedChanged)
    Q_PROPERTY(QStringList otherFolders READ otherFolders NOTIFY installedChanged)
    Q_PROPERTY(QVariantList localAddons READ localAddons NOTIFY localChanged)
    Q_PROPERTY(bool apiKeySet READ apiKeySet NOTIFY settingsChanged)
    Q_PROPERTY(QString addonsDir READ addonsDir NOTIFY settingsChanged)
    Q_PROPERTY(bool addonsDirExists READ addonsDirExists NOTIFY settingsChanged)
    Q_PROPERTY(QString localDir READ localDir NOTIFY settingsChanged)

public:
    explicit Backend(AddonManager *manager, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    bool searching() const { return m_searching; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QVariantList results() const;
    int resultsTotal() const { return m_total; }
    QString searchError() const { return m_searchError; }
    QVariantList installed() const;
    int updateCount() const { return m_manager->modsWithUpdates().size(); }
    QStringList otherFolders() const { return m_manager->otherFolders(); }
    QVariantList localAddons() const { return m_manager->localAddons().toVariantList(); }
    bool apiKeySet() const { return m_manager->hasApiKey(); }
    QString addonsDir() const { return m_manager->addonsDir(); }
    bool addonsDirExists() const { return m_manager->addonsDirExists(); }
    QString localDir() const { return m_manager->localDir(); }

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void install(int modId);
    Q_INVOKABLE void uninstall(int modId);
    Q_INVOKABLE void checkUpdates();
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void link(const QString &name);
    Q_INVOKABLE void unlink(const QString &name);
    Q_INVOKABLE QString createAddon(const QString &name, const QString &title, const QString &interfaceVersion);
    Q_INVOKABLE int suggestedInterface() const { return m_manager->guessInterface(); }
    Q_INVOKABLE void openLocalFolder();
    Q_INVOKABLE QString saveSettings(const QString &apiKey, const QString &addonsDir, const QString &localDir);
    Q_INVOKABLE void clearMessage();

signals:
    void busyChanged();
    void statusChanged();
    void resultsChanged();
    void installedChanged();
    void localChanged();
    void settingsChanged();

private:
    void runSearch(bool append);
    void begin(const QString &status);
    void finish(const QString &status, const QString &error);
    void report(const QString &error, const QString &okStatus = {});
    void watchFolders();

    AddonManager *m_manager;
    bool m_busy = false;
    bool m_searching = false;
    QString m_status;
    QString m_error;
    QString m_query;
    QJsonArray m_results;
    QString m_searchError;
    int m_total = 0;
    int m_searchGeneration = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
