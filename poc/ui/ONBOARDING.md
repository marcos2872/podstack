# Onboarding / Setup — PodStack UI Concept

> Assistente de **primeira execução** para preparar a máquina: detectar o que existe, remover o Docker quando presente, instalar/atualizar o Podman, preparar QEMU/KVM e validar tudo.
> Na POC ele é uma **simulação**; no app real precisa de detecção, consentimento, helper privilegiado e jobs com progresso/cancelamento.

---

## 1. Objetivo

- Reduzir o atrito do primeiro uso: o usuário não precisa descobrir sozinho que o PodStack usa Podman (e não Docker), que machines exigem QEMU/KVM e que alguns componentes são opcionais.
- Deixar claro **o que será alterado** antes de alterar; nunca desinstalar/instalar nada silenciosamente.
- Validar a instalação e permitir abrir o app mesmo com avisos (ex.: k3s opcional, KVM ausente → TCG).
- Poder ser reexecutado depois (Settings → “Executar configuração novamente” / `podstack setup`).

---

## 2. Entrada e janela

### 2.1 Na POC

- Botão **“Configurar máquina”** na barra do desktop (fora do app).
- Ícone de **setup** na dock (fora do app), com badge.
- O assistente abre em uma **janela separada** sobre o app, com backdrop blur/dim.

### 2.2 No app real

- **Primeiro uso**: abrir automaticamente se `setup.completed != true`.
- **CLI**: `podstack setup` (e talvez `podstack doctor --fix` para reparos).
- **Settings**: seção “Configuração da máquina” → “Executar novamente”.
- Janela própria (`SetupWindow.qml`) ou `Dialog` grande; não colocar dentro da `Main.qml` como uma aba.

---

## 3. Layout

```text
┌──────────────────────────────────────────────────────────────────┐
│ PodStack Setup  [primeira execução]                         [×]  │  38 px
├───────────────┬──────────────────────────────────────────────────┤
│ 1 Boas-vindas │  conteúdo da etapa (scroll)                      │
│ 2 Diagnóstico │                                                  │
│ 3 Docker→Pod  │                                                  │
│ 4 QEMU/KVM    │                                                  │
│ 5 Componentes │                                                  │
│ 6 Validação   │                                                  │
│ 7 Pronto      │                                                  │
│               ├──────────────────────────────────────────────────┤
│ ⓘ nota        │  status                         [Voltar] [Avançar]│  56 px
└───────────────┴──────────────────────────────────────────────────┘
      220 px
```

- Janela: `min(1020px, 100% - 60px)` × `min(680px, 100% - 48px)`; raio 14; borda forte; sombra.
- Titlebar: 38 px; logo + “PodStack Setup” + badge “primeira execução” + fechar.
- Rail: 220 px; fundo `surface-2`; passos com dot 24, label 11.5/600, hint 9.5; ativo `accent-soft`; concluído verde.
- Content: padding 24/28; scroll; animação `setupIn` (slide 16 px + fade, 260 ms).
- Footer: 56 px; status 10 `text-3`; botões Voltar/Avançar (primário).
- Nota fixa no rail: “Nada é alterado sem sua confirmação.”

---

## 4. Etapas

| # | Etapa | Objetivo | Ação principal |
|---:|---|---|---|
| 1 | **Boas-vindas** | explicar o que será feito | Começar |
| 2 | **Diagnóstico** | detectar Docker, Podman, QEMU/KVM, componentes | Escanear novamente |
| 3 | **Docker → Podman** | remover Docker (se houver) e instalar/atualizar Podman | aplicar engine |
| 4 | **QEMU/KVM** | preparar virtualização para machines | instalar virtualização |
| 5 | **Componentes** | Podman socket/rootless/SQLite (obrigatórios), Caddy/k3s (opcionais) | instalar componentes |
| 6 | **Validação** | checklist final; exigir OK nos obrigatórios | validar instalação |
| 7 | **Pronto** | resumo e abrir o app | Abrir PodStack |

O usuário pode **Voltar** e **Continuar** a qualquer momento (exceto durante uma instalação); a validação pode ser pulada, mas o app mostrará avisos.

---

## 5. Etapa 1 — Boas-vindas

