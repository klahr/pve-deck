import QtQuick 2.0
import Sailfish.Silica 1.0

// Shared text and colours for a guest's status.
QtObject {
    function text(status) {
        switch (status) {
        case "running":
            //% "Running"
            return qsTrId("pvedeck-la-status-running")
        case "stopped":
            //% "Stopped"
            return qsTrId("pvedeck-la-status-stopped")
        case "paused":
            //% "Paused"
            return qsTrId("pvedeck-la-status-paused")
        case "suspended":
            //% "Suspended"
            return qsTrId("pvedeck-la-status-suspended")
        case "prelaunch":
            //% "Starting"
            return qsTrId("pvedeck-la-status-prelaunch")
        default:
            return status || "–"
        }
    }

    // Same green as the server list's "logged in" dot.
    function color(status) {
        if (status === "running")
            return "#4caf50"
        if (status === "stopped")
            return "transparent"
        return "#ffb300"
    }

    function bytes(n) {
        var units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
        var i = 0
        n = n || 0
        while (n >= 1024 && i < units.length - 1) {
            n /= 1024
            ++i
        }
        return Number(n).toLocaleString(Qt.locale(), "f", i === 0 || n >= 10 ? 0 : 1) + " " + units[i]
    }

    // Menu text for a PowerActions action.
    function actionText(action) {
        switch (action) {
        case "start":
            //% "Start"
            return qsTrId("pvedeck-me-start")
        case "shutdown":
            //% "Shut down"
            return qsTrId("pvedeck-me-shutdown")
        case "reboot":
            //% "Reboot"
            return qsTrId("pvedeck-me-reboot")
        case "stop":
            //% "Stop"
            return qsTrId("pvedeck-me-stop")
        case "reset":
            //% "Reset"
            return qsTrId("pvedeck-me-reset")
        case "suspend":
            //% "Suspend"
            return qsTrId("pvedeck-me-suspend")
        case "resume":
            //% "Resume"
            return qsTrId("pvedeck-me-resume")
        default:
            return action
        }
    }

    // Actions that lose unsaved state get a remorse timer first.
    function isDestructive(action) {
        return action === "stop" || action === "reset" || action === "reboot"
    }

    function remorseText(action) {
        switch (action) {
        case "stop":
            //% "Stopping"
            return qsTrId("pvedeck-la-remorse-stop")
        case "reset":
            //% "Resetting"
            return qsTrId("pvedeck-la-remorse-reset")
        default:
            //% "Rebooting"
            return qsTrId("pvedeck-la-remorse-reboot")
        }
    }

    // Notice when a task ended well. %1 is the guest's name.
    function doneText(action) {
        switch (action) {
        case "start":
            //% "%1 started"
            return qsTrId("pvedeck-la-done-start")
        case "shutdown":
            //% "%1 shut down"
            return qsTrId("pvedeck-la-done-shutdown")
        case "reboot":
            //% "%1 rebooted"
            return qsTrId("pvedeck-la-done-reboot")
        case "stop":
            //% "%1 stopped"
            return qsTrId("pvedeck-la-done-stop")
        case "reset":
            //% "%1 reset"
            return qsTrId("pvedeck-la-done-reset")
        case "suspend":
            //% "%1 suspended"
            return qsTrId("pvedeck-la-done-suspend")
        case "resume":
            //% "%1 resumed"
            return qsTrId("pvedeck-la-done-resume")
        default:
            return "%1"
        }
    }

    // A tag's colour: from Datacenter > Options > Tag Style when set there
    // (overrides is ResourceModel.tagColors), otherwise computed from the
    // name the same way as the Proxmox web UI (stringToRGB).
    function tagColor(tag, overrides) {
        if (overrides && overrides[tag] !== undefined)
            return overrides[tag]
        var hash = 0
        var s = tag + "prox"
        for (var i = 0; i < s.length; ++i) {
            hash = s.charCodeAt(i) + ((hash << 5) - hash)
            hash = hash & hash
        }
        // Blended with white, as the web UI does.
        var alpha = 0.7
        return Qt.rgba(((hash & 255) * alpha + 255 * (1 - alpha)) / 255,
                       (((hash >> 8) & 255) * alpha + 255 * (1 - alpha)) / 255,
                       (((hash >> 16) & 255) * alpha + 255 * (1 - alpha)) / 255, 1)
    }

    function uptime(seconds) {
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

    function bytesPerSecond(n) {
        //: %1 is an amount of data such as "1.2 MiB"
        //% "%1/s"
        return qsTrId("pvedeck-la-per-second").arg(bytes(n))
    }

    function percent(fraction) {
        return Number((fraction || 0) * 100).toLocaleString(Qt.locale(), "f", 0) + " %"
    }
}
