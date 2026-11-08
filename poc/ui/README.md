# PodStack — UI Concept (POC)

> Protótipo navegável para **validar o design antes da implementação** no app Qt 6/QML.
> Não é o app: é a referência visual, de componentes, de layout e de fluxo.

---

## 1. Como abrir

```bash
# a partir da raiz do repositório
python3 -m http.server 4173 --directory poc/ui
# abra http://localhost:4173/
```

Ou, servindo o repositório inteiro:

```bash
python3 -m http.server 4173
# abra http://localhost:4173/poc/ui/
```

Não há build, dependências ou backend. A fonte **Inter** é carregada do Google Fonts; se não houver internet, o fallback é `system-ui`/`Cantarell`/`Noto Sans`.

---

## 2. O que a demo cobre

| Área | Conteúdo |
|---|---|
| **Shell do desktop** | Barra do GNOME (Atividades, relógio, tray), janela com titlebar, minimizar/maximizar/fechar, dock. **Moldura de demonstração**, não faz parte do app. |
| **Containers** | Lista, filtros, busca, multi-seleção, inspector com Info/Logs/Stats/Files, gráficos de CPU e RAM, criar, lifecycle, terminal, renomear, duplicar, remover. |
| **Images** | Tabela, pull, importar, prune, remover, dangling, uso. |
| **Volumes** | Tabela, criar, clone, importar do host, prune, remover. |
| **Networks** | Tabela, criar, remover, conectar/desconectar. |
| **Compose** | Projetos por labels, serviços, status, up/down/restart/logs, abrir YAML/diretório. |
| **Builds** | Histórico, novo build, progresso/log, cancelar, remover. |
| **Pods** | Tabela de pods, status, containers, IP. |
| **Machines** | Tabela + detalhe, recursos, SSH, mounts, portas, boot log, criar, start/stop/restart, terminal, remover. |
| **Devices** | USB/serial, anexar/desanexar, escanear, erro e retry. |
| **Onboarding** | Assistente de primeira execução em 7 etapas, fora da janela do app: diagnóstico, Docker → Podman; QEMU/KVM; componentes; validação. |
| **Temas** | Escuro (padrão) e claro. |

---

## 3. Arquivos

```text
poc/ui/
├── index.html          # shell do desktop + janela + app
├── styles.css          # tokens, layout e todos os componentes
├── app.js              # dados mock, estado, navegação e ações simuladas
├── README.md           # este arquivo
├── DESIGN-SYSTEM.md    # cores, tipografia, espaçamento, componentes
├── SCREENS.md         # especificação tela a tela
├── ONBOARDING.md      # assistente de primeira execução
├── FLOWS.md           # fluxos e diagramas
├── DATA-MODEL.md      # shapes dos dados mock e mapeamento RPC
└── IMPLEMENTATION.md  # guia para implementar em Qt 6/QML
```

---

## 4. Documentos

| Documento | Use quando… |
|---|---|
| [DESIGN-SYSTEM.md](DESIGN-SYSTEM.md) | for criar/alterar componentes, tokens, tipografia ou layout. |
| [SCREENS.md](SCREENS.md) | for implementar uma tela, colunas, ações, estados ou inspector. |
| [ONBOARDING.md](ONBOARDING.md) | for implementar o primeiro uso, o setup da máquina e o fluxo Docker → Podman. |
| [FLOWS.md](FLOWS.md) | precisar entender a navegação e as sequências de ação. |
| [DATA-MODEL.md](DATA-MODEL.md) | precisar dos campos, exemplos e mapeamento para o RPC/daemon. |
| [IMPLEMENTATION.md](IMPLEMENTATION.md) | for traduzir a POC para QML, componentes, Backend e testes. |

---

## 5. Tour rápido

1. Abra a demo. O app já está aberto em **Containers**.
2. Clique em um container (`api`) e navegue pelas abas **Info**, **Logs**, **Stats** (gráficos de CPU e RAM) e **Files**.
3. Use o botão **Novo container**, `Start all`, os filtros e a busca.
4. Troque para **Images**, **Volumes**, **Networks**, **Compose**, **Builds**, **Pods**, **Machines** e **Devices** na sidebar.
5. Clique no botão **Configurar máquina** na barra do desktop (ou no ícone de setup na dock) para abrir o **onboarding**.
6. No onboarding, percorra as 7 etapas; na etapa **Docker → Podman**, use o seletor **Cenário de demonstração** para alternar entre Podman ausente, desatualizado (5.4.2) e atualizado (5.8.9) e veja o rótulo/ação mudar.
7. Use o botão sol/lua no header do app para alternar o tema.

---

## 6. Limites da POC (importante)

- Todos os dados são **mock** e vivem em `app.js`; nada é persistido.
- Todas as ações são **simuladas** (pull, build, instalar/remover pacotes, etc.); **nada altera o sistema**.
- Não há daemon, RPC, SQLite, QEMU, Podman ou rede.
- O **shell do desktop** (barra do GNOME, dock, wallpaper, controles da janela) é apenas uma moldura para visualizar o app aberto; **não implementar no app**.
- O seletor **Cenário de demonstração** do onboarding existe só para validar os caminhos de instalação/atualização; **não deve existir no app real**.
- Os controles minimizar/maximizar/fechar são da moldura da demo; o app real usa a janela nativa do sistema.
- O onboarding é uma simulação de UX; a implementação real precisa de detecção do sistema, consentimento, helper privilegiado e jobs com progresso/cancelamento.

---

## 7. Clean-room e marca

- A POC é **inspirada no estilo** do OrbStack (densidade, sidebar, listas, cartões, tipografia), sem copiar código, assets, ícones, textos ou layout pixel a pixel.
- A marca é **PodStack** (cápsula + camadas). Use os assets de [`assets/`](../../assets/) e as regras de [`docs/branding.md`](../../docs/branding.md).
- “OrbStack” é marca registrada de terceiros; não use o nome em telas, textos ou assets do produto.

---

## 8. Relação com a especificação

A POC é a **referência visual**. A fonte de verdade de comportamento continua sendo:

- [`docs/PRD.md`](../../docs/PRD.md) — requisitos e critérios de aceite.
- [`docs/spec.md`](../../docs/spec.md) — fluxos detalhados.
- [`docs/ui-map.md`](../../docs/ui-map.md) — mapa de telas.
- [`docs/api-rpc.md`](../../docs/api-rpc.md) — contrato do daemon.
- [`docs/tasks.md`](../../docs/tasks.md) — marcos e pendências.

Quando a POC divergir da spec, **a spec vence**; atualize a spec antes de implementar.