- Cápsula grande (48×31) centralizada.
- Título “Vamos preparar sua máquina” (24/650).
- Lead: “O PodStack roda sobre o Podman nativo e usa QEMU/KVM para máquinas Linux. Vamos detectar o que já existe, remover conflitos e validar cada componente antes de abrir o app.”
- Grid 2×2 de features:
  - Podman nativo — engine rootless, socket e CLI, sem Docker.
  - QEMU/KVM — machines com cloud-init e virtiofs.
  - Domínios locais — Caddy para `*.localhost`.
  - Sem conflito — Docker detectado e removido antes do Podman.
- Nota: “Nada é alterado sem sua confirmação. Você pode pular etapas e revisar depois em Settings.”
- Footer: **Começar**.

---

## 6. Etapa 2 — Diagnóstico

Lista de checks (linha 9/12; ícone 22; label 11/600; detalhe 9.5; badge):

| Check | Estados |
|---|---|
| Docker Engine | presente (atenção: “docker.socket ativo — será removido”) ou “não instalado” |
| Podman | ausente; `v5.4.2 → 5.8.9` (atenção); `v5.8.9 · rootless · atualizado` |
| QEMU/KVM | ausente; `QEMU 9.0.2 · /dev/kvm disponível` |
| `/dev/kvm` | disponível; indisponível (atenção: TCG) |
| virtiofsd | ausente; instalado |
| systemd (user) | OK |
| SQLite | OK/ausente |
| Caddy | opcional/OK |
| k3s | opcional/OK |

- **Escanear novamente**: 900 ms simulados; desabilita o botão; toast “Diagnóstico atualizado”.
- **Cenário de demonstração** (somente POC): select com “Podman ausente”, “Podman 5.4.2 (atualizar)”, “Podman 5.8.9 (atualizado)”. No app real **não existe**: o estado vem da detecção.
- Footer: Voltar/Continuar.

### 6.1 Detecção real (app)

- Podman: `QStandardPaths::findExecutable("podman")`, `podman --version`, `podman info --format json`, socket em `$XDG_RUNTIME_DIR/podman/podman.sock`, `systemctl --user is-active podman.socket`.
- Docker: binário `docker`, `docker version`, `docker.socket`/`docker.service`, `/var/run/docker.sock`; **não** confundir `podman-docker` com Docker Engine.
- QEMU: `qemu-system-x86_64`, `qemu-system-aarch64`; `/dev/kvm` acessível; grupo `kvm`; `virtiofsd`; `libvirtd` (opcional); OVMF.
- Componentes: SQLite (versão), Caddy, k3s; systemd de usuário; `loginctl show-user $USER`.
- Tudo somente leitura nesta etapa.

---

## 7. Etapa 3 — Docker → Podman

### 7.1 Objetivo

Garantir um único engine: **Podman**. Se o Docker estiver instalado, ele é desinstalado; o Podman é instalado se ausente ou atualizado se desatualizado. O socket de usuário e o modo rootless são ativados.

> **Importante:** desinstalar o Docker é uma ação destrutiva para o ambiente Docker do usuário (containers, imagens e redes locais podem deixar de existir). No app real, exigir confirmação explícita, explicar a consequência e **nunca** fazer isso silenciosamente. Dados não são migrados automaticamente.

### 7.2 Matriz de estado e ação

| Docker | Podman | Rótulo do botão | Comandos simulados (POC) | Resultado |
|---|---|---|---|---|
| presente | ausente | **Desinstalar Docker e instalar Podman** | stop docker.socket/service; remove docker-ce/containerd; install podman; enable socket; podman info | Docker removido; Podman atual; socket/rootless OK |
| presente | desatualizado (5.4.2) | **Desinstalar Docker e atualizar Podman** | stop/remove Docker; upgrade podman; enable socket; podman info | Docker removido; Podman atualizado |
| presente | atualizado | **Desinstalar Docker** | stop/remove Docker; `podman --version`; enable socket; podman info | Docker removido; Podman mantido |
| ausente | ausente | **Instalar Podman** | install podman; enable socket; podman info | Podman instalado |
| ausente | desatualizado | **Atualizar Podman** | upgrade podman; enable socket; podman info | Podman atualizado |
| ausente | atualizado | **Engine pronto** (sem ação) | — | Nada a fazer |

