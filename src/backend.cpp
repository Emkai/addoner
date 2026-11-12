#include "backend.h"

#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

Backend::Backend(AddonManager *manager, QObject *parent) : QObject(parent), m_manager(manager) {
    connect(m_manager, &AddonManager::changed, this, [this] {
        refresh();
        watchFolders();
    });

    // New downloads, folders added to local-addons by hand, and AddOns
    // changing under us all show up without a restart. Browsers rename a
    // finished download into place, so the zip is complete when it appears.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(300);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        refresh();
        watchFolders();
    });
    watchFolders();
    refresh();
    // Once at startup, after the window is up.
    QTimer::singleShot(0, this, &Backend::checkWeb);
}

void Backend::watchFolders() {
    const auto paths = m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);
    for (const QString &path : {m_manager->localDir(), m_manager->addonsDir(), m_manager->downloadsDir()})
        if (QDir(path).exists())
            m_watcher.addPath(path);
}

void Backend::refresh() {
    m_updates = m_manager->updates();
    m_downloads = m_manager->downloads().toVariantList();

    const QJsonObject addons = m_manager->installedAddons();
    m_installed.clear();
    m_webUpdateCount = 0;
    for (auto it = addons.begin(); it != addons.end(); ++it) {
        QVariantMap row = it.value().toObject().toVariantMap();
        row["main"] = it.key();
        row["folders"] = it.value().toObject().value("folders").toVariant().toStringList().join(", ");
        row["updateZip"] = m_updates.value(it.key());
        // Results for a page that's since been changed don't count.
        const QString page = row.value("page").toString();
        const bool checked = !page.isEmpty() && m_webPages.value(it.key()) == page && m_webResults.contains(it.key());
        const WebChecker::Result web = checked ? m_webResults.value(it.key()) : WebChecker::Result{};
        const bool newer = !web.version.isEmpty()
            && AddonManager::compareVersions(web.version, row.value("version").toString()) > 0;
        row["webChecked"] = checked;
        row["webVersion"] = web.version;
        row["webError"] = web.error;
        row["webNewer"] = newer;
        if (newer)
            ++m_webUpdateCount;
        m_installed.append(row);
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(m_installed.begin(), m_installed.end(), [&](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value("title").toString(), b.toMap().value("title").toString()) < 0;
    });
    emit listsChanged();
}

void Backend::report(const QString &error, const QString &okStatus) {
    m_error = error;
    m_status = error.isEmpty() ? okStatus : QString();
    emit statusChanged();
}

void Backend::clearMessage() { report({}); }

void Backend::rescan() {
    refresh();
    report({}, tr("%n addon zips in %1", "", m_downloads.size()).arg(m_manager->downloadsDir()));
}

void Backend::install(const QString &zipPath) {
    QString name;
    const QString error = m_manager->install(zipPath, &name);
    report(error, tr("Installed %1").arg(name));
}

void Backend::updateAll() {
    if (m_updates.isEmpty())
        return;
    QStringList names;
    const QString error = m_manager->updateAll(&names);
    report(error, tr("Updated %1").arg(names.join(", ")));
}

void Backend::uninstall(const QString &folder) {
    const QString title = m_manager->installedAddons().value(folder).toObject().value("title").toString(folder);
    report(m_manager->uninstall(folder), tr("Removed %1").arg(title));
}

void Backend::deleteZip(const QString &zipPath) {
    report(m_manager->deleteZip(zipPath), tr("Deleted %1").arg(QFileInfo(zipPath).fileName()));
}

void Backend::deleteInstalledZips() {
    QStringList files;
    const QString error = m_manager->deleteInstalledZips(&files);
    report(error, files.isEmpty() ? tr("No installed zips to delete") : tr("Deleted %1").arg(files.join(", ")));
}

void Backend::link(const QString &name) {
    report(m_manager->link(name), tr("Linked %1 into AddOns").arg(name));
}

void Backend::unlink(const QString &name) {
    report(m_manager->unlink(name), tr("Unlinked %1").arg(name));
}

QString Backend::createAddon(const QString &name, const QString &title, const QString &interfaceVersion) {
    const QString error = m_manager->createAddon(name.trimmed(), title, interfaceVersion.trimmed().toInt());
    report(error, tr("Created %1 in local addons").arg(name.trimmed()));
    return error;
}

void Backend::openFolder(const QString &path) {
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        report(tr("Couldn't open %1").arg(path));
}

void Backend::checkWeb() {
    if (m_pending > 0)
        return;
    const QJsonObject addons = m_manager->installedAddons();
    for (auto it = addons.begin(); it != addons.end(); ++it) {
        const QString page = it.value().toObject().value("page").toString();
        if (!page.isEmpty())
            checkPage(it.key(), page);
    }
    if (m_pending == 0 && !addons.isEmpty())
        report({}, tr("No addon has a web page to check yet. Add one with Page… on the Installed tab."));
}

void Backend::checkPage(const QString &main, const QString &page) {
    if (m_pending++ == 0)
        emit checkingChanged();
    m_web.check(QUrl(page), [this, main, page](const WebChecker::Result &result) {
        m_webResults[main] = result;
        m_webPages[main] = page;
        if (--m_pending == 0) {
            refresh();
            emit checkingChanged();
            report({}, m_webUpdateCount > 0
                           ? tr("%n newer versions on the web", "", m_webUpdateCount)
                           : tr("Checked the web: nothing newer"));
        }
    });
}

QString Backend::setPage(const QString &main, const QString &url) {
    const QString error = m_manager->setPage(main, url);
    report(error, url.trimmed().isEmpty() ? tr("Forgot the page") : tr("Saved the page"));
    const QString page = m_manager->installedAddons().value(main).toObject().value("page").toString();
    if (error.isEmpty() && !page.isEmpty())
        checkPage(main, page);
    return error;
}

void Backend::openPage(const QString &url) {
    if (url.isEmpty() || !QDesktopServices::openUrl(QUrl(url)))
        report(tr("Couldn't open %1").arg(url));
}

QString Backend::saveSettings(const QString &addonsDir, const QString &localDir, const QString &downloadsDir) {
    const QString error = m_manager->saveSettings(addonsDir, localDir, downloadsDir);
    report(error, tr("Settings saved"));
    if (error.isEmpty())
        emit settingsChanged();
    return error;
}
