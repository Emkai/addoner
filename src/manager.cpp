#include "manager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

namespace {

const int WowGameId = 1;
const int RequiredDependency = 3;  // CurseForge's relationType for "required"

QString defaultAddonsDir() {
#ifdef Q_OS_WIN
    return "C:/Program Files (x86)/World of Warcraft/_retail_/Interface/AddOns";
#else
    return QDir::homePath() + "/Games/battlenet/drive_c/Program Files (x86)/"
                              "World of Warcraft/_forever_/Interface/AddOns";
#endif
}

QString defaultLocalDir() {
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Addoner/local-addons";
#else
    return QDir::homePath() + "/src/addoner/local-addons";
#endif
}

// Local addons are symlinks on Linux and directory junctions on Windows:
// WoW follows both, and junctions need no admin rights (a Qt link there would
// be a .lnk shortcut, which WoW ignores).
QString linkTarget(const QString &path) {
    const QFileInfo info(path);
#ifdef Q_OS_WIN
    if (info.isJunction())
        return info.junctionTarget();
#endif
    return info.symLinkTarget();
}

bool makeLink(const QString &source, const QString &target) {
#ifdef Q_OS_WIN
    QProcess mklink;
    mklink.start("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(target), QDir::toNativeSeparators(source)});
    return mklink.waitForFinished(10000) && mklink.exitStatus() == QProcess::NormalExit && mklink.exitCode() == 0;
#else
    return QFile::link(source, target);
#endif
}

// Removes only the link, never what it points at.
bool removeLink(const QString &path) {
#ifdef Q_OS_WIN
    return QDir().rmdir(path);
#else
    return QFile::remove(path);
#endif
}

QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Written beside the target and renamed into place, so a crash never leaves
// half a file behind.
bool writeJson(const QString &path, const QJsonObject &object, bool privateFile = false) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (privateFile)
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(object).toJson());
    return file.commit();
}

// A single folder name: nothing that could climb out of the AddOns folder.
bool safeName(const QString &name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') && !name.contains('\\');
}

// Removes a real folder or file. Links are never followed or removed here:
// they point at local addons, which belong to the user.
bool removeRealEntry(const QString &path) {
    const QFileInfo info(path);
    if (AddonManager::isLink(path))
        return false;
    if (info.isDir())
        return QDir(path).removeRecursively();
    if (info.exists())
        return QFile::remove(path);
    return true;
}

QString tocTitle(const QString &folder) {
    static const QRegularExpression colors(R"(\|c[0-9a-fA-F]{8}|\|r)");
    const QDir dir(folder);
    for (const QString &toc : dir.entryList({"*.toc"}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(toc));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith("## Title:", Qt::CaseInsensitive))
                return line.mid(9).remove(colors).trimmed();
        }
    }
    return {};
}

}  // namespace

struct AddonManager::InstallJob {
    QList<int> queue;
    QSet<int> forced;  // installed even if already present (the ones asked for)
    QSet<int> seen;
    QStringList names;
    InstallCallback done;
};

AddonManager::AddonManager(const QString &configDir, const QString &dataDir, QObject *parent)
    : QObject(parent) {
    const QString config = configDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/addoner" : configDir;
    const QString data = dataDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/addoner" : dataDir;
    m_configFile = config + "/config.json";
    m_stateFile = data + "/installed.json";
    loadSettings();
    m_state = readJson(m_stateFile);
}

void AddonManager::loadSettings() {
    const QJsonObject config = readJson(m_configFile);
    m_apiKey = config.value("apiKey").toString();
    if (!qEnvironmentVariableIsEmpty("CURSEFORGE_API_KEY"))
        m_apiKey = qEnvironmentVariable("CURSEFORGE_API_KEY");
    m_addonsDir = config.value("addonsDir").toString(defaultAddonsDir());
    m_localDir = config.value("localDir").toString(defaultLocalDir());
    QDir().mkpath(m_localDir);
    m_curseforge.setApiKey(m_apiKey);
}

bool AddonManager::addonsDirExists() const { return QFileInfo(m_addonsDir).isDir(); }

