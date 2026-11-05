import QtQuick 2.0
import Sailfish.Silica 1.0
import PveDeck 1.0
import "../components"

// Live status of one VM or container.
Page {
    id: page

    readonly property bool needsSession: true
    property string profileId
    property string node
    property string type
    property int vmid
    property string name

    readonly property var info: details.status
    readonly property bool running: info.status === "running"
    readonly property bool isContainer: type === "lxc"
    readonly property bool busy: powerActions.busyGuests.indexOf(profileId + "/" + vmid) >= 0
    readonly property var actions: busy || !details.loaded
                                   ? []
                                   : powerActions.availableActions(type, info.status || "",
                                                                   info.qmpstatus || "",
                                                                   info.template === 1,
                                                                   info.lock || "")

    function runAction(action) {
        var label = page.info.name || page.name || String(page.vmid)
        if (guestStatus.isDestructive(action)) {
            remorse.execute(guestStatus.remorseText(action), function() {
                powerActions.run(page.profileId, page.node, page.type, page.vmid, action, label)
            })
        } else {
            powerActions.run(page.profileId, page.node, page.type, page.vmid, action, label)
        }
    }

    GuestStatus { id: guestStatus }

    RemorsePopup { id: remorse }

    // Show the new state as soon as the task is done.
    Connections {
        target: powerActions
        onFinished: {
            if (profileId === page.profileId && vmid === page.vmid)
                details.refresh()
        }
    }

    GuestDetails {
        id: details
        client: sessionManager.client(page.profileId)
        node: page.node
        type: page.type
        vmid: page.vmid
        pollInterval: page.status === PageStatus.Active
                      && Qt.application.state === Qt.ApplicationActive ? 5000 : 0
    }

    // Proxmox keeps one sample per minute for the last hour, so polling
    // faster than that adds nothing.
    RrdHistory {
        id: rrdHistory
        client: sessionManager.client(page.profileId)
        node: page.node
        type: page.type
        vmid: page.vmid
        timeframe: ["hour", "day", "week", "month", "year"][timeframeBox.currentIndex]
        pollInterval: page.status === PageStatus.Active
                      && Qt.application.state === Qt.ApplicationActive ? 60000 : 0
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            busy: page.busy

            MenuItem {
                //% "Refresh"
                text: qsTrId("pvedeck-me-refresh")
                onClicked: details.refresh()
            }
            MenuItem {
                //% "Console"
                text: qsTrId("pvedeck-me-console")
                visible: page.isContainer && page.running && !page.busy
                onClicked: pageStack.push(Qt.resolvedUrl("ConsolePage.qml"), {
                                              profileId: page.profileId,
                                              node: page.node,
                                              type: page.type,
                                              vmid: page.vmid
                                          })
            }
            Repeater {
                model: page.actions
                MenuItem {
                    text: guestStatus.actionText(modelData)
                    onClicked: page.runAction(modelData)
                }
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
                    value: page.busy
                           //% "Working…"
                           ? qsTrId("pvedeck-la-working")
                           : guestStatus.text(page.info.qmpstatus || page.info.status)
                }
                DetailItem {
                    //% "Uptime"
                    label: qsTrId("pvedeck-la-uptime")
                    value: guestStatus.uptime(page.info.uptime || 0)
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

            SectionHeader {
                //% "History"
                text: qsTrId("pvedeck-la-history")
                visible: details.loaded
            }

            Column {
                width: parent.width
                spacing: Theme.paddingLarge
                visible: details.loaded

                ComboBox {
                    id: timeframeBox
                    width: parent.width
                    //% "Period"
                    label: qsTrId("pvedeck-la-period")
                    menu: ContextMenu {
                        //% "Hour"
                        MenuItem { text: qsTrId("pvedeck-me-period-hour") }
                        //% "Day"
                        MenuItem { text: qsTrId("pvedeck-me-period-day") }
                        //% "Week"
                        MenuItem { text: qsTrId("pvedeck-me-period-week") }
                        //% "Month"
                        MenuItem { text: qsTrId("pvedeck-me-period-month") }
                        //% "Year"
                        MenuItem { text: qsTrId("pvedeck-me-period-year") }
                    }
                }

                HistoryChart {
                    history: rrdHistory
                    title: qsTrId("pvedeck-la-cpu")
                    keys: ["cpu"]
                    maximum: 1
                    format: function(v) { return guestStatus.percent(v) }
                }
                HistoryChart {
                    history: rrdHistory
                    title: qsTrId("pvedeck-la-memory")
                    keys: ["mem"]
                    referenceKey: "maxmem"
                    bytes: true
                    format: function(v) { return guestStatus.bytes(v) }
                }
                HistoryChart {
                    history: rrdHistory
                    //% "Network"
                    title: qsTrId("pvedeck-la-network")
                    keys: ["netin", "netout"]
                    //% "In"
                    labels: [qsTrId("pvedeck-la-in"),
                             //% "Out"
                             qsTrId("pvedeck-la-out")]
                    bytes: true
                    format: function(v) { return guestStatus.bytesPerSecond(v) }
                }
                HistoryChart {
                    history: rrdHistory
                    //% "Disk I/O"
                    title: qsTrId("pvedeck-la-disk-io")
                    keys: ["diskread", "diskwrite"]
                    //% "Read"
                    labels: [qsTrId("pvedeck-la-read"),
                             //% "Write"
                             qsTrId("pvedeck-la-write")]
                    bytes: true
                    format: function(v) { return guestStatus.bytesPerSecond(v) }
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
