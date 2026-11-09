# AGENTS.md — PodStack

> Instruções para agentes de IA e desenvolvedores que vão **iniciar, retomar ou contribuir** no PodStack. Leia este arquivo antes de qualquer alteração.

---

## 1. O que é o projeto

**PodStack** é uma alternativa Linux ao OrbStack: um app desktop em **Qt 6/QML + C++20** para gerenciar containers, imagens, volumes, redes, Compose, máquinas Linux (QEMU/KVM), Kubernetes e domínios locais sobre o **Podman Engine nativo**.

Arquitetura: `podstack-gui` + `podstack` (CLI) ↔ **JSON-RPC sobre Unix socket** ↔ `podstackd` (daemon de usuário) ↔ **Libpod REST** (`podman.socket`); QEMU/KVM para machines; Caddy para HTTPS; k3s para Kubernetes.

**Licença:** GPL-3.0-or-later (ver [`LICENSE`](LICENSE) e [`NOTICE`](NOTICE)).

**Status atual:** **M3 concluído** (Compose por labels + provider, logs por serviço/agregados, builds via Libpod com histórico SQLite, eventos globais em toasts) sobre o M2 (imagens/volumes/redes/pods) e o M1 (containers, logs, stats, files, terminal, batch, health, rootful). **Onboarding/setup implementado** (`SetupManager`, `SetupWindow` com 7 etapas, `setup.*`, `podstack setup` e `podstack-helper` com polkit/allowlist); falta validar a instalação real em máquina limpa. **Bandeja implementada** (`TrayController` SNI, fechar-para-bandeja, menu de containers por projeto Compose, Activity Monitor, configurações, autostart, notificações de falha/OOM/unhealthy e instância única). 13 testes; cobertura **79,7%** após a bandeja (gate de 80% ainda não atingido; ver `docs/testing.md`). Ressalvas para o M4: buildx multi-plataforma real, stack Supabase, T2-15/T2-16 (LAN/proxy), terminal embutido e prévia de prune; tray de Machines/K8s e updates dependem de M4/M5 e T7-06. Próximas tarefas em [`docs/tasks.md`](docs/tasks.md).

---

## 2. Leia antes de agir

A especificação é a fonte de verdade (ver [`docs/constitution.md`](docs/constitution.md), princípio 1).

| Ordem | Documento | Por quê |
|---|---|---|
| 1 | [`docs/constitution.md`](docs/constitution.md) | Princípios inegociáveis |
| 2 | [`docs/PRD.md`](docs/PRD.md) | Escopo, requisitos e critérios de aceite |
| 3 | [`docs/spec.md`](docs/spec.md) | Comportamento detalhado |
| 4 | [`docs/plan.md`](docs/plan.md) | Stack e decisões técnicas |
| 5 | [`docs/tasks.md`](docs/tasks.md) | Tarefa atual, dependências e definição de pronto |
| 6 | [`docs/architecture.md`](docs/architecture.md) | Componentes, fluxos e concorrência |
| 7 | [`docs/testing.md`](docs/testing.md) | Como provar que funciona |

Para a área que você vai mexer, leia também:

- Containers/Podman: [`docs/api-rpc.md`](docs/api-rpc.md), [`docs/adr/0002-libpod-libcurl.md`](docs/adr/0002-libpod-libcurl.md)
- Machines: [`docs/adr/0003-machines-qemu-kvm.md`](docs/adr/0003-machines-qemu-kvm.md)
- UI/QML: [`docs/ui-map.md`](docs/ui-map.md), [`docs/branding.md`](docs/branding.md)
- CLI: [`docs/cli-parity.md`](docs/cli-parity.md)
- Settings: [`docs/settings.md`](docs/settings.md)
- Segurança/helper: [`docs/security.md`](docs/security.md)
- Domínios/HTTPS: [`docs/adr/0004-domains-localhost-helper.md`](docs/adr/0004-domains-localhost-helper.md)
- Compose: [`docs/adr/0007-compose-provider.md`](docs/adr/0007-compose-provider.md)
- Empacotamento: [`docs/packaging.md`](docs/packaging.md), [`docs/adr/0006-rpm-first.md`](docs/adr/0006-rpm-first.md)
- Planejamento: [`docs/roadmap.md`](docs/roadmap.md)

