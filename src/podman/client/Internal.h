// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/error/Error.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>

namespace PodStack::PodmanInternal {

QString encodeId(const QString &id);
int jsonInt(const QJsonValue &value, int fallback = 0);
int jsonInt(const QJsonObject &object, const QString &primary, const QString &alternate);
QString jsonTimestamp(const QJsonValue &value);
QString encodePath(const QString &path);
QString normalizeEntryName(QString name);
QStringList stringListFromJson(const QJsonArray &array);
QJsonArray jsonFromStringList(const QStringList &values);
QMap<QString, QString> labelsFromJson(const QJsonObject &object);
QJsonObject jsonFromLabels(const QMap<QString, QString> &labels);
QString statsHumanBytes(qint64 bytes);
QJsonObject formatContainerStats(const QJsonObject &raw);
bool buildContextTar(const QString &contextPath, QByteArray *tar, Error *error);

} // namespace PodStack::PodmanInternal
