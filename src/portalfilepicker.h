#pragma once
#include "filepicker.h"
#include <QVariantMap>
class PortalFilePicker : public FilePicker {
    Q_OBJECT
  public:
    using FilePicker::FilePicker;
    void open(const QString &title, bool multiple, Picked done) override;
    void save(const QString &name, Picked done) override;
  private slots:
    void response(uint code, const QVariantMap &results);

  private:
    void request(const QString &method, const QString &title, QVariantMap options, Picked done);
    Picked m_done;
    QString m_path;
};
