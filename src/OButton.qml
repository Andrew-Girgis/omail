import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Button {
    id: control
    property string glyph: ""
    property bool primary: false
    property bool selected: false
    property string tip: ""
    implicitHeight: Math.max(36, backend.settings.fontSize + 14)
    implicitWidth: text ? Math.ceil(textMetrics.width) + (glyph ? 26 : 0) + 28 : 36
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    padding: 10
    leftPadding: text ? 12 : 9
    rightPadding: text ? 12 : 9
    topPadding: 6
    bottomPadding: 6
    Material.roundedScale: theme.radius
    font.pixelSize: backend.settings.fontSize
    font.weight: primary || selected ? Font.DemiBold : Font.Normal
    TextMetrics {
        id: textMetrics
        text: control.text
        font: control.font
    }
    Accessible.name: text || tip
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
    ToolTip.delay: 650
    background: Rectangle {
        radius: theme.radius
        color: control.primary ? theme.accent : control.selected ? Qt.alpha(theme.accent, .15) : control.hovered ? theme.panel : "transparent"
        border.width: control.activeFocus ? 1 : 0
        border.color: theme.accent
        opacity: control.enabled ? 1 : .4
    }
    contentItem: RowLayout {
        spacing: 8
        opacity: control.enabled ? 1 : .4
        OIcon {
            visible: control.glyph.length > 0
            name: control.glyph
            tint: control.primary ? theme.accentForeground : control.selected ? theme.accent : theme.foreground
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
        }
        OLabel {
            visible: control.text.length > 0
            text: control.text
            color: control.primary ? theme.accentForeground : control.selected ? theme.accent : theme.foreground
            font.weight: control.primary || control.selected ? Font.DemiBold : Font.Normal
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
    }
}
