import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"
import "components"

ApplicationWindow {
    id: appWindow

    initialPage: Component { ServerListPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    // Set by the cover: true while it is shown on the home screen.
    property bool coverActive

    LoginFlow { }

    GuestStatus { id: guestStatus }

    // When a server is logged out or its session ends, close its pages
    // (those with needsSession: true) by going back to the page below them.
    Connections {
        target: sessionManager
        onConnectedIdsChanged: leaveSessionTimer.restart()
    }

    Timer {
        id: leaveSessionTimer
        interval: 50
        onTriggered: {
            if (pageStack.busy) {
                restart()
                return
            }
            var lowest = null
            for (var p = pageStack.currentPage; p; p = pageStack.previousPage(p)) {
                if (p.needsSession === true && sessionManager.connectedIds.indexOf(p.profileId) < 0)
                    lowest = p
            }
            if (lowest)
                pageStack.pop(pageStack.previousPage(lowest))
        }
    }

    Connections {
        target: powerActions
        onFinished: {
            if (ok) {
                Notices.show(guestStatus.doneText(action).arg(label), Notice.Short, Notice.Center)
            } else {
                //: %1 is an action such as "Shut down", %2 the guest's name, %3 the error
                //% "%1 %2 failed: %3"
                Notices.show(qsTrId("pvedeck-la-action-failed")
                             .arg(guestStatus.actionText(action)).arg(label).arg(message),
                             Notice.Long, Notice.Center)
            }
        }
    }

    // Guest lists refresh every 5 s while the app is in front, once a
    // minute while only the cover shows them, and not at all otherwise
    // (screen off, cover out of view).
    Binding {
        target: sessionManager
        property: "pollInterval"
        value: Qt.application.state === Qt.ApplicationActive ? 5000
                                                             : appWindow.coverActive ? 60000 : 0
    }
}
