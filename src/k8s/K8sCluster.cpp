// SPDX-License-Identifier: GPL-3.0-or-later

#include "k8s/K8sCluster.h"

namespace PodStack {

QJsonObject K8sCluster::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("provider"), provider);
    object.insert(QStringLiteral("state"), state);
    object.insert(QStringLiteral("apiPort"), apiPort);
    object.insert(QStringLiteral("kubeconfigPath"), kubeconfigPath);
    object.insert(QStringLiteral("contextName"), contextName);
    object.insert(QStringLiteral("vmMachineId"), vmMachineId);
    object.insert(QStringLiteral("createdAt"), createdAt);
    object.insert(QStringLiteral("updatedAt"), updatedAt);
    object.insert(QStringLiteral("error"), error);
    return object;
}

K8sCluster K8sCluster::fromJson(const QJsonObject &object)
{
    K8sCluster cluster;
    cluster.id = object.value(QStringLiteral("id")).toString();
    cluster.name = object.value(QStringLiteral("name")).toString();
    cluster.provider = object.value(QStringLiteral("provider")).toString(QStringLiteral("k3s-vm"));
    cluster.state = object.value(QStringLiteral("state")).toString(QStringLiteral("disabled"));
    cluster.apiPort = object.value(QStringLiteral("apiPort")).toInt();
    cluster.kubeconfigPath = object.value(QStringLiteral("kubeconfigPath")).toString();
    cluster.contextName = object.value(QStringLiteral("contextName")).toString();
    cluster.vmMachineId = object.value(QStringLiteral("vmMachineId")).toString();
    cluster.createdAt = object.value(QStringLiteral("createdAt")).toString();
    cluster.updatedAt = object.value(QStringLiteral("updatedAt")).toString();
    cluster.error = object.value(QStringLiteral("error")).toString();
    return cluster;
}

} // namespace PodStack
