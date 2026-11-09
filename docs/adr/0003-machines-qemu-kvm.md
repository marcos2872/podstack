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
- Console gráfico/VNC fica para P2.

## Implementação

- `MachineManager` isola QEMU/QMP; `meta.json` é recuperável.
- Limpeza de processos órfãos no start.
- PoC de virtiofs e cloud-init no início do M4.
- Seed ISO (T4-02): `CloudInit::writeSeedIso` monta a ISO9660 com libarchive
  (`volume-id=cidata`, Rock Ridge `strict` e Joliet), sem `cloud-localds`/`genisoimage`. As
  entradas `meta-data`, `user-data` e `network-config` ficam na raiz em modo `0600`; a senha é
  convertida para hash `$6$` com `crypt_r` (libxcrypt) e nunca aparece em texto claro. A ISO é
  gravada em `<path>.tmp`, restringida a `0600` e publicada por rename atômico.
