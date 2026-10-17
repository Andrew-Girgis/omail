import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: appWindow
    objectName: "omailWindow"
    visible: true
    width: initialWidth
    height: initialHeight
    minimumWidth: 380
    minimumHeight: 500
    title: backend.demo ? qsTr("Omail · Preview") : "Omail"
    color: theme.background
    font.pixelSize: backend.settings.fontSize
    font.family: backend.settings.fontFamily || Qt.application.font.family
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.foreground: theme.foreground
    Material.background: theme.panel
    Material.roundedScale: theme.radius
    property string tab: "mail"
    property bool narrow: width < Math.max(760, backend.settings.listWidth + 360)
    property bool wide: width >= Math.max(1160, backend.settings.sidebarWidth + backend.settings.listWidth + 480)
    property bool detail: false
    readonly property bool typing: activeFocusItem && (activeFocusItem.hasOwnProperty("cursorPosition") || activeFocusItem.hasOwnProperty("inputMethodComposing"))
    function showSettings(section) {
        settingsDialog.showSection(section || "accounts");
    }
    function compose() {
        backend.newDraft();
    }
    function showConfirm(title, text, action) {
        confirmDialog.title = title;
        confirmBody.text = text;
        confirmDialog.action = action;
        confirmDialog.open();
    }
    function dateText(value, format) {
        translator.language;
        return new Date(value).toLocaleString(Qt.locale(), format || "ddd, MMM d · h:mm AP");
    }
    onClosing: function (close) {
        if (composeDialog.visible || eventDialog.visible) {
            close.accepted = false;
            showConfirm(qsTr("Close Omail?"), qsTr("Save your changes before closing, or discard them and close the window."), function () {
                composeDialog.close();
                eventDialog.close();
                Qt.quit();
            });
        }
    }
    Connections {
        target: backend
        function onSourceOpened(tab) {
            appWindow.tab = tab;
            appWindow.detail = true;
        }
        function onDraftReady(draft) {
            if (composeDialog.visible && composeDialog.draft.id !== draft.id) {
                appWindow.showConfirm(qsTr("Replace the open draft?"), qsTr("Save your current draft first, or discard its unsaved changes."), function () {
                    composeDialog.load(draft);
                });
            } else
                composeDialog.load(draft);
        }
        function onDraftSaved() {
            composeDialog.close();
        }
        function onEventReady(event) {
            eventDialog.load(event);
        }
        function onEventSaved() {
            eventDialog.close();
        }
    }
    Shortcut {
        sequence: backend.settings.shortcutCompose
        context: Qt.ApplicationShortcut
        onActivated: compose()
    }
    Shortcut {
        sequence: backend.settings.shortcutSearch
        context: Qt.ApplicationShortcut
        onActivated: {
            tab = "mail";
            mailPage.focusSearch();
        }
    }
    Shortcut {
        sequence: backend.settings.shortcutSync
        context: Qt.ApplicationShortcut
        onActivated: backend.sync()
    }
    Shortcut {
        sequence: backend.settings.shortcutMail
        context: Qt.ApplicationShortcut
        onActivated: tab = "mail"
    }
    Shortcut {
        sequence: backend.settings.shortcutCalendar
        context: Qt.ApplicationShortcut
        onActivated: tab = "calendar"
    }
    Shortcut {
        sequence: backend.settings.shortcutAi
        context: Qt.ApplicationShortcut
        onActivated: tab = "ai"
    }
    Shortcut {
        sequence: "?"
        enabled: !typing
        context: Qt.ApplicationShortcut
        onActivated: helpDialog.open()
    }
    Shortcut {
        sequence: "Q"
        enabled: !typing
        context: Qt.ApplicationShortcut
        onActivated: appWindow.close()
    }
    Shortcut {
        sequence: "Space"
        enabled: !typing && tab === "mail" && !composeDialog.visible && !eventDialog.visible && !settingsDialog.visible
        context: Qt.ApplicationShortcut
        onActivated: {
            if (backend.data.selectedThread)
                backend.threadAction("read");
            else if (backend.data.threads.length) {
                const t = backend.data.threads[0];
                backend.selectThread(t.account_id, t.thread_id);
                detail = true;
            }
        }
    }
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: !typing
        context: Qt.ApplicationShortcut
        onActivated: backend.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo]
        enabled: !typing
        context: Qt.ApplicationShortcut
        onActivated: backend.redo()
    }
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                color: theme.background
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 10
                    OButton {
                        visible: !appWindow.wide
                        glyph: "menu"
                        tip: qsTr("Profiles and views")
                        onClicked: sidebarDrawer.open()
                    }
                    OIcon {
                        name: "mail"
                        visible: !appWindow.narrow
                        tint: theme.accent
                        Layout.preferredWidth: 25
                        Layout.preferredHeight: 25
                    }
                    OLabel {
                        text: "omail"
                        font.pixelSize: 21
                        font.weight: Font.DemiBold
                        font.letterSpacing: -.5
                        Layout.rightMargin: appWindow.narrow ? 0 : 44
                    }
                    Repeater {
                        model: [
                            {
                                id: "mail",
                                text: qsTr("Mail"),
                                icon: "mail"
                            },
                            {
                                id: "calendar",
                                text: qsTr("Calendar"),
                                icon: "calendar"
                            },
                            {
                                id: "ai",
                                text: qsTr("AI"),
                                icon: "ai"
                            }
                        ]
                        OButton {
                            required property var modelData
                            text: appWindow.narrow ? "" : modelData.text
                            glyph: modelData.icon
                            tip: modelData.text
                            selected: appWindow.tab === modelData.id
                            onClicked: appWindow.tab = modelData.id
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    OLabel {
                        visible: !appWindow.narrow && backend.demo
                        text: qsTr("PREVIEW")
                        font.pixelSize: 10
                        font.letterSpacing: 1.5
                        color: theme.muted
                    }
                    OButton {
                        glyph: "sync"
                        tip: qsTr("Sync mail and calendars")
                        enabled: !backend.busy
                        onClicked: backend.sync()
                    }
                    OButton {
                        objectName: "settingsButton"
                        glyph: "settings"
                        tip: qsTr("Settings")
                        onClicked: showSettings("current")
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme.border
                }
            }
            Rectangle {
                visible: backend.error.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: errorRow.implicitHeight + 16
                color: Qt.alpha(theme.accent, .12)
                RowLayout {
                    id: errorRow
                    anchors.fill: parent
                    anchors.margins: 8
                    OIcon {
                        name: "priority"
                        tint: theme.accent
                        Layout.leftMargin: 12
                    }
                    OLabel {
                        text: backend.error
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                        Layout.fillWidth: true
                    }
                    OButton {
                        glyph: "close"
                        tip: qsTr("Dismiss error")
                        onClicked: backend.clearError()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                Sidebar {
                    visible: appWindow.wide
                    Layout.preferredWidth: backend.settings.sidebarWidth
                    Layout.fillHeight: true
                    onSettingsRequested: showSettings()
                    onComposeRequested: compose()
                    onViewChosen: {
                        appWindow.tab = "mail";
                        appWindow.detail = false;
                    }
                }
                Rectangle {
                    visible: appWindow.wide
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    color: theme.border
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: appWindow.tab === "mail" ? 0 : appWindow.tab === "calendar" ? 1 : 2
                    MailPage {
                        id: mailPage
                        window: appWindow
                    }
                    CalendarPage {
                        window: appWindow
                    }
                    AiPage {
                        window: appWindow
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: theme.background
                Rectangle {
                    width: parent.width
                    height: 1
                    color: theme.border
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    Rectangle {
                        width: 5
                        height: 5
                        radius: 3
                        color: backend.busy ? theme.accent : theme.muted
                    }
                    OLabel {
                        text: backend.status
                        font.pixelSize: 11
                        color: theme.muted
                        Layout.fillWidth: true
                    }
                    OLabel {
                        visible: !appWindow.narrow
                        text: backend.aiBusy ? qsTr("Local AI is working…") : qsTr("Local AI · %1").arg(backend.settings.ollamaModel)
                        font.pixelSize: 11
                        color: theme.muted
                    }
                    OButton {
                        text: "?"
                        tip: qsTr("Keyboard shortcuts")
                        implicitHeight: 24
                        implicitWidth: 26
                        onClicked: helpDialog.open()
                    }
                }
            }
        }
    }
    Drawer {
        id: sidebarDrawer
        width: Math.min(300, appWindow.width - 48)
        height: appWindow.height
        edge: Qt.LeftEdge
        dragMargin: 0
        background: Rectangle {
            color: theme.background
        }
        Sidebar {
            anchors.fill: parent
            onSettingsRequested: {
                sidebarDrawer.close();
                showSettings();
            }
            onComposeRequested: {
                sidebarDrawer.close();
                compose();
            }
            onViewChosen: {
                sidebarDrawer.close();
                tab = "mail";
                detail = false;
            }
        }
    }
    DraftDialog {
        id: composeDialog
        window: appWindow
    }
    EventDialog {
        id: eventDialog
        window: appWindow
    }
    SettingsDialog {
        id: settingsDialog
        window: appWindow
    }
    Dialog {
        id: confirmDialog
        objectName: "confirmationDialog"
        property var action: function () {}
        anchors.centerIn: parent
        width: Math.min(480, appWindow.width - 32)
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: action()
        contentItem: OLabel {
            id: confirmBody
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }
    Dialog {
        id: helpDialog
        anchors.centerIn: parent
        width: Math.min(500, appWindow.width - 32)
        title: qsTr("Keyboard shortcuts")
        modal: true
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 14
            Repeater {
                model: [
                    {
                        key: backend.settings.shortcutCompose,
                        name: qsTr("Compose")
                    },
                    {
                        key: backend.settings.shortcutSearch,
                        name: qsTr("Search mail")
                    },
                    {
                        key: backend.settings.shortcutSync,
                        name: qsTr("Sync")
                    },
                    {
                        key: backend.settings.shortcutMail,
                        name: qsTr("Mail")
                    },
                    {
                        key: backend.settings.shortcutCalendar,
                        name: qsTr("Calendar")
                    },
                    {
                        key: backend.settings.shortcutAi,
                        name: qsTr("AI assistant")
                    },
                    {
                        key: "Space",
                        name: qsTr("Open / mark read")
                    },
                    {
                        key: "Ctrl+Z / Ctrl+Shift+Z",
                        name: qsTr("Undo / redo local organization")
                    },
                    {
                        key: "?",
                        name: qsTr("Keyboard shortcuts")
                    },
                    {
                        key: "Q",
                        name: qsTr("Close window")
                    }
                ]
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    OLabel {
                        text: modelData.name
                        Layout.fillWidth: true
                    }
                    OLabel {
                        text: modelData.key
                        color: theme.muted
                        font.pixelSize: 12
                    }
                }
            }
            OLabel {
                Layout.fillWidth: true
                text: qsTr("Sync and scheduled mail continue after the window closes.")
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                color: theme.muted
                font.pixelSize: 12
            }
        }
    }
    Component.onCompleted: if (backend.demo && backend.data.threads.length) {
        const t = backend.data.threads[0];
        backend.selectThread(t.account_id, t.thread_id);
    }
}
