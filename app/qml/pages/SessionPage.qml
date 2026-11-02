import QtQuick 2.0
import Sailfish.Silica 1.0
import PveDeck 1.0
import "../components"

// The guests (VMs and containers) of a logged-in server, per node, or of
// one node when node is set.
Page {
    id: page

    readonly property bool needsSession: true
    property string profileId
    property string node
    readonly property var profile: serverStore.get(profileId)
    readonly property QtObject client: sessionManager.client(profileId)
    readonly property QtObject resources: sessionManager.resources(profileId)

    GuestStatus { id: guestStatus }

    ResourceFilterModel {
        id: filterModel
        resources: page.resources
        node: page.node
    }

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: filterModel

        PullDownMenu {
            MenuItem {
                //% "Log out"
                text: qsTrId("pvedeck-me-logout")
                onClicked: sessionManager.logout(page.profileId)
            }
            MenuItem {
                //% "Cluster"
                text: qsTrId("pvedeck-me-cluster")
                visible: page.node.length === 0
                onClicked: pageStack.push(Qt.resolvedUrl("ClusterPage.qml"), { profileId: page.profileId })
            }
            MenuItem {
                //% "Refresh"
                text: qsTrId("pvedeck-me-refresh")
                onClicked: page.resources.refresh()
            }
        }

        header: Column {
            width: listView.width

            PageHeader {
                title: page.node.length > 0 ? page.node : page.profile.displayName || ""
                description: {
                    var parts = []
                    if (page.node.length > 0)
                        parts.push(page.profile.displayName || "")
                    if (page.client && page.client.userId.length > 0)
                        parts.push(page.client.userId)
                    if (page.client && page.client.serverVersion.length > 0)
                        parts.push("PVE " + page.client.serverVersion)
                    return parts.join(" · ")
                }
            }

            SearchField {
                width: parent.width
                //% "Search"
                placeholderText: qsTrId("pvedeck-ph-search")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextChanged: filterModel.searchText = text
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            ComboBox {
                width: parent.width
                //% "Show"
                label: qsTrId("pvedeck-la-show")
                currentIndex: filterModel.show
                onCurrentIndexChanged: filterModel.show = currentIndex
                // Same order as ResourceFilterModel::Show.
                menu: ContextMenu {
                    //% "All"
                    MenuItem { text: qsTrId("pvedeck-me-show-all") }
                    //% "Running"
                    MenuItem { text: qsTrId("pvedeck-me-show-running") }
                    //% "Stopped"
                    MenuItem { text: qsTrId("pvedeck-me-show-stopped") }
                    //% "Virtual machines"
                    MenuItem { text: qsTrId("pvedeck-me-show-vms") }
                    //% "Containers"
                    MenuItem { text: qsTrId("pvedeck-me-show-containers") }
                }
            }
        }

        section {
            property: page.node.length > 0 ? "" : "node"
            delegate: SectionHeader { text: section }
        }

        delegate: ListItem {
            id: item

            readonly property bool running: model.status === "running"
            readonly property var tags: model.tags
            readonly property bool busy: powerActions.busyGuests.indexOf(page.profileId + "/" + model.vmid) >= 0
            readonly property var actions: busy ? []
                                                : powerActions.availableActions(model.type, model.status, "",
                                                                                model.template, model.lock)

            function runAction(action) {
                var guestNode = model.node
                var guestType = model.type
                var guestId = model.vmid
                var label = model.name || String(model.vmid)
                if (guestStatus.isDestructive(action)) {
                    remorseAction(guestStatus.remorseText(action), function() {
                        powerActions.run(page.profileId, guestNode, guestType, guestId, action, label)
                    })
                } else {
                    powerActions.run(page.profileId, guestNode, guestType, guestId, action, label)
                }
            }

            // A third line for the tags when there are any.
            contentHeight: Math.max(Theme.itemSizeMedium, rowText.height + 2 * Theme.paddingMedium)
            menu: actions.length > 0 ? contextMenu : null

            onClicked: pageStack.push(Qt.resolvedUrl("GuestPage.qml"), {
                                          profileId: page.profileId,
                                          node: model.node,
                                          type: model.type,
                                          vmid: model.vmid,
                                          name: model.name
                                      })

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
                color: guestStatus.color(model.status)
                border.width: model.status === "stopped" ? 1 : 0
                border.color: Theme.secondaryColor
                visible: !item.busy
            }

            BusyIndicator {
                anchors.centerIn: statusDot
                size: BusyIndicatorSize.ExtraSmall
                running: item.busy
            }

            Component {
                id: contextMenu
                ContextMenu {
                    Repeater {
                        model: item.actions
                        MenuItem {
                            text: guestStatus.actionText(modelData)
                            onClicked: item.runAction(modelData)
                        }
                    }
                }
            }

            Column {
                id: rowText
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
                    text: model.vmid + "  " + (model.name || "")
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    text: {
                        var parts = [model.type === "lxc"
                                     //% "CT"
                                     ? qsTrId("pvedeck-la-type-ct-short")
                                     //% "VM"
                                     : qsTrId("pvedeck-la-type-vm-short")]
                        if (model.template)
                            //% "Template"
                            parts.push(qsTrId("pvedeck-la-template"))
                        else if (item.running)
                            parts.push(guestStatus.percent(model.cpu) + " · "
                                       + guestStatus.bytes(model.mem) + " / " + guestStatus.bytes(model.maxmem))
                        else
                            parts.push(guestStatus.text(model.status))
                        if (model.lock)
                            //: %1 is the lock type, e.g. "backup" or "migrate"
                            //% "Locked (%1)"
                            parts.push(qsTrId("pvedeck-la-locked").arg(model.lock))
                        return parts.join(" · ")
                    }
                }

                // The tags, as coloured dots with their names.
                Row {
                    width: parent.width
                    spacing: Theme.paddingMedium
                    clip: true
                    visible: item.tags.length > 0

                    Repeater {
                        model: item.tags

                        Row {
                            spacing: Theme.paddingSmall

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.paddingMedium
                                height: width
                                radius: width / 2
                                color: guestStatus.tagColor(modelData, page.resources ? page.resources.tagColors : null)
                            }
                            Label {
                                font.pixelSize: Theme.fontSizeExtraSmall
                                color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                                text: modelData
                            }
                        }
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
                visible: item.running

                UsageBar {
                    width: parent.width
                    value: model.cpu
                }
                UsageBar {
                    width: parent.width
                    value: model.maxmem > 0 ? model.mem / model.maxmem : 0
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: page.resources && !page.resources.loaded && page.resources.errorString.length === 0
        }

        ViewPlaceholder {
            enabled: page.resources !== null
                     && (page.resources.loaded ? filterModel.count === 0
                                               : page.resources.errorString.length > 0)
            text: !page.resources || !page.resources.loaded
                  //% "Could not load guests"
                  ? qsTrId("pvedeck-la-resources-error")
                  : page.resources.count === 0
                    //% "No virtual machines or containers"
                    ? qsTrId("pvedeck-la-no-guests")
                    //% "No matches"
                    : qsTrId("pvedeck-la-no-matches")
            hintText: page.resources && !page.resources.loaded ? page.resources.errorString : ""
        }

        VerticalScrollDecorator { }
    }
}
