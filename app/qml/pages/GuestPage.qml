import QtQuick 2.0
import Sailfish.Silica 1.0
import PveDeck 1.0
import "../components"

// Live status of one VM or container.
Page {
    id: page

    property string profileId
    property string node
    property string type
    property int vmid
    property string name

    readonly property var info: details.status
    readonly property bool running: info.status === "running"
    readonly property bool isContainer: type === "lxc"

    function formatUptime(seconds) {
        var minutes = Math.floor(seconds / 60) % 60
        var hours = Math.floor(seconds / 3600) % 24
        var days = Math.floor(seconds / 86400)
        if (days > 0)
            //% "%1 d %2 h"
            return qsTrId("pvedeck-la-uptime-days").arg(days).arg(hours)
        if (hours > 0)
            //% "%1 h %2 min"
            return qsTrId("pvedeck-la-uptime-hours").arg(hours).arg(minutes)
        //% "%1 min"
        return qsTrId("pvedeck-la-uptime-minutes").arg(minutes)
    }

    GuestStatus { id: guestStatus }

    GuestDetails {
        id: details
        client: sessionManager.client(page.profileId)
        node: page.node
        type: page.type
        vmid: page.vmid
        pollInterval: page.status === PageStatus.Active
                      && Qt.application.state === Qt.ApplicationActive ? 5000 : 0
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                //% "Refresh"
                text: qsTrId("pvedeck-me-refresh")
                onClicked: details.refresh()
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: page.info.name || page.name || String(page.vmid)
                description: (page.isContainer
                              //% "Container %1"
                              ? qsTrId("pvedeck-la-container-id")
                              //% "Virtual machine %1"
                              : qsTrId("pvedeck-la-vm-id")).arg(page.vmid)
                             + " · " + page.node
            }

            Column {
                width: parent.width
                visible: details.loaded

                DetailItem {
                    //% "Status"
                    label: qsTrId("pvedeck-la-status")
                    // qmpstatus tells a paused VM apart from a running one.
                    value: guestStatus.text(page.info.qmpstatus || page.info.status)
                }
                DetailItem {
                    //% "Uptime"
                    label: qsTrId("pvedeck-la-uptime")
                    value: page.formatUptime(page.info.uptime || 0)
                    visible: page.running
                }
                DetailItem {
                    //% "CPU"
                    label: qsTrId("pvedeck-la-cpu")
                    //: %1 is a percentage, %n the number of CPU cores
                    //% "%1 of %n CPU(s)"
                    value: qsTrId("pvedeck-la-cpu-usage", page.info.cpus || 0)
                           .arg(guestStatus.percent(page.info.cpu))
                    visible: page.running
                }
                DetailItem {
                    //% "Memory"
                    label: qsTrId("pvedeck-la-memory")
                    value: page.running
                           //: %1 used, %2 total
                           //% "%1 of %2"
                           ? qsTrId("pvedeck-la-used-of-total")
                             .arg(guestStatus.bytes(page.info.mem))
                             .arg(guestStatus.bytes(page.info.maxmem))
                           : guestStatus.bytes(page.info.maxmem)
                }
                // A VM's disk usage is not known to PVE, only its size.
                DetailItem {
                    //% "Disk"
                    label: qsTrId("pvedeck-la-disk")
                    value: page.isContainer && page.running
                           ? qsTrId("pvedeck-la-used-of-total")
                             .arg(guestStatus.bytes(page.info.disk))
                             .arg(guestStatus.bytes(page.info.maxdisk))
                           : guestStatus.bytes(page.info.maxdisk)
                    visible: (page.info.maxdisk || 0) > 0
                }
                DetailItem {
                    //% "IP addresses"
                    label: qsTrId("pvedeck-la-addresses")
                    value: details.addresses.join("\n")
                    visible: details.addresses.length > 0
                }
                DetailItem {
                    //% "Tags"
                    label: qsTrId("pvedeck-la-tags")
                    value: (page.info.tags || "").split(/[;,\s]+/).join(", ")
                    visible: (page.info.tags || "").length > 0
                }
                DetailItem {
                    //% "Lock"
                    label: qsTrId("pvedeck-la-lock")
                    value: page.info.lock || ""
                    visible: value.length > 0
                }
                DetailItem {
                    //% "High availability"
                    label: qsTrId("pvedeck-la-ha")
                    value: page.info.ha ? page.info.ha.state || "" : ""
                    visible: page.info.ha !== undefined && page.info.ha.managed === 1
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: !details.loaded && details.errorString.length === 0
        }

        ViewPlaceholder {
            enabled: !details.loaded && details.errorString.length > 0
            //% "Could not load the guest"
            text: qsTrId("pvedeck-la-guest-error")
            hintText: details.errorString
        }

        VerticalScrollDecorator { }
    }
}
