// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QString>

namespace PodStack {

/// Metadados persistidos de um cluster Kubernetes local.
/// `state`: provisioning|installing|ready|stopped|error|deleting|disabled.
struct K8sCluster {
    QString id;
    QString name;
    QString provider = QStringLiteral("k3s-vm");
    QString state = QStringLiteral("disabled");
    int apiPort = 0;
    QString kubeconfigPath;
    QString contextName;
    QString vmMachineId;
    QString createdAt;
    QString updatedAt;
    QString error;

    bool isReady() const { return state == QStringLiteral("ready"); }

    QJsonObject toJson() const;
    static K8sCluster fromJson(const QJsonObject &object);
};

} // namespace PodStack
