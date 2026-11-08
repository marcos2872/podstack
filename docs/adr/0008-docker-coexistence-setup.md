# ADR-0008 — Coexistência Docker/Podman e setup não destrutivo

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-08 |
| Decisores | Arquitetura, Produto, Segurança |
| Relacionados | ADR-0002, ADR-0004, `PRD.md` §7.18, `spec.md` §2.6, `poc/ui/ONBOARDING.md` |

## Contexto

O PodStack usa o **Podman nativo** como engine. No Linux, o Docker pode já estar instalado e em uso pelo desenvolvedor. As duas pilhas usam sockets diferentes:

- Docker: `/var/run/docker.sock` (rootful) ou `$XDG_RUNTIME_DIR/docker.sock` (rootless).
- Podman: `/run/podman/podman.sock` (rootful) ou `$XDG_RUNTIME_DIR/podman/podman.sock` (rootless).

Não há conflito técnico de socket que obrigue remover o Docker. A remoção, porém, é **destrutiva** para containers, imagens, redes e volumes do Docker; não há migração automática. O primeiro uso também precisa preparar Podman, QEMU/KVM e componentes opcionais, o que exige decisões de consentimento e privilégio.

Opções consideradas:

| Opção | Prós | Contras |
|---|---|---|
| **Remover o Docker no setup** | um único engine; menos confusão | perda de dados; atrito alto; desnecessário tecnicamente |
| **Exigir a remoção do Docker** | ambiente "limpo" | bloqueia quem usa Docker; contraria o usuário |
| **Coexistir + remoção opt-in** | menor atrito; preserva dados; setup rápido | dois engines consomem recursos; exige clareza na UI |
| **Empacotar o Podman** | independência da distro | duplica engine; foge do gerenciador de pacotes; escopo enorme |

## Decisão

1. **Podman é obrigatório; Docker é opcional.** O setup instala/atualiza o Podman e ativa `podman.socket` rootless. O Docker **não** é removido pela ação principal.
2. **A remoção do Docker é uma ação separada, opt-in e destrutiva.** Exige `confirm: true`, lista o impacto (containers/imagens/redes/volumes), avisa que não há migração automática e passa pelo helper allowlisted. Pode ser ignorada; o app funciona com o Docker instalado.
3. **O setup é um assistente em janela separada, com 7 etapas:** boas-vindas, diagnóstico, Podman, QEMU/KVM, componentes, validação e pronto. A detecção é **read-only**; toda alteração exige consentimento.
4. **Ações privilegiadas nunca saem da GUI.** Instalar/remover pacotes e remover o Docker passam pelo `podstack-helper` com métodos allowlisted (`InstallPackages`, `RemovePackages`); habilitar `podman.socket` é uma operação do systemd do usuário, feita pelo daemon. Proibido `Exec`/shell.
5. **Estado persistido:** `setup.completed`, `setup.completedAt`, `setup.dockerPolicy` (`keep`/`removed`), `setup.components`, `setup.lastValidation`. `dockerPolicy=removed` só é gravado após a ação confirmada.
6. **`DOCKER_HOST` é uma opção do usuário**, não uma remoção: ferramentas Docker podem apontar para o socket do Podman quando o usuário quiser. O PodStack não altera essa variável sozinho.
7. **Pods do Podman ficam no grupo Engine**, não em Kubernetes; o grupo Kubernetes é reservado para k3s/clusters (M5).

## Consequências

**Positivas:**

- Primeiro uso sem perda de dados e sem exigir que o usuário abandone o Docker.
- Setup rápido e reversível; a remoção fica sob controle explícito.
- Base de segurança clara: helper allowlisted, consentimento e auditoria.
- UI e CLI compartilham o mesmo contrato `setup.*`.

**Negativas / riscos:**

- Dois engines podem consumir recursos e confundir qual está em uso. Mitigação: badge/pill "Podman" na UI, status do engine, documentação e detecção explícita.
- A remoção do Docker é irreversível para os dados não exportados. Mitigação: confirmação, aviso, opção de exportar e nunca remover volumes sem escolha.
- Pacotes e nomes variam por distro. Mitigação: detectar o gerenciador (dnf/apt/pacman) ou limitar o v1 a Fedora com remediação documentada.
- O helper ganha métodos de instalação/remoção de pacotes. Mitigação: allowlist fixa, polkit por método, auditoria e testes de segurança.

## Implementação

- `SetupManager` (daemon): detecção read-only, aplica ações via helper, jobs com progresso/log/cancelamento/retry.
- `SetupController` (GUI): conduz `SetupWindow` e os 7 passos; não executa comandos.
- RPC: `setup.status`, `setup.check`, `setup.apply`, `setup.removeDocker`, `setup.cancel`, `setup.validate` (propostos; ver `api-rpc.md` §4.12).
- Persistência: `config.json`/SQLite; jobs em `jobs`/`job_events`.
- Testes: matriz de 6 estados, coexistência, remoção opt-in, cancelamento, helper e ausência de `/dev/kvm`.

## Referências

- [`PRD.md`](../PRD.md) §2.4 e §7.18
- [`spec.md`](../spec.md) §2.6
- [`security.md`](../security.md) §4.2 e §4.4
- [`settings.md`](../settings.md) §2.11
- [`poc/ui/ONBOARDING.md`](../../poc/ui/ONBOARDING.md)
- [`poc/ui/FLOWS.md`](../../poc/ui/FLOWS.md) §12.3