QString AddonManager::saveSettings(const QString &apiKey, const QString &addonsDir, const QString &localDir) {
    const QString addons = QDir::cleanPath(addonsDir.trimmed());
    const QString local = QDir::cleanPath(localDir.trimmed());
    if (!QFileInfo(addons).isDir())
        return tr("That AddOns folder doesn't exist: %1").arg(addons);
    if (local.isEmpty() || !QDir().mkpath(local))
        return tr("Can't create the local addons folder: %1").arg(local);

    QJsonObject config = readJson(m_configFile);
    if (!apiKey.trimmed().isEmpty())
        config["apiKey"] = apiKey.trimmed();
    config["addonsDir"] = addons;
    config["localDir"] = local;
    if (!writeJson(m_configFile, config, true))
        return tr("Couldn't save settings to %1").arg(m_configFile);
    loadSettings();
    m_latest.clear();
    emit changed();
    return {};
}

// ---------------------------------------------------------------- CurseForge

void AddonManager::search(const QString &query, int index, SearchCallback done) {
    QUrlQuery params;
    params.addQueryItem("gameId", QString::number(WowGameId));
    params.addQueryItem("sortField", "2");  // popularity
    params.addQueryItem("sortOrder", "desc");
    params.addQueryItem("pageSize", "30");
    params.addQueryItem("index", QString::number(index));
    if (!query.trimmed().isEmpty())
        params.addQueryItem("searchFilter", query.trimmed());
    m_curseforge.get("/v1/mods/search", params, [done](const QJsonDocument &doc, const QString &error) {
        if (!error.isEmpty())
            return done({}, 0, error);
        const QJsonObject root = doc.object();
        done(root.value("data").toArray(), root.value("pagination").toObject().value("totalCount").toInt(), {});
    });
}

// The newest release, or the newest beta or alpha when a project has no
// releases. File ids only grow, so the highest id is the newest upload.
QJsonObject AddonManager::bestFile(const QJsonObject &mod) {
    QJsonObject best;
    int bestType = 99, bestId = 0;
    auto consider = [&](int fileId, int releaseType, const QJsonObject &entry) {
        if (fileId <= 0)
            return;
        if (releaseType < 1 || releaseType > 3)
            releaseType = 3;
        if (releaseType < bestType || (releaseType == bestType && fileId > bestId)) {
            best = entry;
            bestType = releaseType;
            bestId = fileId;
        }
    };
    for (const auto &value : mod.value("latestFilesIndexes").toArray()) {
        const QJsonObject index = value.toObject();
        consider(index.value("fileId").toInt(), index.value("releaseType").toInt(), index);
    }
    if (best.isEmpty()) {
        for (const auto &value : mod.value("latestFiles").toArray()) {
            const QJsonObject file = value.toObject();
            consider(file.value("id").toInt(), file.value("releaseType").toInt(),
                     QJsonObject{{"fileId", file.value("id")}, {"filename", file.value("fileName")},
                                 {"releaseType", file.value("releaseType")}});
        }
    }
    return best;
}

// Some authors turn off third-party download links, which leaves downloadUrl
// empty; the CDN path is predictable from the file id, so build it instead.
QUrl AddonManager::downloadUrl(const QJsonObject &file) {
    const QString url = file.value("downloadUrl").toString();
    if (!url.isEmpty())
        return QUrl(url);
    const int id = file.value("id").toInt();
    const QString name = QString::fromUtf8(QUrl::toPercentEncoding(file.value("fileName").toString()));
    return QUrl(QString("https://edge.forgecdn.net/files/%1/%2/%3").arg(id / 1000).arg(id % 1000).arg(name));
}

void AddonManager::install(int modId, InstallCallback done) {
    auto job = std::make_shared<InstallJob>();
    job->queue = {modId};
    job->forced = {modId};
    job->done = std::move(done);
    runJob(job);
}

void AddonManager::updateAll(InstallCallback done) {
    auto job = std::make_shared<InstallJob>();
    job->queue = modsWithUpdates();
    job->forced = QSet<int>(job->queue.begin(), job->queue.end());
    job->done = std::move(done);
    runJob(job);
}

