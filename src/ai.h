#pragma once
#include "config.h"
#include "http.h"
#include "store.h"
#include <QObject>

using AiCallback = std::function<void(const QJsonObject &, const QString &)>;
class Ai : public QObject {
    Q_OBJECT
  public:
    Ai(Config *config, Store *store, HttpClient *http, QObject *parent = nullptr);
    void models(std::function<void(const QJsonArray &, const QString &)> done);
    void classify(const QJsonObject &message, AiCallback done);
    void summarize(const QJsonArray &thread, bool draft, AiCallback done);
    void ask(const QString &profile, const QString &question, AiCallback done);
    void attachment(const QString &name, const QString &text, const QString &question,
                    AiCallback done);
    static QJsonObject classificationSchema();

  private:
    void generate(const QString &system, const QString &input, const QJsonObject &schema,
                  AiCallback done);
    QString endpointError() const;
    Config *m_config;
    Store *m_store;
    HttpClient *m_http;
};
