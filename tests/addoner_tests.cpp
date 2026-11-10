#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include "manager.h"
#include "theme.h"
#include "translator.h"

class AddonerTests : public QObject {
    Q_OBJECT

private slots:
    void init() {
        m_dir.reset(new QTemporaryDir);
        QDir(m_dir->path()).mkpath("wow/_forever_/Interface/AddOns");
        QDir(m_dir->path()).mkpath("local");
        QDir(m_dir->path()).mkpath("downloads");
        m_manager.reset(new AddonManager(path("config"), path("data")));
        QCOMPARE(m_manager->saveSettings(addons(), path("local"), path("downloads")), QString());
    }

    void installZipPlacesEveryFolder() {
        const QString zip = makeZip("one", {{"Core/Core.toc", ""}, {"Core_Options/Core_Options.toc", ""}});
        QStringList folders;
        QCOMPARE(m_manager->installZip(zip, {}, &folders), QString());
        QCOMPARE(folders, QStringList({"Core", "Core_Options"}));
        QVERIFY(QFile::exists(addons() + "/Core/Core.toc"));
        QVERIFY(QFile::exists(addons() + "/Core_Options/Core_Options.toc"));
        // The staging folder is gone afterwards.
        QCOMPARE(QDir(addons()).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden).size(), 2);
    }

    void installZipReplacesOldFolders() {
        QStringList folders;
        QCOMPARE(m_manager->installZip(makeZip("v1", {{"Core/Core.toc", ""}, {"Core/old.lua", ""}, {"Core_Legacy/Core_Legacy.toc", ""}}), {}, &folders), QString());
        QCOMPARE(m_manager->installZip(makeZip("v2", {{"Core/Core.toc", ""}, {"Core/new.lua", ""}}), folders, &folders), QString());
        QVERIFY(QFile::exists(addons() + "/Core/new.lua"));
        QVERIFY(!QFile::exists(addons() + "/Core/old.lua"));
        QVERIFY(!QDir(addons() + "/Core_Legacy").exists());
    }

    void installZipLeavesLocalLinksAlone() {
        createLocal("Core");
        QCOMPARE(m_manager->link("Core"), QString());
        QStringList folders;
        QVERIFY(!m_manager->installZip(makeZip("z", {{"Core/Core.toc", ""}}), {}, &folders).isEmpty());
        QVERIFY(AddonManager::isLink(addons() + "/Core"));
        QVERIFY(QFile::exists(path("local/Core/Core.toc")));
    }

    void inspectFindsMainFolderAndVersion() {
        const QString zip = makeZip("Bagnon-12.1.9", {
            {"BagBrother/BagBrother.toc", "## Title: Bag Brother\n## Version: 1\n"},
            {"Bagnon/Bagnon.toc", "## Title: |cff00ff00Bagnon|r\n## Version: 12.1.9\n"},
            {"Bagnon_Config/Bagnon_Config.toc", "## Version: 12.1.9\n"},
            {"Bagnon/core.lua", "-- core"},
        });
        const auto info = m_manager->inspectZip(zip);
        QCOMPARE(info.folders, QStringList({"BagBrother", "Bagnon", "Bagnon_Config"}));
        QCOMPARE(info.main, QString("Bagnon"));
        QCOMPARE(info.title, QString("Bagnon"));
        QCOMPARE(info.version, QString("12.1.9"));
    }

    void downloadsSkipsZipsWithoutAddons() {
        makeZip("photos", {{"holiday/beach.jpg", "jpeg"}});
        makeZip("Thing-1.0", {{"Thing/Thing.toc", "## Version: 1.0\n"}});
        const QJsonArray zips = m_manager->downloads();
        QCOMPARE(zips.size(), 1);
        QCOMPARE(zips.at(0).toObject().value("main").toString(), QString("Thing"));
        QCOMPARE(zips.at(0).toObject().value("status").toString(), QString("new"));
    }

    void wrapperFoldersAreUnwrapped() {
        // GitHub's "Download ZIP": one folder named after the branch.
        const QString zip = makeZip("MyAddon-main", {
            {"MyAddon-main/MyAddon_Mainline.toc", "## Version: 2\n"},
            {"MyAddon-main/MyAddon.lua", "-- hi"},
        });
        QString name;
        QCOMPARE(m_manager->install(zip, &name), QString());
        QVERIFY(QFile::exists(addons() + "/MyAddon/MyAddon_Mainline.toc"));
        QVERIFY(!QDir(addons() + "/MyAddon-main").exists());

        // A wrapper around several addon folders.
        const QString pack = makeZip("Pack-main", {
            {"Pack-main/README.md", "readme"},
            {"Pack-main/PackA/PackA.toc", "## Version: 1\n"},
            {"Pack-main/PackB/PackB.toc", "## Version: 1\n"},
        });
        QCOMPARE(m_manager->inspectZip(pack).folders, QStringList({"PackA", "PackB"}));
    }

    void statusFollowsInstalledVersion() {
        const QString v1 = makeZip("Thing-1.0", {{"Thing/Thing.toc", "## Version: 1.0\n"}});
        QCOMPARE(m_manager->install(v1), QString());
        QCOMPARE(status("Thing-1.0.zip"), QString("installed"));

        makeZip("Thing-1.10", {{"Thing/Thing.toc", "## Version: 1.10\n"}});
        makeZip("Thing-1.2", {{"Thing/Thing.toc", "## Version: 1.2\n"}});
        QCOMPARE(status("Thing-1.10.zip"), QString("update"));
        QCOMPARE(m_manager->updates().value("Thing"), path("downloads/Thing-1.10.zip"));

        QStringList names;
        QCOMPARE(m_manager->updateAll(&names), QString());
        QCOMPARE(names, QStringList{"Thing"});
        QCOMPARE(m_manager->installedAddons().value("Thing").toObject().value("version").toString(), QString("1.10"));
        QCOMPARE(status("Thing-1.0.zip"), QString("older"));
        QCOMPARE(status("Thing-1.2.zip"), QString("older"));
        QVERIFY(m_manager->updates().isEmpty());
    }

    void linkedFolderBlocksZip() {
        createLocal("Mine");
        QCOMPARE(m_manager->link("Mine"), QString());
        const QString zip = makeZip("Mine-1", {{"Mine/Mine.toc", "## Version: 1\n"}});
        QCOMPARE(status("Mine-1.zip"), QString("linked"));
        QVERIFY(!m_manager->install(zip).isEmpty());
        QVERIFY(QFile::exists(path("local/Mine/Mine.toc")));
    }

    void compareVersionsByNumber() {
        QVERIFY(AddonManager::compareVersions("12.1.10", "12.1.9") > 0);
        QVERIFY(AddonManager::compareVersions("v1.2", "1.3") < 0);
        QCOMPARE(AddonManager::compareVersions("1.0", "1.0.0"), 0);
        QVERIFY(AddonManager::compareVersions("abc", "") != 0);
    }

    void uninstallKeepsSharedFolders() {
        QCOMPARE(m_manager->install(makeZip("A", {{"A/A.toc", ""}, {"Lib/Lib.toc", ""}})), QString());
        QCOMPARE(m_manager->install(makeZip("B", {{"B/B.toc", ""}, {"Lib/Lib.toc", ""}})), QString());
        QCOMPARE(m_manager->uninstall("A"), QString());
        QVERIFY(!QDir(addons() + "/A").exists());
        QVERIFY(QDir(addons() + "/Lib").exists());
        QVERIFY(!m_manager->installedAddons().contains("A"));
        QVERIFY(m_manager->installedAddons().contains("B"));
        QVERIFY(!m_manager->uninstall("A").isEmpty());
    }

    void installedStateSurvivesRestart() {
        QCOMPARE(m_manager->install(makeZip("Seven", {{"Seven/Seven.toc", ""}})), QString());
        AddonManager again(path("config"), path("data"));
        QVERIFY(again.installedAddons().contains("Seven"));
        QCOMPARE(again.addonsDir(), addons());
        QCOMPARE(again.downloadsDir(), path("downloads"));
    }

    void otherFoldersAreUnmanaged() {
        QDir(addons()).mkpath("Manual");
        QCOMPARE(m_manager->install(makeZip("Managed", {{"Managed/Managed.toc", ""}})), QString());
        createLocal("Mine");
        QCOMPARE(m_manager->link("Mine"), QString());
        QCOMPARE(m_manager->otherFolders(), QStringList{"Manual"});
    }

    void linkAndUnlinkLocalAddons() {
        createLocal("Mine");
        QJsonObject entry = m_manager->localAddons().at(0).toObject();
        QCOMPARE(entry.value("linked").toBool(), false);
        QCOMPARE(entry.value("title").toString(), QString("Mine Title"));

        QCOMPARE(m_manager->link("Mine"), QString());
        QVERIFY(AddonManager::isLink(addons() + "/Mine"));
        QVERIFY(m_manager->localAddons().at(0).toObject().value("linked").toBool());
        QVERIFY(!m_manager->link("Mine").isEmpty());

        QCOMPARE(m_manager->unlink("Mine"), QString());
        QVERIFY(!QFileInfo(addons() + "/Mine").exists());
        QVERIFY(QFile::exists(path("local/Mine/Mine.toc")));
        QVERIFY(!m_manager->unlink("Mine").isEmpty());
        QVERIFY(!m_manager->link("../escape").isEmpty());
    }

    void linkRefusesExistingFolder() {
        createLocal("Mine");
        QDir(addons()).mkpath("Mine");
        QVERIFY(m_manager->localAddons().at(0).toObject().value("conflict").toBool());
        QVERIFY(!m_manager->link("Mine").isEmpty());
        QVERIFY(!AddonManager::isLink(addons() + "/Mine"));
    }

    void createAddonScaffoldsAndLinks() {
        QCOMPARE(m_manager->createAddon("My_Addon", "My Addon", 16001), QString());
        const QString toc = read(path("local/My_Addon/My_Addon.toc"));
        QVERIFY(toc.contains("## Interface: 16001\n"));
        QVERIFY(toc.contains("## Title: My Addon\n"));
        QVERIFY(toc.contains("\nMy_Addon.lua\n"));
        const QString lua = read(path("local/My_Addon/My_Addon.lua"));
        QVERIFY(lua.contains("SLASH_MYADDON1 = \"/myaddon\""));
        QVERIFY(lua.contains("My_AddonDB = My_AddonDB or {}"));
        QVERIFY(AddonManager::isLink(addons() + "/My_Addon"));

        QVERIFY(!m_manager->createAddon("My_Addon", "", 1).isEmpty());
        QVERIFY(!m_manager->createAddon("bad name", "", 1).isEmpty());
        QVERIFY(!m_manager->createAddon("../x", "", 1).isEmpty());
    }

    void guessInterfaceReadsBuildInfo() {
        QCOMPARE(AddonManager::interfaceFromVersion("12.0.1.6"), 120001);
        QCOMPARE(AddonManager::interfaceFromVersion("1.60.1.70862"), 16001);
        QCOMPARE(AddonManager::interfaceFromVersion("junk"), 0);

        write(path("wow/_forever_/.flavor.info"), "Product Flavor!STRING:0\nwowf\n");
        write(path("wow/.build.info"),
              "Branch!STRING:0|Active!DEC:1|Version!STRING:0|Product!STRING:0\n"
              "eu|1|11.2.5.1|wow\n"
              "eu|1|1.60.1.70862|wowf\n");
        QCOMPARE(m_manager->guessInterface(), 16001);
    }

    void settingsRejectMissingFolders() {
        QVERIFY(!m_manager->saveSettings(path("nope"), path("local"), path("downloads")).isEmpty());
        QVERIFY(!m_manager->saveSettings(addons(), path("local"), path("nope")).isEmpty());
        QCOMPARE(m_manager->addonsDir(), addons());
        QCOMPARE(m_manager->downloadsDir(), path("downloads"));
    }

    // Theme and translator, from the Omarchy app template.

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void englishCatalogHasPlurals() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", QFINDTESTDATA("../src/i18n"));
        QCOMPARE(translator.translate("Main", "%n addon zips in %1", nullptr, 1), QString("%n addon zip in %1"));
        QCOMPARE(translator.translate("Main", "%n addon zips in %1", nullptr, 3), QString("%n addon zips in %1"));
    }

