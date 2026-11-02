import QtQuick 2.0
import Sailfish.Silica 1.0

// Trust on first use: show the certificate fingerprint and let the user
// pin it. A changed certificate gets a loud warning.
Dialog {
    id: dialog

    property string profileId
    property string fingerprint
    property string subject
    property string issuer
    property var expires
    property bool mismatch
    readonly property var profile: serverStore.get(profileId)

    canAccept: fingerprint.length > 0

    onAccepted: sessionManager.trustCertificate(profileId, fingerprint)
    onRejected: sessionManager.cancelLogin(profileId)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            DialogHeader {
                //% "Trust"
                acceptText: qsTrId("pvedeck-he-trust")
                title: dialog.mismatch
                       //% "Certificate changed"
                       ? qsTrId("pvedeck-he-cert-changed")
                       //% "Unknown certificate"
                       : qsTrId("pvedeck-he-cert-unknown")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: dialog.mismatch ? Theme.errorColor : Theme.highlightColor
                text: dialog.mismatch
                      //% "The certificate of %1 is different from the one you trusted before. This happens when the certificate is renewed, but it can also mean that someone is intercepting the connection. Only continue if you know why it changed."
                      ? qsTrId("pvedeck-la-cert-changed").arg(dialog.profile.host)
                      //% "%1 uses a certificate that this phone cannot verify. Proxmox VE uses a self-signed certificate by default. Compare the fingerprint with the one shown in the Proxmox web UI under Node > System > Certificates."
                      : qsTrId("pvedeck-la-cert-unknown").arg(dialog.profile.host)
            }

            SectionHeader {
                //% "SHA-256 fingerprint"
                text: qsTrId("pvedeck-la-cert-fingerprint")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.WrapAnywhere
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeSmall
                text: dialog.fingerprint
            }

            DetailItem {
                //% "Issued to"
                label: qsTrId("pvedeck-la-cert-subject")
                value: dialog.subject
                visible: dialog.subject.length > 0
            }
            DetailItem {
                //% "Issued by"
                label: qsTrId("pvedeck-la-cert-issuer")
                value: dialog.issuer
                visible: dialog.issuer.length > 0
            }
            DetailItem {
                //% "Expires"
                label: qsTrId("pvedeck-la-cert-expires")
                value: dialog.expires ? Format.formatDate(dialog.expires, Formatter.DateLong) : ""
                visible: value.length > 0
            }
        }

        VerticalScrollDecorator { }
    }
}
