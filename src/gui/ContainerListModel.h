#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PodStack {

class ContainerListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        StateRole,
        ImageRole,
        PortsRole,
        IdRole,
        HealthRole,
    };

    explicit ContainerListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    int revision() const { return m_revision; }
    Q_INVOKABLE QVariantMap get(int row) const;

public slots:
    void setContainers(const QVariantList &items);

signals:
    void countChanged();
    void revisionChanged();

private:
    QVector<QVariantMap> m_items;
    int m_revision = 0;
};

} // namespace PodStack