private:
    QString path(const QString &relative) const { return m_dir->path() + "/" + relative; }
    QString addons() const { return path("wow/_forever_/Interface/AddOns"); }

    static void write(const QString &file, const QByteArray &contents) {
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(contents);
    }

    static QString read(const QString &file) {
        QFile in(file);
        return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
    }

    void createLocal(const QString &name) {
        write(path("local/" + name + "/" + name + ".toc"), "## Interface: 1\n## Title: |cff00ff00" + name.toUtf8() + " Title|r\n");
    }

    QString status(const QString &file) const {
        for (const auto &value : m_manager->downloads())
            if (value.toObject().value("file").toString() == file)
                return value.toObject().value("status").toString();
        return {};
    }

    // Builds downloads/<label>.zip from relative paths and their contents.
    QString makeZip(const QString &label, const QList<std::pair<QString, QByteArray>> &files) {
        const QString root = path("zipsrc-" + label);
        QSet<QString> tops;
        for (const auto &[file, contents] : files) {
            write(root + "/" + file, contents);
            tops.insert(file.section('/', 0, 0));
        }
        const QString zip = path("downloads/" + label + ".zip");
        QStringList args{"-a", "-cf", zip, "-C", root};
        args += QStringList(tops.begin(), tops.end());
        QProcess tar;
        tar.start(AddonManager::tarProgram(), args);
        tar.waitForFinished();
        return tar.exitCode() == 0 ? zip : QString();
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<AddonManager> m_manager;
};

QTEST_MAIN(AddonerTests)
#include "addoner_tests.moc"
