import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"
import "components"

ApplicationWindow {
    id: appWindow

    initialPage: Component { ServerListPage { } }
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    LoginFlow { }

    // Guest lists refresh every 5 s while the app is in front; in the
    // background only the cover needs them, once a minute.
    Binding {
        target: sessionManager
        property: "pollInterval"
        value: Qt.application.state === Qt.ApplicationActive ? 5000 : 60000
    }
}
