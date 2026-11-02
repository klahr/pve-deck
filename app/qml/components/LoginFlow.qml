import QtQuick 2.0
import Sailfish.Silica 1.0

// Reacts to SessionManager login signals by opening the right dialog.
// Pushes are queued until the page stack is idle, because a signal can
// arrive while a dialog is still closing.
Item {
    id: flow

    property var queue: []

    function pushWhenIdle(page, properties) {
        queue.push({ page: page, properties: properties })
        drain()
    }

    function drain() {
        if (pageStack.busy || queue.length === 0)
            return
        var next = queue.shift()
        pageStack.push(Qt.resolvedUrl(next.page), next.properties)
        // The push makes the stack busy; continue when it settles.
    }

    Connections {
        target: pageStack
        onBusyChanged: if (!pageStack.busy) flow.drain()
    }

    Connections {
        target: sessionManager

        onSecretRequired: {
            flow.pushWhenIdle("../pages/LoginDialog.qml",
                              { profileId: profileId, message: message })
        }

        onTfaRequired: {
            flow.pushWhenIdle("../pages/TfaDialog.qml", { profileId: profileId })
        }

        onTfaFailed: {
            flow.pushWhenIdle("../pages/TfaDialog.qml",
                              { profileId: profileId, message: message })
        }

        onCertificateUntrusted: {
            flow.pushWhenIdle("../pages/CertificateDialog.qml", {
                                  profileId: profileId,
                                  fingerprint: fingerprint,
                                  subject: subject,
                                  issuer: issuer,
                                  expires: expires,
                                  mismatch: mismatch
                              })
        }

        onConnected: {
            flow.pushWhenIdle("../pages/SessionPage.qml", { profileId: profileId })
        }

        onLoginFailed: Notices.show(message, Notice.Long, Notice.Center)

        onSessionExpired: {
            //% "Session ended: %1"
            Notices.show(qsTrId("pvedeck-la-session-expired").arg(message),
                         Notice.Long, Notice.Center)
        }
    }
}
