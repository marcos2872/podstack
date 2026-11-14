#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace PodStack {

/// Configuração do daemon/CLI. O schema completo (86 chaves) vive em
/// `core/Settings.h` e gera os defaults usados aqui.
class Config {
public:
    Config();
    /// Carrega do disco e aplica migrações de schema (ex.: portas antigas do proxy).
    static Config load();
    bool save(QString *error = nullptr) const;

    QJsonValue get(const QString &key) const;
    void set(const QString &key, const QJsonValue &value);

    /// Restaura uma chave para o default do schema.
    bool reset(const QString &key, Error *error = nullptr);
    /// Restaura todas as chaves de configuração, preservando `schemaVersion` e
    /// todo o bloco `setup.*` (estado do onboarding).
    void resetAll();

    /// Valida uma chave contra o schema central (tipo, enum, faixa, listas).
    static bool validate(const QString &key, const QJsonValue &value, Error *error = nullptr);

    static QJsonObject defaults();
    static QJsonObject schema();

    /// Mapa plano `chave -> valor` com as chaves conhecidas (ou apenas uma).
    QJsonObject flatValues(const QString &onlyKey = {}) const;

    QJsonObject toJson() const { return m_values; }

private:
    QJsonObject m_values;
};

} // namespace PodStack
