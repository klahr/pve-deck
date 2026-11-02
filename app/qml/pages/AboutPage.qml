import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                //% "About"
                title: qsTrId("pvedeck-he-about")
            }

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "/usr/share/icons/hicolor/172x172/apps/pvedeck.png"
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: appInfo.name + " " + appInfo.version
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.secondaryHighlightColor
                //% "Proxmox VE client for Sailfish OS. Monitor and control virtual machines and containers, and open a shell."
                text: qsTrId("pvedeck-la-about-description")
            }
        }
    }
}
