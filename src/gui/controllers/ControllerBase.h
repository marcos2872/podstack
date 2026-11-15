// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace PodStack {

/// Base dos controllers de domínio da GUI.
///
/// Concentra o que era repetido no Backend monolítico: chamadas RPC
/// assíncronas, fila de ações com `actionActive` e o último erro exibível.
/// Cada controller é dono do seu próprio estado; o `Backend` apenas agrega.
class ControllerBase : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool actionActive READ actionActive NOTIFY actionStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    /// Alvos com ação em andamento: chave = id/nome do recurso, valor = método.
    /// Permite desabilitar e mostrar progresso **por item**, não por controller.
    Q_PROPERTY(QVariantMap runningTargets READ runningTargets NOTIFY runningTargetsChanged)

public:
    explicit ControllerBase(QObject *parent = nullptr);

    bool actionActive() const { return m_actionActive; }
    QString lastError() const { return m_lastError; }
    QVariantMap runningTargets() const { return m_runningTargets; }
    Q_INVOKABLE bool isRunning(const QString &target) const;

    void clearLastError();

    /// Limpa o último erro exibido (botão "Dispensar" do banner da página).
    Q_INVOKABLE void dismissError() { clearLastError(); }

signals:
    void actionStateChanged();
    void lastErrorChanged();
    void runningTargetsChanged();
    /// Emitido ao fim de toda ação (sucesso, falha ou lote parcial), sempre no
    /// mesmo formato. É o que permite à UI fechar o diálogo e mostrar o erro
    /// sem depender do sinal específico de cada área.
    void actionSettled(const QString &method, bool ok, const QString &message);

protected:
    using Callback = std::function<void(const QJsonObject &)>;
    using Job = std::function<QJsonObject()>;

    void callRpc(const QString &method, const QJsonObject &params, Callback done,
                 int timeoutMs = 30000);
    void runAction(const QString &method, const QJsonObject &params, Callback done = {},
                   int timeoutMs = 30000);
    /// Executa um job local (sem RPC) pela mesma fila de ações.
    void runJob(const QString &label, const QJsonObject &params, Job job, Callback done = {},
                int timeoutMs = 30000);

    struct PendingAction {
        QString method;
        QJsonObject params;
        int timeoutMs = 30000;
        Job job;
    };
    void enqueueAction(const PendingAction &action);
    void startAction(const PendingAction &action, Callback done);

    virtual void onActionFinished(const QString &method, const QJsonObject &params,
                                  const QJsonObject &result);

    void setLastError(const QString &message);

    /// Limite de linhas mantidas pelos buffers de log (setting logs.bufferLines).
    static int logBufferLines();
    static void setLogBufferLines(int lines);
    static QString errorMessage(const QJsonObject &result);
    static bool isErrorResult(const QJsonObject &result);
    /// Resultado sintético de `runBatchCalls` (`count`/`failed`): a falha de um
    /// item não é falha do método.
    static bool isBatchResult(const QJsonObject &result);
    /// "3 de 5 itens falharam. <detalhe>" ou vazio quando nada falhou.
    static QString batchSummary(const QJsonObject &result);

    /// Loop síncrono de uma ação em lote (`prefix + action` por id).
    static QJsonObject runBatchCalls(const QString &prefix, const QString &idKey,
                                     const QString &action, const QStringList &ids, bool force,
                                     const QJsonObject &extra);

    bool m_actionActive = false;
    QQueue<PendingAction> m_actionQueue;
    PendingAction m_currentAction;
    QString m_lastError;
    QVariantMap m_runningTargets;

private:
    /// Chave do alvo de uma ação (`id`, `name`, `projectPath`...).
    static QString actionTarget(const QJsonObject &params);
    /// Já existe a mesma ação, no mesmo alvo, em andamento ou na fila?
    bool hasPending(const QString &method, const QString &target) const;
};

} // namespace PodStack
