# ADR-0007 — Compose via provider Docker Compose + estado por labels

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-08 |
| Decisores | Arquitetura |
| Relacionados | ADR-0002, `spec.md` |

## Contexto

Precisamos de paridade com `docker compose` (projetos, serviços, logs, up/down) sobre Podman. As opções:

| Opção | O que é | Prós | Contras |
|---|---|---|---|
| `docker compose` (Go, v5.x) | implementação de referência, fala a **API** | `--format json`, labels padrão, convergência, `ps --services` | binário extra; requer `DOCKER_HOST` |
| `podman-compose` (Python) | traduz YAML em chamadas à **CLI** `podman` | leve, popular | semântica parcial, `ps` incompatível |
| `podman compose` | wrapper que delega a um provider | comando único | exige provider instalado; warning |

## Decisão

- Usar **`docker compose` (v5.x)** como provider oficial, apontando `DOCKER_HOST=unix://$XDG_RUNTIME_DIR/podman/podman.sock`.
- Invocar via `podman compose` quando disponível; senão, `docker compose` diretamente.
- **Não parsear saída de CLI para estado.** O estado vem do **Libpod**, agrupando containers por labels:
  - `com.docker.compose.project`
  - `com.docker.compose.service`
  - `com.docker.compose.project.working_dir`
- O CLI Compose é usado apenas para `up/down/build/config` (e plugins/`x-*`).
- Fallback `podman-compose` com aviso explícito de limitações.

## Consequências

**Positivas:**

- Convergência e semântica próximas do Docker.
- UI independente do provider (labels são padrão).
- Logs/stats por serviço direto do Libpod.

**Negativas:**

- Dependência de binário externo (~70 MB) ou provider instalado.
- Compat API do Podman pode ter regressões (ex.: inspect de rede 6.1).
- `x-*` extensions e recursos específicos podem variar.

## Implementação

- `ComposeManager` detecta provider e versão; escolhe estratégia.
- Containers de Compose são agrupados por label, nunca por parsing.
- Testes com projeto de exemplo e provider real; fallback testado.
- Documentar instalação do provider (`dnf install docker-compose`).
