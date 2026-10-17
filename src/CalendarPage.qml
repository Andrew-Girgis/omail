import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property var window
    readonly property string mode: window.narrow ? "agenda" : backend.settings.calendarView
    function dayEvents(date) {
        return backend.data.events.filter(e => {
            const start = new Date(e.start), end = new Date(e.end), day = new Date(date + "T00:00:00");
            return start < new Date(day.getTime() + 86400000) && end > day;
        });
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.narrow ? 16 : 26
        spacing: 20
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                OLabel {
                    text: qsTr("Calendar")
                    font.pixelSize: 27
                    font.weight: Font.DemiBold
                }
                OLabel {
                    text: window.dateText(backend.data.anchor + "T12:00:00", "MMMM yyyy")
                    font.pixelSize: 14
                    color: theme.muted
                }
            }
            OButton {
                glyph: "back"
                tip: qsTr("Previous period")
                onClicked: backend.calendarNavigate(-1)
            }
            OButton {
                text: qsTr("Today")
                onClicked: backend.calendarToday()
            }
            OButton {
                glyph: "next"
                tip: qsTr("Next period")
                onClicked: backend.calendarNavigate(1)
            }
            OButton {
                text: window.narrow ? "" : qsTr("New event")
                glyph: "plus"
                primary: true
                onClicked: backend.newEvent()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: [
                    {
                        id: "week",
                        text: qsTr("Week")
                    },
                    {
                        id: "month",
                        text: qsTr("Month")
                    },
                    {
                        id: "agenda",
                        text: qsTr("Agenda")
                    }
                ]
                OButton {
                    required property var modelData
                    text: modelData.text
                    selected: backend.settings.calendarView === modelData.id
                    onClicked: backend.setSetting("calendarView", modelData.id)
                }
            }
            Item {
                Layout.fillWidth: true
            }
            OLabel {
                visible: !window.narrow
                text: qsTr("%n events", "", backend.data.events.length)
                color: theme.muted
                font.pixelSize: 12
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                id: calendarScroll
                anchors.fill: parent
                visible: page.mode !== "agenda"
                clip: true
                contentWidth: availableWidth
                contentHeight: page.mode === "week" ? 1180 : Math.max(540, height)
                GridLayout {
                    width: calendarScroll.availableWidth
                    columns: 7
                    columnSpacing: 1
                    rowSpacing: 1
                    Repeater {
                        model: backend.data.dates
                        Rectangle {
                            id: dayCell
                            required property string modelData
                            required property int index
                            property var events: page.dayEvents(modelData)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 100
                            Layout.preferredHeight: page.mode === "week" ? 1160 : Math.max(90, (calendarScroll.height - 12) / 6)
                            color: theme.panel
                            border.width: Qt.formatDate(new Date(), "yyyy-MM-dd") === modelData ? 1 : 0
                            border.color: Qt.alpha(theme.accent, .5)
                            OLabel {
                                id: dayLabel
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.margins: 10
                                text: window.dateText(modelData + "T12:00:00", page.mode === "week" ? "ddd d" : "d")
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                                color: Qt.formatDate(new Date(), "yyyy-MM-dd") === modelData ? theme.accent : theme.muted
                            }
                            TapHandler {
                                onDoubleTapped: backend.newEvent(dayCell.modelData)
                            }
                            Item {
                                visible: page.mode === "week"
                                anchors.top: parent.top
                                anchors.topMargin: 50
                                width: parent.width
                                height: 1104
                                Repeater {
                                    model: 24
                                    Item {
                                        required property int index
                                        y: index * 46
                                        width: parent.width
                                        height: 46
                                        Rectangle {
                                            width: parent.width
                                            height: 1
                                            color: theme.border
                                        }
                                        OLabel {
                                            visible: dayCell.index === 0
                                            x: 5
                                            y: 3
                                            text: Qt.formatTime(new Date(2000, 0, 1, index, 0), "h AP")
                                            font.pixelSize: 9
                                            color: theme.muted
                                        }
                                    }
                                }
                                Repeater {
                                    model: dayCell.events
                                    Rectangle {
                                        id: weekEvent
                                        required property var modelData
                                        property var start: new Date(modelData.start)
                                        property var end: new Date(modelData.end)
                                        x: 4
                                        width: parent.width - 8
                                        y: modelData.all_day ? 0 : Math.max(0, (start.getHours() + start.getMinutes() / 60) * 46)
                                        height: modelData.all_day ? 28 : Math.max(30, Math.min(1104 - y, (end - start) / 3600000 * 46))
                                        color: Qt.alpha(modelData.color || theme.accent, .22)
                                        border.color: Qt.alpha(modelData.color || theme.accent, .4)
                                        border.width: 1
                                        radius: Math.min(4, theme.radius)
                                        clip: true
                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 3
                                            OLabel {
                                                width: parent.width
                                                text: weekEvent.modelData.title
                                                font.pixelSize: 11
                                                font.weight: Font.DemiBold
                                                wrapMode: Text.Wrap
                                                maximumLineCount: 3
                                            }
                                            OLabel {
                                                width: parent.width
                                                text: weekEvent.modelData.all_day ? qsTr("All day") : window.dateText(weekEvent.modelData.start, "h:mm AP")
                                                font.pixelSize: 10
                                                color: theme.muted
                                            }
                                        }
                                        TapHandler {
                                            onTapped: backend.editEvent(weekEvent.modelData.id)
                                        }
                                    }
                                }
                            }
                            Column {
                                visible: page.mode === "month"
                                anchors.top: dayLabel.bottom
                                anchors.topMargin: 8
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 5
                                spacing: 4
                                Repeater {
                                    model: dayCell.events.slice(0, 3)
                                    Rectangle {
                                        required property var modelData
                                        width: parent.width
                                        height: 22
                                        color: Qt.alpha(modelData.color || theme.accent, .18)
                                        radius: Math.min(4, theme.radius)
                                        OLabel {
                                            anchors.fill: parent
                                            anchors.leftMargin: 5
                                            verticalAlignment: Text.AlignVCenter
                                            text: parent.modelData.title
                                            font.pixelSize: 10
                                        }
                                        TapHandler {
                                            onTapped: backend.editEvent(parent.modelData.id)
                                        }
                                    }
                                }
                                OLabel {
                                    visible: dayCell.events.length > 3
                                    text: qsTr("+%n more", "", dayCell.events.length - 3)
                                    font.pixelSize: 10
                                    color: theme.muted
                                    TapHandler {
                                        onTapped: {
                                            backend.calendarDate(dayCell.modelData);
                                            backend.setSetting("calendarView", "agenda");
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            ListView {
                id: agenda
                visible: page.mode === "agenda"
                anchors.fill: parent
                clip: true
                spacing: 8
                model: backend.data.events
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: agenda.width
                    height: 90
                    background: Rectangle {
                        color: parent.hovered ? Qt.alpha(theme.accent, .07) : theme.panel
                        radius: theme.radius
                    }
                    contentItem: RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16
                        Rectangle {
                            width: 3
                            Layout.fillHeight: true
                            color: modelData.color || theme.accent
                        }
                        ColumnLayout {
                            Layout.preferredWidth: window.narrow ? 70 : 110
                            OLabel {
                                text: window.dateText(modelData.start, "MMM d")
                                font.weight: Font.DemiBold
                            }
                            OLabel {
                                text: modelData.all_day ? qsTr("All day") : window.dateText(modelData.start, "h:mm AP")
                                color: theme.muted
                                font.pixelSize: 11
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            OLabel {
                                text: modelData.title
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                            }
                            OLabel {
                                text: modelData.location || modelData.calendar_name
                                color: theme.muted
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                        }
                        OIcon {
                            name: "next"
                            tint: theme.muted
                        }
                    }
                    onClicked: backend.editEvent(modelData.id)
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                visible: backend.data.calendars.length === 0
                width: Math.min(420, parent.width - 32)
                spacing: 14
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: emptyCalendar.implicitHeight + 48
                    color: theme.background
                    radius: theme.radius
                    border.color: theme.border
                    border.width: 1
                    ColumnLayout {
                        id: emptyCalendar
                        anchors.fill: parent
                        anchors.margins: 24
                        spacing: 14
                        OIcon {
                            name: "calendar"
                            tint: theme.accent
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                        }
                        OLabel {
                            text: qsTr("Your time, together")
                            font.pixelSize: 22
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        OLabel {
                            text: qsTr("Connect an account and sync to see its calendars here.")
                            color: theme.muted
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        OButton {
                            text: qsTr("Manage accounts")
                            primary: true
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: window.showSettings()
                        }
                    }
                }
            }
        }
    }
}
