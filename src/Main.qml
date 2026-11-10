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
    property bool freshSearch: true
    readonly property bool modalOpen: newAddonDialog.visible || quitDialog.visible

    readonly property var lists: [browseList, installedList, localList]
    function currentList() { return tabs.currentIndex < 3 ? lists[tabs.currentIndex] : null }
    function currentRow() {
        const list = currentList()
        return list && list.currentIndex >= 0 && list.model ? list.model[list.currentIndex] : null
    }

    function compact(n) {
        if (n >= 1e6) return qsTr("%1M").arg((n / 1e6).toLocaleString(Qt.locale(), 'f', 1))
        if (n >= 1e3) return qsTr("%1K").arg((n / 1e3).toLocaleString(Qt.locale(), 'f', 0))
        return Number(n).toLocaleString(Qt.locale(), 'f', 0)
    }

    // Lists get a fresh array on every change; keep the scroll position and
    // selection instead of jumping back to the top.
    function refill(list, model, keep) {
        const y = list.contentY, i = list.currentIndex
        list.model = model
        if (keep) {
            list.currentIndex = Math.min(Math.max(i, 0), list.count - 1)
            list.contentY = y
            list.returnToBounds()
        } else {
            list.currentIndex = list.count > 0 ? 0 : -1
            list.positionViewAtBeginning()
        }
    }

    function runSearch() {
        freshSearch = true
        backend.search(searchField.text)
    }

    function primaryAction() {
        const row = currentRow()
        if (!row) return
        if (tabs.currentIndex === 2) {
            row.linked ? backend.unlink(row.name) : backend.link(row.name)
        } else if (!row.installed || row.hasUpdate) {
            backend.install(row.id)
        }
    }

    function removeAction() {
        const row = currentRow()
        if (!row) return
        if (tabs.currentIndex === 2) {
            if (row.linked) backend.unlink(row.name)
        } else if (row.installed) {
            backend.uninstall(row.id)
        }
    }

    function openAction() {
        const row = currentRow()
        if (tabs.currentIndex === 2) backend.openLocalFolder()
        else if (row && row.url) Qt.openUrlExternally(row.url)
    }

    function quit() {
        if (backend.busy) quitDialog.open()
        else Qt.quit()
    }

    Component.onCompleted: {
        refill(installedList, backend.installed, false)
        refill(localList, backend.localAddons, false)
        if (backend.apiKeySet) runSearch()
        else tabs.currentIndex = 3
    }

    Connections {
        target: backend
        function onResultsChanged() {
            if (backend.searching) return
            refill(browseList, backend.results, !win.freshSearch)
            win.freshSearch = false
        }
        function onInstalledChanged() { refill(installedList, backend.installed, true) }
        function onLocalChanged() { refill(localList, backend.localAddons, true) }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: win.quit() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut
        onActivated: {
            if (newAddonDialog.visible) newAddonDialog.reject()
            else if (quitDialog.visible) quitDialog.reject()
            else if (win.helpVisible) win.helpVisible = false
            else { backend.clearMessage(); content.forceActiveFocus() }
        }
    }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: tabs.currentIndex = 0 }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: tabs.currentIndex = 1 }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: tabs.currentIndex = 2 }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen; onActivated: tabs.currentIndex = 3 }
    Shortcut { sequences: ["/", StandardKey.Find]; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: { tabs.currentIndex = 0; searchField.forceActiveFocus(); searchField.selectAll() }
    }
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
        onActivated: { tabs.currentIndex = 1; backend.checkUpdates() }
    }
    Shortcut { sequence: "U"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen
        onActivated: { tabs.currentIndex = 1; backend.updateAll() }
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
                TabButton { text: qsTr("Browse"); focusPolicy: Qt.NoFocus }
                TabButton {
                    Material.roundedScale: theme.radius
                    focusPolicy: Qt.NoFocus
                    text: backend.updateCount > 0
                          ? qsTr("Installed (%1)").arg(backend.updateCount.toLocaleString(Qt.locale(), 'f', 0))
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

                // Browse
                ColumnLayout {
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        Layout.bottomMargin: 0
                        spacing: 12
                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Search CurseForge")
                            onAccepted: { win.runSearch(); content.forceActiveFocus() }
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Search")
                            highlighted: true
                            onClicked: win.runSearch()
                        }
                    }

                    Label {
                        Layout.leftMargin: 18
                        opacity: 0.6
                        font.pixelSize: 12
                        visible: backend.apiKeySet
                        text: backend.searching ? qsTr("Searching…")
                            : searchField.text.length > 0 ? qsTr("%n results", "", backend.resultsTotal)
                            : qsTr("Most popular")
                    }

                    ListView {
                        id: browseList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 8
                        Layout.rightMargin: 8
                        clip: true
                        spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        onAtYEndChanged: if (atYEnd && count > 0) backend.loadMore()
                        delegate: AddonRow {
                            subtitle: qsTr("by %1 · %2 downloads").arg(modelData.authors).arg(win.compact(modelData.downloads))
                            onActivated: backend.install(modelData.id)
                            onRemoveRequested: backend.uninstall(modelData.id)
                        }

                        ColumnLayout {
                            parent: browseList
                            anchors.centerIn: parent
                            visible: !backend.apiKeySet
                            spacing: 12
                            Label { text: qsTr("Add a CurseForge API key to browse addons."); Layout.alignment: Qt.AlignHCenter }
                            Button {
                                Material.roundedScale: theme.radius
                                focusPolicy: Qt.NoFocus
                                text: qsTr("Open Settings")
                                highlighted: true
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: tabs.currentIndex = 3
                            }
                        }
                        Label {
                            parent: browseList
                            anchors.centerIn: parent
                            visible: backend.apiKeySet && !backend.searching && browseList.count === 0
                            opacity: 0.6
                            text: qsTr("Nothing found")
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
                            text: backend.updateCount > 0
                                  ? qsTr("%n addons", "", installedList.count) + " · " + qsTr("%n updates available", "", backend.updateCount)
                                  : qsTr("%n addons", "", installedList.count)
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Check for updates")
                            flat: true
                            enabled: !backend.busy && installedList.count > 0
                            onClicked: backend.checkUpdates()
                        }
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Update all")
                            highlighted: true
                            enabled: !backend.busy && backend.updateCount > 0
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
                            subtitle: (modelData.displayName || modelData.fileName || "") + "  ·  " + modelData.folders
                            onActivated: backend.install(modelData.id)
                            onRemoveRequested: backend.uninstall(modelData.id)
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
                            text: qsTr("No CurseForge addons installed yet")
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
                            onClicked: backend.openLocalFolder()
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
                            id: keyField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            placeholderText: backend.apiKeySet ? qsTr("CurseForge API key (saved, type to replace)")
                                                               : qsTr("CurseForge API key")
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            opacity: 0.7
                            textFormat: Text.StyledText
                            linkColor: theme.accent
                            text: qsTr("Get a free key at <a href=\"https://console.curseforge.com\">console.curseforge.com</a>. You can also set CURSEFORGE_API_KEY.")
                            onLinkActivated: (link) => Qt.openUrlExternally(link)
                        }
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
                        Button {
                            Material.roundedScale: theme.radius
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Save")
                            highlighted: true
                            onClicked: {
                                if (backend.saveSettings(keyField.text, addonsField.text, localField.text) === "") {
                                    keyField.text = ""
                                    if (backend.apiKeySet) tabs.currentIndex = 0
                                }
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
                    BusyIndicator {
                        running: backend.busy || backend.searching
                        visible: running
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                    }
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
                            ["1 – 4", qsTr("Browse, Installed, Local, Settings")],
                            ["/", qsTr("Search CurseForge")],
                            ["↑ ↓  J K", qsTr("Move the selection")],
                            [qsTr("Space"), qsTr("Install, update, link or unlink")],
                            [qsTr("Delete") + "  X", qsTr("Remove or unlink")],
                            ["O", qsTr("Open the addon's page, or the local folder")],
                            ["R", qsTr("Check for updates")],
                            ["U", qsTr("Update all")],
                            ["N", qsTr("New local addon")],
                            [qsTr("Esc"), qsTr("Leave the search field")],
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
        id: quitDialog
        title: qsTr("Quit while installing?")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Material.roundedScale: theme.radius
        Label { text: qsTr("An install is still running. Quitting now may leave that addon half updated.") }
        onAccepted: Qt.quit()
    }
}
