#pragma once
#include <QQuickPaintedItem>
#include <QTextDocument>
class SafeDocument : public QTextDocument {
  public:
    using QTextDocument::QTextDocument;

  protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};
class MailDocument : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QString html READ html WRITE setHtml NOTIFY changed)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY changed)
    Q_PROPERTY(int textSize READ textSize WRITE setTextSize NOTIFY changed)
  public:
    explicit MailDocument(QQuickItem *parent = nullptr);
    QString html() const { return m_html; }
    QColor textColor() const { return m_color; }
    int textSize() const { return m_size; }
    void setHtml(const QString &v);
    void setTextColor(const QColor &v);
    void setTextSize(int v);
    void paint(QPainter *) override;
    Q_INVOKABLE QString linkAt(qreal x, qreal y) const;
  signals:
    void changed();

  private:
    void layout();
    SafeDocument m_document;
    QString m_html;
    QColor m_color = "#ffffff";
    int m_size = 14;
};
