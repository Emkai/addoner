#include "backend.h"

#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

Backend::Backend(AddonManager *manager, QObject *parent) : QObject(parent), m_manager(manager) {
    connect(m_manager, &AddonManager::changed, this, [this] {
        emit installedChanged();
        emit localChanged();
        emit resultsChanged();
        watchFolders();
    });

    // Folders added to local-addons by hand (or WoW's AddOns changing under
    // us) show up without a restart.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        emit localChanged();
        emit installedChanged();
        watchFolders();
    });
    watchFolders();

    if (m_manager->hasApiKey())
        QTimer::singleShot(0, this, [this] { checkUpdates(); });
}

void Backend::watchFolders() {
    const auto paths = m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);
    for (const QString &path : {m_manager->localDir(), m_manager->addonsDir()})
        if (QDir(path).exists())
            m_watcher.addPath(path);
}

QVariantList Backend::results() const {
    const QJsonObject installed = m_manager->installedMods();
    QVariantList rows;
    for (const auto &value : m_results) {
        const QJsonObject mod = value.toObject();
        const int id = mod.value("id").toInt();
        const QJsonObject record = installed.value(QString::number(id)).toObject();
        QStringList authors;
        for (const auto &author : mod.value("authors").toArray())
            authors.append(author.toObject().value("name").toString());
        const int latest = AddonManager::bestFile(mod).value("fileId").toInt();
        rows.append(QVariantMap{
            {"id", id},
            {"name", mod.value("name").toString()},
            {"summary", mod.value("summary").toString()},
            {"authors", authors.join(", ")},
            {"downloads", mod.value("downloadCount").toDouble()},
            {"logo", mod.value("logo").toObject().value("thumbnailUrl").toString()},
            {"url", mod.value("links").toObject().value("websiteUrl").toString()},
            {"installed", !record.isEmpty()},
            {"hasUpdate", !record.isEmpty() && latest > 0 && latest != record.value("fileId").toInt()},
        });
    }
    return rows;
}

QVariantList Backend::installed() const {
    const QJsonObject mods = m_manager->installedMods();
    QVariantList rows;
    for (auto it = mods.begin(); it != mods.end(); ++it) {
        QVariantMap row = it.value().toObject().toVariantMap();
        const int id = it.key().toInt();
        row["id"] = id;
        row["installed"] = true;
        row["hasUpdate"] = m_manager->hasUpdate(id);
        row["folders"] = it.value().toObject().value("folders").toVariant().toStringList().join(", ");
        rows.append(row);
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(rows.begin(), rows.end(), [&](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0;
    });
    return rows;
}

// ---------------------------------------------------------------- actions

void Backend::begin(const QString &status) {
    m_busy = true;
    m_status = status;
    m_error.clear();
    emit busyChanged();
    emit statusChanged();
}

void Backend::finish(const QString &status, const QString &error) {
    m_busy = false;
    m_status = error.isEmpty() ? status : QString();
    m_error = error;
    emit busyChanged();
    emit statusChanged();
}

void Backend::report(const QString &error, const QString &okStatus) {
    m_error = error;
    m_status = error.isEmpty() ? okStatus : QString();
    emit statusChanged();
}

void Backend::clearMessage() { report({}); }

void Backend::search(const QString &query) {
    m_query = query;
    runSearch(false);
}

void Backend::loadMore() {
    if (!m_searching && m_results.size() < m_total)
        runSearch(true);
}

void Backend::runSearch(bool append) {
    const int generation = ++m_searchGeneration;
    m_searching = true;
    emit resultsChanged();
    m_manager->search(m_query, append ? m_results.size() : 0,
                      [this, generation, append](const QJsonArray &mods, int total, const QString &error) {
        // A newer search started while this one was in flight; drop it.
        if (generation != m_searchGeneration)
            return;
        m_searching = false;
        // Search problems show where the results would be, not just in the
        // status bar, so a bad API key can't look like "nothing found".
        m_searchError = error;
        if (error.isEmpty()) {
            if (!append)
                m_results = {};
            for (const auto &mod : mods)
                m_results.append(mod);
            m_total = total;
        } else if (!append) {
            m_results = {};
            m_total = 0;
        }
        emit resultsChanged();
    });
}

void Backend::install(int modId) {
    if (m_busy)
        return;
    begin(tr("Installing…"));
    m_manager->install(modId, [this](const QStringList &names, const QString &error) {
        finish(names.isEmpty() ? QString() : tr("Installed %1").arg(names.join(", ")), error);
    });
}

void Backend::updateAll() {
    if (m_busy || m_manager->modsWithUpdates().isEmpty())
        return;
    begin(tr("Updating %n addons…", "", m_manager->modsWithUpdates().size()));
    m_manager->updateAll([this](const QStringList &names, const QString &error) {
        finish(tr("Updated %1").arg(names.join(", ")), error);
    });
}

void Backend::uninstall(int modId) {
    if (m_busy)
        return;
    const QString name = m_manager->installedMods().value(QString::number(modId)).toObject().value("name").toString();
    report(m_manager->uninstall(modId), tr("Removed %1").arg(name));
}

void Backend::checkUpdates() {
    if (m_busy)
        return;
    begin(tr("Checking for updates…"));
    m_manager->checkUpdates([this](const QString &error) {
        const int count = m_manager->modsWithUpdates().size();
        finish(count ? tr("%n updates available", "", count) : tr("Everything is up to date"), error);
    });
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

void Backend::openLocalFolder() {
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(m_manager->localDir())))
        report(tr("Couldn't open %1").arg(m_manager->localDir()));
}

QString Backend::saveSettings(const QString &apiKey, const QString &addonsDir, const QString &localDir) {
    const bool hadKey = m_manager->hasApiKey();
    const QString error = m_manager->saveSettings(apiKey, addonsDir, localDir);
    report(error, tr("Settings saved"));
    if (error.isEmpty()) {
        emit settingsChanged();
        watchFolders();
        if (!hadKey && m_manager->hasApiKey())
            search(m_query);
    }
    return error;
}
