import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    readonly property int connectedCount: sessionManager.connectedIds.length

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingMedium

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "PVE Deck"
            font.pixelSize: Theme.fontSizeLarge
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: Theme.secondaryColor
            text: connectedCount === 0
                  //% "Not connected"
                  ? qsTrId("pvedeck-cover-not-connected")
                  //% "Connected to %n server(s)"
                  : qsTrId("pvedeck-cover-connected", connectedCount)
        }
    }
}
