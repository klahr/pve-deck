import QtQuick 2.0
import Sailfish.Silica 1.0

// Asks for the password (or API token secret) of a server.
Dialog {
    id: dialog

    property string profileId
    property string message
    readonly property var profile: serverStore.get(profileId)
    readonly property bool tokenAuth: profile.authMethod === "token"

    canAccept: secretField.text.length > 0

    onAccepted: sessionManager.login(profileId, secretField.text,
                                     tokenAuth ? true : rememberSwitch.checked)
    onRejected: sessionManager.cancelLogin(profileId)

    Column {
        width: parent.width

        DialogHeader {
            //% "Log in"
            acceptText: qsTrId("pvedeck-he-login")
            title: dialog.profile.displayName || ""
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            visible: dialog.message.length > 0
            text: dialog.message
            wrapMode: Text.Wrap
            color: Theme.errorColor
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            text: (dialog.tokenAuth ? dialog.profile.userId + "!" + dialog.profile.tokenName
                                    : dialog.profile.userId)
                  + " · " + dialog.profile.host
            wrapMode: Text.Wrap
            color: Theme.secondaryHighlightColor
        }

        Item { width: 1; height: Theme.paddingLarge }

        PasswordField {
            id: secretField
            width: parent.width
            focus: true
            label: dialog.tokenAuth
                   //% "Token secret"
                   ? qsTrId("pvedeck-la-token-secret")
                   //% "Password"
                   : qsTrId("pvedeck-la-password")
            EnterKey.enabled: dialog.canAccept
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }

        TextSwitch {
            id: rememberSwitch
            visible: !dialog.tokenAuth
            //% "Remember password"
            text: qsTrId("pvedeck-la-remember-password")
            checked: dialog.profile.rememberSecret === true
        }
    }
}
