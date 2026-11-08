# Estratégia de testes — PodStack

> Regra do projeto: **nenhuma tarefa é "pronta" sem evidência de teste/lint**. Este documento define o que testar, como, onde e com qual critério.

---

## 1. Pirâmide

```
        ┌─────────────────────────────┐
        │ E2E (GUI/CLI + Podman real) │  ~10%
        ├─────────────────────────────┤
        │ Integração (Podman/QEMU)    │  ~25%
        ├─────────────────────────────┤
        │ Unitários (core/podman/rpc) │  ~65%
        └─────────────────────────────┘
```

| Nível | Ferramenta | Escopo | Tempo alvo |
|---|---|---|---|
| Unitário | Qt Test (C++) | parsers, modelos, validação, state machines, RPC | < 30 s total |
| Contrato | Qt Test + `MockPodmanServer` | PodmanClient ↔ HTTP/Unix socket | < 60 s |
| Integração | Qt Test + Podman real | containers, imagens, volumes, redes, logs, exec | < 5 min |
| VM | script + pytest-shell | QEMU/cloud-init/SSH/virtiofs | < 15 min |
| E2E GUI | QML Test + acessibilidade | fluxos J1–J3 | < 10 min |
| E2E CLI | golden tests | comandos, JSON, exit codes | < 2 min |
| Performance | benchmark | startup, memória, idle CPU, listas | < 5 min |
| Segurança | testes dedicados | socket, traversal, redaction, assinatura | < 5 min |

---

## 2. Unitários

### 2.1 Core

- Modelos: serialização/desserialização, defaults, igualdade.
- Validação: nomes RFC 1123, portas, CIDR, tamanhos, paths.
- State machines: container/machine/cluster/job (todas as transições + inválidas).
- Erros: mapeamento Libpod → `PodstackError` (tabela de casos).
- Config: merge, migração, validação, campos desconhecidos preservados.

### 2.2 PodmanClient

- Parsing de JSON do Libpod (fixtures reais capturadas).
- Construção de URLs/query (filtros, `all`, `follow`, `tail`).
- Multiplexação de streams (frames 8 bytes) com dados binários/UTF-8.
- Timeouts, cancelamento, retries idempotentes.
- Erros HTTP (400/404/409/500) → códigos estáveis.

### 2.3 RPC

- Framing NDJSON (mensagens parciais, múltiplas por chunk, > 8 MiB rejeitada).
- `SO_PEERCRED` (mock de credenciais).
- Handshake/versionamento.
- Dispatch de métodos, params inválidos, método desconhecido.
- Subscriptions: subscribe/unsubscribe, backpressure, drop.

### 2.4 CLI

- Parsing de flags/aliases; help.
- Formatação humana/JSON/quiet.
- Exit codes por classe de erro.
- Modo não interativo (sem TTY).

---

## 3. Integração com Podman

### 3.1 Pré-requisitos

- Podman ≥ 5.8, `podman.socket` ativo, cgroup v2.
- Imagens de teste leves: `alpine:3.20`, `busybox:1.36`, `nginx:alpine`.
- Rede isolada de teste; limpeza por label `dev.podstack.test=1`.

### 3.2 Casos

| Área | Casos |
|---|---|
| Containers | create/start/stop/restart/pause/kill/remove; exit codes; healthcheck; `--rm` |
| Logs | follow, tail, stdout/stderr, timestamps, reconexão com `since` |
| Exec | TTY e não-TTY, resize, Ctrl+C/D, exit code, sessão órfã |
| Stats | CPU/mem/rede, stream, múltiplos containers |
| Imagens | pull, tag, remove, prune, load/save |
| Volumes | create, clone, export/import, prune |
| Redes | create, subnet, DNS interno, connect/disconnect |
| Pods | create/start/stop/remove |
| Eventos | sequência de eventos, reconexão |

### 3.3 Isolamento

