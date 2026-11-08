#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace PodStack {

/// Configuração mínima do daemon/CLI (schema completo em docs/settings.md).
class Config {
public:
    Config();
    static Config load();
    bool save(QString *error = nullptr) const;

    QJsonValue get(const QString &key) const;
    void set(const QString &key, const QJsonValue &value);

    QJsonObject toJson() const { return m_values; }

private:
    QJsonObject m_values;

    static QJsonObject defaults();
};

} // namespace PodStack
