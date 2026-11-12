# 001 — Apps remotos da VM (xpra, RAIL e canal de guest)

> **Status:** proposta corrigida após revisão — não promovida ao núcleo SDD.
> **Marcos:** M9 (guest Windows, pré-requisito) e M10 (apps remotos: T10-01…T10-03).
> **Data:** 2026-11-12 (rev. 2).
> **Promove para:** [`../../sdd/PRD.md`](../../sdd/PRD.md) (FR-MAC-101…124), [`../../sdd/spec.md`](../../sdd/spec.md) §7, [`../../sdd/tasks.md`](../../sdd/tasks.md) (M9/M10).

---

## 1. Contexto

Hoje o único caminho para usar um app de uma machine na tela do host é **X11 forwarding sobre SSH** (T8-04, `machine ssh --x11`). Ele funciona, mas:

- o protocolo X faz milhares de round-trips por interação e não usa MIT-SHM entre host e guest; toolkits modernos degradam e o GL cai para software;
- depende de `xauth`, `PermitUserEnvironment` e de o `XAUTHORITY` sobreviver a `HOME` redirecionado (já gerou correção específica para o Firefox snap);
- **não existe equivalente para Windows**: o guest não tem SSH por padrão, não há display gráfico (`-display none`) e nenhum canal de guest (zero virtio-serial/vsock/qemu-ga).

Comunicação host↔guest hoje: **SSH/SCP** para terminal, stats, arquivos e provisionamento, mais **virtiofs** para mounts. O k3s já usa `cloud-init status --wait` (`src/k8s/K8sManager.cpp:1162`), mas não há detecção genérica de "guest pronto" por machine.

Sobre o console gráfico: ele está **adiado como P2** em `docs/adr/0003-machines-qemu-kvm.md:50`, `docs/sdd/plan.md:259` e `docs/sdd/PRD.md:309`; `VNC` é excluído **daquelas tarefas específicas** em `docs/sdd/spec.md:432`, `docs/sdd/tasks.md:136` e `docs/engineering/api-rpc.md:321`. Este delta **reverte o adiamento** para permitir instalar Windows, e isso é declarado em FR-MAC-111 — não é uma decisão silenciosa.

## 2. Objetivos

| # | Objetivo | Métrica |
|---|---|---|
| O1 | Apps Linux com qualidade perceptivelmente melhor que X11 forwarding | app já instalado abre em ≤ 2 s; janela redimensiona sem travar |
| O2 | Apps Windows por RAIL, sem embutir protocolo na UI | duas janelas na mesma sessão RDP |
| O3 | Segundo canal de guest independente de rede | `guest-ping` responde com a VM sem IP |
| O4 | API e UX únicas, backends plugáveis | `machines.apps.*` igual para Linux e Windows |

## 3. Não-objetivos

- Substituir SSH no Linux (continua o canal padrão; `~/.ssh/config`, terminal e `machine ssh` permanecem).
- Remover X11 forwarding (vira modo legado, mantido e testado).
- Console gráfico como experiência de uso diário — o SPICE de M9 existe para instalação e emergência, não para uso contínuo.
- Wayland/Waypipe no v1 (P2 — casamento de versão de protocolo é frágil).
- GPU/3D, passthrough VFIO, DRM/anticheat.
- Seamless em Windows Home (sem servidor RDP).

## 4. Requisitos funcionais

### 4.1 Canal de guest

| ID | Requisito | P |
|---|---|---|
| FR-MAC-101 | A machine expõe um canal de guest plugável (`ssh \| agent`), resolvido por capability e por SO do guest. | P0 |
| FR-MAC-102 | O canal `agent` usa `qemu-guest-agent` sobre virtio-serial e responde `guest-ping` **sem** rede no guest. | P0 |
| FR-MAC-103 | O canal `agent` executa comandos e transfere arquivos **pequenos** (≤ 1 MiB: scripts, JSON, ícones); transferência de usuário continua no backend existente (`scp`). | P0 |
| FR-MAC-104 | O canal `agent` informa IP e SO do guest sem SSH (`guest-network-get-interfaces`, `guest-get-osinfo`). | P0 |
| FR-MAC-105 | A machine permanece no estado existente `provisioning` até o agente responder; a transição para `running` é dirigida por `guest-ping` e exposta como capability `guestReady`. **Nenhum estado novo** é criado em `MachineMeta`/RPC. | P0 |
| FR-MAC-106 | Falha do canal primário cai para o secundário (e vice-versa) com erro classificado, sem travar a UI. | P1 |

