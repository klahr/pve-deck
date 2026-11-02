import QtQuick 2.0
import Sailfish.Silica 1.0

// One key in the console's key row. key is an entry of ConsolePage's
// key list: {label, name} for a terminal key, {label, text} to type,
// {label, modifier} for Ctrl/Alt, or {label, action}.
BackgroundItem {
    id: button

    property var key
    // The TerminalItem the keys go to.
    property Item view

    readonly property bool latched: view !== null
                                    && ((key.modifier === "ctrl" && (view.ctrlLatched || view.ctrlLocked))
                                        || (key.modifier === "alt" && (view.altLatched || view.altLocked)))
    readonly property bool locked: view !== null
                                   && ((key.modifier === "ctrl" && view.ctrlLocked)
                                       || (key.modifier === "alt" && view.altLocked))

    height: Theme.itemSizeExtraSmall
    highlighted: down || latched

    Label {
        anchors.centerIn: parent
        text: button.key.label
        font.pixelSize: Theme.fontSizeSmall
        font.underline: button.locked
        font.bold: button.locked
        color: button.highlighted ? Theme.highlightColor : Theme.primaryColor
    }
}
