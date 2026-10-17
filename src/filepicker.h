#pragma once
#include <QObject>
#include <QStringList>
#include <functional>
using Picked = std::function<void(const QStringList &, const QString &)>;
class FilePicker : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void open(const QString &title, bool multiple, Picked done) = 0;
    virtual void save(const QString &name, Picked done) = 0;
};