---

## 3. Regras inegociáveis

1. **Spec antes de código.** Nenhuma funcionalidade sem `FR-*`/`NFR-*` e critério de aceite. Divergência → atualize a spec ou o código, nunca ignore.
2. **Clean-room.** Proibido copiar código, assets, ícones, sons, textos longos ou layout pixel-a-pixel do OrbStack. Use apenas comportamento documentado publicamente. "OrbStack" é marca registrada; o produto é **PodStack**.
3. **Podman é o engine.** Não reimplemente runtime, rede ou storage. Containers rodam nativos; VM só para machines e K8s.
4. **Rootless por padrão.** Rootful é opt-in explícito, com aviso. Nada de setuid no app.
5. **Menor privilégio.** Helper só com API declarativa allowlisted; socket `0600` + `SO_PEERCRED`; nunca TCP.
6. **Sem segredos em log.** Redaction obrigatória; nunca logar tokens, senhas, chaves ou `Authorization`.
7. **Nada de trabalho pesado na UI.** Streams/jobs em worker threads; UI nunca bloqueia > 16 ms.
8. **Toda mudança atualiza a doc afetada** (spec/plan/api/settings/ADR).
9. **Toda tarefa termina com evidência** de teste/lint (ver [`docs/testing.md`](docs/testing.md)).
10. **Sem escopo extra.** Não refatore, renomeie ou "melhore" o que a tarefa não pediu.

---

## 4. Estrutura do repositório

```
.
├── README.md                 # visão geral e índice
├── AGENTS.md                 # este arquivo
├── LICENSE                   # GPL-3.0-or-later
├── NOTICE                    # aviso de copyright/licença
├── assets/                   # marca aprovada (cápsula + camadas)
│   ├── podstack-logo.svg
│   ├── podstack-capsule.svg
│   ├── podstack-layers.svg
│   ├── podstack-capsule-small.svg
│   ├── podstack-icon.svg
│   ├── podstack_logo_concepts.svg
│   ├── favicon.ico
│   ├── favicon-32.png
│   ├── og-image.svg
│   ├── og-image.png
│   ├── png/
│   └── logo.md
├── docs/                     # especificação, PRD, arquitetura, ADRs, referência
│   ├── README.md
│   ├── constitution.md
│   ├── PRD.md
│   ├── spec.md
│   ├── plan.md
│   ├── tasks.md
│   ├── architecture.md
│   ├── data-model.md
│   ├── api-rpc.md
│   ├── ui-map.md
│   ├── branding.md
│   ├── cli-parity.md
│   ├── settings.md
│   ├── security.md
│   ├── testing.md
│   ├── packaging.md
│   ├── roadmap.md
│   ├── adr/
│   └── reference/
├── CMakeLists.txt
├── CMakePresets.json
├── src/                      # core, podman, daemon, cli, gui
│   ├── core/
│   ├── podman/
│   ├── daemon/
│   ├── cli/
│   └── gui/
├── tests/
├── packaging/
├── scripts/
└── .github/workflows/
```

---

## 5. Fluxo de trabalho

1. **Escolha uma tarefa** em [`docs/tasks.md`](docs/tasks.md): menor ID disponível do marco atual, respeitando `Dep`.
2. **Leia o contexto**: PRD (requisito), spec (fluxo), plan/architecture (desenho), ADR da área.
3. **Planeje** em voz alta (ou com `planner`): arquivos, abordagem, testes, riscos.
4. **Implemente** o menor incremento que satisfaz o critério de aceite.
5. **Teste**: unitário + integração quando aplicável; rode `clang-format`/`clang-tidy`/`qmllint`.
6. **Atualize a doc** se o comportamento/contrato mudou.
7. **Registre evidência**: comando, resultado, cobertura, screenshot/log.
8. **Só então** marque `[x]` na tarefa e prossiga.