### 4.2 Apps Linux (xpra)

| ID | Requisito | P |
|---|---|---|
| FR-MAC-107 | Machines Linux X11 oferecem o backend `xpra` em modo `seamless`, com uma janela nativa por app. | P0 |
| FR-MAC-108 | O binário `xpra` é exigido **no host** (checado no setup, com override `PODSTACK_XPRA_BINARY`); no guest é instalado **sob demanda e com consentimento**, nunca silenciosamente. | P0 |
| FR-MAC-109 | X11 forwarding continua disponível como modo legado, sem regressão de CLI, RPC ou testes. | P0 |
| FR-MAC-110 | Clientes de app (xpra, FreeRDP) rodam na **sessão gráfica do usuário**, nunca no daemon; nenhuma janela remota é embutida no QML. | P0 |

### 4.3 Guest Windows — pré-requisito (M9)

| ID | Requisito | P |
|---|---|---|
| FR-MAC-111 | O PodStack oferece console gráfico para instalação/emergência de guests Windows via SPICE em socket Unix, com viewer em janela própria. **Reverte o adiamento P2** e exige atualizar ADR-0003, `spec.md` §7, `plan.md` e `api-rpc.md`. | P0 |
| FR-MAC-112 | Machines Windows usam OVMF Secure Boot (`OVMF_CODE/VARS.secboot`, `-machine q35,smm=on`, `-global cfi.pflash01.secure=on`) com NVRAM por VM. | P0 |
| FR-MAC-113 | Machines Windows usam TPM 2.0 via `swtpm` com estado persistido; clone regenera o TPM e export/import não transporta o estado. | P0 |
| FR-MAC-114 | Criação de machine Windows aceita ISO do usuário (nunca distribuída pelo PodStack), `autounattend.xml` gerado, `virtio-win.iso` anexado, default ≥ 4 GB RAM / 64 GB disco e enlightenments `hv_*`; sem cloud-init. | P0 |
| FR-MAC-115 | O provisionamento instala drivers virtio e o `qemu-guest-agent`; a machine só é considerada operacional quando o agente responde. | P0 |
| FR-MAC-116 | Áudio do guest Windows usa o `intel-hda` duplex já existente, com nó PipeWire nomeado por VM e captura validada. | P1 |

### 4.4 Apps Windows (RAIL)

| ID | Requisito | P |
|---|---|---|
| FR-MAC-117 | Machines Windows oferecem o backend `rail` (RDP RemoteApp), condicionado a M9. | P1 |
| FR-MAC-118 | O host mantém **uma** sessão RDP por machine e lança apps adicionais nela (Windows client SKU permite uma sessão interativa). | P1 |
| FR-MAC-119 | A senha RDP nunca aparece em argv, log ou `meta.json`; o certificado é fixado por TOFU (SHA-256 em `MachineMeta`) e a conexão só escuta em `127.0.0.1`. | P0 |
| FR-MAC-120 | Windows Home não oferece `rail`; a UI explica o motivo em vez de esconder a ação sem contexto. | P1 |

### 4.5 Catálogo, diagnóstico e ciclo de vida

| ID | Requisito | P |
|---|---|---|
| FR-MAC-121 | `machines.apps.list/launch/stop` é o contrato único; o catálogo vem do guest (`.desktop` no Linux, Start Menu no Windows) com ícone e executável. | P0 |
| FR-MAC-122 | Falha ao abrir app retorna código `PODSTACK_*` estável + `reason` (`backend_missing`, `guest_unreachable`, `app_not_found`, `rdp_auth_failed`, `session_conflict`, `backend_unsupported`, `gui_required`). | P1 |
| FR-MAC-123 | `machines.stats` e a identificação da machine informam `origin` (`ssh` \| `agent`) e continuam funcionando com a rede do guest bloqueada. | P1 |
| FR-MAC-124 | Parar, deletar ou perder o daemon encerra as sessões remotas (xpra/RAIL) e os processos locais, sem órfãos. | P1 |

## 5. Requisitos não funcionais

