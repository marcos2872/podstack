# Tarefas — PodStack

> Breakdown executável derivado de [`plan.md`](plan.md). Cada tarefa tem ID, dependências e critério de conclusão. Marque `[x]` somente com evidência (teste/lint/build).

**Legenda:** `T<marco>-<NN>` · **P** = prioridade (P0/P1/P2) · **Dep** = dependências.

---

## M0 — Fundação

> **Status (2026-11-08): concluído.** Evidências: `cmake --build --preset dev` sem warnings; `ctest --preset dev` 3/3 (core, contrato e Podman real); CLI/daemon E2E (`status`, `ps`, `exec`); hijack de exec e demux de logs validados.

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T0-01 | P0 | Criar repositório, `.gitignore`, `LICENSE`, `README.md` | — | `git status` limpo; licença definida |
| T0-02 | P0 | Scaffold CMake + presets (`dev`, `release`, `asan`) | T0-01 | `cmake --preset dev` + build vazio |
| T0-03 | P0 | Estrutura de diretórios (`core`, `podman`, `daemon`, `cli`, `gui`, `machines`, `k8s`, `proxy`, `platform`) | T0-02 | Árvore criada; targets compilam |
| T0-04 | P0 | `PodmanClient`: descoberta de socket + `/_ping` + versão da API | T0-03 | Teste unitário com socket fake; log de versão |
| T0-05 | P0 | HTTP sobre Unix socket com libcurl (GET/POST/DELETE, JSON) | T0-04 | Teste contra `podman.socket` real |
| T0-06 | P0 | Streams: logs (`follow`) e eventos | T0-05 | Teste recebe linhas/eventos; cancelamento |
| T0-07 | P0 | Hijack: `exec/start` + TTY + resize (PoC) | T0-05 | Teste com `/bin/sh`; Ctrl+C/D; resize |
| T0-08 | P0 | Servidor JSON-RPC (`QLocalServer`, `SO_PEERCRED`) | T0-03 | `socat`/cliente de teste; UID errado rejeitado |
| T0-09 | P0 | `podstackd`: main, config, logging, systemd user unit | T0-08 | `systemctl --user start podstack`; socket 0600 |
| T0-10 | P0 | CLI `podstack version`, `status`, `ps` | T0-08 | Saída humana + JSON; exit codes |
| T0-11 | P0 | Shell QML: `MainWindow` (sidebar/lista/inspector) + tema | T0-03 | GUI abre; layout persiste |
| T0-12 | P0 | Modelo `ContainerListModel` + RPC `containers.list` | T0-08, T0-10 | Lista containers reais |
| T0-13 | P0 | `MockPodmanServer` para testes (HTTP Unix socket) | T0-05 | Testes de contrato passam sem Podman |
| T0-14 | P1 | CI inicial (build + unit + clang-format/tidy) | T0-02 | Pipeline verde |
| T0-15 | P1 | `podstack doctor` (socket, Podman, versão) | T0-09 | Relatório com checks |
| T0-16 | P1 | Script `dev/run.sh` (build + daemon + GUI) | T0-09, T0-11 | Um comando sobe tudo |
| T0-17 | P1 | ADR-0005 (licença/terminal) decidido | — | ✅ GPL-3.0-or-later + QTermWidget (ADR aceito) |
| T0-18 | P1 | ADR-0001/0002 revisados com PoC | T0-05, T0-07 | ADRs atualizados |

**Saída M0:** `podstack ps` lista containers; GUI abre e mostra a lista; daemon sobrevive ao fechar a GUI; testes verdes.

---