// Installs the queue one addon at a time, adding each one's required
// libraries to the end of it, and skipping libraries already installed.
void AddonManager::runJob(const std::shared_ptr<InstallJob> &job) {
    while (!job->queue.isEmpty()) {
        const int modId = job->queue.takeFirst();
        if (job->seen.contains(modId) || (isInstalled(modId) && !job->forced.contains(modId)))
            continue;
        job->seen.insert(modId);
        installOne(modId, [this, job](const QJsonObject &file, const QString &name, const QString &error) {
            if (!error.isEmpty()) {
                job->done(job->names, error);
                return;
            }
            job->names.append(name);
            emit changed();
            for (const auto &value : file.value("dependencies").toArray()) {
                const QJsonObject dependency = value.toObject();
                if (dependency.value("relationType").toInt() == RequiredDependency)
                    job->queue.append(dependency.value("modId").toInt());
            }
            runJob(job);
        });
        return;
    }
    job->done(job->names, {});
}

void AddonManager::installOne(int modId, FileCallback done) {
    if (!addonsDirExists()) {
        const QString error = tr("The AddOns folder doesn't exist: %1").arg(m_addonsDir);
        QMetaObject::invokeMethod(this, [done, error] { done({}, {}, error); }, Qt::QueuedConnection);
        return;
    }
    m_curseforge.get(QString("/v1/mods/%1").arg(modId), {}, [this, modId, done](const QJsonDocument &doc, const QString &error) {
        if (!error.isEmpty())
            return done({}, {}, error);
        const QJsonObject mod = doc.object().value("data").toObject();
        const QString name = mod.value("name").toString(QString::number(modId));
        const QJsonObject best = bestFile(mod);
        if (best.isEmpty())
            return done({}, name, tr("%1 has no files to download.").arg(name));

        const QString path = QString("/v1/mods/%1/files/%2").arg(modId).arg(best.value("fileId").toInt());
        m_curseforge.get(path, {}, [this, modId, mod, name, done](const QJsonDocument &fileDoc, const QString &fileError) {
            if (!fileError.isEmpty())
                return done({}, name, fileError);
            const QJsonObject file = fileDoc.object().value("data").toObject();

            m_curseforge.download(downloadUrl(file), [this, modId, mod, name, file, done](const QByteArray &data, const QString &downloadError) {
                if (!downloadError.isEmpty())
                    return done({}, name, tr("Couldn't download %1: %2").arg(name, downloadError));
                QTemporaryFile zip(QDir::tempPath() + "/addoner-XXXXXX.zip");
                if (!zip.open() || zip.write(data) != data.size() || !zip.flush())
                    return done({}, name, tr("Couldn't save the download of %1.").arg(name));

                const QJsonObject previous = installedMods().value(QString::number(modId)).toObject();
                QStringList replace, folders;
                for (const auto &folder : previous.value("folders").toArray())
                    replace.append(folder.toString());
                const QString error = installZip(zip.fileName(), replace, &folders);
                if (!error.isEmpty())
                    return done({}, name, tr("%1: %2").arg(name, error));

                recordInstall(modId, QJsonObject{
                    {"name", name},
                    {"fileId", file.value("id")},
                    {"fileName", file.value("fileName")},
                    {"displayName", file.value("displayName")},
                    {"folders", QJsonArray::fromStringList(folders)},
                    {"logo", mod.value("logo").toObject().value("thumbnailUrl")},
                    {"url", mod.value("links").toObject().value("websiteUrl")},
                    {"summary", mod.value("summary")},
                });
                m_latest[modId] = file.value("id").toInt();
                done(file, name, {});
            });
        });
    });
}

