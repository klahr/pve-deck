import QtQuick 2.0
import Sailfish.Silica 1.0
import PveDeck 1.0
import "../components"

// The guests (VMs and containers) of a logged-in server, per node.
Page {
    id: page

    property string profileId
    readonly property var profile: serverStore.get(profileId)
    readonly property QtObject client: sessionManager.client(profileId)
    readonly property QtObject resources: sessionManager.resources(profileId)

    GuestStatus { id: guestStatus }

    ResourceFilterModel {
        id: filterModel
        resources: page.resources
    }

    // Leave the page, and any guest page above it, if the session ends.
    Connections {
        target: page.client
        onStateChanged: {
            if (page.client.state === PveClient.Disconnected)
                pageStack.pop(pageStack.previousPage(page))
        }
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
                //% "Refresh"
                text: qsTrId("pvedeck-me-refresh")
                onClicked: page.resources.refresh()
            }
        }

        header: Column {
            width: listView.width

            PageHeader {
                title: page.profile.displayName || ""
                description: {
                    var parts = []
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
            property: "node"
            delegate: SectionHeader { text: section }
        }

        delegate: ListItem {
            id: item

            readonly property bool running: model.status === "running"

            contentHeight: Theme.itemSizeMedium

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
                        if (model.tags.length > 0)
                            parts.push(model.tags.join(", "))
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
