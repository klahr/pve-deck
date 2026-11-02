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

    function percent(fraction) {
        return Number((fraction || 0) * 100).toLocaleString(Qt.locale(), "f", 0) + " %"
    }
}
