import QtQuick 2.0
import Sailfish.Silica 1.0

// Add or edit a server profile.
Dialog {
    id: dialog

    property string profileId
    readonly property bool editing: profileId.length > 0
    readonly property var profile: editing ? serverStore.get(profileId) : ({})
    readonly property bool tokenAuth: authCombo.currentIndex === 1

    canAccept: hostField.text.trim().length > 0
               && userField.text.trim().length > 0
               && (!tokenAuth || tokenNameField.text.trim().length > 0)
               && (!tokenAuth || editing || tokenSecretField.text.length > 0)

    onAccepted: {
        var data = {
            name: nameField.text,
            host: hostField.text,
            port: parseInt(portField.text, 10) || 8006,
            username: userField.text,
            realm: realmField.text,
            authMethod: tokenAuth ? "token" : "password",
            tokenName: tokenNameField.text,
            rememberSecret: tokenAuth ? true : rememberSwitch.checked
        }
        var id = profileId
        var identityChanged = false
        if (editing) {
            var before = profile
            serverStore.update(id, data)
            var after = serverStore.get(id)
            // A saved secret belongs to the old server and user.
            identityChanged = ["host", "port", "username", "realm", "authMethod", "tokenName"]
                    .some(function(key) { return before[key] !== after[key] })
        } else {
            id = serverStore.add(data)
        }
        if (id.length === 0)
            return
        if (tokenAuth && tokenSecretField.text.length > 0)
            sessionManager.storeSecret(id, tokenSecretField.text)
        else if (identityChanged || (!tokenAuth && !rememberSwitch.checked))
            sessionManager.forgetSecret(id)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            DialogHeader {
                acceptText: dialog.editing
                            //% "Save"
                            ? qsTrId("pvedeck-he-save")
                            //% "Add"
                            : qsTrId("pvedeck-he-add")
                //% "Edit server"
                title: dialog.editing ? qsTrId("pvedeck-he-edit-server")
                                      //% "Add server"
                                      : qsTrId("pvedeck-he-add-server")
            }

            TextField {
                id: nameField
                width: parent.width
                //% "Name"
                label: qsTrId("pvedeck-la-name")
                //% "Optional, e.g. Home lab"
                placeholderText: qsTrId("pvedeck-ph-name")
                text: dialog.profile.name || ""
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: hostField.focus = true
            }

            TextField {
                id: hostField
                width: parent.width
                //% "Host"
                label: qsTrId("pvedeck-la-host")
                placeholderText: "pve.example.lan"
                text: dialog.profile.host || ""
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: portField.focus = true
            }

            TextField {
                id: portField
                width: parent.width
                //% "Port"
                label: qsTrId("pvedeck-la-port")
                text: dialog.profile.port ? String(dialog.profile.port) : "8006"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 65535 }
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: userField.focus = true
            }

            ComboBox {
                id: authCombo
                width: parent.width
                //% "Log in with"
                label: qsTrId("pvedeck-la-auth-method")
                currentIndex: dialog.profile.authMethod === "token" ? 1 : 0
                menu: ContextMenu {
                    //% "Password"
                    MenuItem { text: qsTrId("pvedeck-me-auth-password") }
                    //% "API token"
                    MenuItem { text: qsTrId("pvedeck-me-auth-token") }
                }
            }

            TextField {
                id: userField
                width: parent.width
                //% "User name"
                label: qsTrId("pvedeck-la-username")
                text: dialog.profile.username || "root"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: realmField.focus = true
            }

            TextField {
                id: realmField
                width: parent.width
                //% "Realm"
                label: qsTrId("pvedeck-la-realm")
                //% "pam, pve, or the name of an LDAP/AD realm"
                description: qsTrId("pvedeck-la-realm-description")
                text: dialog.profile.realm || "pam"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            TextSwitch {
                id: rememberSwitch
                visible: !dialog.tokenAuth
                //% "Remember password"
                text: qsTrId("pvedeck-la-remember-password")
                //% "Stored encrypted with Sailfish Secrets. 2FA codes are never stored."
                description: qsTrId("pvedeck-la-remember-password-description")
                checked: dialog.editing ? dialog.profile.rememberSecret : true
            }

            TextField {
                id: tokenNameField
                width: parent.width
                visible: dialog.tokenAuth
                //% "Token ID"
                label: qsTrId("pvedeck-la-token-name")
                //% "The part after the !, e.g. phone"
                description: qsTrId("pvedeck-la-token-name-description")
                text: dialog.profile.tokenName || ""
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }

            PasswordField {
                id: tokenSecretField
                width: parent.width
                visible: dialog.tokenAuth
                //% "Token secret"
                label: qsTrId("pvedeck-la-token-secret")
                placeholderText: dialog.editing
                                 //% "Leave empty to keep the stored secret"
                                 ? qsTrId("pvedeck-ph-token-secret-keep")
                                 : "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
            }
        }

        VerticalScrollDecorator { }
    }
}