// Unpacks into a staging folder inside AddOns (so moving is a rename), then
// swaps each top-level folder in. bsdtar refuses absolute paths and ".." by
// default, so nothing in the zip can land outside the staging folder.
QString AddonManager::installZip(const QString &zipPath, const QStringList &replace, QStringList *folders) {
    const QDir addons(m_addonsDir);
    if (!addons.exists())
        return tr("The AddOns folder doesn't exist: %1").arg(m_addonsDir);
    QTemporaryDir staging(addons.filePath(".addoner-XXXXXX"));
    if (!staging.isValid())
        return tr("Can't write to the AddOns folder.");

    QProcess tar;
    tar.start(tarProgram(), {"-xf", zipPath, "-C", staging.path()});
    if (!tar.waitForStarted())
        return tr("%1 is missing, so zips can't be unpacked.").arg(tarProgram());
    tar.waitForFinished(120000);
    if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0)
        return tr("Couldn't unpack the download: %1").arg(QString::fromUtf8(tar.readAllStandardError()).trimmed());

    const QStringList found = QDir(staging.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (found.isEmpty())
        return tr("The download has no addon folders in it.");
    for (const QString &name : found) {
        if (isLink(addons.filePath(name)))
            return tr("%1 is linked from your local addons. Unlink it first.").arg(name);
    }

    for (const QString &name : replace) {
        if (safeName(name) && !found.contains(name))
            removeRealEntry(addons.filePath(name));
    }
    for (const QString &name : found) {
        const QString target = addons.filePath(name);
        if (!removeRealEntry(target))
            return tr("Couldn't replace %1.").arg(target);
        if (!QDir().rename(QDir(staging.path()).filePath(name), target))
            return tr("Couldn't move %1 into AddOns.").arg(name);
    }
    *folders = found;
    return {};
}

QJsonObject AddonManager::installedMods() const { return m_state.value(m_addonsDir).toObject(); }

void AddonManager::setMods(const QJsonObject &mods) {
    m_state[m_addonsDir] = mods;
    writeJson(m_stateFile, m_state);
}

void AddonManager::recordInstall(int modId, const QJsonObject &record) {
    QJsonObject mods = installedMods();
    mods[QString::number(modId)] = record;
    setMods(mods);
}

bool AddonManager::isInstalled(int modId) const { return installedMods().contains(QString::number(modId)); }

bool AddonManager::hasUpdate(int modId) const {
    const int latest = m_latest.value(modId);
    const int current = installedMods().value(QString::number(modId)).toObject().value("fileId").toInt();
    return latest > 0 && current > 0 && latest != current;
}

QList<int> AddonManager::modsWithUpdates() const {
    QList<int> ids;
    for (const QString &key : installedMods().keys())
        if (hasUpdate(key.toInt()))
            ids.append(key.toInt());
    return ids;
}

void AddonManager::checkUpdates(DoneCallback done) {
    const QJsonObject mods = installedMods();
    if (mods.isEmpty()) {
        QMetaObject::invokeMethod(this, [done] { done({}); }, Qt::QueuedConnection);
        return;
    }
    QJsonArray ids;
    for (const QString &key : mods.keys())
        ids.append(key.toInt());
    m_curseforge.post("/v1/mods", QJsonDocument(QJsonObject{{"modIds", ids}}),
                      [this, done](const QJsonDocument &doc, const QString &error) {
        if (!error.isEmpty())
            return done(error);
        for (const auto &value : doc.object().value("data").toArray()) {
            const QJsonObject mod = value.toObject();
            m_latest[mod.value("id").toInt()] = bestFile(mod).value("fileId").toInt();
        }
        emit changed();
        done({});
    });
}

QString AddonManager::uninstall(int modId) {
    QJsonObject mods = installedMods();
    const QString key = QString::number(modId);
    if (!mods.contains(key))
        return tr("That addon isn't installed.");
    const QJsonObject record = mods.take(key).toObject();

    // Libraries are often bundled by several addons; keep folders another
    // installed addon still uses.
    QSet<QString> stillUsed;
    for (const auto &other : mods)
        for (const auto &folder : other.toObject().value("folders").toArray())
            stillUsed.insert(folder.toString());
    const QDir addons(m_addonsDir);
    for (const auto &value : record.value("folders").toArray()) {
        const QString folder = value.toString();
        if (safeName(folder) && !stillUsed.contains(folder))
            removeRealEntry(addons.filePath(folder));
    }
    setMods(mods);
    m_latest.remove(modId);
    emit changed();
    return {};
}

