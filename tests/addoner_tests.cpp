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
        m_manager.reset(new AddonManager(path("config"), path("data")));
        QCOMPARE(m_manager->saveSettings("", addons(), path("local")), QString());
    }

    void bestFilePrefersNewestRelease() {
        const QJsonObject mod{{"latestFilesIndexes", QJsonArray{
            QJsonObject{{"fileId", 300}, {"releaseType", 2}},
            QJsonObject{{"fileId", 100}, {"releaseType", 1}},
            QJsonObject{{"fileId", 200}, {"releaseType", 1}},
        }}};
        QCOMPARE(AddonManager::bestFile(mod).value("fileId").toInt(), 200);

        const QJsonObject betaOnly{{"latestFilesIndexes", QJsonArray{
            QJsonObject{{"fileId", 5}, {"releaseType", 3}},
            QJsonObject{{"fileId", 4}, {"releaseType", 2}},
        }}};
        QCOMPARE(AddonManager::bestFile(betaOnly).value("fileId").toInt(), 4);
        QVERIFY(AddonManager::bestFile(QJsonObject{}).isEmpty());
    }

    void downloadUrlFallsBackToCdn() {
        QCOMPARE(AddonManager::downloadUrl(QJsonObject{{"downloadUrl", "https://x/y.zip"}}).toString(),
                 QString("https://x/y.zip"));
        const QJsonObject file{{"id", 4567089}, {"fileName", "My Addon 1.0.zip"}};
        QCOMPARE(AddonManager::downloadUrl(file).toString(QUrl::FullyEncoded),
                 QString("https://edge.forgecdn.net/files/4567/89/My%20Addon%201.0.zip"));
    }

    void installZipPlacesEveryFolder() {
        const QString zip = makeZip("one", {"Core/Core.toc", "Core_Options/Options.toc"});
        QStringList folders;
        QCOMPARE(m_manager->installZip(zip, {}, &folders), QString());
        QCOMPARE(folders, QStringList({"Core", "Core_Options"}));
        QVERIFY(QFile::exists(addons() + "/Core/Core.toc"));
        QVERIFY(QFile::exists(addons() + "/Core_Options/Options.toc"));
        // The staging folder is gone afterwards.
        QCOMPARE(QDir(addons()).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden).size(), 2);
    }

    void installZipReplacesOldFolders() {
        QStringList folders;
        QCOMPARE(m_manager->installZip(makeZip("v1", {"Core/old.lua", "Core_Legacy/x.toc"}), {}, &folders), QString());
        QCOMPARE(m_manager->installZip(makeZip("v2", {"Core/new.lua"}), folders, &folders), QString());
        QVERIFY(QFile::exists(addons() + "/Core/new.lua"));
        QVERIFY(!QFile::exists(addons() + "/Core/old.lua"));
        QVERIFY(!QDir(addons() + "/Core_Legacy").exists());
    }

    void installZipLeavesLocalLinksAlone() {
        createLocal("Core");
        QCOMPARE(m_manager->link("Core"), QString());
        QStringList folders;
        QVERIFY(!m_manager->installZip(makeZip("z", {"Core/Core.toc"}), {}, &folders).isEmpty());
        QVERIFY(AddonManager::isLink(addons() + "/Core"));
        QVERIFY(QFile::exists(path("local/Core/Core.toc")));
    }

    void uninstallKeepsSharedFolders() {
        QDir(addons()).mkpath("A");
        QDir(addons()).mkpath("Lib");
        QDir(addons()).mkpath("B");
        m_manager->recordInstall(1, QJsonObject{{"name", "A"}, {"fileId", 10}, {"folders", QJsonArray{"A", "Lib"}}});
        m_manager->recordInstall(2, QJsonObject{{"name", "B"}, {"fileId", 20}, {"folders", QJsonArray{"B", "Lib"}}});
        QCOMPARE(m_manager->uninstall(1), QString());
        QVERIFY(!QDir(addons() + "/A").exists());
        QVERIFY(QDir(addons() + "/Lib").exists());
        QVERIFY(!m_manager->isInstalled(1));
        QVERIFY(m_manager->isInstalled(2));
        QVERIFY(!m_manager->uninstall(1).isEmpty());
    }

    void installedStateSurvivesRestart() {
        m_manager->recordInstall(7, QJsonObject{{"name", "Seven"}, {"fileId", 70}, {"folders", QJsonArray{"Seven"}}});
        AddonManager again(path("config"), path("data"));
        QVERIFY(again.isInstalled(7));
        QCOMPARE(again.addonsDir(), addons());
    }

    void otherFoldersAreUnmanaged() {
        QDir(addons()).mkpath("Manual");
        QDir(addons()).mkpath("Managed");
        m_manager->recordInstall(1, QJsonObject{{"name", "M"}, {"fileId", 1}, {"folders", QJsonArray{"Managed"}}});
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

    void settingsRejectMissingAddonsFolder() {
        QVERIFY(!m_manager->saveSettings("", path("nope"), path("local")).isEmpty());
        QCOMPARE(m_manager->addonsDir(), addons());
    }

    void apiKeyIsSavedPrivately() {
        QCOMPARE(m_manager->saveSettings("secret", addons(), path("local")), QString());
        QVERIFY(m_manager->hasApiKey());
#ifndef Q_OS_WIN  // NTFS permissions aren't Unix mode bits
        const auto perms = QFile(path("config/config.json")).permissions();
        QVERIFY(!(perms & QFileDevice::ReadOther));
        QVERIFY(!(perms & QFileDevice::ReadGroup));
#endif
        // An empty key on a later save keeps the saved one.
        QCOMPARE(m_manager->saveSettings("", addons(), path("local")), QString());
        QVERIFY(m_manager->hasApiKey());
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
        QCOMPARE(translator.translate("Main", "%n addons", nullptr, 1), QString("%n addon"));
        QCOMPARE(translator.translate("Main", "%n addons", nullptr, 3), QString("%n addons"));
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

    // Builds a zip the way CurseForge ships them: addon folders at the top.
    QString makeZip(const QString &label, const QStringList &files) {
        const QString root = path("zipsrc-" + label);
        QSet<QString> tops;
        for (const QString &file : files) {
            write(root + "/" + file, "-- " + file.toUtf8());
            tops.insert(file.section('/', 0, 0));
        }
        const QString zip = path(label + ".zip");
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
