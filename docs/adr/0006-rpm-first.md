# ADR-0006 — RPM como formato primário

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-08 |
| Decisores | Arquitetura, Release |
| Relacionados | `packaging.md` |

## Contexto

O PodStack não é só uma GUI: precisa de daemon systemd --user, helper root, polkit, SELinux, `/dev/kvm` e integração com Podman. Formatos universais (Flatpak/AppImage) têm limitações estruturais para esse modelo.

## Decisão

- **RPM** (Fedora 43+, COPR) é o formato primário e o único com suporte completo no v1.
- **AppImage** é suportado apenas para demonstração/GUI (sem helper/units).
- **Flatpak** não está previsto no v1; se reavaliado, apenas como **cliente** de um daemon já instalado no host.

## Alternativas

| Formato | Prós | Contras |
|---|---|---|
| Flatpak | sandbox, distro-agnóstico | não roda Podman, não instala units, não acessa `/dev/kvm`, helper impossível |
| AppImage | portátil | sem systemd/polkit/SELinux; QML/plugins precisam ser bundlados |
| Snap | loja | confinamento semelhante ao Flatpak; menos comum em Fedora |
| tarball | simples | sem integração, atualização manual |

## Consequências

**Positivas:**

- Integração correta com systemd, polkit, SELinux e atualizações.
- Experiência esperada pelo público Fedora.
- Helper e units versionados junto.

**Negativas:**

- Alcance inicial limitado a Fedora/openSUSE (RPM).
- Precisa manter spec + COPR + assinatura.
- Usuários de Ubuntu/Arch dependem de port comunitário (documentar).

## Implementação

- `podstack.spec` com `%systemd_user_post`, `%systemd_post`, polkit, metainfo.
- COPR stable/beta; AppImage no CI.
- Pacote `podstack-selinux` separado.
- Flatpak: não previsto no v1; reavaliar sob demanda com escopo de cliente.
