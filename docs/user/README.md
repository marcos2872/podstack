# Documentação de usuário — PodStack 0.1.3

Guias para instalar, configurar e usar o PodStack 0.1.3 em um Linux desktop. A documentação
técnica (PRD, spec, arquitetura, ADRs e contrato RPC) continua em [`../README.md`](../README.md);
para desenvolver ou contribuir, leia o [`AGENTS.md`](../../AGENTS.md).

## Guias

| Documento | Conteúdo |
|---|---|
| [`install.md`](install.md) | Requisitos, instalação por RPM/DEB, build do fonte, primeiro start, setup em 7 etapas, helper/polkit, verificação e desinstalação. |
| [`quickstart.md`](quickstart.md) | Do zero ao primeiro container acessível, primeira machine com SSH, Kubernetes, domínios `*.localhost`, HTTPS/CA, bandeja e atalhos. |
| [`cli.md`](cli.md) | Referência dos comandos e flags reais do `podstack` 0.1.3, saída JSON, exit codes e exemplos. |
| [`troubleshooting.md`](troubleshooting.md) | Diagnóstico de daemon/socket, Podman, polkit/helper, KVM, virtiofsd, SSH, portas, firewall, CA/HTTPS, DNS, CRIU e erros `PODSTACK_*`. |

## Limitações conhecidas do 0.1.3

- O console serial das machines é somente texto; não há VNC nem console gráfico.
- `machine export`/`import` e `machine clone` não preservam snapshots internos do qcow2.
- O stack Supabase (T3-09) não foi entregue.
- Mounts de host em machines (T4-07) existem na GUI e no RPC, mas `machine create` não tem flags
  de mount no CLI.
- O roteamento de domínios customizados via Compose (T6-08) não está disponível; labels e montagem
  da CA em containers de Compose ficam pendentes.
- T5-07 e T5-08 são parciais: não há NodePort/LoadBalancer nativos nem `k8s.podstack.local`; a troca
  de contexto é explícita (`podstack kube context use`), não automática por projeto.
- Portas não publicadas não podem ser encaminhadas no modo rootless (T6-10). Publique a porta na
  criação do container ou no Compose.
- No Flatpak, o aplicativo é apenas cliente: `podstackd` e `podstack-helper` precisam estar
  instalados no host, fora do sandbox.
- A instalação limpa tem smoke automatizado em container (`scripts/clean-install-check.sh` e o
  workflow `packaging.yml`); a validação com GUI, KVM e polkit reais em máquina limpa ainda é manual.

Outras limitações desta versão:

- O proxy de domínios usa `network.httpPort`/`network.httpsPort` (47080/47443 por padrão). A ativação
  das portas 80/443 pelo helper existe na API privilegiada, mas não é acionada pela GUI/CLI em
  0.1.3.
- A confiança na CA local e a instalação do drop-in de `*.podstack.local` ainda não têm um fluxo
  pronto na GUI/CLI. Use `*.localhost` ou os passos manuais de
  [`troubleshooting.md`](troubleshooting.md).

Licença: GPL-3.0-or-later. Consulte [`LICENSE`](../../LICENSE) e [`NOTICE`](../../NOTICE).
