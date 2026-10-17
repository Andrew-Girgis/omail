import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property var window
    function citationText(row) {
        let body = row.body;
        for (let i = 0; i < row.sources.length; i++)
            body = body.split("[" + row.sources[i].id + "]").join("[" + (i + 1) + "]");
        return body;
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: window.narrow ? 20 : 36
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            OIcon {
                name: "ai"
                tint: theme.accent
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
            }
            OLabel {
                text: qsTr("Ask Omail")
                font.pixelSize: 27
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            OLabel {
                visible: !window.narrow
                text: qsTr("READ ONLY")
                font.pixelSize: 10
                font.letterSpacing: 1.5
                color: theme.muted
            }
        }
        OLabel {
            Layout.fillWidth: true
            text: backend.data.profile === "all" ? qsTr("Choose one profile in the sidebar before asking a question.") : qsTr("Answers from this profile’s mail and calendar, with sources. Your local model cannot send, delete, or change anything.")
            color: theme.muted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            font.pixelSize: 13
        }
        OLabel {
            visible: !!backend.data.aiError
            text: backend.data.aiError || ""
            color: theme.accent
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Layout.fillWidth: true
        }
        ListView {
            id: chatList
            objectName: "chatList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 20
            model: backend.data.chats
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: Qt.callLater(positionViewAtEnd)
            delegate: ColumnLayout {
                required property var modelData
                width: chatList.width
                spacing: 10
                OLabel {
                    text: modelData.role === "user" ? qsTr("You") : "Omail"
                    color: modelData.role === "user" ? theme.muted : theme.accent
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                TextEdit {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    text: page.citationText(modelData)
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: theme.foreground
                    font.pixelSize: backend.settings.fontSize
                    textFormat: TextEdit.PlainText
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: modelData.sources
                        OButton {
                            required property var modelData
                            required property int index
                            text: "[" + (index + 1) + "] " + modelData.title
                            glyph: modelData.type === "mail" ? "mail" : "calendar"
                            selected: true
                            width: Math.min(implicitWidth, chatList.width)
                            onClicked: backend.openSource(modelData)
                        }
                    }
                }
            }
            ColumnLayout {
                visible: chatList.count === 0
                anchors.centerIn: parent
                width: Math.min(540, parent.width)
                spacing: 18
                OIcon {
                    name: "ai"
                    tint: theme.accent
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                }
                OLabel {
                    text: qsTr("A second pair of eyes.")
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
                OLabel {
                    text: qsTr("Find the decision in a long thread. Prepare for a meeting. See what needs your attention.")
                    color: theme.muted
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
                Repeater {
                    model: [qsTr("What needs a response from me?"), qsTr("Help me prepare for my next meeting."), qsTr("What decisions were made this week?")]
                    OButton {
                        required property string modelData
                        text: modelData
                        Layout.fillWidth: true
                        enabled: !backend.aiBusy && backend.data.profile !== "all"
                        onClicked: backend.ask(modelData)
                    }
                }
            }
        }
        OLabel {
            visible: backend.aiBusy
            text: qsTr("Reading the available context with your local model…")
            color: theme.accent
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            OField {
                id: question
                objectName: "aiQuestion"
                Layout.fillWidth: true
                placeholderText: qsTr("Ask about your mail or calendar…")
                enabled: backend.data.profile !== "all"
                onAccepted: if (!backend.aiBusy && text.trim()) {
                    backend.ask(text);
                    text = "";
                }
            }
            OButton {
                text: qsTr("Ask")
                glyph: "ai"
                primary: true
                enabled: !backend.aiBusy && backend.data.profile !== "all" && question.text.trim().length > 0
                onClicked: {
                    backend.ask(question.text);
                    question.text = "";
                }
            }
        }
        OLabel {
            text: qsTr("Uses cached text from the last 90 days and mail you fetched. Attachments are excluded unless you explicitly ask to analyze one.")
            Layout.fillWidth: true
            font.pixelSize: 11
            color: theme.muted
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }
}
