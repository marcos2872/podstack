# Handoff — M9 (guest Windows)

> Nota de continuidade para o próximo agente. O estado abaixo reflete a branch
> `feat/m9-m10-apps-remotos-update-logs` em 2026-11-13. Leia também
> [`AGENTS.md`](AGENTS.md), [`docs/sdd/tasks.md`](docs/sdd/tasks.md) (M9) e
> [`docs/sdd/spec.md`](docs/sdd/spec.md) §7.9.

## O que já está implementado

### T9-01 — console SPICE
- `QemuSpec`/`QemuCommand` emitem `-spice unix=on,addr=<machineDir>/spice.sock,disable-ticketing=on`,
  `virtio-vga`, `qemu-xhci,id=usbconsole`, `usb-tablet` e mantêm `-display none`.
- `machines.console.spice {id}` devolve `{program, args, socket}`; a GUI lança o
  `remote-viewer` com `QProcess::startDetached`. O daemon nunca executa o viewer.
- Erros: Linux → `PODSTACK_UNSUPPORTED`; parada/sem socket → `PODSTACK_CONFLICT`;
  viewer ausente → `PODSTACK_DEPENDENCY_MISSING` com `reason=backend_missing`.
- Arquivos: `src/machines/QemuCommand.*`, `MachineManager::spiceConsole`,
  `src/daemon/Daemon.*`, `src/gui/Backend.*`, `src/gui/qml/MachineInspector.qml`.

### T9-02 — OVMF Secure Boot
- `-M q35,smm=on`, `-global driver=cfi.pflash01,property=secure,value=on`,
  `-cpu host,hv_relaxed,hv_vpindex,hv_time,hv_synic,hv_stimer,hv_reset,hv_frequencies`.
- Candidatos Fedora/Ubuntu `secboot`; NVRAM por machine em
  `<machineDir>/nvram.fd` (`0600`). Linux continua com `OVMF_VARS.fd`.
- Par incompatível → `PODSTACK_VALIDATION`; firmware ausente →
  `PODSTACK_DEPENDENCY_MISSING`.

### T9-03 — TPM 2.0
- Novo `SwtpmProcess` (`src/machines/SwtpmProcess.*`), mesmo modelo do
  `VirtiofsdProcess`: sobe antes do QEMU, é adotado por `cmdline`/socket após
  restart do daemon e encerrado no stop/delete.
- `swtpm socket --tpm2 --tpmstate dir=<machineDir>/tpm,mode=0600
  --ctrl type=unixio,path=<machineDir>/swtpm.sock,mode=0600`.
- `tpm/` é `0700`, estado `0600`; argv QEMU usa `chrtpm` + `tpmdev` +
  `-device tpm-tis,tpmdev=tpm0`.
- `PODSTACK_SWTPM_BINARY` sobrescreve o binário em dev/teste.

### T9-04 / T9-04a — criação Windows
- `provisioner: "windows"` com `distro` ausente/`"windows"`.
- **ISO local do Windows obrigatório** (`installerIso`); o do virtio-win
  (`virtioWinIso`) é opcional desde a T9-08 (sem ele, baixa/cacheia o oficial).
  O PodStack não baixa/redistribui o Windows.
- `WindowsProvisioner` (`src/machines/WindowsProvisioner.*`):
  - valida ISOs `.iso` absolutos/legíveis, answer file XML (opcional);
  - gera `autounattend.xml` + `autounattend.iso` (`0600`);
  - partições GPT/EFI/MSR, conta local, OOBE, AutoLogon, `/IMAGE/NAME`
    (`windowsEdition`, default `Windows 11 Pro`);
  - `FirstLogonCommands`: `virtio-win-gt-x64.msi` (raiz do ISO),
    **`guest-agent\qemu-ga-x86_64.msi`** (não fica na raiz), OpenSSH opcional
    com injeção da chave pública em `administrators_authorized_keys`, e
    `shutdown /r /t 10` no fim;
  - `DriverPaths` em `E:` e `F:` para `viostor`, `NetKVM`, `vioscsi`,
    `viogpudo`, `Balloon`, `viorng`, `vioserial` em `w11/amd64`.
- `QemuCommand` anexa o virtio-win e o answer file como CD-ROMs IDE
  (`if=ide,media=cdrom,readonly=on`); o instalador usa CD USB (`usb-storage` +
  `bootindex=0`) porque o OVMF não inicializa o El Torito pelo CD IDE/AHCI, e o daemon
  envia `send-key` via QMP para responder ao prompt "Press any key to boot from CD or DVD".
