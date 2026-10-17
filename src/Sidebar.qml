import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    signal settingsRequested()
    signal composeRequested()
    signal viewChosen()
    color: theme.background
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
                    onClicked: {
                        backend.selectProfile(modelData.id);
                        sidebar.viewChosen();
                    }
                    background: Rectangle {
                        color: backend.data.profile === modelData.id ? theme.panel : parent.hovered ? Qt.alpha(theme.foreground, 0.035) : "transparent"
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
                model: [{
                    "id": "inbox",
                    "text": qsTr("Inbox"),
                    "icon": "inbox"
                }, {
                    "id": "priority",
                    "text": qsTr("Priority"),
                    "icon": "priority"
                }, {
                    "id": "flagged",
                    "text": qsTr("Flagged"),
                    "icon": "flag"
                }, {
                    "id": "drafts",
                    "text": qsTr("Drafts & scheduled"),
                    "icon": "draft"
                }, {
                    "id": "sent",
                    "text": qsTr("Sent"),
                    "icon": "send"
                }, {
                    "id": "archive",
                    "text": qsTr("Archive"),
                    "icon": "archive"
                }, {
                    "id": "junk",
                    "text": qsTr("Junk"),
                    "icon": "junk"
                }, {
                    "id": "trash",
                    "text": qsTr("Trash"),
                    "icon": "trash"
                }, {
                    "id": "all",
                    "text": qsTr("All mail"),
                    "icon": "mail"
                }].filter((v) => {
                    return backend.settings.viewOrder.split(",").includes(v.id);
                }).sort((a, b) => {
                    return backend.settings.viewOrder.split(",").indexOf(a.id) - backend.settings.viewOrder.split(",").indexOf(b.id);
                })
                Item {
                    id: viewRow
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: backend.settings.density === "compact" ? 32 : 38
                    z: dragHandle.drag.active ? 100 : 0
                    DropArea {
                        anchors.fill: parent
                        keys: ["omail-view"]
                        onDropped: function(drop) {
                            const source = drop.source.viewId;
                            const target = viewRow.modelData.id;
                            if (source !== target) {
                                drop.accept(Qt.MoveAction);
                                // Rebuilding the repeater must wait until the drag has finished.
                                Qt.callLater(function() {
                                    backend.moveView(source, target);
                                });
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: parent.containsDrag && !dragHandle.drag.active
                            color: Qt.alpha(theme.accent, 0.15)
                            radius: theme.radius
                        }
                    }
                    ItemDelegate {
                        id: viewButton
                        property string viewId: viewRow.modelData.id
                        objectName: "mailView-" + viewRow.modelData.id
                        width: viewRow.width
                        height: viewRow.height
                        Drag.active: dragHandle.drag.active
                        Drag.source: viewButton
                        Drag.keys: ["omail-view"]
                        Drag.supportedActions: Qt.MoveAction
                        Drag.hotSpot.x: width - 12
                        Drag.hotSpot.y: height / 2
                        leftPadding: 10
                        rightPadding: 32
                        onClicked: {
                            backend.selectFolder(viewRow.modelData.id);
                            sidebar.viewChosen();
                        }
                        Keys.onPressed: function(event) {
                            if (event.modifiers & Qt.AltModifier) {
                                const ids = backend.settings.viewOrder.split(",");
                                const offset = event.key === Qt.Key_Up ? -1 : event.key === Qt.Key_Down ? 1 : 0;
                                const target = ids.indexOf(viewId) + offset;
                                if (offset && target >= 0 && target < ids.length) {
                                    const source = viewId;
                                    const destination = ids[target];
                                    Qt.callLater(function() {
                                        backend.moveView(source, destination);
                                    });
                                    event.accepted = true;
                                }
                            }
                        }
                        OIcon {
                            anchors.right: parent.right
                            anchors.rightMargin: 7
                            anchors.verticalCenter: parent.verticalCenter
                            name: "grip"
                            width: 15
                            height: 18
                            tint: theme.muted
                            opacity: viewButton.hovered || dragHandle.containsMouse || dragHandle.drag.active ? 1 : 0.4
                        }
                        MouseArea {
                            id: dragHandle
                            objectName: "viewDrag-" + viewRow.modelData.id
                            anchors.right: parent.right
                            width: 28
                            height: parent.height
                            hoverEnabled: true
                            preventStealing: true
                            cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            drag.target: viewButton
                            drag.axis: Drag.YAxis
                            onReleased: {
                                viewButton.Drag.drop();
                                viewButton.y = 0;
                            }
                            onCanceled: viewButton.y = 0
                            ToolTip.visible: containsMouse && !pressed
                            ToolTip.text: qsTr("Drag to reorder · Alt+Up / Alt+Down when focused")
                            ToolTip.delay: 650
                            Accessible.name: qsTr("Reorder %1").arg(viewRow.modelData.text)
                        }
                        background: Rectangle {
                            radius: theme.radius
                            color: dragHandle.drag.active ? theme.panel : backend.data.folder === viewRow.modelData.id ? Qt.alpha(theme.accent, 0.12) : parent.hovered ? theme.panel : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            OIcon {
                                name: viewRow.modelData.icon
                                tint: backend.data.folder === viewRow.modelData.id ? theme.accent : theme.muted
                                Layout.preferredWidth: 17
                                Layout.preferredHeight: 17
                            }
                            OLabel {
                                text: viewRow.modelData.text
                                Layout.fillWidth: true
                                font.pixelSize: backend.settings.fontSize - 1
                                color: backend.data.folder === viewRow.modelData.id ? theme.accent : theme.foreground
                            }
                            OLabel {
                                property int count: viewRow.modelData.id === "drafts" ? backend.data.drafts.length : (backend.data.counts[viewRow.modelData.id] || 0)
                                text: count ? count.toLocaleString(Qt.locale(), "f", 0) : ""
                                color: backend.data.folder === viewRow.modelData.id ? theme.accent : theme.muted
                                font.pixelSize: 11
                            }
                        }
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
                objectName: "manageAccountsButton"
                text: qsTr("Manage accounts")
                glyph: "settings"
                Layout.fillWidth: true
                onClicked: sidebar.settingsRequested()
            }
        }
    }
}
