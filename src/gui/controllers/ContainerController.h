// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/rpc/StreamClient.h"
#include "gui/controllers/CatalogController.h"

#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace PodStack {

class ContainerController : public CatalogController {
    Q_OBJECT
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QString filesPath READ filesPath NOTIFY filesChanged)
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList memHistory READ memHistory NOTIFY statsChanged)

public:
    explicit ContainerController(QObject *parent = nullptr);

    QVariantList files() const { return m_files; }
    QString filesPath() const { return m_filesPath; }
    QStringList logLines() const { return m_logLines; }
    QVariantMap stats() const { return m_stats; }
    QVariantList cpuHistory() const { return m_cpuHistory; }
    QVariantList memHistory() const { return m_memHistory; }

    Q_INVOKABLE void refresh() override;

    Q_INVOKABLE void lifecycle(const QString &action, const QString &id);
    Q_INVOKABLE void removeContainer(const QString &id, bool force, bool volumes);
    Q_INVOKABLE void createContainer(const QVariantMap &spec, bool start);
    Q_INVOKABLE void exposeContainerLan(const QString &id, bool expose);
    Q_INVOKABLE void renameContainer(const QString &id, const QString &name);
    Q_INVOKABLE void duplicateContainer(const QString &id);
    Q_INVOKABLE void batchAll(const QString &action);
    Q_INVOKABLE void batchIds(const QStringList &ids, const QString &action);

    Q_INVOKABLE void listFiles(const QString &id, const QString &path);
    Q_INVOKABLE void downloadFile(const QString &id, const QString &path);
    Q_INVOKABLE void uploadFile(const QString &id, const QString &path, const QString &sourcePath);
    Q_INVOKABLE void openTerminal(const QString &id);

    Q_INVOKABLE void subscribeLogs(const QString &id, int tail, bool follow);
    Q_INVOKABLE void unsubscribeLogs();
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE void subscribeStats(const QString &id);
    Q_INVOKABLE void unsubscribeStats();

signals:
    void filesChanged();
    void logsChanged();
    void statsChanged();
    void connectionChanged(bool connected, const QString &error);
    void terminalRequested(const QString &title, const QString &program, const QStringList &args);
    /// Emitido depois de uma ação bem-sucedida para o hub recarregar catálogos.
    void catalogDirty();

protected:
    QString listMethod() const override { return QStringLiteral("containers.list"); }
    QJsonObject listParams() const override { return QJsonObject{{QStringLiteral("all"), true}}; }
    QVariantList parseItems(const QJsonArray &items) const override;
    void onActionFinished(const QString &method, const QJsonObject &params,
                          const QJsonObject &result) override;

private:
    QVariantList m_files;
    QString m_filesPath = QStringLiteral("/");
    QStringList m_logLines;
    QVariantMap m_stats;
    QVariantList m_cpuHistory;
    QVariantList m_memHistory;
    StreamClient m_stream;
    QTimer *m_logsTimer = nullptr;
    bool m_logsPending = false;
};

} // namespace PodStack