- `installPending` no `MachineMeta`/SQLite controla as mídias: no primeiro
  `stop`, o PodStack limpa `autounattend.xml`/`.iso`, os caminhos das ISOs e o
  flag; o próximo start boota do disco. **Não interrompa uma instalação em
  andamento** — o stop limpa as mídias.
- Mínimos: 2 vCPU / 4 GiB / 64 GiB. `distro` ausente é aceito; `arch` precisa
  ser `amd64` (`PODSTACK_UNSUPPORTED`/`reason=backend_unsupported`).
- Mounts e SSH agent forwarding são recusados em Windows.
- A senha é obrigatória no backend; GUI/CLI geram quando o usuário não informa.
  A GUI copia a senha para o clipboard ao concluir; a CLI imprime uma vez
  (ou lê de `--password-stdin`). Ela nunca vai para `meta.json`/log/argv.
- GUI: seletor Linux/Windows no diálogo “Nova machine”, campos de ISOs, answer
  file, edição, senha/Gerar, `windowsSsh` e aviso de licença.
- CLI: `podstack machine create <nome> --provisioner windows --iso <arq>
  [--virtio-win-iso <arq>] [--answer-file <xml>] [--edition <nome>]
  [--windows-ssh] [--password-stdin]`.
- Export de machine Windows **ainda retorna `PODSTACK_UNSUPPORTED`** (o
  manifesto não carrega `provisioner` nem ISOs). Clone é recusado enquanto
  `installPending=true`.
- Campos novos persistidos: `windowsSsh`, `windowsEdition`, `installPending`,
  `installerIso`, `virtioWinIso` (migração aditiva em `MachineStore`).

### T9-08 — download/cache do virtio-win
- `VirtioWinCache` (`src/machines/VirtioWinCache.*`) usa manifesto embutido:
  versão `0.1.302`, URL HTTPS do fedorapeople, SHA-256
  `303f7ae40dad495d6ae474fdc571df58958a4dbc5c37a522d80f9a203867949d` e
  877.373.440 bytes.
- `machines.create` Windows sem `virtioWinIso` baixa o ISO no job, com os
  estágios `virtio-win` (0–68) e `virtio-win-verify` (68–74); cancelamento
  usa `machines.cancel`.
- Cache `~/.local/share/podstack/virtio-win/` (`0700`), ISO e sidecar `0600`;
  reuso por sidecar/tamanho, rehash quando falta sidecar, `.part` + rename
  atômico e lock de 15 min. Versões antigas são removidas após verificar a atual.
- Falha de rede/HTTP → `PODSTACK_DEPENDENCY_MISSING` (`reason=download_failed`)
  com remediação para informar ISO local; hash/tamanho divergente →
  `PODSTACK_DOWNLOAD_FAILED` (`reason=checksum_mismatch`).
- GUI: campo “ISO do virtio-win (opcional)”, vazio baixa automaticamente; CLI
  omite `--virtio-win-iso`; `Backend`/QML mostram os novos estágios.

### T10-01 (parcial) / T9-05 — canal `agent` e provisionamento
- Toda machine sobe com `virtio-serial-pci,id=virtserial0`, chardev
  `qga.sock` (`server=on,wait=off`) e `virtserialport,id=qga0,chardev=qga,
  name=org.qemu.guest_agent.0`; o socket fica no `machineDir` (`0700`), nunca em TCP.
- `GuestAgentClient` fala JSON por linha com `guest-sync-delimited`/`0xFF`:
  `guest-ping`, `guest-get-osinfo`, `guest-info`, `guest-exec/status`,
  `guest-network-get-interfaces` e `guest-file-*` (≤ 1 MiB).
- `GuestChannel` resolve Linux (`ssh` primário, `agent` fallback) e Windows
  (`agent` primário, `ssh` só com `windowsSsh:true`) e reconecta o chardev na
  ordem `device_del qga0` → `chardev-remove qga` → `chardev-add qga` →
  `device_add qga0`.
- `MachineManager` mantém um watcher por machine em `provisioning` (thread
  própria, join no stop/delete/destructor): Linux 300 s sem SSH →
  `error`/`provisioning_timeout`; Windows 10 min sem `guest-ping` →
  `error`/`guest_unreachable`, **sem timeout enquanto `installPending=true`**.
- No ready: `guestReady=true`, `guestOs` (`family/version/edition/productName`),
  `guestChannels`, `state=running`; para Windows com `installPending`, remove
  `autounattend.xml/.iso`, limpa as ISOs e o flag.
