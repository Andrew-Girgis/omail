import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var window
    property var event: ({})
    anchors.centerIn: parent
    width: Math.min(680, window.width - 24)
    height: Math.min(740, window.height - 32)
    modal: true
    title: dialog.event.id ? qsTr("Event details") : qsTr("New event")
    closePolicy: Popup.NoAutoClose
    function load(e) {
        event = e;
        titleField.text = e.title || "";
        locationField.text = e.location || "";
        bodyField.text = e.body || "";
        attendeeField.text = e.attendee_emails || "";
        startField.text = Qt.formatDateTime(new Date(e.start), "yyyy-MM-ddTHH:mm");
        endField.text = Qt.formatDateTime(new Date(e.end), "yyyy-MM-ddTHH:mm");
        allDay.checked = !!e.all_day;
        calendars.currentIndex = Math.max(0, backend.data.calendars.findIndex(c => c.id === e.calendar_id));
        repeat.currentIndex = 0;
        open();
    }
    function value() {
        const e = Object.assign({}, event);
        e.title = titleField.text;
        e.location = locationField.text;
        e.body = bodyField.text;
        e.attendee_emails = attendeeField.text;
        e.start = startField.text;
        e.end = endField.text;
        e.all_day = allDay.checked ? 1 : 0;
        e.calendar_id = calendars.currentValue;
        if (!event.id)
            e.repeat = ["none", "daily", "weekly", "monthly"][repeat.currentIndex];
        return e;
    }
    contentItem: ColumnLayout {
        spacing: 10
        OField {
            id: titleField
            Layout.fillWidth: true
            placeholderText: qsTr("Event title")
        }
        ComboBox {
            id: calendars
            Layout.fillWidth: true
            model: backend.data.calendars
            textRole: "name"
            valueRole: "id"
            enabled: !dialog.event.id
        }
        RowLayout {
            Layout.fillWidth: true
            OField {
                id: startField
                Layout.fillWidth: true
                placeholderText: qsTr("Start · YYYY-MM-DDTHH:MM")
            }
            OField {
                id: endField
                Layout.fillWidth: true
                placeholderText: qsTr("End · YYYY-MM-DDTHH:MM")
            }
        }
        OLabel {
            text: qsTr("Times use your desktop timezone. All-day events end at midnight after the last day.")
            color: theme.muted
            font.pixelSize: 11
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Layout.fillWidth: true
        }
        RowLayout {
            CheckBox {
                id: allDay
                text: qsTr("All day")
            }
            ComboBox {
                id: repeat
                visible: !dialog.event.id
                model: [qsTr("Does not repeat"), qsTr("Daily"), qsTr("Weekly"), qsTr("Monthly")]
                Layout.fillWidth: true
            }
        }
        OField {
            id: locationField
            Layout.fillWidth: true
            placeholderText: qsTr("Location or meeting URL")
        }
        OField {
            id: attendeeField
            Layout.fillWidth: true
            placeholderText: qsTr("Guests · email addresses separated by commas")
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            TextArea {
                id: bodyField
                placeholderText: qsTr("Notes")
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                font.pixelSize: backend.settings.fontSize
                color: theme.foreground
                background: Rectangle {
                    color: theme.panel
                }
            }
        }
        OLabel {
            visible: !!dialog.event.organizer
            text: qsTr("Organizer: %1 · Response: %2").arg(dialog.event.organizer || "").arg(dialog.event.response || qsTr("None"))
            color: theme.muted
            Layout.fillWidth: true
            font.pixelSize: 12
        }
        Flow {
            visible: !!dialog.event.id && !!dialog.event.organizer && dialog.event.organizer.toLowerCase() !== (dialog.event.email || "").toLowerCase()
            Layout.fillWidth: true
            spacing: 6
            OButton {
                text: qsTr("Accept")
                glyph: "check"
                onClicked: window.showConfirm(qsTr("Accept invitation?"), qsTr("Send your response to the organizer."), function () {
                    backend.respondEvent(dialog.event.id, "accepted");
                })
            }
            OButton {
                text: qsTr("Tentative")
                onClicked: window.showConfirm(qsTr("Respond tentative?"), qsTr("Send your response to the organizer."), function () {
                    backend.respondEvent(dialog.event.id, "tentative");
                })
            }
            OButton {
                text: qsTr("Decline")
                onClicked: window.showConfirm(qsTr("Decline invitation?"), qsTr("Send your response to the organizer."), function () {
                    backend.respondEvent(dialog.event.id, "declined");
                })
            }
        }
        OLabel {
            visible: !!dialog.event.id && !!dialog.event.recurrence && dialog.event.recurrence !== "[]" && dialog.event.recurrence !== "{}"
            text: qsTr("Editing an occurrence changes this occurrence only.")
            font.pixelSize: 11
            color: theme.muted
            Layout.fillWidth: true
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            OButton {
                text: qsTr("Save event")
                primary: true
                enabled: !backend.busy && (!dialog.event.id || !!dialog.event.writable)
                onClicked: window.showConfirm(qsTr("Save calendar changes?"), qsTr("Changes sync to your provider. Guests may receive an invitation or update."), function () {
                    backend.saveEvent(dialog.value());
                })
            }
            OButton {
                visible: !!dialog.event.id
                text: qsTr("Delete")
                enabled: !backend.busy && !!dialog.event.writable
                onClicked: window.showConfirm(qsTr("Delete this event?"), qsTr("The provider will remove the event and may notify its guests."), function () {
                    backend.deleteEvent(dialog.event.id);
                })
            }
            OButton {
                visible: !!dialog.event.web_url
                glyph: "external"
                tip: qsTr("Open in browser")
                onClicked: backend.openLink(dialog.event.web_url)
            }
            OButton {
                text: qsTr("Close")
                onClicked: window.showConfirm(qsTr("Close event?"), qsTr("Any unsaved changes will be discarded."), function () {
                    dialog.close();
                })
            }
        }
    }
}
