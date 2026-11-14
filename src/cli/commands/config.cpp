// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/Internal.h"

namespace PodStack::CliInternal {

int commandConfigShow(const QStringList &args)
{
    bool json = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json") || arg == QStringLiteral("--format=json")) {
            json = true;
        } else if (arg.startsWith(QStringLiteral("--format="))) {
            const QString format = arg.mid(9);
            if (format != QStringLiteral("json") && format != QStringLiteral("text")) {
                err() << "formato desconhecido: " << format << '\n';
                return kExitUsage;
            }
            json = format == QStringLiteral("json");
        } else if (arg == QStringLiteral("--format")) {
            // `show --format json` é tratado no laço abaixo.
        } else if (arg == QStringLiteral("text") || arg == QStringLiteral("json")) {
            json = arg == QStringLiteral("json");
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("settings.get"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    if (json) {
        out() << QJsonDocument(values).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        out() << it.key() << '=' << formatConfigValue(it.value()) << '\n';
    }
    out().flush();
    return kExitOk;
}

int commandConfigGet(const QStringList &args)
{
    QString key;
    bool json = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else if (key.isEmpty() && !arg.startsWith(QStringLiteral("-"))) {
            key = arg;
        } else {
            err() << "argumento inesperado: " << arg << '\n';
            return kExitUsage;
        }
    }
    if (key.isEmpty()) {
        err() << "Uso: podstack config get <chave> [--json]\n";
        return kExitUsage;
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("settings.get"),
                                           QJsonObject{{QStringLiteral("key"), key}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }
    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    if (json) {
        out() << QJsonDocument(values).toJson(QJsonDocument::Indented);
    } else {
        out() << formatConfigValue(values.value(key)) << '\n';
    }
    out().flush();
    return kExitOk;
}

int commandConfigSet(const QStringList &args)
{
    if (args.size() != 2) {
        err() << "Uso: podstack config set <chave> <valor>\n";
        return kExitUsage;
    }
    const QString &key = args.at(0);
    const QString &raw = args.at(1);

    QHash<QString, QJsonObject> schema;
    PodStack::Error error;
    if (!fetchConfigSchema(&schema, &error)) {
        return fail(error);
    }
    if (!schema.contains(key)) {
        err() << "chave desconhecida: " << key << '\n';
        err() << "  ação: consulte `podstack config schema`.\n";
        err().flush();
        return kExitError;
    }

    QJsonValue value;
    QString parseError;
    if (!parseConfigValue(schema.value(key), raw, &value, &parseError)) {
        err() << "valor inválido para " << key << ": " << parseError << '\n';
        err().flush();
        return kExitUsage;
    }

    PodStack::RpcClient client;
    const QJsonObject result = client.call(
        QStringLiteral("settings.set"),
        QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("value"), value}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    out() << key << '=' << formatConfigValue(values.value(key)) << '\n';
    if (result.value(QStringLiteral("restartRequired")).toBool()) {
        out() << "reinício necessário: reinicie o podstackd para aplicar.\n";
    }
    out().flush();
    return kExitOk;
}

int commandConfigList(const QString &operation, const QStringList &args)
{
    if (args.size() != 2) {
        err() << "Uso: podstack config " << operation << " <chave> <valor>\n";
        return kExitUsage;
    }
    const QString &key = args.at(0);
    const QString entry = args.at(1).trimmed();
    if (entry.isEmpty()) {
        err() << "valor vazio não é permitido.\n";
        return kExitUsage;
    }

    QHash<QString, QJsonObject> schema;
    PodStack::Error error;
    if (!fetchConfigSchema(&schema, &error)) {
        return fail(error);
    }
    if (!schema.contains(key)) {
        err() << "chave desconhecida: " << key << '\n';
        err().flush();
        return kExitError;
    }
    if (schema.value(key).value(QStringLiteral("type")).toString() !=
        QStringLiteral("stringList")) {
        err() << key << " não é uma lista de strings.\n";
        err().flush();
        return kExitError;
    }

    PodStack::RpcClient client;
    const QJsonObject current = client.call(QStringLiteral("settings.get"),
                                            QJsonObject{{QStringLiteral("key"), key}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }
    QJsonArray entries = current.value(QStringLiteral("values")).toObject().value(key).toArray();

    if (operation == QStringLiteral("add")) {
        bool present = false;
        for (const QJsonValue &value : entries) {
            if (value.toString() == entry) {
                present = true;
                break;
            }
        }
        if (!present) {
            entries.append(entry);
        }
    } else {
        QJsonArray filtered;
        bool removed = false;
        for (const QJsonValue &value : entries) {
            if (value.toString() == entry) {
                removed = true;
                continue;
            }
            filtered.append(value);
        }
        if (!removed) {
            err() << "valor não encontrado em " << key << ": " << entry << '\n';
            err().flush();
            return kExitError;
        }
        entries = filtered;
    }

    const QJsonObject result = client.call(
        QStringLiteral("settings.set"),
        QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("value"), entries}}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    const QJsonObject values = result.value(QStringLiteral("values")).toObject();
    out() << key << '=' << formatConfigValue(values.value(key)) << '\n';
    if (result.value(QStringLiteral("restartRequired")).toBool()) {
        out() << "reinício necessário: reinicie o podstackd para aplicar.\n";
    }
    out().flush();
    return kExitOk;
}

int commandConfigReset(const QStringList &args)
{
    QString key;
    bool yes = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--yes") || arg == QStringLiteral("-y")) {
            yes = true;
        } else if (arg == QStringLiteral("--all")) {
            key.clear();
        } else if (key.isEmpty() && !arg.startsWith(QStringLiteral("-"))) {
            key = arg;
        } else {
            err() << "argumento inesperado: " << arg << '\n';
            return kExitUsage;
        }
    }