- `guestChannels`/`guestReady`/`guestOs`/`errorReason` persistem em `meta.json`
  e SQLite (migração aditiva) e saem em `machines.get/list`.
- GUI: linha **Guest** no Inspector, estágios `provisioning`, Stop habilitado e
  Console SPICE liberado em `provisioning`; timer de 3 s atualiza machines em
  `provisioning`.
- Testes: `MockGuestAgent`, `guestAgentClient`, `guestChannelReconnect`,
  `qemuCommandGuestAgent`, `machineManagerGuestProvisioning`,
  `machineManagerGuestTimeout` e `machineManagerWindowsGuest`.

### T9-07 — dependências de host
- `qemuPackagesForManager()` corrigido (`qemu-system-x86-core`/
  `qemu-system-aarch64-core` no Fedora) e listas com SPICE/áudio/`swtpm`.
- Checks novos no `SetupManager`: `swtpm`, `spice` e `qemu-audio` (módulos por
  candidatos Fedora/Ubuntu), com remediação por distro.
- RPM `Requires`/`Recommends` e DEB `Depends`/`Recommends` atualizados;
  `scripts/build-deb.sh` e `packaging/deb/control` idem.
- `scripts/clean-install-check.sh --deb` cobre Ubuntu 24.04/26.04.

### T10-01 — catálogo e stats `origin`
- `GuestAppCatalog` (`src/machines/GuestAppCatalog.*`): lista `.desktop` em
  `/usr/share/applications`/`~/.local/share/applications` e o Start Menu do Windows via
  `guest-exec`; ignora `NoDisplay`/`Hidden`/`Terminal`, remove `%`-codes do `Exec`, lê
  `Categories` e transfere ícones ≤ 256 KiB por `guest-file-read` (base64, orçamento 4 MiB).
- Cache por machine com TTL de 5 min; `refresh:true` ignora; `cachedAt` volta no RPC.
- `machines.stats`/`stats.subscribe` tentam SSH e caem para o canal `agent` (`guest-exec
  /bin/sh`), devolvendo `origin:"ssh"|"agent"`. `machines.get/list` também expõem `origin`.

### T10-00 — AppSessionManager
- `src/machines/AppSessionManager.*`: sessões em memória, idempotência por
  `(machineId, appId)`, TTL de 60 s, `stop`/`detach`, `reconcileMachine` no stop/delete,
  eventos `machines.apps.session.started|updated|closed` e `trust` de fingerprint.
- RPC `machines.apps.list/launch/stop/detach/subscribe/unsubscribe/trust` no daemon.
- Backend x11/xpra e `LaunchSpec`; RAIL fica para a T10-03. A GUI consome as sessões via
  `AppHost` (T10-02).

### T10-02 — AppHost/GUI/CLI
- `src/gui/AppHost.*`: spawn/supervisão do cliente local (`QProcess`), `stopLocal`,
  `shutdownAll`, sinais `clientExited`/`clientFailed`.
- `Backend`: `appItems`/`appSessions`/`appsLoading`/`appsError`, `refreshApps`,
  `launchApp`, `stopApp`, `closeApps`, polling de 2,5 s e `shutdownAll` em erro de
  RPC; o `LaunchSpec` devolvido pelo daemon é convertido e passado ao `AppHost`.
- QML: botão **Abrir app** no Inspector (Linux) e diálogo com busca, catálogo, sessões
  abertas e **Parar**.
- CLI: `podstack machine apps list <machine>` (texto/`--json`); `launch`/`stop` retornam
  `PODSTACK_GUI_REQUIRED` (`reason=gui_required`), pois o cliente precisa da sessão gráfica.

### T10-03 (parcial) — xpra
- Settings `machines.apps.backend/audio/shareFolder` (86 → 89 chaves).
- Descoberta de `xpra`/`PODSTACK_XPRA_BINARY` com versão; `auto` usa xpra ≥ 4.0 no Linux e
  cai para x11; `xpra` explícito 3.1.5 aceito com warning `xpra_experimental`.
- `LaunchSpec` xpra (`xpra seamless ssh://… --ssh … --start-child … --exit-with-children
  --clipboard --notifications --audio`) e áudio via setting.
- **Pendente:** RAIL/FreeRDP, drive redirect, TOFU persistido, broker de sessão única
  (T10-03a) e instalação do xpra no guest sob consentimento.

## O que falta (em ordem sugerida)