**Nunca:**

- Começar pelo polimento visual antes do comportamento.
- Adicionar dependência sem justificar e atualizar `plan.md`.
- Alterar contrato RPC sem versionar e atualizar `api-rpc.md`.
- Alterar settings sem atualizar `settings.md` e os defaults.

---

## 6. Build e testes (previstos para o M0)

Ambiente de referência: **Fedora 43/44**, Podman ≥ 5.8, Qt ≥ 6.10, `/dev/kvm` para machines.

```bash
# dependências
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
  qt6-qt5compat-devel kf6-kstatusnotifieritem-devel \
  qtermwidget-devel libarchive-devel libcurl-devel libxcrypt-devel sqlite-devel \
  docker-compose

# build
cmake --preset dev
cmake --build --preset dev

# testes
ctest --preset dev

# daemon + GUI (incremental; use PODSTACK_SKIP_BUILD=1 para não recompilar)
./scripts/dev.sh
PODSTACK_SKIP_BUILD=1 ./scripts/dev.sh
```

`PODSTACK_CONTROL_SOCKET` permite usar um socket de dev isolado do daemon do sistema.

> O M0 já entrega `CMakeLists.txt` e os binários em `build/dev/`.

**Verificação mínima por mudança:**

- Build sem warnings novos.
- `clang-format --dry-run --Werror` e `clang-tidy`.
- Testes relevantes passando (unit + integração).
- `qmllint` para QML.
- Evidência anexada no relatório final.

---

## 7. Convenções de código

### C++

- **C++20**, sem extensões proprietárias.
- Todo arquivo fonte começa com `// SPDX-License-Identifier: GPL-3.0-or-later`.
- RAII sempre; `std::unique_ptr`/`QObject` parent ownership; nada de `new` solto.
- `Q_OBJECT` apenas onde necessário; sinais/slots tipados.
- Erros: `PodstackError` com código estável; nada de exceções atravessando limites do Qt.
- Threads: `QThreadPool`/`std::jthread`; nunca tocar QObject de outra thread sem `QueuedConnection`.
- Logs: `qCDebug/qCInfo/qCWarning/qCError` com categoria (`podstack.podman`, `podstack.rpc`, ...).
- Sem `using namespace` em headers; sem macros novas sem justificativa.

### QML

- QML só apresentação; lógica em C++ (models/controllers).
- Componentes pequenos e reutilizáveis; tokens via `Theme.qml`.
- `qmllint` limpo; sem `console.log` em produção.
- Acessibilidade: `Accessible.name`/`description` em controles.
- Strings com `qsTr()`; nada hardcoded.

### CMake

- Targets por módulo (`podstack_core`, `podstack_podman`, `podstack_daemon`, `podstack_gui`, `podstack_cli`).
- `qt_add_qml_module` para QML; `QT_QML_GENERATE_QMLLS_INI` quando disponível.
- Sem paths absolutos; `GNUInstallDirs`.

### Nomenclatura

- Classes `PascalCase`; métodos `camelCase`; membros `m_`; constantes `kPascalCase`.
- Arquivos `.h`/`.cpp` com o mesmo nome da classe.
- IDs de requisitos/tarefas conforme [`docs/README.md`](docs/README.md).

---

## 8. Convenções de documentação

- Requisito novo → `FR-<DOMÍNIO>-<NNN>` no PRD **e** fluxo na spec.
- Decisão arquitetural → ADR em `docs/adr/` (contexto, decisão, alternativas, consequências).
- Novo método RPC → `docs/api-rpc.md` + versão da API se incompatível.
- Nova chave de setting → `docs/settings.md` (tipo/default/validação/restart) + schema.
- Comando/flags de CLI → `docs/cli-parity.md`.
- Tela/componente QML → `docs/ui-map.md`.
- Mudança de marca → `docs/branding.md`.

