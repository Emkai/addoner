#include "cli.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "manager.h"

namespace {

const QStringList Commands{"help", "--help", "-h", "search", "list", "install", "remove", "update",
                           "local", "link", "unlink", "new"};

const char *Usage =
    "Usage: addoner [command] [--json]\n"
    "\n"
    "  (no command)            open the window\n"
    "  search <words>          search CurseForge (empty: most popular)\n"
    "  install <id>            install or update an addon and its required libraries\n"
    "  remove <id>             remove an installed CurseForge addon\n"
    "  list                    installed addons, after checking for updates\n"
    "  update                  update every addon with a newer version\n"
    "  local                   list local addons and whether they're linked\n"
    "  link <name>             link a local addon into AddOns\n"
    "  unlink <name>           remove that link\n"
    "  new <name> [--title T] [--interface N]\n"
    "                          create a starter addon in local addons and link it\n";

QTextStream &out() { static QTextStream s(stdout); return s; }
QTextStream &err() { static QTextStream s(stderr); return s; }

int fail(const QString &message) {
    err() << "addoner: " << message << Qt::endl;
    return 1;
}

void print(bool json, const QJsonValue &value, const QString &text) {
    if (json)
        out() << QJsonDocument(value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject())).toJson();
    else if (!text.isEmpty())
        out() << text << Qt::endl;
}

// Runs an async manager call to completion.
template <typename Start>
void wait(Start start) {
    QEventLoop loop;
    start([&loop] { loop.quit(); });
    loop.exec();
}

QString option(QStringList &args, const QString &name) {
    const int i = args.indexOf(name);
    if (i < 0 || i + 1 >= args.size())
        return {};
    const QString value = args.at(i + 1);
    args.remove(i, 2);
    return value;
}

int modIdArgument(const QStringList &args, bool *ok) {
    *ok = args.size() == 2;
    return *ok ? args[1].toInt(ok) : 0;
}

}  // namespace

bool Cli::isCommand(const QString &word) { return Commands.contains(word); }

int Cli::run(AddonManager &manager, QStringList args) {
    const bool json = args.removeAll("--json") > 0;
    const QString command = args.value(0);

    if (command == "help" || command == "--help" || command == "-h") {
        out() << Usage;
        return 0;
    }

    if (command == "search") {
        QJsonArray mods;
        QString error;
        wait([&](auto quit) {
            manager.search(args.mid(1).join(' '), 0, [&, quit](const QJsonArray &found, int, const QString &e) {
                mods = found;
                error = e;
                quit();
            });
        });
        if (!error.isEmpty())
            return fail(error);
        QJsonArray rows;
        QString text;
        for (const auto &value : mods) {
            const QJsonObject mod = value.toObject();
            const int id = mod.value("id").toInt();
            rows.append(QJsonObject{{"id", id}, {"name", mod.value("name")}, {"summary", mod.value("summary")},
                                    {"downloads", mod.value("downloadCount")}, {"installed", manager.isInstalled(id)}});
            text += QString("%1  %2%3\n").arg(id, 8).arg(mod.value("name").toString(),
                                                         manager.isInstalled(id) ? "  [installed]" : "");
        }
        print(json, rows, text.trimmed());
        return 0;
    }

    if (command == "install" || command == "update") {
        bool ok = true;
        const int modId = command == "install" ? modIdArgument(args, &ok) : 0;
        if (!ok)
            return fail("install takes one CurseForge project id");
        QString error;
        if (command == "update") {
            wait([&](auto quit) { manager.checkUpdates([&, quit](const QString &e) { error = e; quit(); }); });
            if (!error.isEmpty())
                return fail(error);
        }
        QStringList names;
        wait([&](auto quit) {
            auto done = [&, quit](const QStringList &n, const QString &e) { names = n; error = e; quit(); };
            command == "install" ? manager.install(modId, done) : manager.updateAll(done);
        });
        if (!error.isEmpty())
            return fail(error);
        print(json, QJsonObject{{"installed", QJsonArray::fromStringList(names)}},
              names.isEmpty() ? "Nothing to update." : "Installed " + names.join(", "));
        return 0;
    }

    if (command == "remove") {
        bool ok;
        const int modId = modIdArgument(args, &ok);
        if (!ok)
            return fail("remove takes one CurseForge project id");
        const QString error = manager.uninstall(modId);
        if (!error.isEmpty())
            return fail(error);
        print(json, QJsonObject{{"removed", modId}}, "Removed.");
        return 0;
    }

    if (command == "list") {
        QString error;
        if (manager.hasApiKey())
            wait([&](auto quit) { manager.checkUpdates([&, quit](const QString &e) { error = e; quit(); }); });
        if (!error.isEmpty())
            err() << "addoner: couldn't check for updates: " << error << Qt::endl;
        const QJsonObject mods = manager.installedMods();
        QJsonArray rows;
        QString text;
        for (auto it = mods.begin(); it != mods.end(); ++it) {
            QJsonObject row = it.value().toObject();
            const int id = it.key().toInt();
            row["id"] = id;
            row["hasUpdate"] = manager.hasUpdate(id);
            rows.append(row);
            text += QString("%1  %2  %3%4\n").arg(id, 8).arg(row.value("name").toString(), row.value("fileName").toString(),
                                                            manager.hasUpdate(id) ? "  [update available]" : "");
        }
        print(json, rows, text.isEmpty() ? "No CurseForge addons installed." : text.trimmed());
        return 0;
    }

    if (command == "local") {
        const QJsonArray local = manager.localAddons();
        QString text;
        for (const auto &value : local) {
            const QJsonObject addon = value.toObject();
            text += QString("%1  %2\n").arg(addon.value("name").toString(),
                                            addon.value("linked").toBool() ? "linked"
                                            : addon.value("conflict").toBool() ? "blocked: AddOns has a folder with this name"
                                                                               : "not linked");
        }
        print(json, local, text.isEmpty() ? "No local addons in " + manager.localDir() : text.trimmed());
        return 0;
    }

    if (command == "link" || command == "unlink") {
        if (args.size() != 2)
            return fail(command + " takes one local addon name");
        const QString error = command == "link" ? manager.link(args[1]) : manager.unlink(args[1]);
        if (!error.isEmpty())
            return fail(error);
        print(json, QJsonObject{{command == "link" ? "linked" : "unlinked", args[1]}}, "Done.");
        return 0;
    }

    if (command == "new") {
        const QString title = option(args, "--title");
        const QString interface = option(args, "--interface");
        if (args.size() != 2)
            return fail("new takes one addon name");
        const int version = interface.isEmpty() ? manager.guessInterface() : interface.toInt();
        const QString error = manager.createAddon(args[1], title, version);
        if (!error.isEmpty())
            return fail(error);
        print(json, QJsonObject{{"created", manager.localDir() + "/" + args[1]}},
              "Created " + manager.localDir() + "/" + args[1]);
        return 0;
    }

    return fail("unknown command " + command + "\n" + Usage);
}