- Cada teste usa nomes prefixados `pstest-<uuid>`.
- `t.Cleanup`/RAII remove recursos mesmo em falha.
- Nunca tocar em recursos do usuário (filtrar por label).

---

## 4. Machines (QEMU)

| Teste | Verificação |
|---|---|
| Criar Ubuntu 24.04 | SSH responde ≤ 60 s; hostname/user corretos |
| cloud-init | arquivo criado, pacote instalado, comando executado |
| Stop/start | estado consistente; sem QEMU órfão |
| Delete | disco/seed/chave removidos; SSH config limpo |
| Export/import | checksum; máquina restaurada funcional |
| virtiofs | arquivo do host visível/gravável no guest |
| Port forwarding | porta do guest acessível em `localhost` |
| Isolado | sem acesso ao host/rede (quando aplicável) |
| KVM ausente | modo TCG ou erro claro (configurável) |

**Infra:** runner com `/dev/kvm`; imagens cloud em cache; timeout 10 min.

---

## 5. Kubernetes

| Teste | Verificação |
|---|---|
| Enable | cluster Ready ≤ 90 s |
| kubeconfig | `kubectl get nodes` funciona |
| Pods/Services | watch reflete criação/remoção |
| Logs | logs por container |
| Port-forward | porta local acessa o pod |
| Disable | cluster e kubeconfig removidos |

---

## 6. Domínios e proxy

| Teste | Verificação |
|---|---|
| `*.localhost` | resolve; proxy retorna 200 |
| Detecção de porta | escolhe porta correta; cache |
| 404 | host desconhecido |
| LAN toggle | acesso externo sim/não |
| HTTPS | certificado válido; CA confiável |
| DNS `podstack.local` | install/remove idempotente; resolved recarrega |
| Path traversal | rejeitado no proxy/files |

---

## 7. E2E (GUI)

- QML Test para componentes (estados, validação, foco).
- Testes de fluxo com `qmltestrunner` + daemon real:
  1. abrir app → listar containers;
  2. criar container → start → logs → terminal → stop → remove;
  3. criar máquina → SSH → push/pull → delete;
  4. habilitar K8s → ver pods/services.
- Screenshots de referência (golden) para regressão visual leve (não pixel-perfect).
- Acessibilidade: navegação por teclado e labels.

---

## 8. Performance

| Métrica | Método | Meta |
|---|---|---|
| Startup cold | `hyperfine podstack-gui` (com daemon quente) | ≤ 1,0 s |
| Memória idle | RSS após 60 s | ≤ 180 MB |
| CPU idle | `pidstat` 60 s | ≤ 1% |
| Lista 1.000 containers | tempo de scroll/frame | 60 fps |
| Logs 100k linhas | busca | ≤ 200 ms |
| Latência de evento | criação externa → UI | ≤ 1 s |

Benchmarks rodam no CI noturno; regressão > 20% falha o job.

---

## 9. Segurança

| Teste | Verificação |
|---|---|
| Socket | permissões, UID errado rejeitado |
| Helper | métodos allowlisted; sem exec; polkit |
| Redaction | fixtures com tokens/senhas/chaves |
| Path traversal | `..`, symlink, archive malicioso |
| Update | assinatura inválida rejeitada |
| CA/DNS | install/remove idempotente; sem resíduo |
| Rootful | aviso e badge; sem escalada automática |
| SELinux | enforcing sem AVC inesperado |

---

## 10. Matriz de compatibilidade

| Dimensão | Valores |
|---|---|
| Fedora | 43, 44 |
| Sessão | Wayland, X11 |
| Desktop | GNOME, KDE Plasma, niri/Noctalia |
| Podman | 5.8, 6.x |
| Modo | rootless, rootful |
| Arch | x86_64, aarch64 (quando disponível) |
| KVM | presente, ausente (TCG) |

CI cobre o subconjunto P0; matriz completa no release candidate.

---

## 11. Test doubles

