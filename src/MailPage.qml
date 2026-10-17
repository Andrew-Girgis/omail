import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Omail 1.0

Item {
    id: page
    required property var window
    readonly property var messages: backend.data.messages || []
    readonly property var latest: messages.length ? messages[messages.length - 1] : ({})
    readonly property bool draftView: backend.data.folder === "drafts"
    function focusSearch() {
        searchField.forceActiveFocus();
    }
    function viewName() {
        const names = {
            inbox: qsTr("Inbox"),
            priority: qsTr("Priority"),
            flagged: qsTr("Flagged"),
            drafts: qsTr("Drafts & scheduled"),
            sent: qsTr("Sent"),
            archive: qsTr("Archive"),
            junk: qsTr("Junk"),
            trash: qsTr("Trash"),
            all: qsTr("All mail")
        };
        return names[backend.data.folder] || backend.data.folder.replace("topic:", "");
    }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            visible: !window.narrow || !window.detail || page.draftView
            Layout.preferredWidth: window.narrow ? page.width : backend.settings.listWidth
            Layout.fillWidth: window.narrow || page.draftView
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 22
                    spacing: 9
                    RowLayout {
                        Layout.fillWidth: true
                        OLabel {
                            text: page.viewName()
                            font.pixelSize: 26
                            font.weight: Font.DemiBold
                            font.letterSpacing: -.7
                            Layout.fillWidth: true
                        }
                        OLabel {
                            text: page.draftView ? backend.data.drafts.length : backend.data.threads.length
                            color: theme.muted
                            font.pixelSize: 12
                        }
                    }
                    OLabel {
                        Layout.fillWidth: true
                        text: backend.data.folder === "priority" ? qsTr("Important, until you mark it done") : page.draftView ? qsTr("Saved here. Sent only with your approval.") : qsTr("A little more room to focus.")
                        font.pixelSize: 12
                        color: theme.muted
                    }
                    OField {
                        id: searchField
                        objectName: "mailSearch"
                        visible: !page.draftView
                        Layout.fillWidth: true
                        Layout.topMargin: 10
                        placeholderText: qsTr("Search this view…")
                        onTextEdited: searchTimer.restart()
                        onAccepted: {
                            searchTimer.stop();
                            backend.search(text);
                        }
                        Timer {
                            id: searchTimer
                            interval: 180
                            onTriggered: backend.search(searchField.text)
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme.border
                }
                ListView {
                    id: threadList
                    objectName: "threadList"
                    visible: !page.draftView
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    clip: true
                    model: backend.data.threads
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: conversation
                        required property var modelData
                        required property int index
                        width: threadList.width
                        height: backend.settings.density === "compact" ? 90 : 114
                        property bool selected: backend.data.selectedAccount === modelData.account_id && backend.data.selectedThread === modelData.thread_id
                        background: Rectangle {
                            color: conversation.selected ? Qt.alpha(theme.accent, .10) : conversation.hovered ? Qt.alpha(theme.foreground, .025) : "transparent"
                            Rectangle {
                                width: 3
                                height: parent.height
                                color: theme.accent
                                visible: conversation.selected
                            }
                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.leftMargin: 22
                                anchors.right: parent.right
                                height: 1
                                color: theme.border
                                opacity: .65
                            }
                        }
                        contentItem: ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 22
                            anchors.rightMargin: 20
                            anchors.topMargin: backend.settings.density === "compact" ? 11 : 17
                            anchors.bottomMargin: 13
                            spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Rectangle {
                                    visible: !!modelData.unread
                                    width: 5
                                    height: 5
                                    radius: 3
                                    color: theme.accent
                                }
                                OLabel {
                                    text: modelData.sender || modelData.sender_email
                                    font.pixelSize: backend.settings.fontSize - 1
                                    font.weight: modelData.unread ? Font.DemiBold : Font.Normal
                                    Layout.fillWidth: true
                                }
                                OIcon {
                                    visible: !!modelData.flagged
                                    name: "flag"
                                    tint: theme.accent
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                }
                                OLabel {
                                    text: window.dateText(modelData.received, "h:mm AP")
                                    font.pixelSize: 10
                                    color: theme.muted
                                }
                            }
                            OLabel {
                                text: modelData.subject || qsTr("(No subject)")
                                font.weight: modelData.unread ? Font.DemiBold : Font.Normal
                                Layout.fillWidth: true
                            }
                            OLabel {
                                visible: backend.settings.density !== "compact"
                                text: modelData.body.replace(/\s+/g, " ")
                                font.pixelSize: 12
                                color: theme.muted
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                visible: !!modelData.priority
                                spacing: 5
                                Layout.fillWidth: true
                                OIcon {
                                    name: "ai"
                                    tint: theme.accent
                                    Layout.preferredWidth: 11
                                    Layout.preferredHeight: 11
                                }
                                OLabel {
                                    text: modelData.reason || qsTr("Important conversation")
                                    color: theme.accent
                                    font.pixelSize: 10
                                    Layout.fillWidth: true
                                }
                            }
                        }
                        onClicked: {
                            backend.selectThread(modelData.account_id, modelData.thread_id);
                            window.detail = true;
                        }
                    }
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width - 48
                        visible: threadList.count === 0
                        spacing: 16
                        OIcon {
                            name: "inbox"
                            tint: theme.muted
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                        }
                        OLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: backend.data.accounts.length ? qsTr("Nothing here right now") : qsTr("Your inbox starts here")
                            font.pixelSize: 18
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                        }
                        OLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: backend.data.accounts.length ? qsTr("Try another view or search your provider for older mail.") : qsTr("Connect Outlook or Google to bring your mail and calendars into Omail.")
                            color: theme.muted
                            wrapMode: Text.Wrap
                            elide: Text.ElideNone
                        }
                        OButton {
                            objectName: "mailConnectButton"
                            text: backend.data.accounts.length ? qsTr("Search older mail") : qsTr("Connect an account")
                            primary: true
                            Layout.alignment: Qt.AlignHCenter
                            onClicked: window.showSettings(backend.data.accounts.length ? "advanced" : "accounts")
                        }
                    }
                }
                ListView {
                    id: draftList
                    visible: page.draftView
                    model: backend.data.drafts
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        required property var modelData
                        width: draftList.width
                        height: draftContent.implicitHeight + 32
                        background: Rectangle {
                            color: parent.hovered ? theme.panel : "transparent"
                            Rectangle {
                                height: 1
                                width: parent.width
                                anchors.bottom: parent.bottom
                                color: theme.border
                            }
                        }
                        contentItem: ColumnLayout {
                            id: draftContent
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 8
                            OLabel {
                                text: modelData.content.subject || qsTr("Untitled draft")
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                            }
                            OLabel {
                                text: qsTr("To: %1").arg(modelData.content.to || qsTr("No recipient"))
                                color: theme.muted
                                Layout.fillWidth: true
                            }
                            OLabel {
                                text: modelData.status === "scheduled" ? qsTr("Scheduled · %1").arg(window.dateText(modelData.scheduled_at)) : modelData.status === "held" ? qsTr("Held for your review") : modelData.status === "sending" ? qsTr("Sending…") : qsTr("Draft")
                                color: modelData.status === "held" ? theme.accent : theme.muted
                                Layout.fillWidth: true
                            }
                            OLabel {
                                visible: !!modelData.error
                                text: modelData.error
                                wrapMode: Text.Wrap
                                elide: Text.ElideNone
                                Layout.fillWidth: true
                                color: theme.accent
                                font.pixelSize: 12
                            }
                            RowLayout {
                                OButton {
                                    text: qsTr("Edit")
                                    enabled: modelData.status !== "sending"
                                    onClicked: backend.editDraft(modelData.id)
                                }
                                OButton {
                                    visible: modelData.status === "scheduled"
                                    text: qsTr("Cancel schedule")
                                    onClicked: backend.cancelSchedule(modelData.id)
                                }
                                OButton {
                                    text: qsTr("Delete draft")
                                    enabled: modelData.status !== "sending"
                                    onClicked: window.showConfirm(qsTr("Delete this draft?"), qsTr("The local draft and its attachments will be removed."), function () {
                                        backend.deleteDraft(modelData.id);
                                    })
                                }
                            }
                        }
                        onClicked: backend.editDraft(modelData.id)
                    }
                    OLabel {
                        visible: draftList.count === 0
                        anchors.centerIn: parent
                        text: qsTr("No saved drafts")
                        color: theme.muted
                    }
                }
            }
        }
        Rectangle {
            visible: !window.narrow && !page.draftView
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: theme.border
        }
        Item {
            visible: (!window.narrow || window.detail) && !page.draftView
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                visible: page.messages.length > 0
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    color: theme.background
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: window.narrow ? 10 : 22
                        anchors.rightMargin: 16
                        spacing: 4
                        OButton {
                            visible: window.narrow
                            glyph: "back"
                            tip: qsTr("Back to messages")
                            onClicked: window.detail = false
                        }
                        OButton {
                            glyph: "archive"
                            tip: qsTr("Archive conversation")
                            enabled: !backend.busy
                            onClicked: backend.threadAction("archive")
                        }
                        OButton {
                            glyph: "trash"
                            tip: qsTr("Move conversation to Trash")
                            enabled: !backend.busy
                            onClicked: window.showConfirm(qsTr("Move to Trash?"), qsTr("This moves every cached message in this conversation to the provider’s Trash."), function () {
                                backend.threadAction("trash");
                            })
                        }
                        OButton {
                            glyph: "flag"
                            tip: qsTr("Toggle flag")
                            selected: !!page.latest.flagged
                            enabled: !backend.busy
                            onClicked: backend.threadAction(page.latest.flagged ? "unflag" : "flag")
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        OButton {
                            text: window.narrow ? "" : qsTr("Done")
                            glyph: "check"
                            tip: qsTr("Dismiss from Priority")
                            onClicked: backend.dismissPriority()
                        }
                        OButton {
                            glyph: "more"
                            tip: qsTr("More actions")
                            onClicked: moreMenu.open()
                            Menu {
                                id: moreMenu
                                MenuItem {
                                    text: qsTr("Mark read")
                                    onTriggered: backend.threadAction("read")
                                }
                                MenuItem {
                                    text: qsTr("Mark unread")
                                    onTriggered: backend.threadAction("unread")
                                }
                                MenuItem {
                                    text: qsTr("Move to Inbox")
                                    onTriggered: backend.threadAction("inbox")
                                }
                                MenuItem {
                                    text: qsTr("Move to Junk")
                                    onTriggered: window.showConfirm(qsTr("Move to Junk?"), qsTr("This updates the provider’s spam folder."), function () {
                                        backend.threadAction("junk");
                                    })
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("Mark latest message important")
                                    onTriggered: backend.correctPriority(page.latest.id, true)
                                }
                                MenuItem {
                                    text: qsTr("Mark latest message not important")
                                    onTriggered: backend.correctPriority(page.latest.id, false)
                                }
                                MenuItem {
                                    text: qsTr("Set topic on latest message…")
                                    onTriggered: {
                                        topicField.text = page.latest.topic || "";
                                        topicDialog.open();
                                    }
                                }
                                MenuItem {
                                    text: qsTr("Classify latest message")
                                    enabled: !backend.aiBusy
                                    onTriggered: backend.classifySelected()
                                }
                                MenuItem {
                                    text: qsTr("Open in browser")
                                    enabled: !!page.latest.web_url
                                    onTriggered: backend.openLink(page.latest.web_url)
                                }
                            }
                        }
                    }
                    Rectangle {
                        height: 1
                        width: parent.width
                        anchors.bottom: parent.bottom
                        color: theme.border
                    }
                }
                ScrollView {
                    id: readerScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: readerScroll.availableWidth
                        spacing: 0
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.margins: window.narrow ? 22 : 32
                            spacing: 12
                            OLabel {
                                text: page.latest.subject || qsTr("(No subject)")
                                font.pixelSize: 27
                                font.weight: Font.DemiBold
                                font.letterSpacing: -.7
                                wrapMode: Text.Wrap
                                elide: Text.ElideNone
                                Layout.fillWidth: true
                            }
                            OLabel {
                                text: qsTr("%n messages", "", page.messages.length) + " · " + (page.latest.account_email || "")
                                color: theme.muted
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                spacing: 8
                                Layout.topMargin: 5
                                OButton {
                                    text: qsTr("Summarize")
                                    glyph: "ai"
                                    selected: true
                                    enabled: !backend.aiBusy
                                    onClicked: backend.summarize(false)
                                }
                                OButton {
                                    text: qsTr("Suggest reply")
                                    enabled: !backend.aiBusy
                                    onClicked: backend.summarize(true)
                                }
                            }
                            Rectangle {
                                visible: backend.summary.length > 0
                                Layout.fillWidth: true
                                implicitHeight: summaryText.implicitHeight + 28
                                color: Qt.alpha(theme.accent, .08)
                                radius: theme.radius
                                border.width: 1
                                border.color: Qt.alpha(theme.accent, .24)
                                TextEdit {
                                    id: summaryText
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    text: backend.summary
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    color: theme.foreground
                                    font.pixelSize: backend.settings.fontSize
                                    textFormat: TextEdit.PlainText
                                }
                            }
                        }
                        Repeater {
                            model: page.messages
                            ColumnLayout {
                                id: messageCard
                                required property var modelData
                                property bool rich: false
                                Layout.fillWidth: true
                                Layout.leftMargin: window.narrow ? 22 : 32
                                Layout.rightMargin: window.narrow ? 22 : 32
                                spacing: 16
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 1
                                    color: theme.border
                                    Layout.topMargin: 4
                                    Layout.bottomMargin: 8
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Rectangle {
                                        width: 36
                                        height: 36
                                        radius: 18
                                        color: Qt.alpha(theme.accent, .15)
                                        OLabel {
                                            anchors.centerIn: parent
                                            text: (modelData.sender || modelData.sender_email || "?").substring(0, 1).toUpperCase()
                                            color: theme.accent
                                            font.weight: Font.DemiBold
                                        }
                                    }
                                    ColumnLayout {
                                        spacing: 3
                                        Layout.fillWidth: true
                                        OLabel {
                                            text: modelData.sender || modelData.sender_email
                                            font.weight: Font.DemiBold
                                            Layout.fillWidth: true
                                        }
                                        OLabel {
                                            text: qsTr("To %1").arg(modelData.recipients)
                                            color: theme.muted
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                        }
                                    }
                                    OLabel {
                                        visible: !window.narrow
                                        text: window.dateText(modelData.received, "MMM d, h:mm AP")
                                        color: theme.muted
                                        font.pixelSize: 11
                                    }
                                }
                                OLabel {
                                    visible: !!modelData.priority
                                    text: modelData.reason || qsTr("Marked important")
                                    color: theme.accent
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                    elide: Text.ElideNone
                                    Layout.fillWidth: true
                                }
                                TextEdit {
                                    visible: !messageCard.rich
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: contentHeight
                                    text: modelData.body || qsTr("This message has no text body.")
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    textFormat: TextEdit.PlainText
                                    color: theme.foreground
                                    font.pixelSize: backend.settings.fontSize
                                    selectionColor: theme.accent
                                    selectedTextColor: theme.accentForeground
                                }
                                MailDocument {
                                    id: htmlBody
                                    visible: messageCard.rich
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: implicitHeight
                                    html: messageCard.rich ? modelData.html : ""
                                    textColor: theme.foreground
                                    textSize: backend.settings.fontSize
                                    TapHandler {
                                        onTapped: function (eventPoint) {
                                            const link = htmlBody.linkAt(eventPoint.position.x, eventPoint.position.y);
                                            if (link)
                                                window.showConfirm(qsTr("Open this link?"), link, function () {
                                                    backend.openLink(link);
                                                });
                                        }
                                    }
                                }
                                OButton {
                                    visible: !!modelData.html
                                    text: messageCard.rich ? qsTr("Show plain text") : qsTr("Show formatted message")
                                    glyph: "eye"
                                    onClicked: messageCard.rich = !messageCard.rich
                                }
                                Repeater {
                                    model: JSON.parse(messageCard.modelData.attachments || "[]")
                                    RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        OIcon {
                                            name: "clip"
                                            tint: theme.muted
                                        }
                                        OLabel {
                                            text: modelData.name
                                            Layout.fillWidth: true
                                            font.pixelSize: 12
                                        }
                                        OButton {
                                            text: modelData.pending ? qsTr("Load") : qsTr("Save")
                                            onClicked: {
                                                if (modelData.pending)
                                                    backend.loadAttachments(messageCard.modelData.id);
                                                else
                                                    backend.saveAttachment(messageCard.modelData.id, modelData);
                                            }
                                        }
                                        OButton {
                                            visible: !modelData.pending
                                            glyph: "ai"
                                            tip: qsTr("Explicitly analyze this attachment")
                                            enabled: !backend.aiBusy
                                            onClicked: window.showConfirm(qsTr("Analyze attachment?"), qsTr("Read this attachment with the selected local model? Text files and text-based PDFs are supported."), function () {
                                                backend.analyzeAttachment(messageCard.modelData.id, modelData);
                                            })
                                        }
                                    }
                                }
                                Item {
                                    height: 24
                                }
                            }
                        }
                        RowLayout {
                            Layout.margins: window.narrow ? 22 : 32
                            spacing: 12
                            OButton {
                                text: qsTr("Reply")
                                glyph: "reply"
                                primary: true
                                onClicked: backend.newDraft("reply")
                            }
                            OButton {
                                text: qsTr("Forward")
                                glyph: "forward"
                                onClicked: backend.newDraft("forward")
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                visible: page.messages.length === 0
                anchors.centerIn: parent
                width: Math.min(350, parent.width - 48)
                spacing: 16
                OIcon {
                    name: "mail"
                    tint: theme.muted
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                }
                OLabel {
                    text: qsTr("Room for what matters")
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
                OLabel {
                    text: qsTr("Choose a conversation to read, or start a new one.")
                    color: theme.muted
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
            }
        }
    }
    Dialog {
        id: topicDialog
        anchors.centerIn: parent
        width: Math.min(400, page.width - 32)
        title: qsTr("Topic for this message")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: backend.setTopic(page.latest.id, topicField.text)
        contentItem: OField {
            id: topicField
            placeholderText: qsTr("Topic name, or empty to clear")
        }
    }
}
