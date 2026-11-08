# ADR-0001 — Daemon de usuário com JSON-RPC sobre Unix socket

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-08 |
| Decisores | Arquitetura |
| Relacionados | ADR-0002, `architecture.md`, `api-rpc.md` |

## Contexto

GUI, CLI e recursos de longa duração (machines, proxy, DNS, jobs, watchers) precisam de um processo comum. Se a GUI for dona do estado, fechar a janela derruba containers/VMs e o CLI não tem para onde falar. Precisamos de um contrato estável, debugável e fácil de evoluir.

## Decisão

Criar o **`podstackd`**, daemon por usuário (systemd --user, socket activation), que:

- mantém o estado e reconcilia com o Podman;
- expõe **JSON-RPC 2.0** sobre Unix socket em `$XDG_RUNTIME_DIR/podstack/control.sock` (0600);
- autentica por `SO_PEERCRED` (uid do cliente == dono do daemon);
- usa notificações para streams (`logs.delta`, `stats.delta`, `event`, `job.progress`);
- versiona a API com `system.hello`/`apiVersion`.

GUI e CLI são **clientes finos**, sem estado de negócio.

## Alternativas

| Alternativa | Prós | Contras |
|---|---|---|
| gRPC/protobuf | schema forte, codegen, streaming | toolchain extra, complexidade, debug pior |
| D-Bus | integração desktop | verboso, difícil de versionar, menos controle de streams |
| Tudo na GUI (sem daemon) | simples no início | fecha com a janela, CLI não compartilha, watchers morrem |
| HTTP/TCP local | ferramentas prontas | superfície de rede desnecessária, auth extra |

## Consequências

**Positivas:**

- Estado único e consistente entre GUI, CLI e tray.
- Daemon sobrevive à GUI; jobs longos continuam.
- Debug trivial (`socat`, `jq`), sem codegen.
- Streaming e cancelamento sob controle.

**Negativas:**

- Precisa de framing e versionamento manuais.
- Duas camadas de erro (RPC + domínio) para manter.
- Socket vira superfície de segurança (mitigada por `SO_PEERCRED`/0600).

## Implementação

- `QLocalServer` + `QJsonDocument`; NDJSON; limite de 8 MiB.
- `apiVersion` inteiro; compatibilidade mínima de 2 releases.
- Testes de contrato com `MockPodmanServer` e cliente fake.
