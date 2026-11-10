#include "cli.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "manager.h"

namespace {

const QStringList Commands{"help", "--help", "-h", "downloads", "list", "install", "remove", "update",
                           "local", "link", "unlink", "new"};

const char *Usage =
    "Usage: addoner [command] [--json]\n"
    "\n"
    "  (no command)            open the window\n"
    "  downloads               addon zips in the downloads folder, and their status\n"
    "  install <zip>           install or update from a zip (a path, or a file name\n"
    "                          in the downloads folder)\n"
    "  remove <folder>         remove an addon Addoner installed\n"
    "  list                    installed addons, and which have an update zip\n"
    "  update                  install every update zip in the downloads folder\n"
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

QString option(QStringList &args, const QString &name) {
    const int i = args.indexOf(name);
    if (i < 0 || i + 1 >= args.size())
        return {};
    const QString value = args.at(i + 1);
    args.remove(i, 2);
    return value;
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

    if (command == "downloads") {
        const QJsonArray zips = manager.downloads();
        QString text;
        for (const auto &value : zips) {
            const QJsonObject zip = value.toObject();
            const QString status = zip.value("status").toString();
            const QString shown = status == "update" ? "update from " + zip.value("installedVersion").toString()
                                : status == "older" ? "older than installed " + zip.value("installedVersion").toString()
                                : status == "linked" ? "blocked: linked from local addons"
                                : status == "new" ? "not installed" : "installed";
            text += QString("%1  %2 %3  [%4]\n").arg(zip.value("file").toString(), zip.value("title").toString(),
                                                     zip.value("version").toString(), shown);
        }
        print(json, zips, text.isEmpty() ? "No addon zips in " + manager.downloadsDir() : text.trimmed());
        return 0;
    }

    if (command == "install") {
        if (args.size() != 2)
            return fail("install takes one zip");
        QString zip = args[1];
        if (!QFileInfo(zip).isFile() && QFileInfo(QDir(manager.downloadsDir()).filePath(zip)).isFile())
            zip = QDir(manager.downloadsDir()).filePath(zip);
        QString name;
        const QString error = manager.install(zip, &name);
        if (!error.isEmpty())
            return fail(error);
        print(json, QJsonObject{{"installed", name}}, "Installed " + name);
        return 0;
    }

    if (command == "update") {
        QStringList names;
        const QString error = manager.updateAll(&names);
        if (!error.isEmpty())
            return fail(error);
        print(json, QJsonObject{{"installed", QJsonArray::fromStringList(names)}},
              names.isEmpty() ? "Nothing to update." : "Updated " + names.join(", "));
        return 0;
    }

    if (command == "remove") {
        if (args.size() != 2)
            return fail("remove takes one addon folder name");
        const QString error = manager.uninstall(args[1]);
        if (!error.isEmpty())
            return fail(error);
        print(json, QJsonObject{{"removed", args[1]}}, "Removed.");
        return 0;
    }

    if (command == "list") {
        const QJsonObject addons = manager.installedAddons();
        const auto updates = manager.updates();
        QJsonArray rows;
        QString text;
        for (auto it = addons.begin(); it != addons.end(); ++it) {
            QJsonObject row = it.value().toObject();
            row["main"] = it.key();
            row["updateZip"] = updates.value(it.key());
            rows.append(row);
            text += QString("%1  %2%3\n").arg(it.key(), row.value("version").toString(),
                                              updates.contains(it.key()) ? "  [update: " + QFileInfo(updates.value(it.key())).fileName() + "]" : "");
        }
        print(json, rows, text.isEmpty() ? "No addons installed from zips." : text.trimmed());
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
