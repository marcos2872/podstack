// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/Error.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PodStack {

/// Definição de uma chave de configuração. A lista completa é a fonte única
/// usada por `Config`, pelo daemon (`settings.*`) e pela GUI/CLI.
struct SettingDef {
    QString key;
    QString type; // bool | int | int64 | string | stringList | enum | timestamp
    QJsonValue defaultValue;
    QStringList enumValues;
    double min = 0;
    double max = 0;
    bool restartRequired = false;
    QString category;
    QString description;
    bool secret = false;
};

namespace Settings {

/// Definições na ordem documentada em docs/settings.md (86 chaves).
const QVector<SettingDef> &definitions();

const SettingDef *find(const QString &key);

/// Objeto aninhado com os defaults (sem `schemaVersion`).
QJsonObject defaults();

/// Mapa plano `chave -> default`, usado pelo RPC e pela GUI.
QJsonObject defaultValues();

/// Array JSON no formato devolvido por `settings.schema`.
QJsonObject schemaJson();

/// Valida tipo, faixa, enum, listas e regras específicas (proxy/CIDR).
/// Erros usam `PODSTACK_VALIDATION` e detail igual à chave.
bool validate(const SettingDef &definition, const QJsonValue &value, Error *error);

} // namespace Settings

} // namespace PodStack