### 7.3 Conteúdo da etapa

1. Card de aviso **Docker detectado** (se presente): “v27.3.1 · docker.socket ativo” + badge “conflito” + explicação.
   Se ausente: card de sucesso “Docker não instalado · nenhum conflito de engine”.
2. Lista de checks: Docker Engine (atenção/OK) e Podman (ausente/atenção/OK) com versão e “atualização disponível”.
3. Card de ação **Substituir o engine** / **Preparar o Podman**:
   - Descrição: desinstala o Docker e instala/atualiza o Podman; ativa o socket.
   - Botão primário com o rótulo da matriz.
4. Durante a ação: barra de progresso 6 px, percentual e log (terminal 140 px) com um comando por linha.
5. Ao concluir: checks atualizados, log com “✔ concluído” e toast (“Docker desinstalado; Podman atualizado”).

### 7.4 Comandos simulados (POC)

```text
# se Docker presente
systemctl --user stop docker.socket docker.service
sudo dnf remove -y docker-ce docker-ce-cli containerd.io

# Podman ausente
sudo dnf install -y podman
# Podman desatualizado
sudo dnf upgrade -y podman
# Podman atual
podman --version

# sempre
systemctl --user enable --now podman.socket
podman info --format '{{.Host.Security.Rootless}}'
```

### 7.5 Implementação real

- **Nunca** executar `dnf remove`/`dnf install` diretamente no processo da GUI. Usar o **helper privilegiado** com métodos allowlisted (`InstallPackages`, `RemovePackages`, `EnableUserSocket`…) ou `pkexec` com comando fixo e confirmação.
- Detectar a distro/gerenciador (Fedora/RHEL: `dnf`; Debian/Ubuntu: `apt`; Arch: `pacman`) ou limitar o v1 a Fedora e mostrar remediação para outras.
- Antes de remover Docker: verificar containers/imagens/volumes e avisar que serão perdidos; oferecer “Exportar dados”/“Cancelar”; exigir digitação/ação explícita para um caso destrutivo.
- Podman: instalar/atualizar; habilitar `podman.socket` de usuário; verificar `podman info` rootless; `loginctl enable-linger` quando necessário; nunca usar rootful por padrão.
- O progresso deve ser um **job** com passos, log e cancelamento; erros devem permitir retry e não deixar o sistema em estado inconsistente.
- O resultado deve ser persistido (`setup.completed`, versões detectadas, timestamp) para não repetir a remoção.
- A ação “manter Docker” pode ser permitida com aviso (o PodStack usa Podman; o Docker só coexistirá se os sockets não conflitarem), mas o padrão recomendado é remover.

### 7.6 Seletor de cenário (POC)

- **Demo-only**: permite alternar `Podman ausente` / `5.4.2 (atualizar)` / `5.8.9 (atualizado)` e ver o rótulo/comandos mudarem.
- **Remover** na implementação real; o estado vem da detecção.

---

## 8. Etapa 4 — QEMU/KVM

### 8.1 Checks

| Check | Detalhe |
|---|---|
| QEMU | `qemu-system 9.0.2` / ausente |
| `/dev/kvm` | disponível (KVM) / indisponível (TCG, lento) |
| virtiofsd | instalado/ausente (mounts de machines) |
| libvirt | opcional (integração `virsh`) |

### 8.2 Ação simulada

```text
sudo dnf install -y qemu-kvm qemu-system-x86_64 qemu-system-aarch64
sudo dnf install -y virtiofsd libvirt-daemon-kvm edk2-ovmf
sudo systemctl enable --now libvirtd
test -e /dev/kvm && echo 'kvm: disponível'
```

### 8.3 Real

- Instalar QEMU/KVM e OVMF; virtiofsd; libvirt opcional.
- Verificar `/dev/kvm` e acesso ao grupo `kvm`; se indisponível, avisar e permitir TCG (com aviso de lentidão) ou bloquear machines.
- Não iniciar machines sem KVM por padrão; o usuário escolhe.
- Packages via helper allowlisted; nada de shell arbitrário.
- O teste de `/dev/kvm` é somente leitura.

---