1. **E2E real do Windows** (parcialmente executado em 2026-11-13).
   - Há `/dev/kvm` e QEMU 10.2.2; os ISOs usados no teste anterior não estão
     mais no disco. O virtio-win pode ser baixado pelo T9-08 ou de
     `https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/virtio-win-0.1.302-1/virtio-win-0.1.302.iso`.
   - Já validado: o ISO Windows boota em **SeaBIOS/legacy**; no **OVMF** ele
     **não** boota como CD IDE/AHCI (BdsDxe “Time out”), mas boota como **CD USB**
     (`usb-storage` + `bootindex=0`). O `send-key` via QMP passa do prompt
     “Press any key to boot from CD or DVD” e o Setup inicia.
   - O Setup sem TPM/Secure Boot acusa os três requisitos do Windows 11
     (TPM 2.0, Secure Boot e “4 GB”); é esperado no teste sem esses recursos.
     Ainda falta rodar o caminho real (Secure Boot + swtpm + 4 GiB) e confirmar
     se o check de 4 GB passa; se não, avaliar 6 GiB como default/recomendação.
   - Validar: instalação até o desktop, Secure Boot (`Confirm-SecureBootUEFI`),
     TPM 2.0 (`Get-Tpm`), drivers virtio sem `!`, `windowsSsh`, áudio e o
     console SPICE.
   - O ISO é **pt-BR only**; o answer file usa `QLocale::system()`. Se o host
     não estiver em pt-BR, ajuste o locale ou use um `answerFile` próprio.
   - Windows 11 25H2 pode exigir conta Microsoft no OOBE; o XML usa conta local
     + `HideOnlineAccountScreens`. Se falhar, ajustar o OOBE/BypassNRO.
2. **T9-06.** Validar áudio Windows (`intel-hda` duplex + PipeWire) no E2E.
3. **T10-03a/T10-03.** Spike RAIL/RDP com Windows 11 Pro e backend RAIL/FreeRDP; a parte
   xpra e o `AppHost` da T10-02 já estão prontos. A instalação do xpra no guest sob
   consentimento e o E2E ≤ 2 s também faltam.
4. **Export/import Windows.** Levar `provisioner` no manifesto, tratar seed
   opcional e NVRAM/TPM fora do arquivo; habilitar export depois.
5. **M10.** Apps remotos (xpra/RAIL); RAIL depende de T9-05 (canal já pronto).

## Gotchas descobertos no primeiro teste real

- **QEMU 10.2** exige `hv_time` junto de `hv_stimer` no `-cpu`
  (`hv-stimer requires Hyper-V clocksources (hv-time)`) e exige
  `-device tpm-tis,tpmdev=tpm0`; sem isso o QEMU cria o QMP socket, mas sai
  antes do pidfile e o erro na UI é “O QEMU não publicou um pidfile válido”.
- **OVMF e o CD do Windows:** o El Torito do ISO Windows não boota pelo
  CD IDE/AHCI no OVMF 2025 (BdsDxe “Time out”); o mesmo ISO boota como CD USB
  (`usb-storage` + `bootindex=0`). Fedora boota nos dois caminhos, então não é
  falha geral do OVMF/CD. O `send-key` via QMP responde ao prompt do boot
  manager; sem ele o Windows cai em PXE/disco.
- O Setup do Windows 11 25H2 sem TPM/Secure Boot acusa os três requisitos
  (TPM 2.0, Secure Boot e 4 GB). Ainda falta validar o caminho real com
  Secure Boot + swtpm + 4 GiB; se o check de memória continuar falhando,
  testar 6 GiB.
- O stderr do QEMU **não é capturado** hoje; para diagnosticar, reproduza o
  argv manualmente ou rode o QEMU sem `-daemonize`/`-S`.
- Quando o start falha durante o `create`, `cleanupRuntimeFiles` apaga
  `disk.qcow2`, `nvram.fd` e os sockets, mas mantém o `meta.json` em `error`.
  A machine precisa ser removida e recriada; não há retry reaproveitando o
  disco. Considere mudar isso para preservar o disco em falha de start.
- **qga.sock sem agente:** o QEMU cria o chardev `qga.sock` no boot, mesmo sem
  `qemu-ga` no guest. Um `guest-ping` nesse estado fica pendurado até o timeout;
  as sondas usam 1,5 s e só tentam reconectar via QMP quando `guestReady` já era
  verdadeiro.
- **`provisioning` agora é assíncrono:** `create/start` retornam antes de o
  canal primário responder. Linux espera SSH (300 s) e Windows `guest-ping`
  (10 min); a UI é atualizada por polling de 3 s. Não espere `running` imediato
  em scripts/CLI.