| ID | Requisito |
|---|---|
| NFR-MAC-001 | Abertura de app já instalado ≤ 2 s (Linux, host local, sem swap) — teste E2E. |
| NFR-MAC-002 | Nenhuma credencial em argv/log/`meta.json`; teste inspeciona `/proc/<pid>/cmdline` do cliente. |
| NFR-MAC-003 | `qga.sock` vive em `machineDir` (0700); RDP escuta só em `127.0.0.1`. |
| NFR-MAC-004 | Ausência de backend não impede o uso da machine: X11/SSH continuam funcionando. |
| NFR-MAC-005 | Zero regressão nos 17 binários de teste existentes; gate de cobertura de 80% mantido. |
| NFR-MAC-006 | O setup descobre dependências por candidatos **por distro** (nome de pacote, nome de binário e caminho de firmware) e nunca sugere pacote inexistente; teste cobre Fedora e Ubuntu. |

## 6. Casos de uso

**UC-1 — Abrir app Linux por xpra.** O usuário escolhe "Abrir app da VM" e um app do catálogo; a GUI verifica o binário no host, instala xpra no guest (com consentimento, se preciso) e abre a janela nativa. Fechar a janela encerra a sessão.

**UC-2 — Abrir app Windows por RAIL.** Com a machine M9 pronta, o host abre a sessão RAIL, publica o catálogo do Start Menu e lança apps nela; clipboard e drive redirect funcionam.

**UC-3 — VM sem rede.** Stats e identificação continuam disponíveis pelo agente, com `origin=agent`; a UI explica que o SSH está indisponível.

**UC-4 — App não abre.** O erro traz `PODSTACK_*` + `reason` e a ação sugerida.

## 7. Critérios de aceite

| ID | Dado | Quando | Então |
|---|---|---|---|
| AC-1 | Ubuntu 24.04 com xpra instalado | `machines.apps.launch` para `firefox` | janela nativa abre em ≤ 2 s e a sessão aparece em `machines.apps.list` |
| AC-2 | mesma machine, xpra ausente no host | launch | `PODSTACK_DEPENDENCY_MISSING` com `reason=backend_missing` e o pacote exato; X11 continua |
| AC-3 | guest com firewall bloqueando SSH | `machines.stats` | responde com `origin=agent` |
| AC-4 | QEMU com chardev do agente | daemon reinicia | reconecta (ou reabre via QMP) sem reiniciar a VM |
| AC-5 | Windows 11 Pro com RDP habilitado | dois apps abertos | **uma** sessão RDP; segunda janela abre na mesma sessão |
| AC-6 | Windows 11 Home | launch com backend `rail` | `PODSTACK_UNSUPPORTED` com `reason=backend_unsupported` |
| AC-7 | machine Linux existente | upgrade | nada muda: canal `ssh`, backend `x11`, sem migração |
| AC-8 | launch RAIL | inspeção de `/proc/<pid>/cmdline` | senha ausente |
| AC-9 | sessão xpra aberta | stop/delete da machine ou kill do daemon | nenhum processo remoto/local órfão |
| AC-10 | daemon com `PrivateTmp`/`ProtectHome` | launch | cliente nasce na GUI; daemon não tenta abrir display |

## 8. Fora de escopo

Waypipe; console gráfico como modo de uso diário; GPU; áudio por RDP como padrão; publicação de apps para outros hosts da rede; Windows Home seamless; Windows ARM.

## 9. Decisões abertas

| ID | Decisão | Impacto |
|---|---|---|
| D1 | X11 forwarding é depreciado em favor de xpra ou permanece indefinidamente? | aviso/prazo em `machine ssh --x11` |
| D2 | xpra entra no v1 ou é fast-follow? (M10 independe de M9) | ordem de entrega |
| D3 | Áudio do xpra (`--audio=yes`) ou manter HDA→PipeWire por VM? | latência vs. áudio por app |
| D4 | Broker RAIL: `xfreerdp` com sessão única ou helper com `libfreerdp-client`? | esforço/robustez de T10-03 |

## 10. Rastreabilidade

| Requisito | Tarefa |
|---|---|
| FR-MAC-101…106 | T10-01 |
| FR-MAC-107…110, 121 (Linux) | T10-02 |
| FR-MAC-111…116 | T9-01…T9-06 |
| FR-MAC-117…120, 121 (Windows), 122 | T10-03 |
| FR-MAC-123 | T10-01 |
| FR-MAC-124 | T10-02, T10-03 |
| NFR-MAC-006 | T9-01, T10-02 |