**Não crie documentos paralelos**; edite o existente.

---

## 9. Segurança

- Socket do daemon: `$XDG_RUNTIME_DIR/podstack/control.sock`, dir `0700`, socket `0600`, `SO_PEERCRED`.
- Helper: systemd system + polkit; métodos allowlisted (`BindPorts`, `InstallDnsZone`, `TrustCa`, ...); **nunca** `Exec`/shell.
- Containers não recebem sockets do PodStack/Podman.
- Paths de arquivo: canonicalizar, rejeitar `..`/symlink escape.
- Segredos: `0600`/keyring; nunca em argv/log/config.
- Updates: assinatura verificada antes de aplicar.
- Antes de operação privilegiada: explicar o porquê e pedir consentimento.

Detalhes em [`docs/security.md`](docs/security.md).

---

## 10. Marca e assets

- **Direção aprovada:** **cápsula** (símbolo principal) + **camadas** (símbolo secundário). O **monograma P não foi aprovado** — não usar.
- Usar os arquivos em [`assets/`](assets/) e as regras em [`docs/branding.md`](docs/branding.md):
  - `podstack-capsule.svg` — símbolo principal
  - `podstack-layers.svg` — símbolo secundário
  - `podstack-capsule-small.svg` — 16–24 px (favicon)
  - `podstack-capsule-tray.svg` — 16–64 px (bandeja)
  - `podstack-icon.svg` — ícone do app
  - `podstack-logo.svg` — lockup (cápsula + wordmark)
  - `png/podstack-icon-*.png` — ícones 16–512 para `hicolor`
  - `png/tray/*` — bandeja 16/22/24/32/48/64
  - `favicon.ico` / `favicon-32.png` — favicon
  - `og-image.png` — Open Graph 1200×630
- Paleta: índigo `#3730A3`, acento `#6366F1`, ciano `#22D3EE`.
- **Proibido** usar esferas/órbitas, baleia do Docker, roxo do Podman, monograma P ou qualquer asset do OrbStack.
- Alterações de marca exigem aprovação de Produto + Design e atualização de `docs/branding.md`.

---

## 11. Git e commits

- **Conventional Commits** em português: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `build:`, `chore:`.
- Um commit por mudança lógica; mensagem explica o **porquê**.
- Nunca commitar `.env`, segredos, chaves ou o DMG analisado.
- Nunca `git push --force` em branch compartilhada.
- PR descreve requisito(s) atendido(s), testes executados e riscos.

---

## 12. Decisões abertas e resolvidas

| ID | Decisão | Impacto | Onde |
|---|---|---|---|
| Q1 | **Resolvida:** licença **GPL-3.0-or-later**; terminal **QTermWidget** (GPL-2.0-or-later, compatível) | — | [`docs/PRD.md`](docs/PRD.md) §13, [`docs/adr/0005-terminal-backend.md`](docs/adr/0005-terminal-backend.md) |
| Q2 | **Resolvida:** k3s em VM dedicada; kind fora do v1 | — | PRD §13 |
| Q3 | **Resolvida:** Flatpak não no v1; reavaliar sob demanda | — | PRD §13 |
| Q4 | **Resolvida:** rootful suportado como opt-in explícito | — | PRD §13 |
| Q5 | **Resolvida:** `*.podstack.local` via helper; `*.localhost` é o padrão | — | PRD §13 |
| Q6 | **Resolvida:** telemetria opt-in por categoria, off por padrão | — | PRD §13 |
| Q7 | **Resolvida:** pt-BR e en-US, extensível depois | — | PRD §13 |
| Q8 | **Resolvida:** Docker e Podman coexistem; a remoção do Docker é opt-in | — | PRD §13, ADR-0008 |
| Q9 | **Resolvida:** onboarding/setup em 7 etapas, não destrutivo, via helper allowlisted | — | PRD §13, spec §2.6 |
| Q10 | **Resolvida:** Pods do Podman no grupo Engine; Kubernetes só para k3s | — | PRD §13, ui-map §3 |

