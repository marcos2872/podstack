# PodStack — Documentação do projeto

**PodStack** é uma alternativa Linux ao OrbStack: um app desktop para gerenciar containers, imagens, volumes, redes, builds, Compose, máquinas Linux (VMs), Kubernetes e domínios locais sobre o **Podman Engine** nativo.

> **Status:** **M3 concluído** (Compose, builds com histórico SQLite, manifest list multi-plataforma e eventos globais) sobre o M2 (imagens/volumes/redes/pods) e o M1. Ressalvas para o M4: Supabase, LAN/proxy, terminal embutido e prévia de prune.
> **UI Concept:** POC navegável em [`../poc/ui/`](../poc/ui/README.md) — referência visual para o redesign; **não** é o app.
> **Licença:** GPL-3.0-or-later.
> **Baseline funcional:** OrbStack 2.2.5 (build 21124), analisado estaticamente. Ver [`reference/orbstack-2.2.5.md`](reference/orbstack-2.2.5.md).
> **Nome/branding:** PodStack é um projeto independente, sem afiliação com a Orbital Labs. Nenhum asset, ícone ou código proprietário do OrbStack é reutilizado.

## Como ler

O projeto segue **SDD (Spec-Driven Development)**: a especificação é a fonte de verdade; código e testes são derivados dela.

| Ordem | Documento | Pergunta que responde |
|---|---|---|
| 1 | [`constitution.md`](constitution.md) | Quais princípios inegociáveis guiam o projeto? |
| 2 | [`PRD.md`](PRD.md) | O que vamos construir, para quem, por quê e com quais critérios? |
| 3 | [`spec.md`](spec.md) | Como cada funcionalidade se comporta, em detalhe? |
| 4 | [`plan.md`](plan.md) | Como vamos construir tecnicamente? |
| 5 | [`tasks.md`](tasks.md) | Quais tarefas executáveis levam aos marcos? |
| 6 | [`testing.md`](testing.md) | Como provamos que está correto? |

### Mapeamento SDD (Spec Kit)

| Etapa do Spec Kit | Artefato neste repositório |
|---|---|
| `/speckit.constitution` | [`constitution.md`](constitution.md) |
| `/speckit.specify` | [`PRD.md`](PRD.md) + [`spec.md`](spec.md) |
| `/speckit.plan` | [`plan.md`](plan.md) + [`architecture.md`](architecture.md) |
| `/speckit.tasks` | [`tasks.md`](tasks.md) |
| `/speckit.implement` | `src/` (a criar no M0) |
| `/speckit.analyze` | [`testing.md`](testing.md) + checklist do PRD §14 |

Os artefatos foram escritos diretamente em Markdown, no formato do Spec Kit, sem depender do CLI `specify`.

## Documentos de engenharia

| Documento | Conteúdo |
|---|---|
| [`architecture.md`](architecture.md) | Componentes, diagramas C4, fluxos e modelo de concorrência |
| [`data-model.md`](data-model.md) | Entidades, estados e esquema de persistência |
| [`api-rpc.md`](api-rpc.md) | Contrato JSON-RPC do daemon (`podstackd`) |
| [`ui-map.md`](ui-map.md) | Telas, navegação, componentes QML, atalhos e estados |
| [`branding.md`](branding.md) | Logo, paleta, tipografia, uso e assets em [`../assets/`](../assets/) |
| [`cli-parity.md`](cli-parity.md) | Mapeamento `orb` → `podstack` e diferenças de plataforma |
| [`settings.md`](settings.md) | Chaves, defaults, validação e arquivo de configuração |
| [`security.md`](security.md) | Modelo de ameaças, privilégios, helper, polkit e segredos |
| [`packaging.md`](packaging.md) | RPM, systemd, polkit, SELinux, AppImage e avaliação de Flatpak |
| [`roadmap.md`](roadmap.md) | Marcos M0–M8, entregáveis e critérios de saída |
| [UI Concept (POC)](../poc/ui/README.md) | Referência visual navegável (HTML/CSS/JS) para o redesign; **não** é o app |

## Decisões arquiteturais (ADRs)