## M1 — Containers

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T1-01 | P0 | Listar containers com filtros (all/running/stopped) + contadores | T0-12 | Consistente com `podman ps -a` |
| T1-02 | P0 | Ações start/stop/restart/pause/unpause/kill | T1-01 | E2E em container real |
| T1-03 | P0 | Remover (force/volumes) com confirmação | T1-02 | Container some; volumes tratados |
| T1-04 | P0 | Criar container (imagem, nome, portas, env, volumes, workdir, user) | T1-01 | Formulário + validação |
| T1-05 | P0 | Logs em streaming na UI (follow, busca, wrap, timestamps) | T0-06 | Primeira linha ≤ 500 ms |
| T1-06 | P0 | Terminal embutido (exec) com resize e sessões | T0-07 | TTY funcional; sem órfãos |
| T1-07 | P0 | Stats ao vivo (CPU/mem/rede/disco/PIDs) + gráfico | T0-06 | Consistente com `podman stats` |
| T1-08 | P0 | Info tab (imagem, comando, portas, env, mounts, rede, labels) | T1-01 | Dados completos |
| T1-09 | P0 | Ações em lote (multi-seleção) | T1-02, T1-03 | Confirmação com resumo |
| T1-10 | P1 | Renomear/duplicar container | T1-02 | Nome válido; conflito tratado |
| T1-11 | P1 | Files tab (listar/baixar/enviar) | T0-05 | Paths seguros; progresso |
| T1-12 | P1 | Inspect JSON (busca/copiar/exportar) | T1-08 | JSON válido |
| T1-13 | P1 | Abrir porta no navegador / copiar endereço | T1-08 | URL correta |
| T1-14 | P1 | Healthcheck e status visual | T1-01 | Badge/estado correto |
| T1-15 | P2 | CRIU checkpoint/restore | T1-02 | Só se suportado |
| T1-16 | P2 | gVisor/runsc | T1-04 | Runtime selecionável |
| T1-17 | P2 | Debug shell | T1-06 | Shell efêmero |
| T1-18 | P0 | Modo rootful opt-in (socket, confirmação, badge) | T0-09 | Troca de modo funcional; aviso exibido |

**Saída M1:** ciclo de vida completo + logs + terminal + stats; E2E verde.

---

## M2 — Imagens, volumes, redes, pods

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T2-01 | P0 | Listar imagens (repo, tag, digest, tamanho, uso) | T0-05 | Consistente com `podman images` |
| T2-02 | P0 | Pull com progresso/cancelamento + auth | T2-01 | Progresso por camada |
| T2-03 | P0 | Remover/prune com prévia | T2-01 | Bloqueia em uso |
| T2-04 | P0 | Info/histórico/digest | T2-01 | Digest copiável |
| T2-05 | P0 | Listar volumes (driver, mountpoint, tamanho, uso) | T0-05 | Consistente |
| T2-06 | P0 | Criar/remover/prune volumes | T2-05 | Em uso exige confirmação |
| T2-07 | P0 | Listar/criar/remover redes (subnet, gateway) | T0-05 | Validação de CIDR |
| T2-08 | P0 | Conectar/desconectar containers + aliases | T2-07 | DNS interno resolve |
| T2-09 | P0 | Pods: listar/criar/start/stop/remove | T0-05 | Consistente com `podman pod` |
| T2-10 | P1 | Push/tag/untag | T2-01 | Credenciais seguras |
| T2-11 | P1 | Import/export tar de imagens | T2-01 | Arquivo válido |
| T2-12 | P1 | Volume clone (reflink/fallback) | T2-05 | Conteúdo idêntico |
| T2-13 | P1 | Volume export/import `.tar.zst` | T2-05 | Progresso; paths seguros |
| T2-14 | P1 | Volume import do host | T2-05 | Validação |
| T2-15 | P1 | Expor portas na LAN | T2-08 | Toggle + aviso |
| T2-16 | P1 | Proxy global e exclusões | T2-08 | Aplicado a novos containers |
| T2-17 | P2 | Multi-arch/platform selection | T2-02 | Pull da plataforma |
| T2-18 | P2 | Prune avançado (system df) | T2-03 | Prévia |

**Saída M2:** catálogo completo navegável e operável.

---

## M3 — Compose, builds e eventos

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T3-01 | P1 | Detectar projetos por labels e agrupar | T1-01 | Grupos corretos |
| T3-02 | P1 | Start/stop/restart/down do projeto | T3-01 | Equivalente ao compose |
| T3-03 | P1 | Logs por serviço e agregados | T1-05 | Filtro sem misturar |
| T3-04 | P1 | Build via Libpod (tar + progresso/steps/logs) | T0-05 | Imagem criada |
| T3-05 | P1 | Histórico de builds em SQLite | T3-04 | Lista/retém/filtra |
| T3-06 | P1 | Abrir YAML/diretório do projeto | T3-01 | `xdg-open` |
| T3-07 | P1 | Eventos globais na UI (toasts/notificações) | T0-06 | Sem duplicação |
| T3-08 | P2 | Buildx multi-plataforma | T3-04 | Detecção + fallback |
| T3-09 | P2 | Supabase stack | T3-01 | Start/stop |
| T3-10 | P2 | Apagar registros de build | T3-05 | Só metadados |

