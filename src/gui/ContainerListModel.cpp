#include "gui/ContainerListModel.h"

namespace PodStack {

ContainerListModel::ContainerListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ContainerListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_items.size();
}

QVariant ContainerListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const QVariantMap &item = m_items.at(index.row());
    switch (role) {
    case NameRole:
        return item.value(QStringLiteral("name"));
    case StateRole:
        return item.value(QStringLiteral("state"));
    case ImageRole:
        return item.value(QStringLiteral("image"));
    case PortsRole:
        return item.value(QStringLiteral("ports"));
    case IdRole:
        return item.value(QStringLiteral("id"));
    case HealthRole:
        return item.value(QStringLiteral("health"));
    case StatusRole:
        return item.value(QStringLiteral("status"));
    default:
        return {};
    }
}

QHash<int, QByteArray> ContainerListModel::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {StateRole, QByteArrayLiteral("state")},
        {ImageRole, QByteArrayLiteral("image")},
        {PortsRole, QByteArrayLiteral("ports")},
        {IdRole, QByteArrayLiteral("id")},
        {HealthRole, QByteArrayLiteral("health")},
        {StatusRole, QByteArrayLiteral("status")},
    };
}

QVariantMap ContainerListModel::get(int row) const
{
    if (row < 0 || row >= m_items.size()) {
        return {};
    }
    return m_items.at(row);
}

void ContainerListModel::setContainers(const QVariantList &items)
{
    beginResetModel();
    m_items.clear();
    m_items.reserve(items.size());
    for (const QVariant &item : items) {
        m_items.append(item.toMap());
    }
    endResetModel();
    ++m_revision;
    emit revisionChanged();
    emit countChanged();
}

} // namespace PodStack
