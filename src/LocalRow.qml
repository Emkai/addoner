import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One folder from the local addons folder.
Rectangle {
    id: row

    required property var modelData
    required property int index
    property bool selected: ListView.isCurrentItem

    signal toggled()

    width: ListView.view ? ListView.view.width : 0
    height: 62
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
            Layout.preferredWidth: 10
            Layout.preferredHeight: 10
            radius: 5
            color: row.modelData.linked ? theme.accent : row.modelData.conflict ? "#e5484d" : "#55555c"
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: row.modelData.title && row.modelData.title !== row.modelData.name
                      ? qsTr("%1 (%2)").arg(row.modelData.title).arg(row.modelData.name)
                      : row.modelData.name
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: row.modelData.linked ? qsTr("Linked into AddOns. Use /reload in game after edits.")
                    : row.modelData.conflict ? qsTr("AddOns already has a folder with this name")
                    : !row.modelData.hasToc ? qsTr("No .toc file, so WoW won't load it")
                    : qsTr("Not linked")
                color: row.modelData.conflict || (!row.modelData.hasToc && !row.modelData.linked) ? "#e5484d" : "white"
                opacity: 0.7
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        Button {
            Material.roundedScale: theme.radius
            focusPolicy: Qt.NoFocus
            highlighted: !row.modelData.linked
            enabled: row.modelData.linked || (!row.modelData.conflict && backend.addonsDirExists)
            text: row.modelData.linked ? qsTr("Unlink") : qsTr("Link")
            onClicked: row.toggled()
        }
    }
}
