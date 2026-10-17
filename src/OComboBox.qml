import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ComboBox {
    id: control

    property string emptyText: qsTr("No options available")

    enabled: count > 0
    displayText: count > 0 ? currentText : emptyText
    font.pixelSize: backend.settings.fontSize
    font.family: backend.settings.fontFamily || Qt.application.font.family
    Material.roundedScale: theme.radius
    Accessible.name: displayText
    onCountChanged: {
        if (count === 0)
            popup.close();

    }
    popup.y: control.height + 4
    popup.background: Rectangle {
        color: theme.panel
        radius: theme.radius
        border.width: 1
        border.color: theme.border
    }

    contentItem: OLabel {
        text: control.displayText
        color: control.enabled ? theme.foreground : theme.muted
        verticalAlignment: Text.AlignVCenter
        leftPadding: 12
        rightPadding: 8
    }

    indicator: OIcon {
        name: "down"
        visible: control.count > 0
        tint: control.enabled ? theme.foreground : theme.muted
        x: control.mirrored ? 10 : control.width - width - 10
        y: (control.height - height) / 2
        width: 16
        height: 16
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: Math.max(44, backend.settings.fontSize + 24)
        color: theme.panel
        radius: theme.radius
        border.width: 1
        border.color: control.activeFocus && control.enabled ? theme.accent : theme.border
    }

}