    if (!yes) {
        if (!isatty(STDIN_FILENO)) {
            err() << "confirmação necessária: use --yes em modo não interativo.\n";
            err().flush();
            return kExitUsage;
        }
        out() << (key.isEmpty() ? QStringLiteral("Restaurar todas as configurações? [s/N] ")
                                : QStringLiteral("Restaurar %1 para o padrão? [s/N] ").arg(key));
        out().flush();
        std::string answer;
        std::getline(std::cin, answer);
        QString normalized = QString::fromStdString(answer).trimmed().toLower();
        if (normalized != QStringLiteral("s") && normalized != QStringLiteral("sim") &&
            normalized != QStringLiteral("y") && normalized != QStringLiteral("yes")) {
            out() << "cancelado\n";
            out().flush();
            return kExitOk;
        }
    }

    QJsonObject params;
    if (!key.isEmpty()) {
        params.insert(QStringLiteral("key"), key);
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("settings.reset"), params, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    out() << (key.isEmpty() ? QStringLiteral("todas as configurações restauradas")
                            : QStringLiteral("%1 restaurada").arg(key))
          << '\n';
    if (result.value(QStringLiteral("restartRequired")).toBool()) {
        out() << "reinício necessário: reinicie o podstackd para aplicar.\n";
    }
    out().flush();
    return kExitOk;
}

int commandConfigSchema(const QStringList &args)
{
    bool json = false;
    for (const QString &arg : args) {
        if (arg == QStringLiteral("--json")) {
            json = true;
        } else {
            err() << "opção desconhecida: " << arg << '\n';
            return kExitUsage;
        }
    }

    PodStack::RpcClient client;
    PodStack::Error error;
    const QJsonObject result = client.call(QStringLiteral("settings.schema"), {}, &error);
    if (!error.isEmpty()) {
        return fail(error);
    }

    if (json) {
        out() << QJsonDocument(result).toJson(QJsonDocument::Indented);
        out().flush();
        return kExitOk;
    }

    for (const QJsonValue &value : result.value(QStringLiteral("schema")).toArray()) {
        const QJsonObject definition = value.toObject();
        out() << definition.value(QStringLiteral("key")).toString() << '\t'
              << definition.value(QStringLiteral("type")).toString() << '\t'
              << formatConfigValue(definition.value(QStringLiteral("default"))) << '\n';
    }
    out().flush();
    return kExitOk;
}

int commandConfig(const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("show") : args.first();
    const QStringList rest = args.mid(1);

    if (sub == QStringLiteral("--help") || sub == QStringLiteral("-h") ||
        sub == QStringLiteral("help")) {
        printConfigUsage(out());
        return kExitOk;
    }
    if (sub == QStringLiteral("show") || sub == QStringLiteral("ls") ||
        sub == QStringLiteral("list")) {
        return commandConfigShow(rest);
    }
    if (sub == QStringLiteral("get")) {
        return commandConfigGet(rest);
    }
    if (sub == QStringLiteral("set")) {
        return commandConfigSet(rest);
    }
    if (sub == QStringLiteral("add")) {
        return commandConfigList(QStringLiteral("add"), rest);
    }
    if (sub == QStringLiteral("remove") || sub == QStringLiteral("rm")) {
        return commandConfigList(QStringLiteral("remove"), rest);
    }
    if (sub == QStringLiteral("reset")) {
        return commandConfigReset(rest);
    }
    if (sub == QStringLiteral("schema")) {
        return commandConfigSchema(rest);
    }

    err() << "subcomando desconhecido: config " << sub << '\n';
    printConfigUsage(err());
    return kExitUsage;
}

} // namespace PodStack::CliInternal
