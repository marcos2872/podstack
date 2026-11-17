# AGENTS.md — PodStack

> Instruções para agentes de IA e desenvolvedores que vão **iniciar, retomar ou contribuir** no PodStack. Leia este arquivo antes de qualquer alteração.

---

## 1. O que é o projeto

**PodStack** é uma alternativa Linux ao OrbStack: um app desktop em **Qt 6/QML + C++20** para gerenciar containers, imagens, volumes, redes, Compose, máquinas Linux (QEMU/KVM), Kubernetes e domínios locais sobre o **Podman Engine nativo**.

Arquitetura: `podstack-gui` + `podstack` (CLI) ↔ **JSON-RPC sobre Unix socket** ↔ `podstackd` (daemon de usuário) ↔ **Libpod REST** (`podman.socket`); QEMU/KVM para machines; Caddy para HTTPS; k3s para Kubernetes.

**Licença:** GPL-3.0-or-later (ver [`LICENSE`](LICENSE) e [`NOTICE`](NOTICE)).

**Status atual:** containers, imagens, volumes, redes, pods, Compose, builds, machines QEMU/KVM (ciclo de vida, SSH/arquivos, virtiofs, export/import, clone/rename/default, CPU/memória/disco, port forwarding, SSH agent, modo isolado, console serial e snapshots), Kubernetes (k3s em VM dedicada), proxy `*.localhost` (HTTP), bandeja SNI, settings, updates assinados, onboarding/setup e logging de debug estão implementados. Domínios `*.podstack.local` e HTTPS automático estão **planejados** (helper pronto, RPC de domínios pendente — ver `docs/engineering/api-rpc.md` §4.9). **27 testes** CTest, cobertura **~80%** com gate bloqueante de **80%**. Histórico em [`CHANGELOG.md`](CHANGELOG.md).

---

## 2. Antes de agir

A documentação de engenharia descreve o contrato atual; os ADRs registram o porquê das decisões. Para a área que você vai mexer, leia o documento correspondente:

| Área | Documento |
|---|---|
| Componentes, fluxos, concorrência | [`docs/engineering/architecture.md`](docs/engineering/architecture.md) |
| Contrato JSON-RPC | [`docs/engineering/api-rpc.md`](docs/engineering/api-rpc.md) |
| Telas, componentes, atalhos | [`docs/engineering/ui-map.md`](docs/engineering/ui-map.md) |
| Settings | [`docs/engineering/settings.md`](docs/engineering/settings.md) |
| Segurança e helper | [`docs/engineering/security.md`](docs/engineering/security.md) |
| Persistência | [`docs/engineering/data-model.md`](docs/engineering/data-model.md) |
| CLI e paridade | [`docs/engineering/cli-parity.md`](docs/engineering/cli-parity.md) |
| Empacotamento | [`docs/engineering/packaging.md`](docs/engineering/packaging.md) |
| Marca | [`docs/engineering/branding.md`](docs/engineering/branding.md) |
| Site público | [`docs/engineering/web.md`](docs/engineering/web.md) |

ADRs por área:

- Containers/Podman: [`docs/adr/0002-libpod-libcurl.md`](docs/adr/0002-libpod-libcurl.md)
- Machines: [`docs/adr/0003-machines-qemu-kvm.md`](docs/adr/0003-machines-qemu-kvm.md)
- Domínios/HTTPS: [`docs/adr/0004-domains-localhost-helper.md`](docs/adr/0004-domains-localhost-helper.md)
- Compose: [`docs/adr/0007-compose-provider.md`](docs/adr/0007-compose-provider.md)
- Empacotamento: [`docs/adr/0006-rpm-first.md`](docs/adr/0006-rpm-first.md)

---

## 3. Regras inegociáveis

1. **Contrato documentado.** Mudança de comportamento, RPC, settings, CLI ou UI atualiza o documento de engenharia correspondente **no mesmo PR**. Sem requisito claro, abra/consulte a issue antes de implementar.
2. **Clean-room.** Proibido copiar código, assets, ícones, sons, textos longos ou layout pixel-a-pixel do OrbStack. Use apenas comportamento documentado publicamente. "OrbStack" é marca registrada; o produto é **PodStack**.
3. **Podman é o engine.** Não reimplemente runtime, rede ou storage. Containers rodam nativos; VM só para machines e K8s.
4. **Rootless por padrão.** Rootful é opt-in explícito, com aviso. Nada de setuid no app.
5. **Menor privilégio.** Helper só com API declarativa allowlisted; socket `0600` + `SO_PEERCRED`; nunca TCP.
6. **Sem segredos em log.** Redaction obrigatória; nunca logar tokens, senhas, chaves ou `Authorization`.
7. **Nada de trabalho pesado na UI.** Streams/jobs em worker threads; UI nunca bloqueia > 16 ms.
8. **Toda mudança de comportamento atualiza a doc afetada** (architecture/api-rpc/settings/ui-map/cli-parity/ADR).
9. **Toda mudança termina com evidência** de teste/lint.
10. **Sem escopo extra.** Não refatore, renomeie ou "melhore" o que a mudança não pediu.