## 11. Dependências de host por distro

Nada aqui é dependência de instalação do PodStack. Ausência gera `PODSTACK_DEPENDENCY_MISSING` com `reason=backend_missing` e o pacote exato (FR-MAC-122); o restante do app continua funcionando. Nomes validados em **2026-11-12** contra Fedora 44 (`rpm`/`dnf`) e Ubuntu 24.04 noble (`packages.ubuntu.com`).

| Componente | Fedora 43/44 | Ubuntu 24.04 (noble) | Binário esperado | Observação |
|---|---|---|---|---|
| xpra (cliente) | `xpra` (6.5.4) | `xpra` (3.1.5, universe) | `xpra` | versão do Ubuntu é antiga; ver §11.2 |
| FreeRDP (RAIL) | `freerdp` (3.32.2) | `freerdp3-x11` (3.32.2, universe) | `xfreerdp` / **`xfreerdp3`** | **nome do binário difere**; usar `PODSTACK_FREERDP_BINARY` |
| TPM 2.0 | `swtpm` (0.10.2) | `swtpm` (0.7.3, main) | `swtpm` | — |
| Firmware Secure Boot | `edk2-ovmf` | `ovmf` (main) | — | caminhos diferentes; ver §11.1 |
| Módulo SPICE do QEMU | `qemu-ui-spice-core` | `qemu-system-modules-spice` | — | sem ele, `-spice` falha |
| Áudio PipeWire no QEMU | `qemu-audio-pipewire` | `qemu-system-gui` (`audio-pipewire.so`) | — | **pré-requisito do T8-03 no Ubuntu, hoje não declarado** |
| virtiofsd | `virtiofsd` (1.14.0) | `virtiofsd` (1.10.0, universe) | `virtiofsd` | já usado por T4-07 |
| Viewer SPICE | `virt-viewer` | `virt-viewer` (universe) | `remote-viewer` | evita `spice-gtk` como dependência de build |
| ISO virtio-win | — (sem pacote) | — (sem pacote) | — | download com SHA-256 ou arquivo do usuário |

`Debian 12` fica **fora** desta matriz: o `.deb` não o suporta (`docs/engineering/packaging.md` §8). Alvos DEB: Ubuntu 24.04, 25.10 e 26.04.

### 11.1 Caminhos do OVMF (não fixar um só)

| Distro | CODE | VARS |
|---|---|---|
| Fedora | `/usr/share/edk2/ovmf/OVMF_CODE.secboot.fd` | `/usr/share/edk2/ovmf/OVMF_VARS.secboot.fd` |
| Ubuntu | `/usr/share/OVMF/OVMF_CODE_4M.secboot.fd` | `/usr/share/OVMF/OVMF_VARS_4M.ms.fd` |

O Ubuntu **não publica** `OVMF_VARS_4M.secboot.fd`; a variante com Secure Boot usa `OVMF_VARS_4M.ms.fd` (certificados Microsoft). A descoberta deve ser por lista de candidatos, nunca por caminho fixo.

### 11.2 xpra: versão mínima

| Base | Versão | Status |
|---|---|---|
| Fedora 43/44 | 6.5.4 | suportado |
| Ubuntu 24.04 | 3.1.5 (2021) | **experimental** até validar; recomendado repositório oficial do xpra (≥ 6) |
| Ubuntu 26.04 | não listado | sem pacote; mesma instrução |

O PodStack **não adiciona repositório de terceiros automaticamente** — apenas instrui. Em qualquer caso, X11 forwarding segue como fallback (FR-MAC-109) e nenhuma versão é instalada sem consentimento (FR-MAC-108).

### 11.3 Onde cada dependência entra no produto

| Mecanismo | O que entra | Regra |
|---|---|---|
| `Recommends` do RPM/DEB | freerdp, swtpm, firmware, módulos SPICE/áudio | instalado por padrão; removível |
| Etapa do setup via `InstallPackages` | o que faltar, incluindo xpra | consentimento + polkit; allowlist por gerenciador |
| Download em runtime | ISO virtio-win | SHA-256 + consentimento + cache local |

`Requires`/`Depends` fortes são proibidos para estes itens: quebrariam instalação mínima e o `scripts/clean-install-check.sh`.
