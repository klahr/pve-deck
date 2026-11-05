import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    readonly property int connectedCount: sessionManager.connectedIds.length

    onStatusChanged: {
        appWindow.coverActive = status === Cover.Active
        // Polling stops while the cover is out of view; catch up at once.
        if (status === Cover.Active)
            sessionManager.refreshAll()
    }

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
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: connectedCount > 0
            //% "%n running"
            text: qsTrId("pvedeck-cover-running", sessionManager.runningCount)
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: connectedCount > 0
            color: Theme.secondaryColor
            //% "%n stopped"
            text: qsTrId("pvedeck-cover-stopped", sessionManager.stoppedCount)
        }
    }

    CoverActionList {
        enabled: connectedCount > 0

        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: sessionManager.refreshAll()
        }
    }
}
