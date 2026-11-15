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
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY lastErrorCodeChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
    Q_PROPERTY(QString filesPath READ filesPath NOTIFY filesChanged)
    Q_PROPERTY(QStringList logLines READ logLines NOTIFY logsChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(QVariantList cpuHistory READ cpuHistory NOTIFY statsChanged)
    Q_PROPERTY(QVariantList memHistory READ memHistory NOTIFY statsChanged)

public:
    explicit ContainerController(QObject *parent = nullptr);

    QVariantList files() const { return m_files; }
    bool loading() const { return m_loading; }
    /// Código estável do último erro de ação (ex.: `PODSTACK_IMAGE_NOT_FOUND`).
    QString lastErrorCode() const { return m_lastErrorCode; }
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
    void loadingChanged();
    void lastErrorCodeChanged();
    void logsChanged();
    void statsChanged();
    void connectionChanged(bool connected, const QString &error);
    void terminalRequested(const QString &title, const QString &program, const QStringList &args);
    /// Emitido logo depois de `containers.create` para a página selecionar e
    /// abrir o container recém-criado.
    void containerCreated(const QString &id);
    /// Emitido quando uma ação falha, com o código estável e a mensagem pronta
    /// para a UI (inclui a remediação). A criação usa isso para manter o
    /// diálogo aberto e oferecer o pull quando falta a imagem.
    void actionFailed(const QString &method, const QString &code, const QString &message);
    /// Emitido ao fim de toda ação (sucesso ou falha), com o método e o id do
    /// alvo. A UI usa para fechar o diálogo e reconciliar **só** o recurso
    /// afetado, em vez de reagir a qualquer ação do app.
    void actionCompleted(const QString &method, const QString &id, bool ok);
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
    bool m_loading = false;
    QString m_lastErrorCode;
    QVariantMap m_stats;
    QVariantList m_cpuHistory;
    QVariantList m_memHistory;
    StreamClient m_stream;
    QTimer *m_logsTimer = nullptr;
    bool m_logsPending = false;
};

} // namespace PodStack
