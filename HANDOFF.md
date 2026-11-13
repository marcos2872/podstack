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
- **ISOs locais obrigatórias** nesta fase: `installerIso` (Windows) e
  `virtioWinIso` (virtio-win). O PodStack não baixa/redistribui Windows;
  T9-08 (download/cache) segue pendente.
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
  --virtio-win-iso <arq> [--answer-file <xml>] [--edition <nome>]
  [--windows-ssh] [--password-stdin]`.
- Export de machine Windows **ainda retorna `PODSTACK_UNSUPPORTED`** (o
  manifesto não carrega `provisioner` nem ISOs). Clone é recusado enquanto
  `installPending=true`.
- Campos novos persistidos: `windowsSsh`, `windowsEdition`, `installPending`,
  `installerIso`, `virtioWinIso` (migração aditiva em `MachineStore`).

## O que falta (em ordem sugerida)

1. **E2E real do Windows** (parcialmente executado em 2026-11-13).
   - Há `/dev/kvm`, QEMU 10.2.2 e ISOs válidos em `~/Downloads`:
     `Windows11_Client_x64_pt-br_26300_9457.iso` (Windows 11 Pro, build
     10.0.26300, imagem 4) e `virtio-win-0.1.302.iso`
     (fonte oficial: `https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/virtio-win-0.1.302-1/virtio-win-0.1.302.iso`).
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
2. **T9-05 (bloqueada por T10-01).** Implementar o canal `agent`/`GuestChannel`
   (T10-01), `guest-ping`, `guest-get-osinfo` (`guestOs`/Home-Pro), timeout de
   10 min e remoção do answer file dirigida pelo agente. Hoje a machine vai
   para `running` assim que o QEMU sobe.
3. **T9-06.** Validar áudio Windows (`intel-hda` duplex + PipeWire) no E2E.
4. **T9-07.** Empacotamento (`Requires`/`Depends` de QEMU/OVMF/swtpm/SPICE),
   checks no `SetupManager` + `test_setup`, `scripts/clean-install-check.sh`
   para DEB e correção de `qemuPackagesForManager()`.
5. **T9-08.** Download/cache do virtio-win por manifesto com URL HTTPS e
   SHA-256 (o projeto upstream não publica hash do ISO principal; será
   necessário fixar um hash calculado). Manter fallback de ISO local.
6. **Export/import Windows.** Levar `provisioner` no manifesto, tratar seed
   opcional e NVRAM/TPM fora do arquivo; habilitar export depois.
7. **M10.** Apps remotos (xpra/RAIL); RAIL depende de T9-05.

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

## Evidência da última rodada

- `cmake --build --preset dev`: ok, sem warnings novos.
- `ctest --preset dev`: **17/17** testes passando após os ajustes de USB/QMP.
- Cobertura (`gcovr`, gate 80%): **80,6%** de linhas na medição anterior ao
  ajuste final de USB/QMP; repetir no E2E.
- `clang-format` 22.1.8 (linhas alteradas) e `clang-tidy` 22.1.8: limpos.
  (Não há `clang-tools-extra` instalado no host; use
  `uvx --from clang-format==22.1.8` / `uvx --from clang-tidy==22.1.8`.)
- `qmllint`: sem erros; warnings de `Theme`/acesso não qualificado são
  preexistentes no QML.
- Testes focados úteis:
  - `./build/dev/tests/test_machines windowsProvisionerXml windowsProvisionerPrepare windowsProvisionerValidation machineManagerWindowsCreate machineManagerWindowsValidation`
  - `./build/dev/tests/test_cli machineCreateWindows`
  - `./build/dev/tests/test_gui backendMachines`
  - `./build/dev/tests/test_rpc machinesFlow`

## Commits

- Base do M9: `7a80369` (console SPICE/Secure Boot/TPM) e `00b628d`
  (promoção do M9 no SDD).
- Criação Windows: `4b9cff9` (backend), `cebf1ca` (GUI/CLI), `a6d9647`
  (docs) e `43c71cf` (primeira versão desta nota).
- Ajuste pós-E2E (QEMU 10.2/USB/QMP): ver o commit mais recente no
  `git log` (`fix(machines): ...`); esta nota foi atualizada junto.

Nada foi enviado ao remoto. Antes de continuar, rode `git status` e leia
[`docs/sdd/tasks.md`](docs/sdd/tasks.md) para confirmar o estado.


https://fedorapeople.org/groups/virt/virtio-win/direct-downloads/archive-virtio/virtio-win-0.1.302-1/virtio-win-0.1.302.iso