---

## 4. Estrutura do repositório

```
.
├── README.md                 # visão geral e índice
├── AGENTS.md                 # este arquivo
├── LICENSE                   # GPL-3.0-or-later
├── NOTICE                    # aviso de copyright/licença
├── CHANGELOG.md              # histórico de versões
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
├── docs/                     # engenharia, ADRs e documentação de usuário
│   ├── README.md             # índice e convenções
│   ├── engineering/          # referência técnica
│   │   ├── architecture.md
│   │   ├── data-model.md
│   │   ├── api-rpc.md
│   │   ├── ui-map.md
│   │   ├── security.md
│   │   ├── settings.md
│   │   ├── cli-parity.md
│   │   ├── packaging.md
│   │   ├── web.md
│   │   └── branding.md
│   ├── adr/
│   └── user/
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

1. **Contexto**: leia a issue (ou abra uma) e o documento de engenharia/ADR da área.
2. **Planeje** em voz alta (ou com `planner`): arquivos, abordagem, testes, riscos.
3. **Branch**: trabalhe fora de `main` (ela é protegida); um assunto por branch.
4. **Implemente** o menor incremento que resolve o problema.
5. **Teste**: unitário + integração quando aplicável; rode `clang-format`/`clang-tidy`/`qmllint`.
6. **Atualize a doc** se o comportamento/contrato mudou.
7. **Registre evidência**: comando, resultado, cobertura, screenshot/log.
8. **PR** descreve o problema, a solução, os testes executados e os riscos.

**Nunca:**

- Começar pelo polimento visual antes do comportamento.
- Adicionar dependência sem justificar e documentar em `docs/engineering/packaging.md`.
- Alterar contrato RPC sem versionar e atualizar `api-rpc.md`.
- Alterar settings sem atualizar `settings.md` e os defaults.

---

## 6. Build e testes

Ambiente de referência: **Fedora 43/44** (Podman ≥ 5.8, Qt ≥ 6.10) e **Ubuntu 24.04+**
(Podman ≥ 4.9, Qt ≥ 6.4; o DEB usa o qtermwidget 2.0.0 vendorizado), `/dev/kvm` para machines.

```bash
# dependências
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtwayland-devel \
  qt6-qt5compat-devel kf6-kstatusnotifieritem-devel \
  qtermwidget-devel libarchive-devel libcurl-devel libxcrypt-devel sqlite-devel \
  openssl-devel docker-compose openssh-clients

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

**Verificação mínima por mudança:**

- Build sem warnings novos.
- `clang-format --dry-run --Werror` e `clang-tidy`.
- Testes relevantes passando (unit + integração).
- `qmllint` para QML.
- `scripts/qml-smoke.sh` (carrega todas as páginas em offscreen e falha em warning de QML) quando mexer em QML.
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

---

## 8. Convenções de documentação

- Decisão arquitetural → ADR em `docs/adr/` (contexto, decisão, alternativas, consequências).
- Novo método RPC → `docs/engineering/api-rpc.md` + versão da API se incompatível.
- Nova chave de setting → `docs/engineering/settings.md` (tipo/default/validação/restart) + schema.
- Comando/flags de CLI → `docs/engineering/cli-parity.md` e `docs/user/cli.md`.
- Tela/componente QML → `docs/engineering/ui-map.md`.
- Mudança de marca → `docs/engineering/branding.md`.
- Mudança no site → `docs/engineering/web.md` + `scripts/web_build.py --check`.

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

Detalhes em [`docs/engineering/security.md`](docs/engineering/security.md).

---

## 10. Marca e assets

- **Direção aprovada:** **cápsula** (símbolo principal) + **camadas** (símbolo secundário). O **monograma P não foi aprovado** — não usar.
- Usar os arquivos em [`assets/`](assets) e as regras em [`docs/engineering/branding.md`](docs/engineering/branding.md):
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
- Alterações de marca exigem aprovação de Produto + Design e atualização de `docs/engineering/branding.md`.

---

## 11. Git e commits

