# 001 — Apps remotos da VM (xpra, RAIL e canal de guest)

> **Status:** proposta corrigida após revisão (rev. 4) — não promovida ao núcleo SDD.
> **Marcos:** M9 (guest Windows, pré-requisito de RAIL) e M10 (apps remotos: T10-00…T10-03).
> **Prioridade:** **P1 (v1.x, alvo 1.1+), não bloqueia v1.0** — `docs/README.md` define P1 como v1 desejável/v1.x.
> **Data:** 2026-11-12 (rev. 4).
> **Promove para:** [`../../sdd/PRD.md`](../../sdd/PRD.md) (FR-MAC-101…124, NFR-MAC-001…008, §3.1/§3.2, §6.2 e §7.7), [`../../sdd/spec.md`](../../sdd/spec.md) §7, [`../../sdd/tasks.md`](../../sdd/tasks.md) (M9/M10), [`../../sdd/plan.md`](../../sdd/plan.md), `api-rpc.md`, `settings.md`, `ui-map.md`, `data-model.md`, `cli-parity.md`, `security.md`, `architecture.md`, `packaging.md`, `testing.md`, ADR-0003 e **ADR-0011**.

---

## 1. Contexto

Hoje o único caminho para usar um app de uma machine na tela do host é **X11 forwarding sobre SSH** (T8-04, `machine ssh --x11`). Ele funciona, mas:

- o protocolo X faz milhares de round-trips por interação e não usa MIT-SHM entre host e guest; toolkits modernos degradam e o GL cai para software;
- depende de `xauth`, `PermitUserEnvironment` e de o `XAUTHORITY` sobreviver a `HOME` redirecionado (já gerou correção específica para o Firefox snap);
- **não existe equivalente para Windows**: o guest não tem SSH por padrão, não há display gráfico (`-display none`) e nenhum canal de guest (zero virtio-serial/vsock/qemu-ga).

Comunicação host↔guest hoje: **SSH/SCP** para terminal, stats, arquivos e provisionamento, mais **virtiofs** para mounts. O k3s já usa `cloud-init status --wait` (`src/k8s/K8sManager.cpp:1162`), mas não há detecção genérica de "guest pronto" por machine.

Sobre o console gráfico: ele está **adiado como P2** em `docs/adr/0003-machines-qemu-kvm.md:50`, `docs/sdd/plan.md:259` e `docs/sdd/PRD.md:309`; `VNC` é excluído **daquelas tarefas específicas** em `docs/sdd/spec.md:432`, `docs/sdd/tasks.md:136` e `docs/engineering/api-rpc.md:321`. Este delta **reverte o adiamento** para permitir instalar Windows, e isso é declarado em FR-MAC-111 — não é uma decisão silenciosa.

**Mudança de escopo declarada.** O PRD atual descreve machines como “VMs Linux” (§7.7) e não lista guest Windows como objetivo. Este delta amplia o escopo de M9/M10 para **guests Windows** e **apps remotos**; a promoção deve registrar a mudança em `PRD.md` §3.1 e §3.2 (objetivos), §6.2 (fora do escopo do v1.0) e §7.7 (renomear para “Machines — VMs Linux e Windows”), sem alterar as prioridades já ratificadas de M1–M7. A promoção também declara que **FR-MAC-014 (console serial/VNC, P2) é substituído por FR-MAC-111** (console gráfico SPICE com `remote-viewer`). A entrega é **P1 (v1.x, alvo 1.1+), não bloqueia v1.0**, coerente com `docs/README.md`.

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
- Wayland/Waypipe nesta entrega (P2 — casamento de versão de protocolo é frágil).
- GPU/3D, passthrough VFIO, DRM/anticheat.
- Seamless em Windows Home (sem servidor RDP).

## 4. Requisitos funcionais

### 4.1 Canal de guest