- **Instalação Windows >10 min:** enquanto `installPending=true` não há
  timeout. Depois do stop/start pós-instalação, o timeout de 10 min vale.

## Evidência da última rodada

- `cmake --build --preset dev`: ok, sem warnings novos.
- `ctest --preset dev`: **17/17** testes passando.
- Cobertura (`gcovr`, gate 80%): **80,1%** de linhas (26682/33308). A T9-08
  partiu de 79,8% e as T10-00/T10-01/T9-05/T9-07 subiram o gate
  (`GuestAgentClient.cpp` 90%, `GuestChannel.cpp` 97%, `GuestAppCatalog.cpp` 86%,
  `AppSessionManager.cpp` 86%).
- `clang-format` 22.1.8 via `clang-format-diff` (linhas alteradas) e `clang-tidy`
  22.1.8 nos arquivos alterados: limpos.
  (Não há `clang-tools-extra` instalado no host; use
  `uvx --from clang-format==22.1.8` / `uvx --from clang-tidy==22.1.8`.)
- `qmllint`: sem erros; warnings de `Theme`/acesso não qualificado são
  preexistentes no QML.
- T10-01/T10-00: `guestAppCatalog`, `guestAppCatalogWindows`,
  `guestAppCatalogErrors`, `appSessionManager` e `test_rpc/appsFlow` passam
  (catálogo Linux/Windows, cache, ícones, `origin=agent` em stats, sessões
  idempotentes, eventos, TTL, `subscribe/unsubscribe/trust`).
- T10-01/T9-05: `guestChannelPingReconnect`, `guestChannelReconnectFailures`,
  `guestChannelErrors`, `qemuCommandGuestAgent`,
  `machineManagerGuestProvisioning`, `machineManagerGuestTimeout`,
  `machineManagerGuestProbeFailure`, `machineManagerWindowsGuest`,
  `machineManagerWindowsAgentHome`, `machineManagerWindowsProbeFailure` e
  `machineManagerLinuxAgentFallback` passam. `MockGuestAgent` cobre
  ping/osinfo/exec/arquivos/rede, erros e timeouts.
- T9-07: `test_setup` 40/40; listas RPM/DEB e checks `swtpm`/`spice`/
  `qemu-audio` cobertos. `scripts/clean-install-check.sh --deb` foi
  implementado e validado com `bash -n`, mas não executado em container nesta
  sessão.
- T9-08 (cache): `virtioWinCacheIntegration` com o ISO real
  (877.373.440 bytes) servido por HTTP local: **PASS**, SHA-256
  `303f7ae4…949d` confere, cache `0700` e ISO/sidecar `0600`. O download externo pelo
  fedorapeople estagnou no host desta sessão; o hash/tamanho foram conferidos no arquivo
  baixado por `curl` antes do teste.
- Testes focados úteis:
  - `./build/dev/tests/test_machines virtioWinCacheDownload virtioWinCacheReuse virtioWinCacheChecksumFailure virtioWinCacheOffline virtioWinCacheCleanup virtioWinCacheCancel machineManagerWindowsDownload machineManagerWindowsDownloadFailure machineManagerWindowsValidation`
  - `./build/dev/tests/test_machines windowsProvisionerXml windowsProvisionerPrepare windowsProvisionerValidation machineManagerWindowsCreate machineManagerWindowsValidation`
  - `./build/dev/tests/test_cli machineCreateWindows`
  - `./build/dev/tests/test_gui backendMachines`
  - `./build/dev/tests/test_rpc machinesFlow`

## Commits

- Base do M9: `7a80369` (console SPICE/Secure Boot/TPM) e `00b628d`
  (promoção do M9 no SDD).
- Criação Windows: `4b9cff9` (backend), `cebf1ca` (GUI/CLI), `a6d9647`
  (docs) e `43c71cf` (primeira versão desta nota).
- Ajuste pós-E2E (QEMU 10.2/USB/QMP): `fd63fbe`.
- T9-08 (download/cache do virtio-win): `8aa1e76`.
- T10-01 (canal `agent`)/T9-05/T9-07 + CI: commit `feat(machines): ...`
  desta rodada; ver o `git log` para o hash.

Nada foi enviado ao remoto. Antes de continuar, rode `git status` e leia
[`docs/sdd/tasks.md`](docs/sdd/tasks.md) para confirmar o estado.


https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/virtio-win-0.1.302-1/virtio-win-0.1.302.iso
