import QtQuick 2.0
import Sailfish.Silica 1.0
import PveDeck 1.0
import "../components"

// Text console of a container (termproxy), full screen with a key row
// above the keyboard. Tap to type, drag to scroll back, pinch to zoom,
// press and hold to select (double-tap selects a word).
Page {
    id: page

    readonly property bool needsSession: true
    property string profileId
    property string node
    property string type
    property int vmid

    // A keyboard swiped down stays down until the terminal or the key row is tapped.
    property bool keyboardHidden

    function showKeyboard() {
        if (keyboardHidden || status !== PageStatus.Active)
            return
        terminal.forceActiveFocus()
        Qt.inputMethod.show()
    }

    allowedOrientations: Orientation.All

    onStatusChanged: {
        if (status === PageStatus.Active)
            showKeyboard()
    }

    PveConsoleSession {
        id: session
        client: sessionManager.client(page.profileId)
        node: page.node
        type: page.type
        vmid: page.vmid
    }

    Component.onCompleted: session.open()
    Component.onDestruction: session.close()

    Connections {
        target: Qt.inputMethod
        onVisibleChanged: {
            if (Qt.inputMethod.visible)
                page.keyboardHidden = false
            else if (page.status === PageStatus.Active && Qt.application.state === Qt.ApplicationActive)
                keyboardHideTimer.restart()
        }
    }

    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationActive)
                page.showKeyboard()
        }
    }

    // The keyboard also hides briefly while the app or page changes; only
    // a keyboard that stays down was swiped away.
    Timer {
        id: keyboardHideTimer
        interval: 300
        onTriggered: {
            if (page.status === PageStatus.Active && Qt.application.state === Qt.ApplicationActive
                    && !Qt.inputMethod.visible)
                page.keyboardHidden = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    TerminalItem {
        id: terminal
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            bottom: keyBar.top
        }
        backend: session
        fontSize: Theme.fontSizeTiny
        selectionColor: Theme.rgba(Theme.highlightBackgroundColor, 0.5)
        focus: true

        PinchArea {
            anchors.fill: parent

            property real startSize

            onPinchStarted: startSize = terminal.fontSize
            onPinchUpdated: terminal.fontSize = startSize * pinch.scale

            MouseArea {
                anchors.fill: parent

                property real startY
                property int startOffset
                property bool dragged
                property bool selecting

                // Keep the page swipe from taking over a selection or a scroll.
                preventStealing: selecting || dragged

                onPressed: {
                    startY = mouse.y
                    startOffset = terminal.scrollOffset
                    dragged = false
                }
                onPressAndHold: {
                    if (dragged)
                        return
                    selecting = true
                    terminal.startSelection(mouse.x, mouse.y)
                }
                onPositionChanged: {
                    if (selecting) {
                        terminal.updateSelection(mouse.x, mouse.y)
                        return
                    }
                    // A tap that wobbles a little should not scroll.
                    if (!dragged) {
                        if (Math.abs(mouse.y - startY) < Theme.startDragDistance)
                            return
                        dragged = true
                        startY = mouse.y
                    }
                    terminal.scrollOffset = startOffset + Math.round((mouse.y - startY) / terminal.cellHeight)
                }
                onDoubleClicked: terminal.selectWordAt(mouse.x, mouse.y)
                onReleased: selecting = false
                onCanceled: selecting = false
                onClicked: {
                    if (dragged)
                        return
                    if (terminal.hasSelection) {
                        terminal.clearSelection()
                        return
                    }
                    page.keyboardHidden = false
                    page.showKeyboard()
                }
            }
        }

        // What to do with the selected text.
        Rectangle {
            anchors {
                top: parent.top
                right: parent.right
                margins: Theme.paddingSmall
            }
            width: selectionActions.width
            height: selectionActions.height
            radius: Theme.paddingSmall
            color: Theme.rgba(Theme.highlightDimmerColor, 0.9)
            visible: terminal.hasSelection

            Row {
                id: selectionActions

                // Re-read whenever the selection changes.
                readonly property string url: terminal.hasSelection ? terminal.selectedUrl() : ""

                Repeater {
                    model: {
                        var actions = []
                        if (selectionActions.url.length > 0)
                            //% "Open"
                            actions.push({ label: qsTrId("pvedeck-bt-open"), action: "open" })
                        //% "Copy"
                        actions.push({ label: qsTrId("pvedeck-bt-copy"), action: "copy" })
                        //% "Paste"
                        actions.push({ label: qsTrId("pvedeck-bt-paste"), action: "paste" })
                        //% "Clear"
                        actions.push({ label: qsTrId("pvedeck-bt-clear"), action: "clear" })
                        return actions
                    }

                    BackgroundItem {
                        width: actionLabel.width + 2 * Theme.paddingLarge
                        height: Theme.itemSizeExtraSmall
                        onClicked: {
                            if (modelData.action === "open") {
                                Qt.openUrlExternally(selectionActions.url)
                                terminal.clearSelection()
                            } else if (modelData.action === "copy") {
                                Clipboard.text = terminal.selectedText()
                                terminal.clearSelection()
                            } else if (modelData.action === "paste") {
                                terminal.paste(Clipboard.text)
                            } else {
                                terminal.clearSelection()
                            }
                        }

                        Label {
                            id: actionLabel
                            anchors.centerIn: parent
                            text: modelData.label
                            color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                    }
                }
            }
        }

        // Newer output is below: tap to go back to it.
        Rectangle {
            anchors {
                right: parent.right
                bottom: parent.bottom
                margins: Theme.paddingMedium
            }
            width: Theme.itemSizeSmall
            height: width
            radius: width / 2
            color: Theme.rgba(Theme.highlightDimmerColor, 0.9)
            visible: terminal.scrollOffset > 0 && !terminal.hasSelection

            Label {
                anchors.centerIn: parent
                text: "↓"
                color: Theme.highlightColor
            }
            MouseArea {
                anchors.fill: parent
                onClicked: terminal.scrollOffset = 0
            }
        }
    }

    Item {
        id: keyBar

        // Only what a shell needs and the on-screen keyboard lacks or hides.
        readonly property var keys: [
            { label: "Esc", name: "Escape" },
            { label: "Tab", name: "Tab" },
            { label: "Ctrl", modifier: "ctrl" },
            { label: "Alt", modifier: "alt" },
            { label: "←", name: "Left" },
            { label: "↑", name: "Up" },
            { label: "↓", name: "Down" },
            { label: "→", name: "Right" },
            { label: "|", text: "|" },
            { label: "~", text: "~" },
            { label: "/", text: "/" },
            { label: "-", text: "-" },
            //% "Paste"
            { label: qsTrId("pvedeck-bt-paste"), action: "paste" }
        ]
        // Keys share the width while they fit; beyond that the row scrolls.
        readonly property real keyWidth: Math.max(width / keys.length, Theme.itemSizeExtraSmall * 0.75)

        // A second tap on a latched modifier soon after the first locks it.
        property var lastModifierTap: ({})

        function pressModifier(name) {
            var now = Date.now()
            var quick = lastModifierTap[name] !== undefined && now - lastModifierTap[name] < 400
            lastModifierTap[name] = now
            var locked = name === "ctrl" ? terminal.ctrlLocked : terminal.altLocked
            var latched = name === "ctrl" ? terminal.ctrlLatched : terminal.altLatched
            if (locked) {
                locked = false
                latched = false
            } else if (latched && quick) {
                locked = true
                latched = false
            } else {
                latched = !latched
            }
            if (name === "ctrl") {
                terminal.ctrlLocked = locked
                terminal.ctrlLatched = latched
            } else {
                terminal.altLocked = locked
                terminal.altLatched = latched
            }
        }

        function press(key) {
            if (key.modifier !== undefined)
                pressModifier(key.modifier)
            else if (key.action === "paste")
                terminal.paste(Clipboard.text)
            else if (key.name !== undefined)
                terminal.sendKey(key.name)
            else
                terminal.sendText(key.text)
        }

        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: Theme.itemSizeExtraSmall

        Rectangle {
            anchors.fill: parent
            color: Theme.rgba(Theme.highlightDimmerColor, 0.9)
        }

        Flickable {
            anchors.fill: parent
            clip: true
            contentWidth: keyRow.width
            flickableDirection: Flickable.HorizontalFlick
            interactive: contentWidth > width
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: keyRow

                Repeater {
                    model: keyBar.keys

                    TerminalKey {
                        width: keyBar.keyWidth
                        key: modelData
                        view: terminal
                        enabled: modelData.action !== "paste" || Clipboard.hasText
                        onClicked: keyBar.press(modelData)
                    }
                }
            }
        }

        // With the keyboard swiped away, the first tap brings it back.
        MouseArea {
            anchors.fill: parent
            enabled: page.keyboardHidden
            onClicked: {
                page.keyboardHidden = false
                page.showKeyboard()
            }
        }
    }

    // Connecting, or the console ended.
    Rectangle {
        anchors.fill: terminal
        color: Qt.rgba(0, 0, 0, 0.7)
        visible: session.state !== TerminalBackend.Connected

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: session.state === TerminalBackend.Connecting
                     || session.state === TerminalBackend.Idle
        }

        Column {
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.horizontalPageMargin
            spacing: Theme.paddingLarge
            visible: session.state === TerminalBackend.Closed

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
                text: session.errorString.length > 0
                      //% "Could not open the console"
                      ? qsTrId("pvedeck-la-console-failed")
                      //% "The console has ended"
                      : qsTrId("pvedeck-la-console-ended")
            }
            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                text: session.errorString
                visible: text.length > 0
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                //% "Reconnect"
                text: qsTrId("pvedeck-bt-reconnect")
                onClicked: session.open()
            }
        }
    }
}