| ID | Requisito | P |
|---|---|---|
| FR-MAC-101 | A machine expõe canais de guest plugáveis (`ssh` e `agent`), com papel `primary`/`fallback`, resolvidos por SO e por capability; `guestChannels` e `guestReady` são expostos por `machines.get`. | P1 |
| FR-MAC-102 | O canal `agent` usa `qemu-guest-agent` sobre virtio-serial e responde `guest-ping` **sem** rede no guest. | P1 |
| FR-MAC-103 | O canal `agent` executa comandos e transfere arquivos **pequenos** (≤ 1 MiB: scripts, JSON, ícones); transferência de usuário continua no backend existente (`scp`). | P1 |
| FR-MAC-104 | O canal `agent` informa IP e SO do guest sem SSH (`guest-network-get-interfaces`, `guest-get-osinfo`). | P1 |
| FR-MAC-105 | O estado existente `provisioning` só transita para `running` quando o canal **primário** responde (Linux: `ssh`, com timeout de **300 s** → `error`/`reason=provisioning_timeout`; Windows: `agent`, 10 min → `error`/`reason=guest_unreachable`). O timeout de 300 s vale só para máquinas iniciadas após a mudança (não retroage). `guestReady` e `guestChannels` são expostos em `machines.get`. **Nenhum valor novo no enum `state`**; `guestChannels`, `guestOs` e `guestReady` são campos opcionais em `MachineMeta`/RPC. | P1 |
| FR-MAC-106 | A resolução de `auto` é explícita por SO: Linux usa `ssh` primário e `agent` como fallback (o chardev é provisionado mesmo sem qemu-ga instalado); Windows usa `agent` primário e `ssh` apenas com `windowsSsh:true` (opt-in). Falha do primário cai para o fallback; se nenhum canal responder, o erro é classificado (`reason=guest_unreachable`), sem travar a UI e sem depender de rede para o console SPICE. | P1 |

### 4.2 Apps Linux (xpra)

| ID | Requisito | P |
|---|---|---|
| FR-MAC-107 | Machines Linux X11 oferecem o backend `xpra` em modo `seamless`, com uma janela nativa por app. | P1 |
| FR-MAC-108 | Backends gráficos são **opcionais no host** e degradam por backend/SO: `xpra` só existe no Linux; `auto` no Linux exige **xpra ≥ 4.0** e cai em `x11` abaixo disso; `backend=xpra` explícito aceita **3.1.5** com warning `reason=xpra_experimental`. No Windows, RAIL exige FreeRDP; sem ele o resultado é `PODSTACK_DEPENDENCY_MISSING` (`reason=backend_missing`), **nunca `x11`**. `remote-viewer` afeta somente o console SPICE. A descoberta usa candidatos/overrides (`PODSTACK_XPRA_BINARY`, `PODSTACK_FREERDP_BINARY`); no guest, o xpra é instalado **sob demanda e com consentimento**, nunca silenciosamente. | P1 |
| FR-MAC-109 | X11 forwarding continua disponível como modo legado, sem regressão de CLI, RPC ou testes. | P1 |
| FR-MAC-110 | Clientes de app (xpra, FreeRDP) rodam na **sessão gráfica do usuário**, nunca no daemon; nenhuma janela remota é embutida no QML. | P1 |

### 4.3 Guest Windows — pré-requisito (M9)

| ID | Requisito | P |
|---|---|---|
| FR-MAC-111 | O PodStack oferece console gráfico para instalação/emergência de guests Windows via SPICE em socket Unix, com `remote-viewer` (virt-viewer) em janela própria, lançado pela GUI a partir do spec de console devolvido por `machines.console.spice`; o daemon nunca abre display. **Reverte o adiamento P2** e exige atualizar ADR-0003, `spec.md` §7, `plan.md` e `api-rpc.md`. | P1 |
| FR-MAC-112 | Machines Windows usam OVMF Secure Boot (`OVMF_CODE/VARS.secboot`, `-machine q35,smm=on`, `-global cfi.pflash01.secure=on`) com NVRAM por VM em `nvram.fd`; machines Linux mantêm o `OVMF_VARS.fd` atual. | P1 |
| FR-MAC-113 | Machines Windows usam TPM 2.0 via `swtpm` com estado persistido; arquivos de estado são `0600`; clone regenera o TPM e export/import não transporta o estado. | P1 |
| FR-MAC-114 | A criação de machine Windows é exposta por `machines.create` com `provisioner:"windows"`, `distro` opcional/ausente (ou `"windows"`), `installerIso` (ISO do usuário, nunca distribuída pelo PodStack), `answerFile?`, `windowsSsh?` (OpenSSH opt-in) e senha fornecida/gerada; valida ≥ 2 vCPU, ≥ 4 GiB RAM e ≥ 64 GiB de disco; gera `autounattend.xml` (`0600`, apagado após uso e fora do export), anexa `virtio-win.iso` e usa enlightenments `hv_*`; sem cloud-init. UI e CLI expõem os mesmos campos. | P1 |
| FR-MAC-115 | O provisionamento instala drivers virtio e o `qemu-guest-agent`; a machine só é considerada operacional quando o canal `agent` responde (`guestReady`). Com `windowsSsh:true`, o OpenSSH é instalado no mesmo provisionamento. | P1 |
| FR-MAC-116 | Áudio do guest Windows usa o `intel-hda` duplex já existente, com nó PipeWire nomeado por VM e captura validada. | P1 |

