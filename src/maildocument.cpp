#include "maildocument.h"
#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QRegularExpression>
MailDocument::MailDocument(QQuickItem *parent) : QQuickPaintedItem(parent) {
    connect(this, &QQuickItem::widthChanged, this, &MailDocument::layout);
}
void MailDocument::setHtml(const QString &v) {
    if (m_html == v)
        return;
    m_html = v;
    layout();
    emit changed();
}
void MailDocument::setTextColor(const QColor &v) {
    m_color = v;
    layout();
    emit changed();
}
void MailDocument::setTextSize(int v) {
    m_size = v;
    layout();
    emit changed();
}
void MailDocument::layout() {
    QFont font;
    font.setPixelSize(m_size);
    m_document.setDefaultFont(font);
    m_document.setDefaultStyleSheet("body { color:" + m_color.name() + "; } a {color:" +
                                    m_color.name() + ";} img {width:0;height:0;}");
    // QTextDocument executes no scripts. Its resource loader is disabled,
    // including file URLs.
    QString safe = m_html;
    safe.remove(QRegularExpression("<img\\b[^>]*>", QRegularExpression::CaseInsensitiveOption));
    m_document.setHtml(safe);
    m_document.setTextWidth(qMax(1.0, width()));
    setImplicitHeight(m_document.size().height());
    update();
}
void MailDocument::paint(QPainter *painter) {
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, m_color);
    m_document.documentLayout()->draw(painter, context);
}
QString MailDocument::linkAt(qreal x, qreal y) const {
    return m_document.documentLayout()->anchorAt(QPointF(x, y));
}
