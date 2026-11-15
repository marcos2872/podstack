# PodStack — Documentação do projeto

**PodStack** é uma alternativa Linux ao OrbStack: um app desktop para gerenciar containers, imagens, volumes, redes, builds, Compose, máquinas Linux (VMs), Kubernetes e domínios locais sobre o **Podman Engine** nativo.

> **Licença:** GPL-3.0-or-later.
> **Nome/branding:** PodStack é um projeto independente, sem afiliação com a Orbital Labs. Nenhum asset, ícone ou código proprietário do OrbStack é reutilizado.

## Estrutura de `docs/`

| Diretório | Conteúdo |
|---|---|
| [`engineering/`](engineering/) | Referência técnica: arquitetura, data model, RPC, UI, segurança, settings, CLI, empacotamento, web e marca |
| [`adr/`](adr/) | Decisões arquiteturais (contexto, decisão, alternativas e consequências) |
| [`user/`](user/) | Documentação de usuário: instalação, primeiros passos, CLI e solução de problemas |

## Como ler

O comportamento do produto é descrito pelos documentos de engenharia e pelos ADRs. Para uma mudança de código, comece pela documentação da área tocada.

| Pergunta | Documento |
|---|---|
| Como o app é dividido e como os fluxos conversam? | [`engineering/architecture.md`](engineering/architecture.md) |
| Qual é o contrato JSON-RPC do daemon? | [`engineering/api-rpc.md`](engineering/api-rpc.md) |
| Quais telas, componentes e atalhos existem? | [`engineering/ui-map.md`](engineering/ui-map.md) |
| Quais chaves de settings existem, com que default? | [`engineering/settings.md`](engineering/settings.md) |
| Qual é o modelo de ameaças, privilégios e segredos? | [`engineering/security.md`](engineering/security.md) |
| Como o app é empacotado (RPM/DEB)? | [`engineering/packaging.md`](engineering/packaging.md) |
| Como o CLI se compara ao `orb`/`docker`? | [`engineering/cli-parity.md`](engineering/cli-parity.md) |
| Quais entidades e estados são persistidos? | [`engineering/data-model.md`](engineering/data-model.md) |
| Como o site público é gerado e publicado? | [`engineering/web.md`](engineering/web.md) |
| Como usar o app no dia a dia? | [`user/README.md`](user/README.md) |

## Documentos de engenharia

| Documento | Conteúdo |
|---|---|
| [`architecture.md`](engineering/architecture.md) | Componentes, diagramas C4, fluxos e modelo de concorrência |
| [`data-model.md`](engineering/data-model.md) | Entidades, estados e esquema de persistência |
| [`api-rpc.md`](engineering/api-rpc.md) | Contrato JSON-RPC do daemon (`podstackd`) |
| [`ui-map.md`](engineering/ui-map.md) | Telas, navegação, componentes QML, atalhos e estados |
| [`branding.md`](engineering/branding.md) | Logo, paleta, tipografia, uso e assets em [`../assets/`](../assets) |
| [`cli-parity.md`](engineering/cli-parity.md) | Mapeamento `orb` → `podstack` e diferenças de plataforma |
| [`settings.md`](engineering/settings.md) | Chaves, defaults, validação e arquivo de configuração |
| [`security.md`](engineering/security.md) | Modelo de ameaças, privilégios, helper, polkit e segredos |
| [`packaging.md`](engineering/packaging.md) | RPM, systemd, polkit, SELinux, AppImage e avaliação de Flatpak |
| [`web.md`](engineering/web.md) | Site público em GitHub Pages: estrutura de `web/`, publicação, gatilhos e rollback |

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
| [`adr/0010-deb-ubuntu-qtermwidget-vendorizado.md`](adr/0010-deb-ubuntu-qtermwidget-vendorizado.md) | DEB do Ubuntu 24.04 com QTermWidget vendorizado |
| [`adr/0012-update-logging.md`](adr/0012-update-logging.md) | Atualização segura (scripts + runner/helper) e logs de debug com retenção de 7 dias |

## Assets e marca

**Marca aprovada:** cápsula (principal) + camadas (secundário); monograma P não aprovado.

- [`../assets/podstack-logo.svg`](../assets/podstack-logo.svg) — lockup (cápsula + wordmark).
- [`../assets/podstack-capsule.svg`](../assets/podstack-capsule.svg) — símbolo principal.
- [`../assets/podstack-layers.svg`](../assets/podstack-layers.svg) — símbolo secundário.
- [`../assets/podstack-capsule-small.svg`](../assets/podstack-capsule-small.svg) — cápsula compacta (16–24 px).
- [`../assets/podstack-icon.svg`](../assets/podstack-icon.svg) — ícone do app.
- [`../assets/png/`](../assets/png) — ícones 16–512, bandeja 16/22/24/32/48/64 e folha de validação.
- [`../assets/favicon.ico`](../assets/favicon.ico) / [`favicon-32.png`](../assets/favicon-32.png) — favicon.
- [`../assets/og-image.png`](../assets/og-image.png) — Open Graph 1200×630 ([fonte SVG](../assets/og-image.svg)).
- [`../assets/podstack_logo_concepts.svg`](../assets/podstack_logo_concepts.svg) — folha histórica de conceitos.
- [`../assets/logo.md`](../assets/logo.md) — racional original dos conceitos.
- [`branding.md`](engineering/branding.md) — paleta, tipografia, uso, tamanhos mínimos e governança.

## Convenções

- Uso do CLI: `podstack <domínio> <ação>`; flags e exit codes em [`engineering/cli-parity.md`](engineering/cli-parity.md) e [`user/cli.md`](user/cli.md).
- Método RPC novo: documentar em [`engineering/api-rpc.md`](engineering/api-rpc.md) antes de implementar.
- Chave de setting nova: documentar em [`engineering/settings.md`](engineering/settings.md) (tipo, default, validação, restart).
- Tela/componente QML: documentar em [`engineering/ui-map.md`](engineering/ui-map.md).
- Decisão arquitetural: ADR em `docs/adr/` (contexto, decisão, alternativas, consequências).