### 4.4 Apps Windows (RAIL)

| ID | Requisito | P |
|---|---|---|
| FR-MAC-117 | Machines Windows oferecem o backend `rail` (RDP RemoteApp), condicionado a M9. | P1 |
| FR-MAC-118 | O host mantém **uma** sessão RDP por machine e lança apps adicionais nela (Windows client SKU permite uma sessão interativa). | P1 |
| FR-MAC-119 | A senha RDP nunca aparece em argv, log ou `meta.json`; o certificado é fixado por TOFU (SHA-256 em `MachineMeta`) e a conexão só escuta em `127.0.0.1`. Mismatch retorna `PODSTACK_CERT_MISMATCH`/`reason=cert_mismatch` e só um `machines.apps.trust` explícito re-pina. | P1 |
| FR-MAC-120 | Windows Home não oferece `rail`; a edição é detectada por `guest-get-osinfo` (ProductName) e a UI explica o motivo em vez de esconder a ação sem contexto. | P1 |

### 4.5 Catálogo, diagnóstico e ciclo de vida

| ID | Requisito | P |
|---|---|---|
| FR-MAC-121 | `machines.apps.list/launch/stop/detach/subscribe/unsubscribe/trust` é o contrato único de apps (`unsubscribe` é o par explícito de `subscribe`); `list` devolve o catálogo do guest (`.desktop` no Linux, Start Menu no Windows) com ícone/executável em `items[]` e as sessões ativas em `sessions[]` (`sessionId`, `appId`, `backend`, `state`, `createdAt`) com `cachedAt`. | P1 |
| FR-MAC-122 | Falha ao abrir ou gerenciar um app retorna código `PODSTACK_*` estável + `reason` (`backend_missing`, `dependency_missing`, `guest_unreachable`, `app_not_found`, `rdp_auth_failed`, `session_conflict`, `session_not_found`, `backend_unsupported`, `gui_required`, `cert_mismatch`, `consent_required`, `provisioning_timeout`). O warning de backend experimental usa `warnings[].reason=xpra_experimental` no retorno de `launch`. | P1 |
| FR-MAC-123 | `machines.get/list` informam `origin` (`ssh` \| `agent`), `guestReady` e `guestChannels`; `machines.stats` e `machines.stats.subscribe` informam `origin`; todos continuam funcionando com a rede do guest bloqueada quando o canal `agent` responde. | P1 |
| FR-MAC-124 | Parar, deletar, perder o daemon ou sofrer queda do RPC sem restart encerra as sessões remotas (xpra/RAIL) e os processos locais, sem órfãos locais: o `AppHost` detecta a queda do RPC e mata **todos** os clientes locais; no restart, o daemon reconcilia (TTL de 60 s para sessão órfã) e o guest recebe `logoff`/kill do xpra pelo canal disponível. | P1 |

## 5. Requisitos não funcionais

