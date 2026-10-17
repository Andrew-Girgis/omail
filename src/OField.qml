import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

TextField {
    id: field
    implicitHeight: Math.max(42, backend.settings.fontSize + 20)
    color: theme.foreground
    placeholderTextColor: theme.muted
    selectionColor: theme.accent
    selectedTextColor: theme.accentForeground
    font.pixelSize: backend.settings.fontSize
    font.family: backend.settings.fontFamily || Qt.application.font.family
    leftPadding: 12
    rightPadding: 12
    Material.roundedScale: theme.radius
    background: Rectangle {
        color: theme.panel
        radius: theme.radius
        border.width: 1
        border.color: field.activeFocus ? theme.accent : theme.border
    }
}