- **Conventional Commits** em português: `feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `build:`, `chore:`.
- Um commit por mudança lógica; mensagem explica o **porquê**.
- Nunca commitar `.env`, segredos, chaves ou o DMG analisado.
- Nunca `git push --force` em branch compartilhada.
- `main` é protegida: tudo entra por PR com CI verde.
- PR descreve problema, solução, testes executados e riscos.

---

## 12. Decisões ratificadas

| ID | Decisão | Onde |
|---|---|---|
| Q1 | Licença **GPL-3.0-or-later**; terminal **QTermWidget** (GPL-2.0-or-later, compatível) | [`ADR-0005`](docs/adr/0005-terminal-backend.md) |
| Q2 | k3s em VM dedicada; kind fora do v1 | [`ADR-0003`](docs/adr/0003-machines-qemu-kvm.md), [architecture](docs/engineering/architecture.md) |
| Q3 | Flatpak não no v1; reavaliar sob demanda | [`packaging.md`](docs/engineering/packaging.md) |
| Q4 | rootful suportado como opt-in explícito | [`security.md`](docs/engineering/security.md), [`settings.md`](docs/engineering/settings.md) |
| Q5 | `*.podstack.local` via helper; `*.localhost` é o padrão | [`ADR-0004`](docs/adr/0004-domains-localhost-helper.md) |
| Q6 | telemetria opt-in por categoria, off por padrão | [`settings.md`](docs/engineering/settings.md) |
| Q7 | pt-BR e en-US, extensível depois | [`ui-map.md`](docs/engineering/ui-map.md) |
| Q8 | Docker e Podman coexistem; remoção do Docker é opt-in | [`ADR-0008`](docs/adr/0008-docker-coexistence-setup.md) |
| Q9 | onboarding/setup em 7 etapas, não destrutivo, via helper allowlisted | [`ADR-0008`](docs/adr/0008-docker-coexistence-setup.md), [`ui-map.md`](docs/engineering/ui-map.md) |
| Q10 | Pods do Podman no grupo Engine; Kubernetes só para k3s | [`ui-map.md`](docs/engineering/ui-map.md) |

**Decisões acima ratificadas em 2026-11-08.** Novas decisões: registre um ADR e peça confirmação.

---

## 13. Definição de pronto (DoD)

Uma mudança só está concluída quando:

- [ ] O problema/requisito da issue está resolvido e verificado.
- [ ] Código compila sem warnings novos; formatação/lint aplicados.
- [ ] Teste automatizado cobre o comportamento (ou justificativa explícita).
- [ ] Documentação afetada atualizada.
- [ ] Nenhum segredo em log; permissões corretas.
- [ ] Evidência (comando + resultado) registrada no PR.

---

## 14. Índice completo da documentação

**Índice:** [`docs/README.md`](docs/README.md)

**Engenharia:** [`architecture.md`](docs/engineering/architecture.md) · [`data-model.md`](docs/engineering/data-model.md) · [`api-rpc.md`](docs/engineering/api-rpc.md) · [`ui-map.md`](docs/engineering/ui-map.md) · [`branding.md`](docs/engineering/branding.md) · [`cli-parity.md`](docs/engineering/cli-parity.md) · [`settings.md`](docs/engineering/settings.md) · [`security.md`](docs/engineering/security.md) · [`packaging.md`](docs/engineering/packaging.md) · [`web.md`](docs/engineering/web.md)

**Usuário:** [`user/README.md`](docs/user/README.md) · [`install.md`](docs/user/install.md) · [`quickstart.md`](docs/user/quickstart.md) · [`cli.md`](docs/user/cli.md) · [`troubleshooting.md`](docs/user/troubleshooting.md)

**ADRs:** [`0001`](docs/adr/0001-daemon-json-rpc.md) · [`0002`](docs/adr/0002-libpod-libcurl.md) · [`0003`](docs/adr/0003-machines-qemu-kvm.md) · [`0004`](docs/adr/0004-domains-localhost-helper.md) · [`0005`](docs/adr/0005-terminal-backend.md) · [`0006`](docs/adr/0006-rpm-first.md) · [`0007`](docs/adr/0007-compose-provider.md) · [`0008`](docs/adr/0008-docker-coexistence-setup.md) · [`0009`](docs/adr/0009-tray-sni-lifecycle.md) · [`0010`](docs/adr/0010-deb-ubuntu-qtermwidget-vendorizado.md) · [`0012`](docs/adr/0012-update-logging.md)

**Assets:** [`podstack-logo.svg`](assets/podstack-logo.svg) · [`podstack-capsule.svg`](assets/podstack-capsule.svg) · [`podstack-layers.svg`](assets/podstack-layers.svg) · [`podstack-capsule-small.svg`](assets/podstack-capsule-small.svg) · [`podstack-icon.svg`](assets/podstack-icon.svg) · [`png/`](assets/png) · [`favicon.ico`](assets/favicon.ico) · [`og-image.png`](assets/og-image.png) · [`conceitos`](assets/podstack_logo_concepts.svg) · [`racional`](assets/logo.md)

---

## 15. Primeiro comando

Se você é um agente começando agora:

1. Leia a issue (ou os documentos de engenharia da área) e este arquivo.
2. Verifique se o ambiente tem as dependências da §6; se não, peça confirmação antes de instalar.
3. Rode `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` para confirmar a base verde.
4. Implemente **apenas** o escopo pedido, com teste e evidência.
5. Atualize a doc afetada e abra o PR.
