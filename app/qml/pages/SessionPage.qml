import QtQuick 2.0
import Sailfish.Silica 1.0
import PveDeck 1.0

// Placeholder for a logged-in server. M3 replaces the body with the
// list of VMs and containers.
Page {
    id: page

    property string profileId
    readonly property var profile: serverStore.get(profileId)
    readonly property PveClient client: sessionManager.client(profileId)

    // Leave the page if the session ends.
    Connections {
        target: page.client
        onStateChanged: {
            if (page.client.state === PveClient.Disconnected && page.status === PageStatus.Active)
                pageStack.pop()
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                //% "Log out"
                text: qsTrId("pvedeck-me-logout")
                onClicked: sessionManager.logout(page.profileId)
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: page.profile.displayName || ""
                description: page.profile.host || ""
            }

            DetailItem {
                //% "Logged in as"
                label: qsTrId("pvedeck-la-logged-in-as")
                value: page.client ? page.client.userId : ""
            }
            DetailItem {
                //% "Proxmox VE version"
                label: qsTrId("pvedeck-la-pve-version")
                value: page.client && page.client.serverVersion.length > 0
                       ? page.client.serverVersion : "–"
            }
            DetailItem {
                //% "Certificate"
                label: qsTrId("pvedeck-la-certificate")
                value: page.profile.fingerprint
                       //% "Pinned"
                       ? qsTrId("pvedeck-la-certificate-pinned")
                       //% "Trusted by the system"
                       : qsTrId("pvedeck-la-certificate-system")
            }

            Item { width: 1; height: Theme.paddingLarge * 2 }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.secondaryColor
                //% "Virtual machines and containers will be listed here."
                text: qsTrId("pvedeck-la-session-placeholder")
            }
        }
    }
}
