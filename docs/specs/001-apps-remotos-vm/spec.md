# 001 — Apps remotos da VM (xpra, RAIL e canal de guest)

> **Status:** proposta — não promovida ao núcleo SDD.
> **Marco:** M9 (T9-01…T9-03) + pré-requisito M10 (Windows).
> **Data:** 2026-11-12.
> **Promove para:** [`../../sdd/PRD.md`](../../sdd/PRD.md) (FR-MAC-101…115), [`../../sdd/spec.md`](../../sdd/spec.md) §7, [`../../sdd/tasks.md`](../../sdd/tasks.md) (M9).

---

## 1. Contexto

Hoje o único caminho para usar um app de uma machine na tela do host é **X11 forwarding sobre SSH** (T8-04, `machine ssh --x11`). Ele funciona, mas:

- o protocolo X faz milhares de round-trips por interação e não usa MIT-SHM entre host e guest; toolkits modernos degradam e o GL cai para software;
- depende de `xauth`, `PermitUserEnvironment` e de o `XAUTHORITY` sobreviver a `HOME` redirecionado (já gerou correção específica para o Firefox snap);
- **não existe equivalente para Windows**: o guest não tem SSH por padrão, não há display gráfico (`-display none`) e nenhum canal de guest (zero virtio-serial/vsock/qemu-ga).

Além disso, toda a comunicação host↔guest é SSH/SCP. Stats, arquivos e terminal param de funcionar quando a rede do guest não sobe — e não há como detectar "a instalação terminou".

O console gráfico foi **excluído explicitamente** em cinco lugares (`adr/0003` linha 50, `spec.md` linha 432, `tasks.md` linha 136, `plan.md` linha 259, `api-rpc.md` linha 321). Este delta não reverte essa decisão: xpra e RAIL **não são console gráfico** — são caminhos de app por protocolo de aplicação, compatíveis com `-display none`.

## 2. Objetivos

| # | Objetivo | Métrica |
|---|---|---|
| O1 | Apps Linux com qualidade perceptivelmente melhor que X11 forwarding | app já instalado abre em ≤ 2 s; janela redimensiona sem travar |
| O2 | Caminho de apps Windows preparado, sem embutir protocolo na UI | RAIL especificado e desbloqueado por M10 |
| O3 | Segundo canal de guest independente de rede | `guest-ping` responde com a VM sem IP |
| O4 | API e UX únicas, backends plugáveis | `machines.apps.*` igual para Linux e Windows |

## 3. Não-objetivos

- Substituir SSH no Linux (continua o canal padrão; `~/.ssh/config`, terminal e `machine ssh` permanecem).
- Remover X11 forwarding (vira modo legado, mantido e testado).
- Wayland/Waypipe no v1 (P2 — casamento de versão de protocolo é frágil).
- Console gráfico/VNC, GPU/3D, passthrough VFIO, DRM/anticheat.
- Seamless em Windows Home (sem servidor RDP).

## 4. Requisitos funcionais

### 4.1 Canal de guest

| ID | Requisito | Prioridade |
|---|---|---|
| FR-MAC-101 | A machine expõe um canal de guest plugável (`ssh \| agent`), resolvido por capability e por SO do guest. | P0 |
| FR-MAC-102 | O canal `agent` usa `qemu-guest-agent` sobre virtio-serial e responde `guest-ping` **sem** rede no guest. | P0 |
| FR-MAC-103 | O canal `agent` executa comandos e transfere arquivos **pequenos** (scripts, JSON, ícones); transferência de usuário continua no backend de arquivos existente. | P0 |
| FR-MAC-104 | O canal `agent` informa IP e SO do guest sem SSH (`guest-network-get-interfaces`, `guest-get-osinfo`). | P0 |
| FR-MAC-105 | O estado da machine distingue `installing` de `ready`; a transição usa presença do agente. | P0 |
| FR-MAC-106 | Falha do canal primário cai para o secundário (e vice-versa) com erro explícito, sem travar a UI. | P1 |

### 4.2 Apps Linux (xpra)

| ID | Requisito | Prioridade |
|---|---|---|
| FR-MAC-107 | Machines Linux X11 oferecem o backend `xpra` para abrir apps; o modo recomendado é `seamless` com uma janela nativa por app. | P0 |
| FR-MAC-108 | O backend xpra instala o pacote no guest **sob demanda e com consentimento**, exibindo o comando e o custo; nunca instala silenciosamente. | P0 |
| FR-MAC-109 | X11 forwarding continua disponível como modo legado, sem regressão de CLI, RPC ou testes. | P0 |
| FR-MAC-110 | Clientes de app (xpra, FreeRDP) rodam como processo supervisionado; **nenhuma janela remota é embutida no QML**. | P0 |

### 4.3 Apps Windows (RAIL)