**Saída M3:** J1 completo (projeto Compose sobe e é acessível).

---

## M4 — Machines (QEMU/KVM)

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T4-01 | P0 | Registry de distros (URL/checksum/versão) + cache | T0-03 | Ubuntu/Debian/Fedora |
| T4-02 | P0 | Gerar seed ISO cloud-init (NoCloud) | T4-01 | `cloud-init` aplica user-data |
| T4-03 | P0 | Criar disco qcow2 + QEMU + QMP | T4-01 | VM sobe; QMP responde |
| T4-04 | P0 | SSH: chave, porta, bloco `~/.ssh/config`, `podstack ssh` | T4-03 | SSH funciona; bloco idempotente |
| T4-05 | P0 | Ciclo de vida start/stop/restart/delete + limpeza | T4-03 | Sem órfãos |
| T4-06 | P0 | Listar/info/logs de machines na GUI | T4-03 | Estado consistente |
| T4-07 | P1 | Virtiofs mounts + file manager | T4-03 | Arquivos visíveis |
| T4-08 | P1 | pull/push de arquivos | T4-04 | Progresso |
| T4-09 | P1 | Export/import `.tar.zst` | T4-05 | Integridade; pausa |
| T4-10 | P1 | Clone/rename/default | T4-03 | Independentes |
| T4-11 | P1 | Limites CPU/mem/disco + restart | T4-03 | Aplicados |
| T4-12 | P1 | Port forwarding da máquina | T4-03 | `localhost` funciona |
| T4-13 | P1 | SSH agent forwarding | T4-04 | Toggle |
| T4-14 | P1 | Modo isolado | T4-03 | Sem mounts/host |
| T4-15 | P2 | Console serial/VNC | T4-03 | Troubleshooting |
| T4-16 | P2 | Snapshots | T4-03 | Criar/restaurar |

**Saída M4:** J2 completo; criação ≤ 60 s.

---

## M5 — Kubernetes

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T5-01 | P1 | Provisionar VM k3s e instalar | T4-03 | Cluster Ready |
| T5-02 | P1 | kubeconfig isolado + contexto + `podstack kube env` | T5-01 | `kubectl get nodes` |
| T5-03 | P1 | Watch de Pods/Services + modelos | T5-01 | UI atualiza |
| T5-04 | P1 | Logs por container do pod | T5-03 | Streaming |
| T5-05 | P1 | Delete pod/service + port-forward | T5-03 | Ações funcionam |
| T5-06 | P1 | Enable/disable/reset com confirmação | T5-01 | Idempotente |
| T5-07 | P2 | Expor Services (NodePort/LB/API) + TLS SANs | T5-01 | Configurável |
| T5-08 | P2 | Troca automática de contexto | T5-02 | Por projeto |

**Saída M5:** J3 completo.

---

## M6 — Domínios, HTTPS e port forwarding

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T6-01 | P0 | `*.localhost` + proxy por Host (porta alta) | T1-01 | `curl nome.localhost:8080` |
| T6-02 | P0 | Página de índice de serviços | T6-01 | Lista domínios/portas |
| T6-03 | P1 | Detecção automática de porta + cache | T6-01 | Funciona com 1 porta |
| T6-04 | P1 | Helper: DNS userspace + drop-in resolved | T6-01 | `*.podstack.local` resolve |
| T6-05 | P1 | Helper: socket activation :80/:443 | T6-04 | Sem porta no browser |
| T6-06 | P1 | Caddy: CA interna + HTTPS on-the-fly | T6-05 | Certificado válido |
| T6-07 | P1 | Confiar na CA (host/containers) opt-in | T6-06 | `update-ca-trust` |
| T6-08 | P1 | Labels `dev.podstack.domains/http-port/https-port` | T6-03 | Overrides |
| T6-09 | P1 | Exposição LAN de domínios/portas | T6-01 | Acesso externo |
| T6-10 | P2 | Port forwarding de portas não publicadas | T6-04 | Via router/helper |

**Saída M6:** G4 completo.

---

