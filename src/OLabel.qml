import QtQuick

Text {
    color: theme.foreground
    font.pixelSize: backend.settings.fontSize
    font.family: backend.settings.fontFamily || Qt.application.font.family
    textFormat: Text.PlainText
    elide: Text.ElideRight
}
