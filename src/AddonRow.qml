import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One CurseForge addon in the Browse or Installed list.
Rectangle {
    id: row

    required property var modelData
    required property int index
    property string subtitle: ""
    property bool selected: ListView.isCurrentItem

    signal activated()
    signal removeRequested()

    width: ListView.view ? ListView.view.width : 0
    height: 78
    radius: Math.min(6, theme.radius)
    color: selected ? Qt.rgba(1, 1, 1, 0.06) : hover.hovered ? Qt.rgba(1, 1, 1, 0.03) : "transparent"
    border.width: 1
    border.color: selected ? theme.accent : "transparent"

    Material.roundedScale: theme.radius

    HoverHandler { id: hover }
    TapHandler { onTapped: row.ListView.view.currentIndex = row.index }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 14

        Rectangle {
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            radius: Math.min(4, theme.radius)
            color: "#1c1c20"
            clip: true

            Label {
                anchors.centerIn: parent
                visible: logo.status !== Image.Ready
                text: (row.modelData.name || "?").charAt(0).toUpperCase()
                font.pixelSize: 20
                font.bold: true
                color: theme.accent
            }
            Image {
                id: logo
                anchors.fill: parent
                source: row.modelData.logo || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize: Qt.size(96, 96)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                spacing: 8
                Label {
                    text: row.modelData.name
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: false
                    Layout.maximumWidth: row.width * 0.5
                }
                Label {
                    visible: row.modelData.hasUpdate === true
                    text: qsTr("update")
                    color: theme.accentForeground
                    font.pixelSize: 11
                    leftPadding: 6; rightPadding: 6; topPadding: 1; bottomPadding: 1
                    background: Rectangle { color: theme.accent; radius: Math.min(3, theme.radius) }
                }
                Item { Layout.fillWidth: true }
            }
            Label {
                text: row.subtitle
                visible: text.length > 0
                opacity: 0.6
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: row.modelData.summary || ""
                visible: text.length > 0
                opacity: 0.8
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        Button {
            Material.roundedScale: theme.radius
            focusPolicy: Qt.NoFocus
            flat: true
            visible: row.modelData.installed === true
            enabled: !backend.busy
            text: qsTr("Remove")
            onClicked: row.removeRequested()
        }
        Button {
            Material.roundedScale: theme.radius
            focusPolicy: Qt.NoFocus
            highlighted: !row.modelData.installed || row.modelData.hasUpdate
            enabled: !backend.busy && (!row.modelData.installed || row.modelData.hasUpdate)
            text: row.modelData.hasUpdate ? qsTr("Update")
                : row.modelData.installed ? qsTr("Installed")
                : qsTr("Install")
            onClicked: row.activated()
        }
    }
}