**Decisões acima ratificadas em 2026-11-08.** Novas decisões: registre a proposta e peça confirmação.

---

## 13. Definição de pronto (DoD)

Uma tarefa só está concluída quando:

- [ ] Critério de aceite da tarefa atendido e verificado.
- [ ] Código compila sem warnings novos; formatação/lint aplicados.
- [ ] Teste automatizado cobre o comportamento (ou justificativa explícita).
- [ ] Documentação afetada atualizada.
- [ ] Nenhum segredo em log; permissões corretas.
- [ ] Evidência (comando + resultado) registrada.
- [ ] `[x]` marcado em [`docs/tasks.md`](docs/tasks.md) somente após tudo acima.

---

## 14. Índice completo da documentação

**Núcleo SDD:** [`docs/README.md`](docs/README.md) · [`docs/constitution.md`](docs/constitution.md) · [`docs/PRD.md`](docs/PRD.md) · [`docs/spec.md`](docs/spec.md) · [`docs/plan.md`](docs/plan.md) · [`docs/tasks.md`](docs/tasks.md) · [`docs/testing.md`](docs/testing.md)

**Engenharia:** [`docs/architecture.md`](docs/architecture.md) · [`docs/data-model.md`](docs/data-model.md) · [`docs/api-rpc.md`](docs/api-rpc.md) · [`docs/ui-map.md`](docs/ui-map.md) · [`docs/branding.md`](docs/branding.md) · [`docs/cli-parity.md`](docs/cli-parity.md) · [`docs/settings.md`](docs/settings.md) · [`docs/security.md`](docs/security.md) · [`docs/packaging.md`](docs/packaging.md) · [`docs/roadmap.md`](docs/roadmap.md)

**ADRs:** [`0001`](docs/adr/0001-daemon-json-rpc.md) · [`0002`](docs/adr/0002-libpod-libcurl.md) · [`0003`](docs/adr/0003-machines-qemu-kvm.md) · [`0004`](docs/adr/0004-domains-localhost-helper.md) · [`0005`](docs/adr/0005-terminal-backend.md) · [`0006`](docs/adr/0006-rpm-first.md) · [`0007`](docs/adr/0007-compose-provider.md) · [`0008`](docs/adr/0008-docker-coexistence-setup.md) · [`0009`](docs/adr/0009-tray-sni-lifecycle.md)

**Referência OrbStack:** [`análise do DMG`](docs/reference/orbstack-2.2.5.md) · [`notas oficiais`](docs/reference/orbstack-2.2.5-official-notes.md) · [`árvore do CLI`](docs/reference/cli-tree.json) · [`árvore legível`](docs/reference/cli-tree.md) · [`arquivos Swift`](docs/reference/swift-files.txt) · [`settings keys`](docs/reference/settings-keys.txt)

**Assets:** [`podstack-logo.svg`](assets/podstack-logo.svg) · [`podstack-capsule.svg`](assets/podstack-capsule.svg) · [`podstack-layers.svg`](assets/podstack-layers.svg) · [`podstack-capsule-small.svg`](assets/podstack-capsule-small.svg) · [`podstack-icon.svg`](assets/podstack-icon.svg) · [`png/`](assets/png/) · [`favicon.ico`](assets/favicon.ico) · [`og-image.png`](assets/og-image.png) · [`conceitos`](assets/podstack_logo_concepts.svg) · [`racional`](assets/logo.md)

---

## 15. Primeiro comando

Se você é um agente começando agora:

1. Leia os 5 primeiros documentos da §2.
2. Abra [`docs/tasks.md`](docs/tasks.md) e localize o primeiro `T0-*` não concluído.
3. Verifique se o ambiente tem as dependências da §6; se não, peça confirmação antes de instalar.
4. Implemente **apenas** essa tarefa, com teste e evidência.
5. Atualize a doc e marque a tarefa.

**Não comece pelo M1 antes do M0 estar verde.**
