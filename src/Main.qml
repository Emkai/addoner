import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1000
    height: 720
    minimumWidth: 680
    minimumHeight: 480
    visible: true
    title: "Addoner"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property bool modalOpen: newAddonDialog.visible || pageDialog.visible

    readonly property var lists: [downloadsList, installedList, localList]
    function currentList() { return tabs.currentIndex < 3 ? lists[tabs.currentIndex] : null }
    function currentRow() {
        const list = currentList()
        return list && list.currentIndex >= 0 && list.model ? list.model[list.currentIndex] : null
    }

    // Lists get a fresh array on every change; keep the scroll position and
    // selection instead of jumping back to the top.
    function refill(list, model) {
        const y = list.contentY, i = list.currentIndex
        list.model = model
        list.currentIndex = list.count > 0 ? Math.min(Math.max(i, 0), list.count - 1) : -1
        list.contentY = y
        list.returnToBounds()
    }

    function shortDate(iso) {
        return new Date(iso).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    function zipAction(status) {
        switch (status) {
        case "new": return qsTr("Install")
        case "update": return qsTr("Update")
        case "older": return qsTr("Install older")
        case "linked": return qsTr("Linked locally")
        default: return qsTr("Reinstall")
        }
    }

    function zipState(zip) {
        const version = zip.version || qsTr("no version")
        switch (zip.status) {
        case "new": return qsTr("%1 · not installed").arg(version)
        case "update": return qsTr("%1 · installed is %2").arg(version).arg(zip.installedVersion || qsTr("unknown"))
        case "older": return qsTr("%1 · older than the installed %2").arg(version).arg(zip.installedVersion)
        case "linked": return qsTr("%1 · a local addon is linked as %2").arg(version).arg(zip.main)
        default: return qsTr("%1 · installed").arg(version)
        }
    }

    // What the Installed tab says about an addon's web page.
    function webState(addon) {
        if (!addon.page) return qsTr("no web page")
        if (!addon.webChecked) return backend.checking ? qsTr("checking the web…") : qsTr("not checked yet")
        if (addon.webError) return qsTr("web: %1").arg(addon.webError)
        return addon.webNewer ? qsTr("web has %1").arg(addon.webVersion)
                              : qsTr("web has %1, nothing newer").arg(addon.webVersion)
    }

    // Open the page when it has something newer, or when it couldn't be read
    // and only a look will tell.
    function offersPage(addon) {
        return !!addon.page && (addon.webNewer || !!addon.webError)
    }

    function primaryAction() {
        const row = currentRow()
        if (!row) return
        if (tabs.currentIndex === 0) {
            if (row.status !== "linked") backend.install(row.path)
        } else if (tabs.currentIndex === 1) {
            if (row.updateZip) backend.install(row.updateZip)
            else if (row.page) backend.openPage(row.page)
        } else {
            row.linked ? backend.unlink(row.name) : backend.link(row.name)
        }
    }

    function removeAction() {
        const row = currentRow()
        if (!row) return
        if (tabs.currentIndex === 0) backend.deleteZip(row.path)
        else if (tabs.currentIndex === 1) backend.uninstall(row.main)
        else if (tabs.currentIndex === 2 && row.linked) backend.unlink(row.name)
    }

    function openAction() {
        backend.openFolder(tabs.currentIndex === 1 ? backend.addonsDir
                         : tabs.currentIndex === 2 ? backend.localDir
                         : backend.downloadsDir)
    }

    Component.onCompleted: {
        refill(downloadsList, backend.downloads)
        refill(installedList, backend.installed)
        refill(localList, backend.localAddons)
        if (!backend.addonsDirExists) tabs.currentIndex = 3
    }

    Connections {
        target: backend
        function onListsChanged() {
            win.refill(downloadsList, backend.downloads)
            win.refill(installedList, backend.installed)
            win.refill(localList, backend.localAddons)
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: Qt.quit() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut
        onActivated: {
            if (newAddonDialog.visible) newAddonDialog.reject()
            else if (pageDialog.visible) pageDialog.reject()
            else if (win.helpVisible) win.helpVisible = false
            else { backend.clearMessage(); content.forceActiveFocus() }
        }
    }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: tabs.currentIndex = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: tabs.currentIndex = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: tabs.currentIndex = 2 }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: tabs.currentIndex = 3 }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: { const l = win.currentList(); if (l) l.incrementCurrentIndex() }
    }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: { const l = win.currentList(); if (l) l.decrementCurrentIndex() }
    }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.primaryAction() }
    Shortcut { sequences: ["Delete", "X"]; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.removeAction() }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.openAction() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: backend.rescan()
    }
    Shortcut { sequence: "U"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: { tabs.currentIndex = 1; backend.updateAll() }
    }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: { tabs.currentIndex = 1; backend.checkWeb() }
    }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && tabs.currentIndex === 1
        onActivated: { const row = win.currentRow(); if (row) pageDialog.start(row) }
    }
    Shortcut { sequence: "W"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && tabs.currentIndex === 1
        onActivated: { const row = win.currentRow(); if (row && row.page) backend.openPage(row.page) }
    }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: { tabs.currentIndex = 2; newAddonDialog.start() }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: content
        anchors.fill: parent
        focus: true
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TabBar {
                id: tabs
                Layout.fillWidth: true
                background: Rectangle { color: "#141417" }
                TabButton {
                    focusPolicy: Qt.NoFocus
                    text: backend.updateCount > 0
                          ? qsTr("Downloads (%1)").arg(backend.updateCount.toLocaleString(Qt.locale(), 'f', 0))
                          : qsTr("Downloads")
                }
                TabButton {
                    focusPolicy: Qt.NoFocus
                    text: backend.webUpdateCount > 0
                          ? qsTr("Installed (%1)").arg(backend.webUpdateCount.toLocaleString(Qt.locale(), 'f', 0))
                          : qsTr("Installed")
                }
                TabButton { text: qsTr("Local"); focusPolicy: Qt.NoFocus }
                TabButton { text: qsTr("Settings"); focusPolicy: Qt.NoFocus }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: !backend.addonsDirExists
                color: "#3a1517"
                implicitHeight: missingLabel.implicitHeight + 16
                Label {
                    id: missingLabel
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.leftMargin: 16
                    wrapMode: Text.Wrap
                    text: qsTr("The AddOns folder doesn't exist: %1. Fix it under Settings.").arg(backend.addonsDir)
                }
            }

            StackLayout {
                currentIndex: tabs.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Downloads
                ColumnLayout {
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        Layout.bottomMargin: 0
                        spacing: 12
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            opacity: 0.7
                            text: qsTr("%n addon zips in %1", "", downloadsList.count).arg(backend.downloadsDir)
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Open folder")
                            flat: true
                            onClicked: backend.openFolder(backend.downloadsDir)
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Delete installed")
                            flat: true
                            enabled: backend.downloads.some(z => z.status === "installed" || z.status === "older")
                            onClicked: backend.deleteInstalledZips()
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Update all")
                            highlighted: true
                            enabled: backend.updateCount > 0
                            onClicked: backend.updateAll()
                        }
                    }

                    ListView {
                        id: downloadsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: AddonRow {
                            title: modelData.title
                            subtitle: win.zipState(modelData)
                            detail: modelData.file + "  ·  " + win.shortDate(modelData.modified) + "  ·  " + modelData.folders.join(", ")
                            badge: modelData.status === "update" ? qsTr("update") : ""
                            actionText: win.zipAction(modelData.status)
                            actionHighlighted: modelData.status === "new" || modelData.status === "update"
                            actionEnabled: modelData.status !== "linked"
                            removable: true
                            removeText: qsTr("Delete")
                            onActivated: backend.install(modelData.path)
                            onRemoveRequested: backend.deleteZip(modelData.path)
                        }
                        Label {
                            parent: downloadsList
                            anchors.centerIn: parent
                            width: parent.width * 0.7
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: downloadsList.count === 0
                            opacity: 0.6
                            text: qsTr("No addon zips in %1 yet. Download an addon's zip from CurseForge, WoWInterface or GitHub and it shows up here.").arg(backend.downloadsDir)
                        }
                    }
                }

                // Installed
                ColumnLayout {
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        Layout.bottomMargin: 0
                        spacing: 12
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: qsTr("%n addons installed from zips", "", installedList.count)
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Open folder")
                            flat: true
                            onClicked: backend.openFolder(backend.addonsDir)
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: backend.checking ? qsTr("Checking…") : qsTr("Check web")
                            flat: true
                            enabled: !backend.checking
                            onClicked: backend.checkWeb()
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Update all")
                            highlighted: true
                            enabled: backend.updateCount > 0
                            onClicked: backend.updateAll()
                        }
                    }

                    ListView {
                        id: installedList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: AddonRow {
                            title: modelData.title
                            subtitle: (modelData.version || qsTr("no version")) + "  ·  " + modelData.folders
                            detail: qsTr("from %1").arg(modelData.zip) + "  ·  " + win.webState(modelData)
                            badge: modelData.updateZip ? qsTr("update")
                                 : modelData.webNewer ? qsTr("new on web") : ""
                            actionText: modelData.updateZip ? qsTr("Update")
                                      : win.offersPage(modelData) ? qsTr("Open page") : ""
                            actionHighlighted: !!modelData.updateZip || modelData.webNewer
                            secondaryText: qsTr("Page…")
                            removable: true
                            onActivated: modelData.updateZip ? backend.install(modelData.updateZip)
                                                             : backend.openPage(modelData.page)
                            onSecondaryActivated: pageDialog.start(modelData)
                            onRemoveRequested: backend.uninstall(modelData.main)
                        }
                        footer: Item {
                            width: installedList.width
                            height: backend.otherFolders.length > 0 ? otherColumn.implicitHeight + 32 : 0
                            visible: backend.otherFolders.length > 0
                            ColumnLayout {
                                id: otherColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 12
                                anchors.topMargin: 24
                                spacing: 6
                                Label {
                                    text: qsTr("Other folders in AddOns (not managed by Addoner)")
                                    opacity: 0.6
                                    font.pixelSize: 12
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: backend.otherFolders.join(", ")
                                    opacity: 0.8
                                }
                            }
                        }
                        Label {
                            parent: installedList
                            anchors.centerIn: parent
                            visible: installedList.count === 0
                            opacity: 0.6
                            text: qsTr("Nothing installed from a zip yet")
                        }
                    }
                }

                // Local
                ColumnLayout {
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        Layout.bottomMargin: 0
                        spacing: 12
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            opacity: 0.7
                            text: backend.localDir
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Open folder")
                            flat: true
                            onClicked: backend.openFolder(backend.localDir)
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("New addon")
                            highlighted: true
                            onClicked: newAddonDialog.start()
                        }
                    }

                    ListView {
                        id: localList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: LocalRow {
                            onToggled: modelData.linked ? backend.unlink(modelData.name) : backend.link(modelData.name)
                        }
                        Label {
                            parent: localList
                            anchors.centerIn: parent
                            width: parent.width * 0.7
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: localList.count === 0
                            opacity: 0.6
                            text: qsTr("Put addon folders in the local addons folder, or make a new one with N.")
                        }
                    }
                }

                // Settings
                ScrollView {
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: Math.min(parent.width - 32, 720)
                        x: 16
                        spacing: 16

                        Item { Layout.preferredHeight: 8 }

                        TextField {
                            id: addonsField
                            Layout.fillWidth: true
                            placeholderText: qsTr("WoW AddOns folder")
                            text: backend.addonsDir
                        }
                        TextField {
                            id: localField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Local addons folder")
                            text: backend.localDir
                        }
                        TextField {
                            id: downloadsField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Downloads folder, where addon zips are found")
                            text: backend.downloadsDir
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Save")
                            highlighted: true
                            onClicked: {
                                if (backend.saveSettings(addonsField.text, localField.text, downloadsField.text) === "")
                                    tabs.currentIndex = 0
                            }
                        }
                    }
                }
            }

            // Status bar
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 34
                color: backend.error ? "#3a1517" : "#141417"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: backend.error || backend.status
                        color: backend.error ? "#ff8a8f" : "white"
                        opacity: backend.error ? 1 : 0.7
                    }
                    Label {
                        text: qsTr("? for shortcuts")
                        opacity: 0.4
                        font.pixelSize: 12
                    }
                }
            }
        }

        // Shortcut overlay
        Rectangle {
            anchors.fill: parent
            visible: win.helpVisible
            color: Qt.rgba(0, 0, 0, 0.75)
            TapHandler { onTapped: win.helpVisible = false }

            Rectangle {
                anchors.centerIn: parent
                width: helpGrid.implicitWidth + 64
                height: helpGrid.implicitHeight + 64
                color: "#18181b"
                radius: theme.radius
                border.color: Qt.rgba(1, 1, 1, 0.08)

                GridLayout {
                    id: helpGrid
                    anchors.centerIn: parent
                    columns: 2
                    columnSpacing: 28
                    rowSpacing: 8

                    Repeater {
                        model: [
                            ["1 – 4", qsTr("Downloads, Installed, Local, Settings")],
                            ["↑ ↓  J K", qsTr("Move the selection")],
                            [qsTr("Space"), qsTr("Install, update, open page, link or unlink")],
                            [qsTr("Delete") + "  X", qsTr("Delete the zip, remove or unlink")],
                            ["O", qsTr("Open this tab's folder")],
                            ["R", qsTr("Look in Downloads again")],
                            ["C", qsTr("Check the web for newer versions")],
                            ["P", qsTr("Set the addon's web page")],
                            ["W", qsTr("Open the addon's web page")],
                            ["U", qsTr("Update all")],
                            ["N", qsTr("New local addon")],
                            [qsTr("Esc"), qsTr("Clear the message")],
                            ["?", qsTr("Show or hide shortcuts")],
                            ["Q", qsTr("Quit")]
                        ]
                        delegate: Item {
                            required property var modelData
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            implicitWidth: keyLabel.width + 28 + descLabel.implicitWidth
                            implicitHeight: Math.max(keyLabel.implicitHeight, descLabel.implicitHeight)
                            Label {
                                id: keyLabel
                                width: 90
                                text: modelData[0]
                                color: theme.accent
                                font.bold: true
                            }
                            Label {
                                id: descLabel
                                x: keyLabel.width + 28
                                text: modelData[1]
                            }
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: newAddonDialog
        title: qsTr("New local addon")
        anchors.centerIn: parent
        modal: true
        width: Math.min(460, win.width - 48)
        standardButtons: Dialog.Ok | Dialog.Cancel
        Material.roundedScale: theme.radius

        property string problem: ""

        // Reopening after a failed create keeps what was typed and shows why.
        function start() {
            problem = ""
            nameField.text = ""
            titleField.text = ""
            const guess = backend.suggestedInterface()
            interfaceField.text = guess > 0 ? String(guess) : ""
            open()
        }
        onOpened: nameField.forceActiveFocus()
        onAccepted: {
            const error = backend.createAddon(nameField.text, titleField.text, interfaceField.text)
            if (error !== "") {
                problem = error
                open()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Folder name, e.g. MyAddon")
                onAccepted: newAddonDialog.accept()
            }
            TextField {
                id: titleField
                Layout.fillWidth: true
                placeholderText: qsTr("Title shown in game (optional)")
                onAccepted: newAddonDialog.accept()
            }
            TextField {
                id: interfaceField
                Layout.fillWidth: true
                placeholderText: qsTr("Interface version")
                inputMethodHints: Qt.ImhDigitsOnly
                onAccepted: newAddonDialog.accept()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: newAddonDialog.problem !== ""
                text: newAddonDialog.problem
                color: "#ff8a8f"
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.6
                font.pixelSize: 12
                text: qsTr("Creates a .toc and a .lua with a slash command, and links it into AddOns.")
            }
        }
    }

    Dialog {
        id: pageDialog
        title: qsTr("Web page for %1").arg(addonTitle)
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, win.width - 48)
        standardButtons: Dialog.Ok | Dialog.Cancel
        Material.roundedScale: theme.radius

        property string main: ""
        property string addonTitle: ""
        property string problem: ""

        function start(addon) {
            main = addon.main
            addonTitle = addon.title
            problem = ""
            urlField.text = addon.page || ""
            open()
        }
        onOpened: { urlField.forceActiveFocus(); urlField.selectAll() }
        onAccepted: {
            const error = backend.setPage(main, urlField.text)
            if (error !== "") {
                problem = error
                open()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: qsTr("https://www.wowinterface.com/downloads/info…")
                onAccepted: pageDialog.accept()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: pageDialog.problem !== ""
                text: pageDialog.problem
                color: "#ff8a8f"
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.6
                font.pixelSize: 12
                text: qsTr("WoWInterface pages and GitHub repositories are checked for newer versions. CurseForge doesn't let programs read its pages, so those only get an Open page button. Leave it empty to forget the page.")
            }
        }
    }
}