| ID | Requisito |
|---|---|
| NFR-MAC-001 | Abertura de app já instalado ≤ 2 s (Fedora 43/44, host local, sem swap) — teste E2E. Ubuntu 24.04 com xpra 3.1.5 é **experimental** e não entra no gate. |
| NFR-MAC-002 | Nenhuma credencial em argv/log/`meta.json`; o `rdpSecretPath` é lido pelo `AppHost` e passado por stdin; teste inspeciona `/proc/<pid>/cmdline` do cliente. |
| NFR-MAC-003 | `qga.sock`, `spice.sock` e `swtpm.sock` vivem em `machineDir` (0700); arquivos de estado do TPM são `0600` (diretório `0700`); NVRAM é `0600`; o diretório `machineDir/sessions/<sessionId>` é `0700` e o `rdp.secret` é `0600`; RDP escuta só em `127.0.0.1`. |
| NFR-MAC-004 | Ausência de backend gráfico não impede o uso da machine: X11/SSH continuam funcionando; no Linux `auto` degrada para `x11`; no Windows, RAIL retorna `PODSTACK_DEPENDENCY_MISSING`. |
| NFR-MAC-005 | Zero regressão nos 17 binários de teste existentes; gate de cobertura de 80% mantido. |
| NFR-MAC-006 | O setup descobre dependências por candidatos **por distro** (nome de pacote, nome de binário e caminho de firmware) e nunca sugere pacote inexistente; teste cobre Fedora, Ubuntu 24.04 e Ubuntu 26.04. |
| NFR-MAC-007 | A instalação padrão (RPM/DEB) entrega as dependências **obrigatórias de host** (QEMU, firmware OVMF, `swtpm`, virtiofsd, módulos SPICE e áudio). Backends gráficos (`xpra`, `remote-viewer`, FreeRDP) são pacotes **opcionais** em `Recommends`/`Suggests`, detectados em runtime; no Linux a ausência de xpra faz `auto` usar `x11`, no Windows a ausência de FreeRDP retorna `PODSTACK_DEPENDENCY_MISSING` e `remote-viewer` só afeta o console. Única dependência de **host** baixada sob demanda: o ISO do virtio-win; a instalação do xpra **no guest** continua sob consentimento (FR-MAC-108). |
| NFR-MAC-008 | M9 e RAIL são **x86_64-only**; host aarch64 ou guest Windows ARM retornam `PODSTACK_UNSUPPORTED`/`reason=backend_unsupported`, sem tentar bootar Windows x86_64 sob emulação. |

## 6. Casos de uso

**UC-1 — Abrir app Linux por xpra.** O usuário escolhe "Abrir app da VM" e um app do catálogo; a GUI verifica o binário opcional no host, instala xpra no guest (com consentimento, se preciso) e abre a janela nativa. Se o xpra não estiver disponível, `auto` cai para X11 e a UI informa a degradação. Fechar a janela encerra a sessão.

**UC-2 — Abrir app Windows por RAIL.** Com a machine M9 pronta (canal `agent` e edição Pro), o host abre a sessão RAIL, publica o catálogo do Start Menu e lança apps nela; clipboard e drive redirect (opt-in, read-only) funcionam.

**UC-3 — VM sem rede.** Stats, identificação e catálogo continuam disponíveis pelo agente, com `origin=agent` e `guestChannels` mostrando o fallback; a UI explica que o SSH está indisponível.

**UC-4 — App não abre.** O erro traz `PODSTACK_*` + `reason`, a ação sugerida e, quando aplicável, o pacote exato da distro.

## 7. Critérios de aceite

| ID | Dado | Quando | Então |
|---|---|---|---|
| AC-1 | Fedora 44 com xpra ≥ 4 instalado | `machines.apps.launch` para `firefox` | janela nativa abre em ≤ 2 s e a sessão aparece em `sessions[]` de `machines.apps.list` |
| AC-2 | mesma machine, xpra ausente no host | launch | `PODSTACK_DEPENDENCY_MISSING` com `reason=backend_missing` e o pacote exato; X11 continua |
| AC-3 | guest Linux com firewall bloqueando SSH e qemu-ga ativo | `machines.stats` | responde com `origin=agent` e `guestChannels` mostra `ssh` primário em falha e `agent` fallback ativo |
| AC-4 | QEMU com chardev do agente | daemon reinicia | reconecta (ou reabre via QMP com `device_del qga0` → `chardev-remove` → `chardev-add` → `device_add`) sem reiniciar a VM |
| AC-5 | Windows 11 Pro com RDP habilitado | dois apps abertos | **uma** sessão RDP; segunda janela abre na mesma sessão |
| AC-6 | Windows 11 Home (ProductName detectado) | launch com backend `rail` | `PODSTACK_UNSUPPORTED` com `reason=backend_unsupported` e texto explicativo na UI |
| AC-7 | machine Linux existente | upgrade + próximo start | sem migração de dados e backend `x11` inalterado; no próximo start entram o chardev `qga0` e o fallback `agent`, e a máquina só volta a `running` quando o SSH responde (timeout de 300 s → `error`/`provisioning_timeout`); máquinas em execução não mudam até o restart |
| AC-8 | launch RAIL | inspeção de `/proc/<pid>/cmdline` | senha ausente |
| AC-9 | sessão xpra/RAIL aberta | stop/delete da machine, kill do daemon ou queda do RPC sem restart | o `AppHost` detecta a queda e encerra todos os clientes locais; no stop/delete e no restart do daemon, a reconciliação + TTL de 60 s encerram a sessão remota (`xpra stop`/`logoff`); nenhum órfão local |
| AC-10 | daemon com `PrivateTmp`/`ProtectHome` | launch | cliente nasce na GUI; daemon não tenta abrir display |
| AC-11 | Fedora 44 | `machines.create {provisioner:"windows", installerIso:…}` | job conclui; machine em `provisioning`, NVRAM/TPM em `machineDir` (`0600`), `autounattend.xml` removido após uso; defaults ≥ 2 vCPU/4 GiB/64 GiB aplicados |
| AC-12 | machine Windows com fingerprint pinado | certificado do guest muda e novo launch | `PODSTACK_CERT_MISMATCH`/`reason=cert_mismatch`; `machines.apps.trust {id, fingerprint}` re-pina e o próximo launch conecta |
| AC-13 | machine Linux já `running` com SSH bloqueado e qemu-ga ativo | `machines.stats.subscribe` | eventos trazem `origin=agent`, `guestReady=true` (o primário já havia respondido) e o canal usado em `guestChannels` |
| AC-14 | Ubuntu 24.04 com xpra 3.1.5 | `launch` com `backend=xpra` explícito; depois `auto` | explícito abre com warning `reason=xpra_experimental`; `auto` exige ≥ 4.0 e usa `x11` |

