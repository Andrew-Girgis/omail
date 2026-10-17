import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var window
    property alias currentSection: tabs.currentIndex
    function showSection(section) {
        const index = ["accounts", "appearance", "ai", "advanced"].indexOf(section);
        if (index >= 0) {
            tabs.currentIndex = index;
            scroll.contentItem.contentY = 0;
        }
        open();
    }
    objectName: "settingsDialog"
    anchors.centerIn: parent
    width: Math.min(820, window.width - 24)
    height: Math.min(810, window.height - 32)
    title: qsTr("Settings")
    modal: true
    standardButtons: Dialog.Close
    Material.roundedScale: theme.radius
    Material.elevation: 0
    background: Rectangle {
        color: theme.panel
        radius: theme.radius
        border.width: 1
        border.color: theme.border
    }
    contentItem: ColumnLayout {
        spacing: 12
        TabBar {
            id: tabs
            objectName: "settingsTabs"
            Layout.fillWidth: true
            TabButton {
                text: qsTr("Accounts")
            }
            TabButton {
                text: qsTr("Appearance")
            }
            TabButton {
                text: qsTr("Local AI")
            }
            TabButton {
                text: qsTr("Advanced")
            }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 18
                ColumnLayout {
                    visible: tabs.currentIndex === 0
                    Layout.fillWidth: true
                    spacing: 14
                    OLabel {
                        text: qsTr("Connect your accounts")
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                        Layout.topMargin: 8
                    }
                    OLabel {
                        text: qsTr("Choose a profile, then sign in with Microsoft or Google to connect mail and calendars.")
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                        color: theme.muted
                    }
                    OComboBox {
                        id: connectProfile
                        emptyText: qsTr("No profiles available")
                        model: backend.data.profiles
                        textRole: "name"
                        valueRole: "id"
                        Layout.fillWidth: true
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 8
                        OButton {
                            text: qsTr("Connect Microsoft")
                            glyph: "plus"
                            primary: true
                            enabled: connectProfile.count > 0 && !!backend.settings.microsoftClientId
                            onClicked: backend.connectAccount("microsoft", connectProfile.currentValue)
                        }
                        OButton {
                            text: qsTr("Connect Google")
                            glyph: "plus"
                            primary: true
                            enabled: connectProfile.count > 0 && !!backend.settings.googleClientId
                            onClicked: backend.connectAccount("google", connectProfile.currentValue)
                        }
                    }
                    OLabel {
                        visible: !backend.settings.microsoftClientId || !backend.settings.googleClientId
                        text: qsTr("This preview needs a one-time provider setup before sign-in. Open Provider setup below for instructions.")
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        color: theme.muted
                        Layout.fillWidth: true
                    }
                    OButton {
                        id: providerSetupToggle
                        text: qsTr("Provider setup")
                        glyph: "settings"
                        checkable: true
                        checked: false
                    }
                    ColumnLayout {
                        visible: providerSetupToggle.checked
                        Layout.fillWidth: true
                        spacing: 10
                        OButton {
                            text: qsTr("Open registration guide")
                            glyph: "external"
                            onClicked: backend.openGuide()
                        }
                        OField {
                            Layout.fillWidth: true
                            placeholderText: qsTr("Microsoft Application (client) ID")
                            text: backend.settings.microsoftClientId
                            onEditingFinished: backend.setSetting("microsoftClientId", text.trim())
                        }
                        OButton {
                            text: qsTr("Import Google OAuth JSON")
                            glyph: "folder"
                            onClicked: backend.importGoogleClient()
                        }
                        OLabel {
                            text: backend.settings.googleClientId ? qsTr("Google configured") : qsTr("Google not configured")
                            color: theme.muted
                        }
                    }
                    OLabel {
                        text: qsTr("Work or university accounts may require your administrator’s consent.")
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        color: theme.muted
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: theme.border
                        Layout.topMargin: 8
                    }
                    Repeater {
                        model: backend.data.accounts
                        ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            OLabel {
                                text: modelData.email
                                Layout.fillWidth: true
                                font.weight: Font.DemiBold
                            }
                            OLabel {
                                text: modelData.sync_error || (modelData.last_sync ? qsTr("Synced %1").arg(window.dateText(modelData.last_sync)) : qsTr("Waiting for first sync"))
                                color: modelData.sync_error ? theme.accent : theme.muted
                                wrapMode: Text.Wrap
                                elide: Text.ElideNone
                                Layout.fillWidth: true
                                font.pixelSize: 12
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                OComboBox {
                                    model: backend.data.profiles
                                    textRole: "name"
                                    valueRole: "id"
                                    Layout.fillWidth: true
                                    currentIndex: backend.data.profiles.findIndex((p) => {
                                        return p.id === modelData.profile_id;
                                    })
                                    onActivated: backend.assignAccount(modelData.id, currentValue)
                                }
                                OButton {
                                    text: qsTr("Reconnect")
                                    onClicked: backend.connectAccount(modelData.provider, modelData.profile_id)
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: theme.border
                            }
                        }
                    }
                    OLabel {
                        text: qsTr("Profiles")
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        Layout.topMargin: 8
                    }
                    Repeater {
                        model: backend.data.profiles
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            OField {
                                id: profileName
                                Layout.fillWidth: true
                                text: modelData.name
                                placeholderText: qsTr("Profile name")
                            }
                            OField {
                                id: profileColor
                                Layout.preferredWidth: 110
                                text: modelData.color
                                placeholderText: qsTr("Colour")
                            }
                            OButton {
                                text: qsTr("Save")
                                onClicked: backend.saveProfile(modelData.id, profileName.text, profileColor.text)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        OField {
                            id: newProfileName
                            placeholderText: qsTr("New profile name")
                            Layout.fillWidth: true
                        }
                        OButton {
                            text: qsTr("Add profile")
                            onClicked: {
                                backend.saveProfile("", newProfileName.text, theme.accent);
                                newProfileName.text = "";
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: tabs.currentIndex === 1
                    Layout.fillWidth: true
                    spacing: 16
                    OLabel {
                        text: qsTr("Made for your desktop")
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                        Layout.topMargin: 8
                    }
                    OLabel {
                        text: qsTr("Colours, corners, and language follow Omarchy live.")
                        color: theme.muted
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        OLabel {
                            text: qsTr("Text size")
                            Layout.fillWidth: true
                        }
                        SpinBox {
                            from: 11
                            to: 28
                            value: backend.settings.fontSize
                            onValueModified: backend.setSetting("fontSize", value)
                        }
                    }
                    OField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Font family · empty uses the desktop font")
                        text: backend.settings.fontFamily
                        onEditingFinished: backend.setSetting("fontFamily", text)
                    }
                    RowLayout {
                        OLabel {
                            text: qsTr("Density")
                            Layout.fillWidth: true
                        }
                        OComboBox {
                            model: [qsTr("Comfortable"), qsTr("Compact")]
                            currentIndex: backend.settings.density === "compact" ? 1 : 0
                            onActivated: backend.setSetting("density", currentIndex ? "compact" : "comfortable")
                        }
                    }
                    RowLayout {
                        OLabel {
                            text: qsTr("Sidebar width")
                            Layout.fillWidth: true
                        }
                        OComboBox {
                            property var widths: [180, 224, 300]
                            property int presetIndex: widths.indexOf(backend.settings.sidebarWidth)
                            objectName: "sidebarWidthPreset"
                            model: presetIndex < 0 ? [qsTr("Thin"), qsTr("Medium"), qsTr("Wide"), qsTr("Custom")] : [qsTr("Thin"), qsTr("Medium"), qsTr("Wide")]
                            currentIndex: {
                                model;
                                return presetIndex < 0 ? 3 : presetIndex;
                            }
                            onActivated: {
                                if (currentIndex < widths.length)
                                    backend.setSetting("sidebarWidth", widths[currentIndex]);
                            }
                        }
                    }
                    RowLayout {
                        OLabel {
                            text: qsTr("Message list width")
                            Layout.fillWidth: true
                        }
                        OComboBox {
                            property var widths: [280, 360, 480]
                            property int presetIndex: widths.indexOf(backend.settings.listWidth)
                            objectName: "listWidthPreset"
                            model: presetIndex < 0 ? [qsTr("Thin"), qsTr("Medium"), qsTr("Wide"), qsTr("Custom")] : [qsTr("Thin"), qsTr("Medium"), qsTr("Wide")]
                            currentIndex: {
                                model;
                                return presetIndex < 0 ? 3 : presetIndex;
                            }
                            onActivated: {
                                if (currentIndex < widths.length)
                                    backend.setSetting("listWidth", widths[currentIndex]);
                            }
                        }
                    }
                    OLabel {
                        Layout.fillWidth: true
                        text: qsTr("Drag the handles beside mail views in the sidebar to reorder them. Exact pane widths can be set in your config or by your agent.")
                        color: theme.muted
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                    Switch {
                        text: qsTr("Show topic views")
                        checked: backend.settings.showTopics
                        onToggled: backend.setSetting("showTopics", checked)
                    }
                    OLabel {
                        text: qsTr("Keyboard shortcuts")
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        Layout.topMargin: 16
                    }
                    Repeater {
                        model: [{
                            "key": "shortcutCompose",
                            "name": qsTr("Compose")
                        }, {
                            "key": "shortcutSearch",
                            "name": qsTr("Search")
                        }, {
                            "key": "shortcutSync",
                            "name": qsTr("Sync")
                        }, {
                            "key": "shortcutMail",
                            "name": qsTr("Mail")
                        }, {
                            "key": "shortcutCalendar",
                            "name": qsTr("Calendar")
                        }, {
                            "key": "shortcutAi",
                            "name": qsTr("AI")
                        }]
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            OLabel {
                                text: modelData.name
                                Layout.fillWidth: true
                            }
                            OField {
                                Layout.preferredWidth: 160
                                text: backend.settings[modelData.key]
                                onEditingFinished: backend.setSetting(modelData.key, text)
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: tabs.currentIndex === 2
                    Layout.fillWidth: true
                    spacing: 16
                    OLabel {
                        text: qsTr("Intelligence, kept local")
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                        Layout.topMargin: 8
                    }
                    OLabel {
                        text: qsTr("Ollama handles classification, summaries, suggested replies, and chat. Omail verifies that the chosen model is downloaded locally before sharing message text.")
                        color: theme.muted
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                    }
                    Switch {
                        text: qsTr("Enable local AI")
                        checked: backend.settings.aiEnabled
                        onToggled: backend.setSetting("aiEnabled", checked)
                    }
                    OField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Local Ollama URL")
                        text: backend.settings.ollamaUrl
                        onEditingFinished: backend.setSetting("ollamaUrl", text)
                    }
                    OField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Installed model name")
                        text: backend.settings.ollamaModel
                        onEditingFinished: backend.setSetting("ollamaModel", text)
                    }
                    OButton {
                        text: qsTr("Check installed models")
                        glyph: "sync"
                        onClicked: backend.checkModels()
                    }
                    OLabel {
                        text: qsTr("Install the default model in a terminal:")
                        color: theme.muted
                        Layout.fillWidth: true
                    }
                    TextEdit {
                        text: "ollama pull qwen3.5:4b"
                        readOnly: true
                        selectByMouse: true
                        color: theme.foreground
                        font.family: "monospace"
                        font.pixelSize: 13
                    }
                    OLabel {
                        text: qsTr("Automatic classification covers newly received mail. Older cached messages can be classified from the conversation menu. Your corrections affect only the selected message.")
                        color: theme.muted
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                    }
                    OLabel {
                        text: qsTr("Cloud adapters are not enabled in v1. No profile sends its email to a cloud AI provider.")
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    visible: tabs.currentIndex === 3
                    Layout.fillWidth: true
                    spacing: 16
                    OLabel {
                        text: qsTr("Sync & agent access")
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                        Layout.topMargin: 8
                    }
                    RowLayout {
                        OLabel {
                            text: qsTr("Sync interval (minutes)")
                            Layout.fillWidth: true
                        }
                        SpinBox {
                            from: 1
                            to: 60
                            value: backend.settings.syncMinutes
                            onValueModified: backend.setSetting("syncMinutes", value)
                        }
                    }
                    RowLayout {
                        OLabel {
                            text: qsTr("Mail history (days)")
                            Layout.fillWidth: true
                        }
                        SpinBox {
                            from: 1
                            to: 3650
                            value: backend.settings.cacheDays
                            onValueModified: backend.setSetting("cacheDays", value)
                        }
                    }
                    Switch {
                        text: qsTr("Notify about new Priority mail")
                        checked: backend.settings.notifications
                        onToggled: backend.setSetting("notifications", checked)
                    }
                    OLabel {
                        text: qsTr("Older mail")
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        Layout.topMargin: 12
                    }
                    OLabel {
                        text: qsTr("Search the provider explicitly to fetch older messages. Google accepts Gmail search syntax; Microsoft searches mail text. Results appear in All mail.")
                        color: theme.muted
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                    }
                    OComboBox {
                        id: olderAccount
                        emptyText: qsTr("No accounts connected")
                        model: backend.data.accounts
                        textRole: "email"
                        valueRole: "id"
                        Layout.fillWidth: true
                    }
                    OField {
                        id: olderQuery
                        placeholderText: qsTr("Provider search query")
                        Layout.fillWidth: true
                    }
                    OButton {
                        text: qsTr("Fetch matching mail")
                        enabled: !backend.busy && olderQuery.text.length > 0 && backend.data.accounts.length > 0
                        onClicked: backend.fetchOlder(olderAccount.currentValue, olderQuery.text)
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: theme.border
                        Layout.topMargin: 8
                    }
                    OLabel {
                        text: qsTr("Agent interface")
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    OLabel {
                        text: qsTr("Mail and calendar commands are read-only. Configuration commands can change settings. Run omail help --json for the command contract and omail skill install for the agent documentation.")
                        color: theme.muted
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                    }
                    TextEdit {
                        text: "omail mail list --profile personal --view priority --json\nomail calendar events --profile work --json\nomail config set fontSize 16 --json"
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        color: theme.foreground
                        font.family: "monospace"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    OLabel {
                        text: qsTr("Scheduled mail runs in the login service. If the desktop is asleep, offline, or the service is stopped when a message is due, the message is held for review.")
                        color: theme.muted
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                    }
                }
                Item {
                    height: 16
                }
            }
        }
    }
}
