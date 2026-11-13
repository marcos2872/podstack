# ADR-0011 — Apps remotos de machines (M10)

- **Status:** aceito (2026-11-13)
- **Contexto:** spec [`docs/specs/001-apps-remotos-vm/`](../specs/001-apps-remotos-vm/spec.md)
- **Relacionado:** ADR-0003 (machines QEMU/KVM), ADR-0005 (terminal)

## Contexto

O PodStack já opera machines Linux e Windows, mas abrir um app do guest exigia SSH/X11
manual ou o console SPICE. A spec 001 define apps remotos com backend plugável: `xpra` no
Linux, RAIL no Windows e X11 legado. O daemon roda com `PrivateTmp=yes`,
`ProtectSystem=strict` e `ProtectHome=read-only`, portanto **não pode abrir display** nem
gravar no home; o cliente gráfico precisa nascer na sessão do usuário.

## Decisão

1. O **daemon** é dono das sessões: `AppSessionManager` mantém `AppSession` em memória,
   faz `launch` idempotente por `(machineId, appId)`, `stop`/`detach`, TTL de 60 s,
   reconciliação no stop/delete e publica `machines.apps.session.*`.
2. A **GUI** hospeda o cliente local (`AppHost`): recebe o `LaunchSpec` na resposta de
   `machines.apps.launch`, cria/supervisiona o processo (xpra/ssh/FreeRDP) e o encerra no
   stop, na saída do processo ou na queda do RPC. O daemon nunca abre janela.
3. O contrato RPC é `machines.apps.list/launch/stop/detach/subscribe/unsubscribe/trust`.
   `list` devolve catálogo + sessões + `cachedAt`; `LaunchSpec` não contém senha, e o
   segredo RDP fica em `machineDir/sessions/<id>/rdp.secret` (`0600`).
4. O catálogo (`GuestAppCatalog`) usa o canal `agent`/SSH para ler `.desktop` (Linux) e o
   Start Menu (Windows), com cache de 5 min e ícones pequenos em base64.
5. `xpra`/FreeRDP continuam **opcionais** no host (`Recommends`/`Suggests`); a ausência
   retorna `PODSTACK_DEPENDENCY_MISSING`/`backend_missing` (nunca degrada RAIL para x11).
6. O guest Windows é pré-requisito do RAIL: `qemu-guest-agent`, Secure Boot/TPM e
   `guest-get-osinfo` vêm do M9 (T9-05).

## Consequências

- A GUI precisa detectar queda do RPC e executar `shutdownAll()`; a reconexão reconcilia
  via `machines.apps.list`.
- Não há estado durável de sessão: reiniciar o daemon fecha os clientes e a GUI reconcilia.
- O `LaunchSpec` é versionável e testável sem abrir display (x11/xpra/rail).
- Catálogo e sessões não entram em `meta.json`/export; o fingerprint TOFU será persistido
  em `MachineMeta` na T10-03.
- `xpra`/RAIL exigem validação real (T10-03a) antes do E2E; a parte xpra não depende do M9.