### 11.1 `MockPodmanServer`

- Servidor HTTP sobre Unix socket em C++/Qt Test.
- Rotas configuráveis com fixtures JSON.
- Simula streams (logs/events) e hijack (exec).
- Injeção de erros (timeout, 500, conexão fechada).
- Usado em unit/contract tests, sem Podman.

### 11.2 Fixtures

- JSON reais capturados de Podman 5.8/6.x, versionados em `tests/fixtures/`.
- Imagens de teste fixadas por digest.
- Cloud images fixadas por checksum.

---

## 12. CI

Workflow: [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) — roda em **toda pull request** e em push para `main`.

| Etapa | Conteúdo | Bloqueia merge |
|---|---|---|
| `ci` (job único) | checkout + deps Fedora 44, build com `coverage`, testes, gcovr | ✅ |
| Cobertura | relatório `src/` (XML/HTML) + gate de **80%** de linhas | ✅ |
| Artefato | `coverage.xml`, `coverage/`, resumos | — |
| Futuro | lint (`clang-format`/`clang-tidy`/`qmllint`), E2E GUI/CLI, segurança, performance, RPM | planejado |

- Container: `fedora:44` com Qt 6.11, CMake/Ninja, libcurl e gcovr.
- O teste de integração com Podman roda quando há socket disponível; caso contrário, é `QSKIP` (o contrato e o RPC cobrem o caminho sem engine).
- Cache: ccache e imagens Podman (quando o runner permitir).
- Flaky tests: quarentena com issue e prazo; não podem ficar > 2 semanas.

---

## 13. Cobertura e qualidade

- **M3 (atual):** **80,7%** de linhas no total (`src/`); gate do CI em **80%** sobre `src/`. O gcovr roda com `--gcov-ignore-parse-errors` e `--merge-mode-functions=separate` para contornar bugs conhecidos do gcov (negative hits e merge de `main.cpp` de targets diferentes).
- **Meta v1.0:** manter ≥ 80% e buscar ≥ 85% em caminhos críticos; testes QML/terminal dedicados entram no M2.
- Nenhum merge com teste vermelho.
- Bug corrigido → teste de regressão obrigatório.
- Código novo sem teste exige justificativa no PR.

Comandos locais:

```bash
cmake --preset coverage
cmake --build --preset coverage
ctest --preset coverage --output-on-failure
mkdir -p coverage
gcovr -r . --filter 'src/' --xml coverage.xml \
  --html-details coverage/index.html --txt-metric line --print-summary
gcovr -r . --filter 'src/' --txt-metric line --print-summary --fail-under-line 80
```

---

## 14. Roteiro manual (release)

### J1 — Projeto Compose

1. `podstack compose up -d` em projeto de exemplo.
2. GUI mostra grupo, serviços e portas.
3. `curl http://web.localhost:8080` responde.
4. Logs por serviço; terminal; stop/down.

### J2 — Máquina

1. `podstack machine create ubuntu:24.04 dev --cpus 2 --memory 4G`.
2. `podstack ssh dev -- uname -a`.
3. `podstack machine push ./file dev:~/`.
4. Reiniciar host; máquina sobe; SSH funciona.
5. Export/import.

### J3 — Kubernetes

1. `podstack kube start`.
2. `podstack kube env`; `kubectl get pods -A`.
3. Criar deployment de teste; ver na GUI; logs; port-forward.
4. `podstack kube disable`.

### J4 — CI

1. Runner sem TTY.
2. `podstack machine create ... --yes`.
3. `podstack machine run ...`.
4. `podstack machine delete ... --force`.
5. Exit codes corretos.

---

## 15. Evidência

Todo PR/tarefa anexa:

- comando executado (`ctest`, `pytest`, etc.);
- resumo (passou/falhou, tempo);
- cobertura quando aplicável;
- screenshot/log para E2E;
- benchmark antes/depois para performance.
