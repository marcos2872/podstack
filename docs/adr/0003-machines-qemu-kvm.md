# ADR-0003 — Machines com QEMU/KVM direto + cloud-init + virtiofs

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-08 |
| Decisores | Arquitetura |
| Relacionados | `plan.md`, `spec.md`, `security.md` |

## Contexto

"Linux machines" é um dos pilares do produto: criar VMs de distros arbitrárias, com cloud-init, SSH, compartilhamento de arquivos e port forwarding. O `podman machine` no Linux existe, mas é limitado às imagens do Podman (CoreOS/podman-machine-os), rootless e sem distros arbitrárias/cloud-init genérico. Libvirt adiciona daemon, XML e complexidade.

## Decisão

Usar **QEMU/KVM diretamente**:

- `qemu-system-x86_64`/`aarch64` com `-M q35 -cpu host` (KVM) ou TCG (fallback).
- Discos **qcow2** esparsos.
- Provisionamento por **cloud-init NoCloud** (seed ISO com `meta-data`, `user-data`, `network-config`).
- Compartilhamento via **virtiofs** (`virtiofsd`), com fallback 9p.
- Rede via `-netdev user` (slirp) + hostfwd no v1; `passt` avaliado depois.
- Controle por **QMP** (socket Unix): start/stop/reset/query.
- SSH por chave ed25519 por máquina + bloco gerenciado em `~/.ssh/config`.
- Metadados em `meta.json` + SQLite.

## Alternativas

| Alternativa | Prós | Contras |
|---|---|---|
| `podman machine` | pronto | só imagens Podman, sem distros arbitrárias |
| libvirt (`qemu:///session`) | snapshots, virt-manager | daemon/XML, virtiofs mais complexo |
| VirtualBox/VMware | maduro | dependências pesadas, licenças |
| Conteinerizar "machines" | leve | não é VM; não serve para systemd/kernel próprio |

## Consequências

**Positivas:**

- Controle total sobre boot, discos, rede e cloud-init.
- Sem root/polkit/libvirt no caminho padrão.
- Distros arbitrárias via cloud images.
- Base clara para snapshots (P2).

**Negativas:**

- Muito código de orquestração (QMP, seed, SSH, limpeza).
- virtiofs/SELinux exigem testes.
- Sem `/dev/kvm`, performance cai (TCG).

## Atualização M9 — guest Windows (2026-11-12)

O adiamento do console gráfico para P2 foi **revertido**: a T9-01 entrega o console SPICE em
socket Unix por machine, com o `remote-viewer` lançado pela GUI (o daemon só devolve o spec).
SPICE não usa TCP e o socket fica no `machineDir` (`0700`/`0600`); não há `spice-gtk` como
dependência de build.

O escopo de machines passa a incluir **guest Windows** com `provisioner: "windows"` (M9):
OVMF Secure Boot com NVRAM própria (`nvram.fd`, `0600`), TPM 2.0 via `swtpm` (estado `0600`,
diretório `0700`) e, nas fases seguintes, provisionamento por `installerIso`/`autounattend`.
A criação Windows depende das T9-04…T9-08 e é P1 (v1.x), sem bloquear o v1.0. Machines Linux
mantêm o fluxo cloud-init, o `OVMF_VARS.fd` e o console serial.

## Implementação

- `MachineManager` isola QEMU/QMP; `meta.json` é recuperável.
- Limpeza de processos órfãos no start.
- PoC de virtiofs e cloud-init no início do M4.
- Seed ISO (T4-02): `CloudInit::writeSeedIso` monta a ISO9660 com libarchive
  (`volume-id=cidata`, Rock Ridge `strict` e Joliet), sem `cloud-localds`/`genisoimage`. As
  entradas `meta-data`, `user-data` e `network-config` ficam na raiz em modo `0600`; a senha é
  convertida para hash `$6$` com `crypt_r` (libxcrypt) e nunca aparece em texto claro. A ISO é
  gravada em `<path>.tmp`, restringida a `0600` e publicada por rename atômico.
