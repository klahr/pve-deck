import QtQuick 2.0
import Sailfish.Silica 1.0
import PveDeck 1.0
import "../components"

// The cluster of a logged-in server: name, quorum and its nodes. Tap a
// node for its guests.
Page {
    id: page

    readonly property bool needsSession: true
    property string profileId
    readonly property var profile: serverStore.get(profileId)

    GuestStatus { id: guestStatus }

    ClusterOverview {
        id: cluster
        client: sessionManager.client(page.profileId)
        pollInterval: page.status === PageStatus.Active
                      && Qt.application.state === Qt.ApplicationActive ? 5000 : 0
    }

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: cluster

        PullDownMenu {
            MenuItem {
                //% "Refresh"
                text: qsTrId("pvedeck-me-refresh")
                onClicked: cluster.refresh()
            }
        }

        header: Column {
            width: listView.width

            PageHeader {
                title: cluster.clusterName.length > 0 ? cluster.clusterName : page.profile.displayName || ""
                description: cluster.isCluster
                             //% "Cluster"
                             ? qsTrId("pvedeck-la-cluster")
                             //% "Single node"
                             : qsTrId("pvedeck-la-single-node")
            }

            DetailItem {
                //% "Nodes online"
                label: qsTrId("pvedeck-la-nodes-online")
                //: %1 online nodes, %2 all nodes
                //% "%1 of %2"
                value: qsTrId("pvedeck-la-count-of-total").arg(cluster.onlineCount).arg(cluster.count)
                visible: cluster.loaded && cluster.isCluster
            }
            DetailItem {
                //% "Quorum"
                label: qsTrId("pvedeck-la-quorum")
                value: cluster.quorate
                       //% "Yes"
                       ? qsTrId("pvedeck-la-yes")
                       //% "No, the cluster is read-only"
                       : qsTrId("pvedeck-la-quorum-lost")
                visible: cluster.loaded && cluster.quorumKnown
            }

            SectionHeader {
                //% "Nodes"
                text: qsTrId("pvedeck-la-nodes")
                visible: cluster.loaded && cluster.count > 0
            }
        }

        delegate: ListItem {
            id: item

            contentHeight: Theme.itemSizeMedium
            enabled: model.online

            onClicked: pageStack.push(Qt.resolvedUrl("SessionPage.qml"),
                                      { profileId: page.profileId, node: model.name })

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
                color: model.online ? guestStatus.color("running") : "transparent"
                border.width: model.online ? 0 : 1
                border.color: Theme.secondaryColor
            }

            Column {
                anchors {
                    left: statusDot.right
                    leftMargin: Theme.paddingMedium
                    right: usage.visible ? usage.left : parent.right
                    rightMargin: usage.visible ? Theme.paddingMedium : Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                    text: model.address.length > 0 ? model.name + "  " + model.address : model.name
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: {
                        if (!model.online)
                            //% "Offline"
                            return qsTrId("pvedeck-la-offline")
                        var parts = [
                            guestStatus.percent(model.cpu) + " · "
                            + guestStatus.bytes(model.mem) + " / " + guestStatus.bytes(model.maxmem),
                            //: %1 running guests, %n all guests on the node
                            //% "%1 of %n guest(s) running"
                            qsTrId("pvedeck-la-guests-running", model.guests).arg(model.runningGuests)
                        ]
                        if (model.uptime > 0)
                            //: %1 is how long the node has been up, e.g. "3 d 4 h"
                            //% "up %1"
                            parts.push(qsTrId("pvedeck-la-up-for").arg(guestStatus.uptime(model.uptime)))
                        return parts.join(" · ")
                    }
                }
            }

            Column {
                id: usage
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                width: Theme.itemSizeLarge
                spacing: Theme.paddingSmall
                visible: model.online

                UsageBar {
                    width: parent.width
                    value: model.cpu
                }
                UsageBar {
                    width: parent.width
                    value: model.maxmem > 0 ? model.mem / model.maxmem : 0
                }
                UsageBar {
                    width: parent.width
                    value: model.maxdisk > 0 ? model.disk / model.maxdisk : 0
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: !cluster.loaded && cluster.errorString.length === 0
        }

        ViewPlaceholder {
            enabled: !cluster.loaded && cluster.errorString.length > 0
            //% "Could not load the cluster"
            text: qsTrId("pvedeck-la-cluster-error")
            hintText: cluster.errorString
        }

        VerticalScrollDecorator { }
    }
}
