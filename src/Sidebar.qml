import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    color: theme.background
    signal settingsRequested
    signal composeRequested
    signal viewChosen
    ScrollView {
        id: sidebarScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: sidebarContent.implicitHeight + 32
        ColumnLayout {
            id: sidebarContent
            x: 16
            y: 16
            width: sidebarScroll.availableWidth - 32
            height: implicitHeight
            spacing: 6
            OButton {
                objectName: "composeButton"
                text: qsTr("Compose")
                glyph: "plus"
                primary: true
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 24
                onClicked: sidebar.composeRequested()
            }
            OLabel {
                text: qsTr("PROFILES")
                font.pixelSize: 10
                font.letterSpacing: 1.5
                color: theme.muted
                Layout.leftMargin: 8
                Layout.bottomMargin: 6
            }
            Repeater {
                model: backend.data.profiles
                ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 38
                    leftPadding: 10
                    background: Rectangle {
                        color: backend.data.profile === modelData.id ? theme.panel : parent.hovered ? Qt.alpha(theme.foreground, .035) : "transparent"
                        radius: theme.radius
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: modelData.color
                        }
                        OLabel {
                            text: modelData.name
                            Layout.fillWidth: true
                            font.weight: backend.data.profile === modelData.id ? Font.DemiBold : Font.Normal
                        }
                        OIcon {
                            visible: backend.data.profile === modelData.id
                            name: "check"
                            tint: theme.muted
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                        }
                    }
                    onClicked: {
                        backend.selectProfile(modelData.id);
                        sidebar.viewChosen();
                    }
                }
            }
            OButton {
                text: qsTr("All profiles")
                glyph: "folder"
                selected: backend.data.profile === "all"
                Layout.fillWidth: true
                onClicked: {
                    backend.selectProfile("all");
                    sidebar.viewChosen();
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.border
                Layout.topMargin: 16
                Layout.bottomMargin: 16
            }
            Repeater {
                model: [
                    {
                        id: "inbox",
                        text: qsTr("Inbox"),
                        icon: "inbox"
                    },
                    {
                        id: "priority",
                        text: qsTr("Priority"),
                        icon: "priority"
                    },
                    {
                        id: "flagged",
                        text: qsTr("Flagged"),
                        icon: "flag"
                    },
                    {
                        id: "drafts",
                        text: qsTr("Drafts & scheduled"),
                        icon: "draft"
                    },
                    {
                        id: "sent",
                        text: qsTr("Sent"),
                        icon: "send"
                    },
                    {
                        id: "archive",
                        text: qsTr("Archive"),
                        icon: "archive"
                    },
                    {
                        id: "junk",
                        text: qsTr("Junk"),
                        icon: "junk"
                    },
                    {
                        id: "trash",
                        text: qsTr("Trash"),
                        icon: "trash"
                    },
                    {
                        id: "all",
                        text: qsTr("All mail"),
                        icon: "mail"
                    }
                ].filter(v => backend.settings.viewOrder.split(",").includes(v.id)).sort((a, b) => backend.settings.viewOrder.split(",").indexOf(a.id) - backend.settings.viewOrder.split(",").indexOf(b.id))
                ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: backend.settings.density === "compact" ? 32 : 38
                    leftPadding: 10
                    rightPadding: 12
                    background: Rectangle {
                        radius: theme.radius
                        color: backend.data.folder === modelData.id ? Qt.alpha(theme.accent, .12) : parent.hovered ? theme.panel : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 12
                        OIcon {
                            name: modelData.icon
                            tint: backend.data.folder === modelData.id ? theme.accent : theme.muted
                            Layout.preferredWidth: 17
                            Layout.preferredHeight: 17
                        }
                        OLabel {
                            text: modelData.text
                            Layout.fillWidth: true
                            font.pixelSize: backend.settings.fontSize - 1
                            color: backend.data.folder === modelData.id ? theme.accent : theme.foreground
                        }
                        OLabel {
                            property int count: modelData.id === "drafts" ? backend.data.drafts.length : (backend.data.counts[modelData.id] || 0)
                            text: count ? count.toLocaleString(Qt.locale(), "f", 0) : ""
                            color: backend.data.folder === modelData.id ? theme.accent : theme.muted
                            font.pixelSize: 11
                        }
                    }
                    onClicked: {
                        backend.selectFolder(modelData.id);
                        sidebar.viewChosen();
                    }
                }
            }
            OLabel {
                visible: backend.settings.showTopics && backend.data.topics.length > 0
                text: qsTr("TOPICS")
                font.pixelSize: 10
                font.letterSpacing: 1.5
                color: theme.muted
                Layout.leftMargin: 8
                Layout.topMargin: 18
                Layout.bottomMargin: 6
            }
            Repeater {
                model: backend.settings.showTopics ? backend.data.topics : []
                OButton {
                    required property var modelData
                    text: modelData.topic
                    glyph: "tag"
                    selected: backend.data.folder === "topic:" + modelData.topic
                    Layout.fillWidth: true
                    onClicked: {
                        backend.selectFolder("topic:" + modelData.topic);
                        sidebar.viewChosen();
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
            OLabel {
                visible: backend.data.pendingClassification > 0
                Layout.fillWidth: true
                text: qsTr("%n messages awaiting AI", "", backend.data.pendingClassification)
                color: theme.muted
                font.pixelSize: 11
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
            OButton {
                text: qsTr("Manage accounts")
                glyph: "settings"
                Layout.fillWidth: true
                onClicked: sidebar.settingsRequested()
            }
        }
    }
}