QStringList AddonManager::otherFolders() const {
    QSet<QString> known;
    for (const auto &mod : installedMods())
        for (const auto &folder : mod.toObject().value("folders").toArray())
            known.insert(folder.toString());
    for (const auto &local : localAddons())
        if (local.toObject().value("linked").toBool())
            known.insert(local.toObject().value("name").toString());
    QStringList other;
    for (const QString &name : QDir(m_addonsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
        if (!known.contains(name))
            other.append(name);
    return other;
}

// ---------------------------------------------------------------- local addons

QJsonArray AddonManager::localAddons() const {
    QJsonArray addons;
    const QDir local(m_localDir), target(m_addonsDir);
    for (const QFileInfo &folder : local.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        const QFileInfo placed(target.filePath(folder.fileName()));
        const bool linked = isLink(placed.filePath())
            && QFileInfo(linkTarget(placed.filePath())).canonicalFilePath() == folder.canonicalFilePath();
        addons.append(QJsonObject{
            {"name", folder.fileName()},
            {"title", tocTitle(folder.absoluteFilePath())},
            {"path", folder.absoluteFilePath()},
            {"linked", linked},
            {"conflict", !linked && (placed.exists() || isLink(placed.filePath()))},
            {"hasToc", !QDir(folder.absoluteFilePath()).entryList({"*.toc"}, QDir::Files).isEmpty()},
        });
    }
    return addons;
}

QString AddonManager::localAddonPath(const QString &name, QString *error) const {
    const QString path = QDir(m_localDir).filePath(name);
    if (!safeName(name) || !QFileInfo(path).isDir()) {
        *error = tr("There's no local addon called %1.").arg(name);
        return {};
    }
    return path;
}

QString AddonManager::link(const QString &name) {
    QString error;
    const QString source = localAddonPath(name, &error);
    if (source.isEmpty())
        return error;
    if (!addonsDirExists())
        return tr("The AddOns folder doesn't exist: %1").arg(m_addonsDir);
    const QString target = QDir(m_addonsDir).filePath(name);
    const QFileInfo placed(target);
    if (placed.exists() || isLink(target))
        return tr("AddOns already has a %1 folder. Remove it there first.").arg(name);
    if (!makeLink(QFileInfo(source).absoluteFilePath(), target))
        return tr("Couldn't link %1 into AddOns.").arg(name);
    emit changed();
    return {};
}

QString AddonManager::unlink(const QString &name) {
    QString error;
    const QString source = localAddonPath(name, &error);
    if (source.isEmpty())
        return error;
    const QFileInfo placed(QDir(m_addonsDir).filePath(name));
    if (!isLink(placed.filePath())
        || QFileInfo(linkTarget(placed.filePath())).canonicalFilePath() != QFileInfo(source).canonicalFilePath())
        return tr("%1 isn't linked from your local addons.").arg(name);
    if (!removeLink(placed.absoluteFilePath()))
        return tr("Couldn't remove the link for %1.").arg(name);
    emit changed();
    return {};
}

// "12.0.1" becomes 120001, the number a .toc's "## Interface:" line wants.
int AddonManager::interfaceFromVersion(const QString &version) {
    const QStringList parts = version.split('.');
    if (parts.size() < 3)
        return 0;
    bool a, b, c;
    const int major = parts[0].toInt(&a), minor = parts[1].toInt(&b), patch = parts[2].toInt(&c);
    return a && b && c ? major * 10000 + minor * 100 + patch : 0;
}

// Reads the game version for this AddOns folder's flavor from Battle.net's
// .build.info, which sits next to the flavor folders (_retail_, _forever_).
int AddonManager::guessInterface() const {
    const QDir flavor(QDir::cleanPath(m_addonsDir + "/../.."));
    QFile flavorInfo(flavor.filePath(".flavor.info"));
    if (!flavorInfo.open(QIODevice::ReadOnly))
        return 0;
    const QStringList flavorLines = QString::fromUtf8(flavorInfo.readAll()).split('\n', Qt::SkipEmptyParts);
    if (flavorLines.size() < 2)
        return 0;
    const QString product = flavorLines[1].trimmed();

    QFile buildInfo(QDir::cleanPath(flavor.absolutePath() + "/../.build.info"));
    if (!buildInfo.open(QIODevice::ReadOnly))
        return 0;
    const QStringList lines = QString::fromUtf8(buildInfo.readAll()).split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return 0;
    QStringList header;
    for (const QString &column : lines[0].split('|'))
        header.append(column.section('!', 0, 0).trimmed());
    const int productColumn = header.indexOf("Product"), versionColumn = header.indexOf("Version");
    if (productColumn < 0 || versionColumn < 0)
        return 0;
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList row = lines[i].split('|');
        if (row.size() > qMax(productColumn, versionColumn) && row[productColumn].trimmed() == product)
            return interfaceFromVersion(row[versionColumn].trimmed());
    }
    return 0;
}

QString AddonManager::createAddon(const QString &name, const QString &title, int interfaceVersion) {
    static const QRegularExpression valid("^[A-Za-z][A-Za-z0-9_-]*$");
    if (!valid.match(name).hasMatch())
        return tr("Addon names start with a letter and use only letters, digits, - and _.");
    const QDir local(m_localDir);
    if (local.exists(name))
        return tr("There's already a local addon called %1.").arg(name);
    if (!local.mkpath(name))
        return tr("Couldn't create %1.").arg(local.filePath(name));

    const QString shown = title.trimmed().isEmpty() ? name : title.trimmed();
    QString slash = name.toLower();
    slash.remove(QRegularExpression("[^a-z0-9]"));
    QString command = name.toUpper();
    command.remove(QRegularExpression("[^A-Z0-9]"));

    QString toc;
    if (interfaceVersion > 0)
        toc += QString("## Interface: %1\n").arg(interfaceVersion);
    toc += QString("## Title: %1\n## Notes: My custom addon\n## Version: 0.1.0\n## SavedVariables: %2DB\n\n%2.lua\n")
               .arg(shown, name);

    // Named tokens rather than %1-style placeholders: "SLASH_%31" would read
    // as placeholder 31.
    QString lua =
        "local addonName, ns = ...\n"
        "\n"
        "local frame = CreateFrame(\"Frame\")\n"
        "frame:RegisterEvent(\"ADDON_LOADED\")\n"
        "frame:SetScript(\"OnEvent\", function(self, event, loaded)\n"
        "    if loaded ~= addonName then return end\n"
        "    {NAME}DB = {NAME}DB or {}\n"
        "    print(\"|cff33ff99\" .. addonName .. \"|r loaded. Type /{SLASH}\")\n"
        "    self:UnregisterEvent(\"ADDON_LOADED\")\n"
        "end)\n"
        "\n"
        "SLASH_{COMMAND}1 = \"/{SLASH}\"\n"
        "SlashCmdList[\"{COMMAND}\"] = function(msg)\n"
        "    print(addonName .. \": \" .. (msg ~= \"\" and msg or \"hello!\"))\n"
        "end\n";
    lua.replace("{NAME}", name).replace("{SLASH}", slash).replace("{COMMAND}", command);

    for (const auto &[file, contents] : {std::pair{name + ".toc", toc}, std::pair{name + ".lua", lua}}) {
        QSaveFile out(local.filePath(name + "/" + file));
        if (!out.open(QIODevice::WriteOnly) || out.write(contents.toUtf8()) < 0 || !out.commit())
            return tr("Couldn't write %1.").arg(file);
    }
    emit changed();
    return addonsDirExists() ? link(name) : QString();
}

bool AddonManager::isLink(const QString &path) {
    const QFileInfo info(path);
#ifdef Q_OS_WIN
    return info.isJunction() || info.isSymbolicLink();
#else
    return info.isSymLink();
#endif
}

// Windows 10 and later ship libarchive's bsdtar as tar.exe.
QString AddonManager::tarProgram() {
#ifdef Q_OS_WIN
    return "tar";
#else
    return "bsdtar";
#endif
}