| ID | Requisito | Prioridade |
|---|---|---|
| FR-MAC-111 | Machines Windows oferecem o backend `rail` (RDP RemoteApp), condicionado aos pré-requisitos do M10. | P1 |
| FR-MAC-112 | O host mantém **uma** sessão RDP por machine e lança apps adicionais nela (Windows client SKU permite uma sessão interativa). | P1 |
| FR-MAC-113 | O certificado RDP é fixado por TOFU (SHA-256 em `MachineMeta`) e a conexão só escuta em `127.0.0.1`. | P0 |
| FR-MAC-114 | Windows Home não oferece `rail`; a UI explica o motivo em vez de esconder a ação sem contexto. | P1 |

### 4.4 Catálogo e diagnóstico

| ID | Requisito | Prioridade |
|---|---|---|
| FR-MAC-115 | `machines.apps.list/launch/stop` é o contrato único; o catálogo vem do guest (`.desktop` no Linux, Start Menu no Windows) com ícone e executável. | P0 |
| FR-MAC-116 | Falha ao abrir app retorna causa classificada (`backend_missing`, `guest_unreachable`, `app_not_found`, `rdp_auth_failed`, `session_conflict`) e a ação sugerida. | P1 |

## 5. Requisitos não funcionais

| ID | Requisito |
|---|---|
| NFR-MAC-001 | Abertura de app já instalado ≤ 2 s (Linux, host local, sem swap) — medido em teste E2E. |
| NFR-MAC-002 | Nenhuma credencial em argv, log ou `meta.json`: senha RDP em arquivo `0600`; fingerprint é público. |
| NFR-MAC-003 | Sockets de guest (`qga.sock`) e RDP escutam apenas em caminho/endereço local, com `0600`/`127.0.0.1`. |
| NFR-MAC-004 | Ausência de backend não impede o uso da machine: X11/SSH continuam funcionando. |
| NFR-MAC-005 | Zero regressão nos binários de teste existentes; cobertura do gate de 80% mantida. |

## 6. Casos de uso

**UC-1 — Abrir app Linux por xpra.** O usuário escolhe "Abrir app da VM", escolhe um app do catálogo; o daemon instala xpra (se preciso, com consentimento), inicia o processo e a janela aparece nativa; fechar a janela encerra a sessão daquele app.

**UC-2 — Abrir app Windows por RAIL (pós-M10).** A machine Windows já tem o agente e o RDP habilitados; o host abre a sessão RAIL, publica o catálogo do Start Menu e lança apps nela; clipboard e drive redirect funcionam.

**UC-3 — VM sem rede.** Stats e identificação continuam disponíveis pelo agente; a UI mostra que o canal SSH está indisponível e por quê.

**UC-4 — App não abre.** O erro classifica a causa e oferece a ação (instalar backend, subir a VM, corrigir credencial).

## 7. Critérios de aceite

| ID | Dado | Quando | Então |
|---|---|---|---|
| AC-1 | Ubuntu 24.04 com xpra instalado | `machines.apps.launch` para `firefox` | janela nativa abre em ≤ 2 s; processo supervisionado aparece em `machines.apps.status` |
| AC-2 | mesma machine | xpra ausente e usuário recusa a instalação | erro `backend_missing` com o comando exato; X11 continua funcionando |
| AC-3 | guest com firewall bloqueando SSH | `machines.stats` | responde via agente; origem `agent` reportada |
| AC-4 | QEMU com `-chardev` do agente | daemon reinicia | reconecta ao socket e volta a responder sem reiniciar a VM |
| AC-5 | Windows 11 Pro com RDP habilitado | dois apps abertos | **uma** sessão RDP; segunda janela abre na mesma sessão |
| AC-6 | Windows 11 Home | `machines.apps.launch` com backend `rail` | erro `backend_unsupported` explicando a licença |
| AC-7 | machine existente (Linux, ssh) | upgrade do PodStack | nada muda: canal `ssh`, backend `x11`, sem migração |

## 8. Fora de escopo

Waypipe; console gráfico/VNC; SPICE; GPU; áudio por RDP como padrão; publicação de apps para outros hosts da rede; Windows Home seamless.

## 9. Decisões abertas

| ID | Decisão | Impacto |
|---|---|---|
| D1 | X11 forwarding é depreciado em favor de xpra ou permanece indefinidamente? | define se `machine ssh --x11` ganha aviso e prazo |
| D2 | xpra entra no v1 ou é fast-follow? (o T9-02 é independente do M10) | ordem de entrega |
| D3 | Áudio do xpra (`--audio=yes`) ou manter HDA→PipeWire por VM? | latência vs. áudio por app |
| D4 | Broker RAIL: `xfreerdp` com sessão única ou helper próprio com `libfreerdp-client`? | esforço e robustez do T9-03 |

## 10. Rastreabilidade

| Requisito | Tarefa |
|---|---|
| FR-MAC-101…106 | T9-01 |
| FR-MAC-107…110, 115 (Linux) | T9-02 |
| FR-MAC-111…114, 115 (Windows), 116 | T9-03 (+ M10) |
