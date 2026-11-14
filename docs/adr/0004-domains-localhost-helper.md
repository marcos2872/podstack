# ADR-0004 — Domínios locais: `*.localhost` no v1, `*.podstack.local` via helper

| Campo | Valor |
|---|---|
| Status | Aceito |
| Data | 2026-11-08 |
| Decisores | Arquitetura, Segurança |
| Relacionados | `plan.md`, `security.md`, ADR-0006 |

## Contexto

O OrbStack entrega `container.orb.local` sem configuração usando mDNS + proxy reverso + CA local. No Linux:

- `.local` é território do **mDNS/Avahi**; publicar wildcard causa conflito e spoofing.
- Alterar `/etc/hosts`/resolved exige root.
- Rootless Podman (pasta) **não expõe IP roteável** do container; só portas publicadas.
- Portas 80/443 exigem privilégio ou `CAP_NET_BIND_SERVICE`.

## Decisão

**v1 (sem privilégio):**

- Usar `*.localhost` (RFC 6761): `systemd-resolved`/glibc resolvem para 127.0.0.1/::1.
- Proxy do daemon escuta em portas altas (`8080`/`8443`) e roteia por `Host`/SNI.
- Detecção automática de porta + labels `dev.podstack.http-port`/`https-port`.
- Página de índice em `http://localhost:8080`.
- **Não** alterar `/etc/hosts`, **não** usar mDNS por padrão.

**v1.1 (opt-in com helper):**

- DNS userspace em `127.0.0.1:5353` + drop-in `Domains=~podstack.local` no resolved.
- Socket activation do helper para `:80/:443` → socket do daemon.
- Caddy com CA interna e HTTPS on-the-fly; confiança na CA opt-in e reversível.

## Alternativas

| Alternativa | Prós | Contras |
|---|---|---|
| mDNS/Avahi | sem root, `.local` | conflito na LAN, spoofing, sem wildcard |
| `/etc/hosts` | universal | root, stale, sem wildcard |
| Portas publicadas apenas | simples | sem domínio, sem "sem porta" |
| Container roteador (Caddy/Envoy) | rootless, por rede | complexidade, uma porta publicada por rede |

## Consequências

**Positivas:**

- v1 funciona sem root e sem tocar no sistema.
- Migração suave para `podstack.local` quando o usuário quiser paridade total.
- Sem conflito com Avahi.

**Negativas:**

- v1 exige `:8080` na URL (não é paridade 100% com "sem porta").
- Helper adiciona superfície de segurança (mitigada por polkit/API fechada).
- Rootless não permite IP direto; documentar.

## Implementação

- `ProxyManager` no daemon; Caddy como filho.
- Helper com métodos `BindPorts`, `InstallDnsZone`, `TrustCa`.
- Testes de idempotência e remoção limpa.

## Atualização (2026-11-13)

Os defaults de porta do proxy passaram a `18080`/`18443`, fora da faixa efêmera do kernel. A
decisão de usar portas altas e sem privilégio permanece; configs antigas com `8080`/`8443` são
migradas automaticamente. Ver [`settings.md`](../engineering/settings.md).
