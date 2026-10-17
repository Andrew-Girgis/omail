import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var window
    property var draft: ({})
    property bool dirty: false
    anchors.centerIn: parent
    width: Math.min(800, window.width - 24)
    height: Math.min(760, window.height - 32)
    modal: true
    title: qsTr("Compose")
    closePolicy: Popup.NoAutoClose
    function load(value) {
        draft = value;
        toField.text = value.to || "";
        ccField.text = value.cc || "";
        bccField.text = value.bcc || "";
        subjectField.text = value.subject || "";
        bodyField.text = value.body || "";
        sender.currentIndex = Math.max(0, backend.data.accounts.findIndex(a => a.id === value.account_id));
        dirty = false;
        open();
    }
    function value() {
        const result = Object.assign({}, draft);
        result.account_id = sender.currentValue;
        result.to = toField.text;
        result.cc = ccField.text;
        result.bcc = bccField.text;
        result.subject = subjectField.text;
        result.body = bodyField.text;
        return result;
    }
    contentItem: ColumnLayout {
        spacing: 10
        OLabel {
            visible: !!dialog.draft.previous_error
            text: dialog.draft.previous_error || ""
            color: theme.accent
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            OLabel {
                text: qsTr("From")
                color: theme.muted
                Layout.preferredWidth: 50
            }
            ComboBox {
                id: sender
                Layout.fillWidth: true
                model: backend.data.accounts
                textRole: "email"
                valueRole: "id"
                enabled: dialog.draft.mode === "new" || !dialog.draft.mode
                onActivated: dialog.dirty = true
                Material.roundedScale: theme.radius
            }
        }
        OField {
            id: toField
            objectName: "composeTo"
            Layout.fillWidth: true
            placeholderText: qsTr("To · email addresses separated by commas")
            onTextEdited: dialog.dirty = true
        }
        RowLayout {
            Layout.fillWidth: true
            OField {
                id: ccField
                Layout.fillWidth: true
                placeholderText: qsTr("Cc")
                onTextEdited: dialog.dirty = true
            }
            OField {
                id: bccField
                Layout.fillWidth: true
                placeholderText: qsTr("Bcc")
                onTextEdited: dialog.dirty = true
            }
        }
        OField {
            id: subjectField
            objectName: "composeSubject"
            Layout.fillWidth: true
            placeholderText: qsTr("Subject")
            onTextEdited: dialog.dirty = true
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                id: bodyField
                objectName: "composeBody"
                placeholderText: qsTr("Write your message…")
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                color: theme.foreground
                font.pixelSize: backend.settings.fontSize
                padding: 14
                background: Rectangle {
                    color: theme.panel
                    radius: theme.radius
                }
                onTextChanged: dialog.dirty = true
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: dialog.draft.attachments || []
                OButton {
                    required property var modelData
                    required property int index
                    text: modelData.name
                    glyph: "close"
                    onClicked: {
                        const d = dialog.value();
                        d.attachments = d.attachments.filter((a, i) => i !== index);
                        dialog.draft = d;
                        dialog.dirty = true;
                    }
                }
            }
        }
        OLabel {
            text: qsTr("Saved drafts stay on this desktop. Sending always requires your approval.")
            font.pixelSize: 11
            color: theme.muted
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            OButton {
                text: qsTr("Send")
                primary: true
                glyph: "send"
                onClicked: window.showConfirm(qsTr("Send this email?"), qsTr("From %1\nTo %2\nSubject: %3").arg(sender.currentText).arg(toField.text).arg(subjectField.text), function () {
                    backend.approveDraft(dialog.value(), "");
                })
            }
            OButton {
                text: qsTr("Schedule")
                glyph: "clock"
                onClicked: scheduleDialog.open()
            }
            OButton {
                text: qsTr("Attach")
                glyph: "clip"
                onClicked: backend.addAttachments(dialog.value())
            }
            OButton {
                text: qsTr("Save & close")
                objectName: "saveDraftButton"
                onClicked: if (backend.saveDraft(dialog.value()))
                    dialog.close()
            }
            OButton {
                text: qsTr("Discard")
                onClicked: window.showConfirm(qsTr("Discard changes?"), qsTr("Unsaved edits will be lost. A previously saved draft will remain."), function () {
                    dialog.close();
                })
            }
        }
    }
    Dialog {
        id: scheduleDialog
        anchors.centerIn: parent
        width: Math.min(440, dialog.width - 24)
        title: qsTr("Schedule email")
        modal: true
        standardButtons: Dialog.Cancel
        contentItem: ColumnLayout {
            spacing: 14
            OLabel {
                text: qsTr("Choose a local date and time. The desktop must be awake and online. Missed sends are always held for review.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
            OField {
                id: sendTime
                Layout.fillWidth: true
                placeholderText: qsTr("YYYY-MM-DDTHH:MM")
                text: Qt.formatDateTime(new Date(Date.now() + 3600000), "yyyy-MM-ddTHH:mm")
            }
            OButton {
                text: qsTr("Approve scheduled send")
                primary: true
                Layout.fillWidth: true
                onClicked: window.showConfirm(qsTr("Approve this schedule?"), qsTr("Send to %1 at %2, if the desktop is awake and online.").arg(toField.text).arg(sendTime.text), function () {
                    if (backend.approveDraft(dialog.value(), sendTime.text))
                        scheduleDialog.close();
                })
            }
        }
    }
}