## M7 — Desktop, settings, updates

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T7-01 | P0 | Settings GUI + CLI (schema, validação, sync) | T0-09 | Chaves equivalentes |
| T7-02 | P0 | Tema claro/escuro + escala de fonte | T0-11 | Troca em runtime |
| T7-03 | P1 | Tray SNI + menu de contexto | T0-11 | Noctalia/KDE |
| T7-04 | P1 | Activity Monitor (gráficos + `podstack top`) | T1-07 | Dados corretos |
| T7-05 | P1 | Notificações (OOM, falha, update, disco) | T3-07 | Respeita DND |
| T7-06 | P1 | Check de updates assinado + canal | T7-01 | Assinatura validada |
| T7-07 | P1 | Onboarding (boas-vindas, escolha, create) | T0-11 | Fluxo completo |
| T7-08 | P1 | Diagnóstico/report com redaction | T0-15 | Revisão antes de enviar |
| T7-09 | P1 | Janelas/abas múltiplas + restauração | T0-11 | Estado persiste |
| T7-10 | P1 | Atalhos e acessibilidade | T0-11 | Teclado completo |
| T7-11 | P2 | Migração de Docker/Podman existente | T2-05 | Prévia/rollback |
| T7-12 | P2 | Licença/conta (opcional) | T7-01 | Offline funciona |

**Saída M7:** paridade de desktop; v1.0.

---

## M8 — Extras (pós-v1)

| ID | P | Tarefa | Dep | Conclusão |
|---|---|---|---|---|
| T8-01 | P2 | USB list/attach/detach (QEMU usb-host) | T4-03 | Device no guest |
| T8-02 | P2 | Serial ports | T4-03 | Console serial |
| T8-03 | P2 | Áudio (virtio-snd/PipeWire) | T4-03 | Playback/recording |
| T8-04 | P2 | X11/Wayland forwarding | T4-03 | App gráfico no host |
| T8-05 | P2 | i18n completa + pt-BR/en-US | T0-11 | Strings externalizadas |
| T8-06 | P2 | Avaliar Flatpak sob demanda (não no v1) | T7-01 | Documentado |
| T8-07 | P2 | Snapshots de máquina | T4-03 | Criar/restaurar |
| T8-08 | P2 | Checkpoint/restore containers | T1-02 | CRIU |
| T8-09 | P2 | Multi-arch/emulação | T2-02 | binfmt/FEX |

---

## Transversais

| ID | P | Tarefa | Conclusão |
|---|---|---|---|
| TX-01 | P0 | `security.md` + threat model revisados | Ameaças e mitigações mapeadas |
| TX-02 | P0 | Testes unitários do core (≥ 70%) | Cobertura medida |
| TX-03 | P0 | Testes de integração com Podman real | Matriz rootless/rootful |
| TX-04 | P0 | Testes E2E das jornadas J1–J3 | Critérios do PRD |
| TX-05 | P0 | `clang-format` + `clang-tidy` no CI | Sem warnings novos |
| TX-06 | P0 | Documentação de usuário (install, quickstart, CLI, troubleshooting) | Publicada |
| TX-07 | P0 | RPM + systemd + polkit + SELinux | Instalação limpa |
| TX-08 | P0 | Auditoria de segurança (socket, helper, proxy) | Relatório |
| TX-09 | P1 | Benchmark de performance (startup/mem/idle) | Metas §8.1 |
| TX-10 | P1 | Matriz de compatibilidade (F43/F44, Wayland/X11, GNOME/KDE/niri) | Verde |
| TX-11 | P1 | Golden tests de CLI/exit codes | Estáveis |
| TX-12 | P1 | AppStream/metainfo + ícones | `appstreamcli validate` |
| TX-13 | P2 | Telemetria opt-in por categoria (crash/uso/perf/erros) | Off por padrão |
| TX-14 | P2 | Acessibilidade (WCAG AA) auditada | Checklist |

---

## Grafo de dependências

```mermaid
flowchart LR
  M0 --> M1 --> M2 --> M3
  M2 --> M4 --> M5
  M1 --> M6
  M3 --> M7
  M4 --> M7
  M6 --> M7
  M7 --> M8
```

## Definição de pronto (por tarefa)

1. Código compila sem warnings novos; `clang-format` aplicado.
2. Teste unitário ou de integração cobre o comportamento.
3. Critério de conclusão da tabela verificado e registrado.
4. Documentação afetada atualizada (`spec`/`plan`/`api-rpc`/`settings`).
5. Nenhum segredo em log; permissões corretas.
6. Revisão por outro agente/dev (quando aplicável).
