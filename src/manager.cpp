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

namespace {

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

QString defaultDownloadsDir() {
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return downloads.isEmpty() ? QDir::homePath() + "/Downloads" : downloads;
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
bool writeJson(const QString &path, const QJsonObject &object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
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

QStringList tocsIn(const QString &folder) {
    return QDir(folder).entryList({"*.toc"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
}

// A "## Key: value" line from a folder's .toc, preferring the .toc named after
// the folder. WoW's |cAARRGGBB color codes are dropped.
QString tocField(const QString &folder, const QString &key) {
    static const QRegularExpression colors(R"(\|c[0-9a-fA-F]{8}|\|r)");
    QStringList tocs = tocsIn(folder);
    const QString own = QFileInfo(folder).fileName() + ".toc";
    if (tocs.removeAll(own) > 0)
        tocs.prepend(own);
    const QString prefix = "## " + key + ":";
    for (const QString &toc : tocs) {
        QFile file(QDir(folder).filePath(toc));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(prefix, Qt::CaseInsensitive))
                return line.mid(prefix.size()).remove(colors).trimmed();
        }
    }
    return {};
}

// WoW only loads a folder whose name matches its .toc. That's usually already
// so, but GitHub archives unpack as "MyAddon-main/MyAddon.toc"; those get the
// .toc's name, without a flavor suffix like _Mainline.
QString folderNameFor(const QString &folder, const QStringList &tocs) {
    for (const QString &toc : tocs)
        if (!folder.isEmpty() && QFileInfo(toc).completeBaseName().startsWith(folder, Qt::CaseInsensitive))
            return folder;
    static const QRegularExpression flavor("[_-](Mainline|Classic|Vanilla|TBC|BCC|Wrath|WOTLKC|Cata|Mists|Standard)$",
                                           QRegularExpression::CaseInsensitiveOption);
    QString best;
    for (const QString &toc : tocs) {
        const QString base = QFileInfo(toc).completeBaseName().remove(flavor);
        if (best.isEmpty() || base.size() < best.size())
            best = base;
    }
    return best;
}

struct Placement {
    QString source;  // folder as unpacked
    QString name;    // folder name in AddOns
};

// The addon folders in an unpacked zip. Most zips have them at the top; a zip
// with a single wrapper folder (GitHub, some authors) is looked into once,
// and a zip of loose files is one addon by itself.
QList<Placement> placements(const QString &root, bool lookInside = true) {
    QList<Placement> found;
    QSet<QString> names;
    const QDir dir(root);
    const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QString &sub : subdirs) {
        const QStringList tocs = tocsIn(dir.filePath(sub));
        if (tocs.isEmpty())
            continue;
        const QString name = folderNameFor(sub, tocs);
        if (safeName(name) && !names.contains(name.toLower())) {
            names.insert(name.toLower());
            found.append({dir.filePath(sub), name});
        }
    }
    if (found.isEmpty() && lookInside) {
        const QStringList tocs = tocsIn(root);
        const QString name = folderNameFor({}, tocs);
        if (!tocs.isEmpty() && safeName(name))
            found.append({root, name});
        else if (subdirs.size() == 1)
            return placements(dir.filePath(subdirs.first()), false);
    }
    return found;
}

bool unpack(const QString &zipPath, const QString &into, const QStringList &patterns, QString *error) {
    QProcess tar;
    tar.start(AddonManager::tarProgram(), QStringList{"-xf", zipPath, "-C", into} + patterns);
    if (!tar.waitForStarted()) {
        *error = AddonManager::tr("%1 is missing, so zips can't be unpacked.").arg(AddonManager::tarProgram());
        return false;
    }
    tar.waitForFinished(120000);
    if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        *error = AddonManager::tr("Couldn't unpack %1: %2")
                     .arg(QFileInfo(zipPath).fileName(), QString::fromUtf8(tar.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

}  // namespace

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
    m_addonsDir = config.value("addonsDir").toString(defaultAddonsDir());
    m_localDir = config.value("localDir").toString(defaultLocalDir());
    m_downloadsDir = config.value("downloadsDir").toString(defaultDownloadsDir());
    QDir().mkpath(m_localDir);
}

bool AddonManager::addonsDirExists() const { return QFileInfo(m_addonsDir).isDir(); }

QString AddonManager::saveSettings(const QString &addonsDir, const QString &localDir, const QString &downloadsDir) {
    const QString addons = QDir::cleanPath(addonsDir.trimmed());
    const QString local = QDir::cleanPath(localDir.trimmed());
    const QString downloads = QDir::cleanPath(downloadsDir.trimmed());
    if (!QFileInfo(addons).isDir())
        return tr("That AddOns folder doesn't exist: %1").arg(addons);
    if (local.isEmpty() || !QDir().mkpath(local))
        return tr("Can't create the local addons folder: %1").arg(local);
    if (!QFileInfo(downloads).isDir())
        return tr("That downloads folder doesn't exist: %1").arg(downloads);

    QJsonObject config = readJson(m_configFile);
    config.remove("apiKey");  // left over from when Addoner used CurseForge's API
    config["addonsDir"] = addons;
    config["localDir"] = local;
    config["downloadsDir"] = downloads;
    if (!writeJson(m_configFile, config))
        return tr("Couldn't save settings to %1").arg(m_configFile);
    loadSettings();
    emit changed();
    return {};
}

// ---------------------------------------------------------------- zips

// Only the .toc files are unpacked, into a temporary folder, which is enough
// to tell which addons a zip holds and their versions.
AddonManager::ZipInfo AddonManager::inspectZip(const QString &zipPath) const {
    const QFileInfo file(zipPath);
    CachedZip &cached = m_zipCache[file.absoluteFilePath()];
    if (cached.size == file.size() && cached.modified == file.lastModified())
        return cached.info;
    cached = {file.size(), file.lastModified(), {}};

    QTemporaryDir tmp;
    QString error;
    // tar fails when nothing matches, which just means no addon is inside.
    unpack(zipPath, tmp.path(), {"*.toc"}, &error);
    const QList<Placement> found = placements(tmp.path());
    if (found.isEmpty())
        return {};

    ZipInfo &info = cached.info;
    const QString base = file.completeBaseName();
    QString mainSource;
    for (const Placement &place : found) {
        info.folders.append(place.name);
        // The zip is named after its addon ("Bagnon-12.1.9.zip"): the longest
        // folder name it starts with, or else the shortest folder name.
        const bool better = info.main.isEmpty()
            || (base.startsWith(place.name, Qt::CaseInsensitive)
                    ? !base.startsWith(info.main, Qt::CaseInsensitive) || place.name.size() > info.main.size()
                    : !base.startsWith(info.main, Qt::CaseInsensitive) && place.name.size() < info.main.size());
        if (better) {
            info.main = place.name;
            mainSource = place.source;
        }
    }
    info.title = tocField(mainSource, "Title");
    if (info.title.isEmpty())
        info.title = info.main;
    info.version = tocField(mainSource, "Version");
    return info;
}

// Numbers are compared one by one ("12.1.10" is newer than "12.1.9");
// versions without numbers only tell whether they differ.
int AddonManager::compareVersions(const QString &a, const QString &b) {
    if (a == b)
        return 0;
    static const QRegularExpression number("\\d+");
    QList<qlonglong> left, right;
    for (auto it = number.globalMatch(a); it.hasNext();)
        left.append(it.next().captured().toLongLong());
    for (auto it = number.globalMatch(b); it.hasNext();)
        right.append(it.next().captured().toLongLong());
    if (left.isEmpty() || right.isEmpty())
        return 1;
    for (int i = 0; i < qMax(left.size(), right.size()); ++i) {
        const qlonglong x = left.value(i), y = right.value(i);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

QJsonArray AddonManager::downloads() const {
    QJsonArray zips;
    QSet<QString> present;
    const QDir dir(m_downloadsDir);
    for (const QFileInfo &file : dir.entryInfoList({"*.zip"}, QDir::Files, QDir::Time)) {
        present.insert(file.absoluteFilePath());
        const ZipInfo info = inspectZip(file.absoluteFilePath());
        if (info.folders.isEmpty())
            continue;
        const QString placed = QDir(m_addonsDir).filePath(info.main);
        QString status, installedVersion;
        if (isLink(placed)) {
            status = "linked";
        } else if (!QFileInfo(placed).isDir()) {
            status = "new";
        } else {
            installedVersion = tocField(placed, "Version");
            const int order = compareVersions(info.version, installedVersion);
            status = order > 0 ? "update" : order < 0 ? "older" : "installed";
        }
        zips.append(QJsonObject{
            {"path", file.absoluteFilePath()},
            {"file", file.fileName()},
            {"main", info.main},
            {"title", info.title},
            {"version", info.version},
            {"folders", QJsonArray::fromStringList(info.folders)},
            {"modified", file.lastModified().toString(Qt::ISODate)},
            {"status", status},
            {"installedVersion", installedVersion},
        });
    }
    for (auto it = m_zipCache.begin(); it != m_zipCache.end();)
        it = present.contains(it.key()) ? std::next(it) : m_zipCache.erase(it);
    return zips;
}

QHash<QString, QString> AddonManager::updates() const {
    QHash<QString, QString> newest, versions;
    for (const auto &value : downloads()) {
        const QJsonObject zip = value.toObject();
        if (zip.value("status").toString() != "update")
            continue;
        const QString main = zip.value("main").toString(), version = zip.value("version").toString();
        if (!newest.contains(main) || compareVersions(version, versions.value(main)) > 0) {
            newest[main] = zip.value("path").toString();
            versions[main] = version;
        }
    }
    return newest;
}

QString AddonManager::install(const QString &zipPath, QString *name) {
    if (!addonsDirExists())
        return tr("The AddOns folder doesn't exist: %1").arg(m_addonsDir);
    if (!QFileInfo(zipPath).isFile())
        return tr("There's no zip at %1.").arg(zipPath);
    const ZipInfo info = inspectZip(zipPath);
    if (info.folders.isEmpty())
        return tr("%1 has no addon in it.").arg(QFileInfo(zipPath).fileName());

    QJsonObject addons = installedAddons();
    QStringList replace, folders;
    for (const auto &folder : addons.value(info.main).toObject().value("folders").toArray())
        replace.append(folder.toString());
    const QString error = installZip(zipPath, replace, &folders);
    if (!error.isEmpty())
        return tr("%1: %2").arg(info.title, error);

    addons[info.main] = QJsonObject{
        {"title", info.title},
        {"version", info.version},
        {"folders", QJsonArray::fromStringList(folders)},
        {"zip", QFileInfo(zipPath).fileName()},
        {"installed", QDateTime::currentDateTime().toString(Qt::ISODate)},
    };
    setInstalled(addons);
    if (name)
        *name = info.title;
    emit changed();
    return {};
}

QString AddonManager::updateAll(QStringList *names) {
    const QHash<QString, QString> pending = updates();
    for (const QString &zip : pending) {
        QString name;
        const QString error = install(zip, &name);
        if (!error.isEmpty())
            return error;
        names->append(name);
    }
    return {};
}

// Unpacks into a staging folder inside AddOns (so moving is a rename), then
// swaps each addon folder in. tar refuses absolute paths and ".." by
// default, so nothing in the zip can land outside the staging folder.
QString AddonManager::installZip(const QString &zipPath, const QStringList &replace, QStringList *folders) {
    const QDir addons(m_addonsDir);
    if (!addons.exists())
        return tr("The AddOns folder doesn't exist: %1").arg(m_addonsDir);
    QTemporaryDir staging(addons.filePath(".addoner-XXXXXX"));
    if (!staging.isValid())
        return tr("Can't write to the AddOns folder.");
    QString error;
    if (!unpack(zipPath, staging.path(), {}, &error))
        return error;

    const QList<Placement> found = placements(staging.path());
    if (found.isEmpty())
        return tr("The zip has no addon folders in it.");
    QStringList names;
    for (const Placement &place : found) {
        if (isLink(addons.filePath(place.name)))
            return tr("%1 is linked from your local addons. Unlink it first.").arg(place.name);
        names.append(place.name);
    }

    for (const QString &name : replace) {
        if (safeName(name) && !names.contains(name))
            removeRealEntry(addons.filePath(name));
    }
    for (const Placement &place : found) {
        const QString target = addons.filePath(place.name);
        if (!removeRealEntry(target))
            return tr("Couldn't replace %1.").arg(target);
        if (!QDir().rename(place.source, target))
            return tr("Couldn't move %1 into AddOns.").arg(place.name);
    }
    *folders = names;
    return {};
}

QJsonObject AddonManager::installedAddons() const { return m_state.value(m_addonsDir).toObject(); }

void AddonManager::setInstalled(const QJsonObject &addons) {
    m_state[m_addonsDir] = addons;
    writeJson(m_stateFile, m_state);
}

QString AddonManager::uninstall(const QString &folder) {
    QJsonObject addons = installedAddons();
    if (!addons.contains(folder))
        return tr("%1 wasn't installed by Addoner.").arg(folder);
    const QJsonObject record = addons.take(folder).toObject();

    // Libraries are often bundled by several addons; keep folders another
    // installed addon still uses.
    QSet<QString> stillUsed;
    for (const auto &other : addons)
        for (const auto &name : other.toObject().value("folders").toArray())
            stillUsed.insert(name.toString());
    const QDir dir(m_addonsDir);
    for (const auto &value : record.value("folders").toArray()) {
        const QString name = value.toString();
        if (safeName(name) && !stillUsed.contains(name))
            removeRealEntry(dir.filePath(name));
    }
    setInstalled(addons);
    emit changed();
    return {};
}

QStringList AddonManager::otherFolders() const {
    QSet<QString> known;
    for (const auto &addon : installedAddons())
        for (const auto &folder : addon.toObject().value("folders").toArray())
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
            {"title", tocField(folder.absoluteFilePath(), "Title")},
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
