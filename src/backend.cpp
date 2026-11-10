#include "backend.h"

#include <QCollator>
#include <QDesktopServices>
#include <QDir>
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
    for (auto it = addons.begin(); it != addons.end(); ++it) {
        QVariantMap row = it.value().toObject().toVariantMap();
        row["main"] = it.key();
        row["folders"] = it.value().toObject().value("folders").toVariant().toStringList().join(", ");
        row["updateZip"] = m_updates.value(it.key());
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

QString Backend::saveSettings(const QString &addonsDir, const QString &localDir, const QString &downloadsDir) {
    const QString error = m_manager->saveSettings(addonsDir, localDir, downloadsDir);
    report(error, tr("Settings saved"));
    if (error.isEmpty())
        emit settingsChanged();
    return error;
}