## 8. Fora de escopo

Waypipe; console gráfico como modo de uso diário; GPU; áudio por RDP como padrão; publicação de apps para outros hosts da rede; Windows Home seamless; Windows ARM e guest Windows x86_64 em host aarch64 (M9/RAIL são x86_64-only, NFR-MAC-008).

## 9. Decisões de produto

| ID | Decisão | Status / impacto |
|---|---|---|
| D1 | X11 forwarding é depreciado em favor de xpra ou permanece indefinidamente? | **Resolvida (2026-11-12):** X11 forwarding permanece como fallback **suportado e testado indefinidamente**, sem prazo de remoção; xpra/RAIL são preferenciais quando disponíveis. FR-MAC-109 mantém o modo legado. |
| D2 | xpra entra no v1 ou é fast-follow? (T10-00/01/02 independem de M9) | **Resolvida:** M9/M10 são **P1 (v1.x, alvo 1.1+), não bloqueiam v1.0**. T10-00/01/02 não dependem de M9; T10-03 (RAIL) depende de T9-05. |
| D3 | Áudio do xpra (`--audio=yes`) ou manter HDA→PipeWire por VM? | **Resolvida:** `machines.apps.audio` (bool, default `true`) controla o áudio **por sessão** do xpra; o HDA→PipeWire por VM permanece para Windows. |
| D4 | Broker RAIL: `xfreerdp` com sessão única ou helper com `libfreerdp-client`? | **Decisão adiada para o gate T10-03a (2026-11-12):** o spike com Win11 Pro decide entre as duas opções, com evidência registrada antes de T10-03; sem fallback silencioso para “um app por vez”. |

## 10. Rastreabilidade

| Requisito | Tarefa |
|---|---|
| FR-MAC-101…104 | T10-01 |
| FR-MAC-105 | T10-01, T9-05 |
| FR-MAC-106 | T10-01 |
| FR-MAC-107…109 | T10-03 |
| FR-MAC-110 | T10-02, T10-03 |
| FR-MAC-111 | T9-01 |
| FR-MAC-112 | T9-02 |
| FR-MAC-113 | T9-03 |
| FR-MAC-114 | T9-04, T9-04a |
| FR-MAC-115 | T9-05 |
| FR-MAC-116 | T9-06 |
| FR-MAC-117…120 | T10-03 |
| FR-MAC-121 | T10-00, T10-01, T10-02, T10-03 |
| FR-MAC-122 | T10-00, T10-03 |
| FR-MAC-123 | T10-01 |
| FR-MAC-124 | T10-00, T10-02, T10-03 |
| NFR-MAC-001 | T10-03 (E2E Fedora) |
| NFR-MAC-002 | T10-00, T10-02, T10-03 |
| NFR-MAC-003 | T9-01, T9-02, T9-03, T10-01, T10-03 |
| NFR-MAC-004 | T10-03 |
| NFR-MAC-005 | Todas |
| NFR-MAC-006 | T9-07 |
| NFR-MAC-007 | T9-07, T9-08 |
| NFR-MAC-008 | T9-04, T10-03 |

