# ADR-0002 — Libpod REST via libcurl (incluindo hijack)

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-08 |
| Decisores | Arquitetura |
| Relacionados | ADR-0001, `plan.md` |

## Contexto

O PodStack precisa de containers, imagens, volumes, redes, pods, eventos, builds, logs, stats e exec. O Podman oferece duas APIs: **Compat** (Docker-like) e **Libpod** (nativa). Streams de exec/attach usam **HTTP hijack** (`Upgrade: tcp`), não WebSocket. `QNetworkAccessManager` não faz hijack.

## Decisão

- Usar **Libpod REST** para todas as operações do PodStack.
- Descobrir a versão em runtime (`GET /_ping` → `Libpod-Api-Version`) e montar `/v<versão>/libpod/...`.
- Usar **libcurl** com `CURLOPT_UNIX_SOCKET_PATH` para requisições e streams.
- Para exec/attach, usar `CURLOPT_CONNECT_ONLY=2` + `curl_easy_send/recv` e handshake manual.
- Desserializar o stream multiplexado (`application/vnd.docker.multiplexed-stream`) quando `Tty=false`.
- Compat API apenas como fallback para ferramentas externas/Compose.

## Alternativas

| Alternativa | Prós | Contras |
|---|---|---|
| Compat API | ecossistema Docker | falta pods/play kube/healthcheck/stats, regressões (ex.: inspect de rede no 6.1) |
| Invocar CLI `podman` | simples no início | parsing frágil, sem streams tipados, difícil de testar |
| `QNetworkAccessManager` | integrado ao Qt | sem hijack; streams complicados |
| WebSocket | libs prontas | Podman não usa WebSocket para exec/attach |

## Consequências

**Positivas:**

- Acesso a todos os recursos nativos.
- Streaming eficiente e cancelável.
- Menos dependência de mudanças da Compat.

**Negativas:**

- libcurl adiciona dependência e código de baixo nível.
- Hijack exige testes cuidadosos (TTY, resize, EOF).
- Versão da API precisa ser tratada dinamicamente.

## Implementação

- `PodmanClient` encapsula tudo; nenhum outro componente fala com o socket.
- Testes de contrato com fixtures e `MockPodmanServer`.
- PoC de hijack no M0 (risco principal).
