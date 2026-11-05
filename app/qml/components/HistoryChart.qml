import QtQuick 2.0
import Sailfish.Silica 1.0
import PveDeck 1.0

// One chart of an RrdHistory: title with the newest (or touched) values,
// a legend when there are two series, the lines on their own surface, and
// the time range below. Touch and drag to read values at a time.
Column {
    id: chart

    property QtObject history
    property string title
    property var keys: []
    // Legend names, one per key.
    property var labels: []
    property string referenceKey
    property real maximum
    // function(value) returning the value as text.
    property var format: function(value) { return String(value) }

    // Colours from the validated chart palette (blue, orange), stepped for
    // the light or dark ambience, on the palette's own chart surface.
    readonly property bool dark: Theme.colorScheme === Theme.LightOnDark
    readonly property var seriesColors: dark ? ["#3987e5", "#d95926"] : ["#2a78d6", "#eb6834"]
    readonly property color surfaceColor: dark ? "#1a1a19" : "#fcfcfb"
    readonly property color textSecondary: dark ? "#c3c2b7" : "#52514e"
    readonly property color mutedColor: "#898781"

    readonly property int index: line.cursorIndex

    function formatTime(seconds) {
        if (!seconds)
            return ""
        var date = new Date(seconds * 1000)
        var frame = history ? history.timeframe : "hour"
        if (frame === "hour" || frame === "day")
            return Qt.formatTime(date, "hh:mm")
        if (frame === "week")
            return Qt.formatDateTime(date, "ddd hh:mm")
        return Qt.formatDate(date, "d MMM")
    }

    function valueText(key, i) {
        if (!history || history.count === 0)
            return "–"
        var v = i >= 0 ? history.valueAt(key, i) : history.latest(key)
        return isNaN(v) ? "–" : format(v)
    }

    width: parent ? parent.width : 0
    spacing: Theme.paddingSmall

    // Title, and the values at the touched time or the newest ones.
    Item {
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        height: titleLabel.height

        Label {
            id: titleLabel
            text: chart.title
            color: Theme.highlightColor
        }
        Label {
            anchors {
                right: parent.right
                left: titleLabel.right
                leftMargin: Theme.paddingMedium
                baseline: titleLabel.baseline
            }
            horizontalAlignment: Text.AlignRight
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
            text: {
                var parts = []
                for (var i = 0; i < chart.keys.length; ++i) {
                    var value = chart.valueText(chart.keys[i], chart.index)
                    parts.push(chart.keys.length > 1 ? chart.labels[i] + " " + value : value)
                }
                if (chart.referenceKey.length > 0)
                    parts[0] += " / " + chart.valueText(chart.referenceKey, chart.index)
                var text = parts.join(" · ")
                return chart.index >= 0 ? chart.formatTime(chart.history.timeAt(chart.index)) + "  " + text : text
            }
        }
    }

    Rectangle {
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        height: Theme.itemSizeHuge
        radius: Theme.paddingSmall
        color: chart.surfaceColor

        LineChart {
            id: line
            anchors {
                fill: parent
                leftMargin: Theme.paddingMedium
                rightMargin: Theme.paddingMedium
                topMargin: Theme.paddingLarge + Theme.paddingSmall
                bottomMargin: Theme.paddingSmall
            }
            history: chart.history
            keys: chart.keys
            colors: chart.seriesColors.slice(0, chart.keys.length)
            referenceKey: chart.referenceKey
            referenceColor: chart.mutedColor
            gridColor: chart.dark ? "#2c2c2a" : "#e1e0d9"
            surfaceColor: chart.surfaceColor
            maximum: chart.maximum
        }

        // Top of the y-axis; the bottom is always 0.
        Text {
            anchors {
                left: parent.left
                top: parent.top
                leftMargin: Theme.paddingMedium
                topMargin: Theme.paddingSmall / 2
            }
            font.pixelSize: Theme.fontSizeTiny
            color: chart.textSecondary
            text: chart.format(line.yMaximum)
        }

        MouseArea {
            anchors.fill: parent
            onPressed: line.cursorIndex = line.indexAt(mouse.x - line.x)
            onPositionChanged: line.cursorIndex = line.indexAt(mouse.x - line.x)
            onReleased: line.cursorIndex = -1
            onCanceled: line.cursorIndex = -1
        }

        Label {
            anchors.centerIn: parent
            font.pixelSize: Theme.fontSizeExtraSmall
            color: chart.textSecondary
            visible: chart.history && chart.history.loaded && chart.history.count === 0
            //% "No data"
            text: qsTrId("pvedeck-la-no-data")
        }
    }

    // Legend for two series (identity never by colour alone), and the
    // time range.
    Item {
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        height: Math.max(legend.height, startLabel.height)

        Label {
            id: startLabel
            font.pixelSize: Theme.fontSizeTiny
            color: Theme.secondaryColor
            text: chart.history && chart.history.count > 0 ? chart.formatTime(chart.history.timeAt(0)) : ""
        }

        Row {
            id: legend
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingLarge
            visible: chart.keys.length > 1

            Repeater {
                model: chart.keys.length

                Row {
                    spacing: Theme.paddingSmall

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.paddingLarge
                        height: 2
                        color: chart.seriesColors[index]
                    }
                    Label {
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryColor
                        text: chart.labels[index] || ""
                    }
                }
            }
        }

        Label {
            anchors.right: parent.right
            font.pixelSize: Theme.fontSizeTiny
            color: Theme.secondaryColor
            text: chart.history && chart.history.count > 0
                  ? chart.formatTime(chart.history.timeAt(chart.history.count - 1)) : ""
        }
    }
}