| ADR | Decisão |
|---|---|
| [`adr/0001-daemon-json-rpc.md`](adr/0001-daemon-json-rpc.md) | Daemon de usuário com JSON-RPC sobre Unix socket |
| [`adr/0002-libpod-libcurl.md`](adr/0002-libpod-libcurl.md) | Libpod REST + libcurl para streams/hijack |
| [`adr/0003-machines-qemu-kvm.md`](adr/0003-machines-qemu-kvm.md) | QEMU/KVM direto + cloud-init + virtiofs |
| [`adr/0004-domains-localhost-helper.md`](adr/0004-domains-localhost-helper.md) | `*.localhost` no v1; `*.podstack.local` via helper |
| [`adr/0005-terminal-backend.md`](adr/0005-terminal-backend.md) | QTermWidget (GPL-2.0-or-later, compatível com GPL-3.0-or-later) |
| [`adr/0006-rpm-first.md`](adr/0006-rpm-first.md) | RPM como formato primário |
| [`adr/0007-compose-provider.md`](adr/0007-compose-provider.md) | Compose via provider Docker Compose + labels |
| [`adr/0008-docker-coexistence-setup.md`](adr/0008-docker-coexistence-setup.md) | Docker e Podman coexistem; setup não destrutivo e remoção opt-in |
| [`adr/0009-tray-sni-lifecycle.md`](adr/0009-tray-sni-lifecycle.md) | Bandeja SNI (`QSystemTrayIcon`), fechar-para-bandeja e instância única |

## Referência

- [`reference/orbstack-2.2.5.md`](reference/orbstack-2.2.5.md) — inventário funcional, UI, CLI e chaves extraídos do DMG 2.2.5.
- [`reference/orbstack-2.2.5-official-notes.md`](reference/orbstack-2.2.5-official-notes.md) — correções e detalhes da documentação oficial (abas reais, settings, CLI).
- `reference/cli-tree.json` — árvore completa do CLI (51 nós, flags).
- [`reference/cli-tree.md`](reference/cli-tree.md) — versão legível da árvore.
- `reference/swift-files.txt` — 29 arquivos de tela/modelo Swift identificados.
- `reference/settings-keys.txt` — chaves de settings observadas.

## Assets e marca

**Marca aprovada:** cápsula (principal) + camadas (secundário); monograma P não aprovado.

- [`../assets/podstack-logo.svg`](../assets/podstack-logo.svg) — lockup (cápsula + wordmark).
- [`../assets/podstack-capsule.svg`](../assets/podstack-capsule.svg) — símbolo principal.
- [`../assets/podstack-layers.svg`](../assets/podstack-layers.svg) — símbolo secundário.
- [`../assets/podstack-capsule-small.svg`](../assets/podstack-capsule-small.svg) — cápsula compacta (16–24 px).
- [`../assets/podstack-icon.svg`](../assets/podstack-icon.svg) — ícone do app.
- [`../assets/png/`](../assets/png/) — ícones 16–512, bandeja 16/22/24/32/48/64 e folha de validação.
- [`../assets/favicon.ico`](../assets/favicon.ico) / [`favicon-32.png`](../assets/favicon-32.png) — favicon.
- [`../assets/og-image.png`](../assets/og-image.png) — Open Graph 1200×630 ([fonte SVG](../assets/og-image.svg)).
- [`../assets/podstack_logo_concepts.svg`](../assets/podstack_logo_concepts.svg) — folha histórica de conceitos.
- [`../assets/logo.md`](../assets/logo.md) — racional original dos conceitos.
- [`branding.md`](branding.md) — paleta, tipografia, uso, tamanhos mínimos e governança.

## Convenções

- Requisitos funcionais: `FR-<DOMÍNIO>-<NNN>` (ex.: `FR-CON-003`).
- Requisitos não funcionais: `NFR-<ÁREA>-<NNN>`.
- Tarefas: `T<marco>-<NN>` (ex.: `T1-04`).
- Prioridade: **P0** (v1 obrigatório), **P1** (v1 desejável / v1.x), **P2** (pós-v1).
- Toda mudança de escopo volta para `PRD.md` e `spec.md` antes de virar código.