## 9. Etapa 5 — Componentes

### 9.1 Obrigatórios

- **Podman socket** — `systemd --user` + socket `0600`.
- **Rootless** — sem root/setuid.
- **SQLite** — histórico de builds.

### 9.2 Opcionais

- **Caddy** — HTTPS para `*.localhost` (domínios locais); pode usar helper para portas 80/443.
- **k3s** — Kubernetes local em VM; opcional e mais pesado.

### 9.3 UI

- Linhas com checkbox: obrigatórios marcados e desabilitados; opcionais com toggle (k3s não selecionado por padrão).
- Cada linha: nome, descrição e badge (`OK`, `pendente`, `opcional`).
- Botão **Instalar componentes**; progresso/log; ao concluir, badges atualizados e toast.
- A POC usa `sudo dnf install -y sqlite libarchive`, `systemctl --user enable --now podman.socket`, `podman system service --time=0 &`, `sudo dnf install -y caddy` e, se k3s marcado, o script de instalação do k3s.

### 9.4 Real

- SQLite/libarchive já são dependências do app; validar em vez de instalar quando possível.
- Podman socket/rootless via systemd de usuário.
- Caddy/k3s via helper/instalador oficial; k3s em VM (M4+), não no host.
- Nunca instalar k3s sem ação explícita; o v1 pode ocultar k3s do onboarding e deixar para o M4.

---

## 10. Etapa 6 — Validação

Checklist final:

| Item | Obrigatório | Falha |
|---|---|---|
| Podman | sim | `missing` |
| Podman socket | sim | `missing` |
| Rootless | sim (aviso se pendente) | `warn` |
| Docker ausente | recomendado | `warn` se ainda instalado |
| QEMU | sim para machines | `missing` |
| `/dev/kvm` | desejável | `warn` (TCG) |
| virtiofsd | sim para mounts | `missing` |
| SQLite | sim | `missing` |
| Caddy | opcional | `optional` |
| k3s | opcional | `optional`/`missing` se selecionado |

- **Validar instalação**: 1,3 s simulados; desabilita o botão; toast; banner final.
- Banner:
  - Sem `missing`: “Tudo pronto para abrir o PodStack” (verde).
  - Com `missing`: “Alguns itens precisam de atenção” (âmbar); o usuário pode corrigir ou continuar (“abrir mesmo assim”).
- O footer “Continuar” leva ao resumo; o app pode abrir com avisos.
- No app: a validação deve ser **read-only**, com checks paralelos e timeout; cada falha traz detalhe e remediação.

---

## 11. Etapa 7 — Pronto

- Ícone de check 56 px; “Tudo pronto” (24/650).
- Resumo 2×3: Podman (`v5.8.9 · rootless` / atualização pendente / pendente), QEMU/KVM (KVM/TCG/pendente), Socket, Docker (removido/pendente), Caddy (ativo/opcional), k3s (ativo/opcional).
- Ações:
  - **Abrir PodStack** (footer primário) — fecha o assistente, abre o app, toast “Tudo pronto — bem-vindo ao PodStack”.
  - **Rever diagnóstico** — volta à etapa 2.
- No app: persistir `setup.completed = true`, versões e timestamp; não mostrar o onboarding automaticamente de novo.

---

## 12. Estado e transições

### 12.1 Modelo da POC (`state.setup`)

```js
{
  open, step, busy, progress, logs,
  docker: true, podman: false,
  podmanVersion: "", podmanLatest: "5.8.9", podmanOutdated: false,
  qemu: false, kvm: true, virtiofsd: false, libvirt: false,
  caddy: false, k3s: false, sqlite: true,
  socket: false, rootless: false,
  installK3s: false,
  scanning: false, validating: false, validated: false
}
```

- `busy`: uma instalação em andamento; desabilita Voltar/Continuar/ações.
- `progress` 0–100 + `logs` (comandos) atualizados a cada 360 ms; ao final, `busy=false`, `logs += "✔ concluído"`.
- `scanning`/`validating`: operações curtas com botão desabilitado.
- `validated`: mostra o banner da validação.
- O seletor de cenário altera `podman`, `podmanOutdated`, `podmanVersion` (demo-only).
- `setup-close` limpa `busy/scanning/validating`; o estado dos componentes permanece (a POC não persiste).
- `setup-open` volta à etapa 1; o estado instalado é mantido na sessão.

