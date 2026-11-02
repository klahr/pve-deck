import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: serverStore

        PullDownMenu {
            MenuItem {
                //% "About"
                text: qsTrId("pvedeck-me-about")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                //% "Log out of all"
                text: qsTrId("pvedeck-me-logout-all")
                visible: sessionManager.connectedIds.length > 0
                onClicked: sessionManager.logoutAll()
            }
            MenuItem {
                //% "Add server"
                text: qsTrId("pvedeck-me-add-server")
                onClicked: pageStack.push(Qt.resolvedUrl("ServerDialog.qml"))
            }
        }

        header: PageHeader {
            //% "Servers"
            title: qsTrId("pvedeck-he-servers")
        }

        delegate: ListItem {
            id: item

            readonly property bool isConnected: sessionManager.connectedIds.indexOf(model.profileId) >= 0
            readonly property bool isBusy: sessionManager.busyIds.indexOf(model.profileId) >= 0

            contentHeight: Theme.itemSizeMedium
            menu: contextMenu
            ListView.onRemove: animateRemoval(item)

            function remove() {
                //% "Deleting"
                remorseDelete(function() { sessionManager.removeServer(model.profileId) })
            }

            onClicked: {
                if (isConnected)
                    pageStack.push(Qt.resolvedUrl("SessionPage.qml"), { profileId: model.profileId })
                else if (!isBusy)
                    sessionManager.connectServer(model.profileId)
            }

            Rectangle {
                id: statusDot
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                width: Theme.paddingMedium
                height: width
                radius: width / 2
                color: item.isConnected ? "#4caf50" : "transparent"
                border.width: item.isConnected ? 0 : 1
                border.color: Theme.secondaryColor
                visible: !item.isBusy
            }

            BusyIndicator {
                anchors.centerIn: statusDot
                size: BusyIndicatorSize.ExtraSmall
                running: item.isBusy
            }

            Column {
                anchors {
                    left: statusDot.right
                    leftMargin: Theme.paddingLarge
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: model.displayName
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: (model.authMethod === "token"
                           ? model.userId + "!" + model.tokenName
                           : model.userId) + " · " + model.host + ":" + model.port
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            Component {
                id: contextMenu
                ContextMenu {
                    MenuItem {
                        //% "Log out"
                        text: qsTrId("pvedeck-me-logout")
                        visible: item.isConnected
                        onClicked: sessionManager.logout(model.profileId)
                    }
                    MenuItem {
                        //% "Edit"
                        text: qsTrId("pvedeck-me-edit")
                        onClicked: pageStack.push(Qt.resolvedUrl("ServerDialog.qml"),
                                                  { profileId: model.profileId })
                    }
                    MenuItem {
                        //% "Delete"
                        text: qsTrId("pvedeck-me-delete")
                        onClicked: item.remove()
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0
            //% "No servers"
            text: qsTrId("pvedeck-la-no-servers")
            //% "Pull down to add a Proxmox VE server"
            hintText: qsTrId("pvedeck-la-no-servers-hint")
        }

        VerticalScrollDecorator { }
    }
}