## 11. Dependências de host por distro

Esta matriz separa **dependências obrigatórias** (QEMU, firmware, TPM, virtiofsd, módulos SPICE/áudio) de **backends gráficos opcionais** (`xpra`, `remote-viewer`, FreeRDP), detectados em runtime. O pacote da versão que entrega M9/M10 declara as obrigatórias em `Requires`/`Depends` e as opcionais em `Recommends`/`Suggests` (§11.3). Nomes validados em **2026-11-12** contra Fedora 44 (`rpm`/`dnf`), Ubuntu 24.04 noble e Ubuntu 26.04 (`packages.ubuntu.com`). No Linux, se o xpra opcional faltar, `auto` degrada para `x11`; no Windows, se o FreeRDP faltar, o RAIL retorna `PODSTACK_DEPENDENCY_MISSING` (`reason=backend_missing`, FR-MAC-122) e **nunca** cai em `x11`. `remote-viewer` ausente afeta apenas o console SPICE.

| Componente | Fedora 43/44 | Ubuntu 24.04 (noble) | Ubuntu 26.04 | Binário esperado | Papel | Observação |
|---|---|---|---|---|---|---|
| xpra (cliente) | `xpra` (6.5.4) | `xpra` (3.1.5, universe) | sem pacote | `xpra` | **opcional** | 24.04 é experimental; sem pacote no 26.04 → `auto` usa `x11` (§11.2) |
| FreeRDP (RAIL) | `freerdp` (3.32.2) | `freerdp3-x11` (3.32.2, universe) | `freerdp3-x11` (universe) | `xfreerdp` / **`xfreerdp3`** | **opcional** | **nome do binário difere**; usar `PODSTACK_FREERDP_BINARY` |
| TPM 2.0 | `swtpm` (0.10.2) | `swtpm` (0.7.3, main) | `swtpm` (main) | `swtpm` | obrigatório (M9) | — |
| Firmware Secure Boot | `edk2-ovmf` | `ovmf` (main) | `ovmf` (main) | — | obrigatório (M9) | caminhos diferentes; ver §11.1 |
| Módulo SPICE do QEMU | `qemu-ui-spice-core` | `qemu-system-modules-spice` | `qemu-system-modules-spice` | — | obrigatório (M9) | sem ele, `-spice` falha |
| Áudio PipeWire no QEMU | `qemu-audio-pipewire` | `qemu-system-gui` (`audio-pipewire.so`) | `qemu-system-gui` | — | obrigatório | **corrige o áudio do T8-03 no Ubuntu**, que hoje falha sem ele |
| virtiofsd | `virtiofsd` (1.14.0) | `virtiofsd` (1.10.0, universe) | `virtiofsd` (universe) | `virtiofsd` | obrigatório | já usado por T4-07 |
| Viewer SPICE | `virt-viewer` | `virt-viewer` (universe) | `virt-viewer` (universe) | `remote-viewer` | **opcional** | único viewer de SPICE; sem `spice-gtk` como dependência de build |
| ISO virtio-win | — (sem pacote) | — (sem pacote) | — (sem pacote) | — | download sob demanda | manifest + SHA-256; T9-08 |

`Debian 12` fica **fora** desta matriz: o `.deb` não o suporta (`docs/engineering/packaging.md` §8). Alvos DEB: Ubuntu 24.04 e 26.04.

### 11.1 Caminhos do OVMF (não fixar um só)

| Distro | CODE | VARS |
|---|---|---|
| Fedora | `/usr/share/edk2/ovmf/OVMF_CODE.secboot.fd` | `/usr/share/edk2/ovmf/OVMF_VARS.secboot.fd` |
| Ubuntu | `/usr/share/OVMF/OVMF_CODE_4M.secboot.fd` | `/usr/share/OVMF/OVMF_VARS_4M.ms.fd` |

O Ubuntu **não publica** `OVMF_VARS_4M.secboot.fd`; a variante com Secure Boot usa `OVMF_VARS_4M.ms.fd` (certificados Microsoft). A descoberta deve ser por lista de candidatos, nunca por caminho fixo.

### 11.2 xpra: versão mínima

