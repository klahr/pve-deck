import QtQuick 2.0
import Sailfish.Silica 1.0

// Second factor. Offers only the methods the server lists in the
// challenge, starting with the one used last time on this server.
Dialog {
    id: dialog

    property string profileId
    property string message
    readonly property var profile: serverStore.get(profileId)
    readonly property var challenge: sessionManager.tfaChallenge(profileId)
    readonly property var methods: challenge.methods || []
    readonly property string method: methods.length > 0 ? methods[methodCombo.currentIndex] : ""

    function methodName(m) {
        switch (m) {
        //% "Authenticator app (TOTP)"
        case "totp": return qsTrId("pvedeck-me-tfa-totp")
        //% "YubiKey OTP"
        case "yubico": return qsTrId("pvedeck-me-tfa-yubico")
        //% "Recovery key"
        case "recovery": return qsTrId("pvedeck-me-tfa-recovery")
        }
        return m
    }

    canAccept: method.length > 0 && sessionManager.isValidTfaCode(method, codeField.text)

    onAccepted: sessionManager.submitTfa(profileId, method, codeField.text)
    onRejected: sessionManager.cancelLogin(profileId)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            DialogHeader {
                //% "Verify"
                acceptText: qsTrId("pvedeck-he-verify")
                //% "Two-factor authentication"
                title: qsTrId("pvedeck-he-tfa")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: dialog.message.length > 0
                text: dialog.message
                wrapMode: Text.Wrap
                color: Theme.errorColor
            }

            Item { width: 1; height: Theme.paddingMedium; visible: dialog.message.length > 0 }

            // Only WebAuthn/U2F configured: nothing the app can answer.
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: dialog.methods.length === 0
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                //% "None of this account's second factors can be used from the app (only security keys, or all recovery keys used). Add TOTP or new recovery keys in the Proxmox web UI under Datacenter > Permissions > Two Factor."
                text: qsTrId("pvedeck-la-tfa-unsupported")
            }

            ComboBox {
                id: methodCombo
                width: parent.width
                visible: dialog.methods.length > 1
                //% "Method"
                label: qsTrId("pvedeck-la-tfa-method")
                currentIndex: Math.max(0, dialog.methods.indexOf(dialog.challenge.defaultMethod))
                menu: ContextMenu {
                    Repeater {
                        model: dialog.methods
                        MenuItem { text: dialog.methodName(modelData) }
                    }
                }
                onCurrentIndexChanged: codeField.text = ""
            }

            TextField {
                id: codeField
                width: parent.width
                visible: dialog.methods.length > 0
                focus: true
                label: dialog.methodName(dialog.method)
                placeholderText: {
                    switch (dialog.method) {
                    case "totp": return "123456"
                    //% "Touch the key or type the OTP"
                    case "yubico": return qsTrId("pvedeck-ph-tfa-yubico")
                    case "recovery": return "xxxx-xxxx-xxxx-xxxx"
                    }
                    return ""
                }
                inputMethodHints: dialog.method === "totp"
                                  ? Qt.ImhDigitsOnly
                                  : (Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText)
                font.letterSpacing: dialog.method === "totp" ? Theme.paddingSmall : 0
                EnterKey.enabled: dialog.canAccept
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: dialog.accept()
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: dialog.method === "recovery"
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: {
                    var ids = dialog.challenge.recoveryIds || []
                    if (ids.length === 0)
                        //% "Each recovery key works only once."
                        return qsTrId("pvedeck-la-tfa-recovery-hint")
                    // Same numbering as the Proxmox web UI ("ID 0, ID 2").
                    //% "Unused keys: %1. Each key works only once."
                    var text = qsTrId("pvedeck-la-tfa-recovery-ids")
                            .arg(ids.map(function(i) { return "ID " + i }).join(", "))
                    if (ids.length <= 3)
                        //% "Fewer than 4 keys left: generate a new set in the web UI."
                        text += " " + qsTrId("pvedeck-la-tfa-recovery-low")
                    return text
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: dialog.challenge.recoveryExhausted === true
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.errorColor
                //% "All recovery keys have been used. Generate a new set in the web UI."
                text: qsTrId("pvedeck-la-tfa-recovery-exhausted")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: dialog.challenge.webauthn === true && dialog.methods.length > 0
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //% "Security keys (WebAuthn/U2F) are not supported by the app."
                text: qsTrId("pvedeck-la-tfa-webauthn-note")
            }
        }

        VerticalScrollDecorator { }
    }
}