### 12.2 No app

- Persistir em `Config`/SQLite: `completed`, `completedAt`, `dockerRemoved`, `podmanVersion`, `qemuInstalled`, `kvmAvailable`, `caddy`, `k3s`, `lastValidation`.
- Reexecutar a qualquer momento; checks sempre atuais; não confiar no cache para decidir remoções.
- Jobs persistentes com id, passos, log, cancelamento e retry; a UI reflete `queued/running/success/failed/cancelled`.
- Se o usuário fechar a janela durante um job, o job continua no daemon e a UI pode reconectar.

---

## 13. Erros, cancelamento e retry

- Falha de rede/registry: mensagem + “Tentar novamente”; não marcar como concluído.
- Falha de permissão: explicar pkexec/helper e o que será solicitado; não pedir senha no app.
- Cancelamento: parar o job; se um passo já alterou o sistema, mostrar o estado atual e permitir retomar.
- Docker em uso: avisar containers ativos; pedir confirmação extra; não remover volumes sem escolha.
- KVM ausente: permitir TCG com aviso; machines ficam mais lentas.
- Distro não suportada: mostrar comandos manuais e permitir “Já instalei”/“Validar de novo”.
- Nunca logar tokens/senhas; logs de instalação podem ser exportados com redaction.

---

## 14. Acessibilidade e i18n

- Passos navegáveis por teclado; foco no conteúdo ao avançar; `Esc` fecha (com confirmação se um job estiver ativo).
- Checks com `Accessible.name` e descrição; não depender só de cor.
- Textos em pt-BR (base) e preparados para en-US; strings em `qsTr()`.
- Área de toque mínima 32 px; botões desabilitados com motivo visível.
- Respeitar `prefers-reduced-motion`.

---

## 15. RPC proposto (app)

> Ainda não implementado. Sugestão para o daemon; alinhar com [`docs/api-rpc.md`](../../docs/api-rpc.md).

| Método | Params | Result |
|---|---|---|
| `setup.status` | `{}` | `{completed, completedAt, checks:[{id,status,version,detail}], summary}` |
| `setup.check` | `{deep?:bool}` | `{checks:[...]}` |
| `setup.apply` | `{actions:[{type:"remove-docker"\|"install-podman"\|"upgrade-podman"\|"install-qemu"\|"install-components", options?}]}` | `{jobId}` |
| `setup.cancel` | `{jobId}` | `{}` |
| `setup.validate` | `{}` | `{checks:[...], ok, warnings}` |

- Ações destrutivas exigem confirmação no cliente e no daemon (parâmetro explícito `confirm: true`), com auditoria no log.
- Jobs com `job.*`/subscription `setup.delta`, `setup.done`; progresso/passos.
- O helper privilegiado só aceita métodos allowlisted (`InstallPackages`, `RemovePackages`, `EnableUserSocket`, …), nunca `Exec`/shell.

---

## 16. Checklist de implementação do onboarding

- [ ] Janela separada, 1020×680, rail 220, 7 etapas, carrossel.
- [ ] Diagnóstico read-only com Docker/Podman/QEMU/KVM/virtiofsd/SQLite/Caddy/k3s.
- [ ] Matriz Docker → Podman com os 6 estados e rótulos corretos.
- [ ] Remoção do Docker com confirmação explícita e aviso de perda de dados.
- [ ] Instalação/atualização do Podman via helper/gerenciador de pacotes.
- [ ] QEMU/KVM, virtiofsd, libvirt opcional, TCG quando não houver KVM.
- [ ] Componentes obrigatórios/opcionais, com k3s desmarcado por padrão.
- [ ] Validação com obrigatórios vs opcionais e “continuar mesmo assim”.
- [ ] Progresso/log/cancelamento/retry; jobs persistentes.
- [ ] Estado persistido e reexecução por Settings/CLI.
- [ ] Sem seletor de cenário no app real.
- [ ] Textos pt-BR/en-US, acessibilidade e foco.
- [ ] Testes: detecção, matriz de estados, falha/cancelamento, retry, consentimento.
