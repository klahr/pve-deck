import QtQuick 2.0
import Sailfish.Silica 1.0

// Thin horizontal bar for CPU or memory use, value 0..1.
Rectangle {
    property real value

    height: Math.max(2, Math.round(Theme.paddingSmall / 2))
    radius: height / 2
    color: Theme.rgba(Theme.secondaryColor, 0.3)

    Rectangle {
        width: parent.width * Math.max(0, Math.min(1, parent.value))
        height: parent.height
        radius: parent.radius
        color: parent.value > 0.9 ? "#ffb300" : Theme.highlightColor
    }
}
