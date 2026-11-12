import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One addon in the Downloads or Installed list: a zip, or what was
// installed from one.
Rectangle {
    id: row

    required property var modelData
    required property int index
    property string title: ""
    property string subtitle: ""
    property string detail: ""
    property string badge: ""
    property string actionText: ""
    property bool actionHighlighted: false
    property bool actionEnabled: true
    property bool removable: false
    property string removeText: qsTr("Remove")
    property string secondaryText: ""
    property bool selected: ListView.isCurrentItem

    signal activated()
    signal removeRequested()
    signal secondaryActivated()

    width: ListView.view ? ListView.view.width : 0
    height: 72
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
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            radius: Math.min(4, theme.radius)
            color: "#1c1c20"
            Label {
                anchors.centerIn: parent
                text: (row.title || "?").charAt(0).toUpperCase()
                font.pixelSize: 19
                font.bold: true
                color: theme.accent
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            RowLayout {
                spacing: 8
                Label {
                    text: row.title
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.maximumWidth: row.width * 0.5
                }
                Label {
                    visible: row.badge !== ""
                    text: row.badge
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
                opacity: 0.8
                font.pixelSize: 13
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Label {
                text: row.detail
                visible: text.length > 0
                opacity: 0.5
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        Button {
            Material.roundedScale: theme.radius
            focusPolicy: Qt.NoFocus
            flat: true
            visible: row.secondaryText !== ""
            text: row.secondaryText
            onClicked: row.secondaryActivated()
        }
        Button {
            Material.roundedScale: theme.radius
            focusPolicy: Qt.NoFocus
            flat: true
            visible: row.removable
            text: row.removeText
            onClicked: row.removeRequested()
        }
        Button {
            Material.roundedScale: theme.radius
            focusPolicy: Qt.NoFocus
            visible: row.actionText !== ""
            highlighted: row.actionHighlighted
            enabled: row.actionEnabled
            text: row.actionText
            onClicked: row.activated()
        }
    }
}