| Base | Versão | Status |
|---|---|---|
| Fedora 43/44 | 6.5.4 | **suportado** (alvo do E2E e do gate de ≤ 2 s) |
| Ubuntu 24.04 | 3.1.5 (2021) | **experimental**: `auto` exige ≥ 4.0 e usa `x11`; `backend=xpra` explícito aceita 3.1.5 com warning `reason=xpra_experimental` |
| Ubuntu 26.04 | não listado | sem pacote; `auto` degrada para `x11`; `backend=xpra` explícito → `backend_missing` |

Versão mínima para `auto`: **xpra ≥ 4.0** (protocolo e `--exit-with-children`). `backend=xpra` explícito aceita **3.1.5** com warning `reason=xpra_experimental`; abaixo de 3.1.5 o backend é recusado com `reason=backend_missing`. O PodStack **não adiciona repositório de terceiros automaticamente** — apenas instrui. Em qualquer caso, X11 forwarding segue como fallback (FR-MAC-109) e nenhuma versão é instalada sem consentimento (FR-MAC-108).

### 11.3 Política: obrigatórias × opcionais

Decisão de produto revista: **a instalação padrão entrega o que o PodStack precisa para operar a VM** (QEMU, firmware OVMF, `swtpm`, virtiofsd, módulos SPICE e áudio). **Backends gráficos de cliente não são `Depends`/`Requires` obrigatórios**: entram como `Recommends` (DEB) e `Recommends`/`Suggests` (RPM), são detectados em runtime. No Linux, a ausência de xpra faz `auto` usar `x11`; no Windows, a ausência de FreeRDP retorna `PODSTACK_DEPENDENCY_MISSING` e **nunca** cai em `x11`. Isso mantém a instalação viável no Ubuntu 26.04, que não empacota xpra.

**RPM (Fedora 43/44) — `Requires`:**

```
qemu-kvm
qemu-system-x86-core
qemu-system-aarch64-core
qemu-img
virtiofsd
edk2-ovmf
qemu-ui-spice-core
qemu-audio-pipewire
swtpm
```

**RPM — `Recommends`:**

```
freerdp
xpra
virt-viewer
```

**DEB (Ubuntu 24.04–26.04) — `Depends`:**

```
qemu-system-x86
qemu-system-arm
qemu-utils
virtiofsd
ovmf
qemu-efi-aarch64
qemu-system-modules-spice
qemu-system-gui
swtpm
```

**DEB — `Recommends`:**

```
freerdp3-x11
virt-viewer
# xpra: apenas no 24.04; omitido no 26.04
```

`xpra` entra como `Recommends` **apenas no Ubuntu 24.04**, que o empacota; no 26.04 a linha é omitida pelo empacotamento por release. Nunca em `Depends`, em nenhum release. `qemu-system-gui` continua em `Depends` porque fornece `audio-pipewire.so` no Ubuntu: sem ele, `-audiodev pipewire` não existe e o áudio do T8-03 falha.

**Exceção única de host — ISO do virtio-win.** Não existe como pacote em nenhuma das bases (verificado em Fedora 44, noble e 26.04). O download é **automático** na primeira criação de VM Windows, por manifest com SHA-256, progresso e cache `0600` em `~/.local/share/podstack/virtio-win/` (T9-08). Falha offline retorna `PODSTACK_DEPENDENCY_MISSING` com instrução de arquivo local; nunca no `%post` do pacote. A instalação do xpra **no guest** é outro fluxo, sempre com consentimento (FR-MAC-108).

**Consequências aceitas:**

- instalação base menor; xpra/FreeRDP/virt-viewer só vêm se o gerenciador instalar `Recommends` (padrão) ou se o usuário optar;
- Ubuntu 26.04 instala sem xpra e `auto` usa `x11` — degradação documentada, não bloqueio (NFR-MAC-007);
- `scripts/clean-install-check.sh` e containers de CI validam Fedora 44, Ubuntu 24.04 e Ubuntu 26.04, incluindo o cenário sem `Recommends`;
- Flatpak continua sem AppHost (o sandbox não executa binário do host);
- **correção necessária no daemon:** `qemuPackagesForManager()` cai no caso default com `qemu-system-x86_64` e `qemu-system-aarch64`, que **não são nomes de pacote** no Fedora (`dnf` não resolve). Trocar por `qemu-system-x86-core` e `qemu-system-aarch64-core`, nomes validados nos repos.
- versões de `xpra`/`swtpm` variam por base (§11.2); o código não deve fixar versão, apenas o mínimo suportado.
