# Plano técnico — 002 Atualização segura e logs de debug

> Derivado de [`spec.md`](spec.md). Não altera [`../../sdd/plan.md`](../../sdd/plan.md); é o delta proposto.
> Escopo: **TX-15** (atualização segura) e **TX-16** (logs de debug/retenção).
> **ADR-0011 está reservado à spec 001**; esta spec usa **ADR-0012**.

---

## 1. Estado atual e lacunas

**Atualização (T7-06, apenas verificação).**

- `updates.check` baixa o manifesto de `updates.feedUrl`, valida Ed25519/minisign e compara versões
  (`src/daemon/UpdateChecker.cpp`, `Daemon.cpp:358/2361`); o CLI expõe `podstack update --check`
  (`src/cli/main.cpp:507`). Não existe download/instalação orquestrada.
- O `.deb`/RPM apenas substitui arquivos. O `postinst` faz
  `systemctl enable --now podstack-helper.service` (e `systemctl --global enable podstack.socket`),
  mas **não reinicia** um helper já ativo; o `prerm` só age na remoção. Não há `preinst`/`%pre`.
- `podstack-gui` é um processo da sessão, `podstackd` é ativado por `podstack.socket`, o helper é
  `Type=dbus` no systemd de sistema. Nenhum é encerrado no upgrade; a unit não define `KillMode`
  (default `control-group`), e a GUI não tem handler de SIGTERM — só salva `UiState` em
  `closeEvent`.
- O QEMU das machines é iniciado com `QProcess::startDetached` (`src/machines/QemuRuntime.cpp`) e o
  virtiofsd idem; ambos ficam no cgroup do `podstack.service`. Um `systemctl --user stop` derruba as
  VMs e os mounts virtiofs. `KillMode=process` **não move** esses processos de cgroup; só evita que
  sejam mortos. O scope próprio é a correção preferencial.
- O incidente 0.1.2→0.1.3: GUI/daemon/helper antigos continuaram; o check de OVMF do daemon 0.1.2
  não via `/usr/share/OVMF/OVMF_CODE_4M.fd`; o helper 0.1.3 (allowlist com `ovmf`) não estava
  carregado; o usuário viu o mesmo erro até reiniciar os três.

**Logs (T7-08/FR-SYS-009, incompleto).**

- `src/core/Logging.cpp` só chama `qSetMessagePattern`; não há file sink, writer assíncrono,
  rotação, retenção nem redaction no caminho das mensagens.
- `Daemon::systemReport` lê `~/.local/state/podstack/logs/podstackd.log` (200 linhas) e aplica
  `Redaction` apenas quando `diagnostics.redact` está ligado; o arquivo depende de um
  redirecionamento do systemd que a unit não define e não é rotacionado pelo app.
- O job de setup (`SetupManager::appendLog`) vive em memória/GUI; ao reiniciar o daemon, o log e o
  erro do helper se perdem.
- O helper grava no journal do sistema (`journalctl -u podstack-helper.service`), não em arquivo
  próprio; o `report` não o inclui.
- `advanced.logLevel` (error/warn/info/debug/trace, default info) já existe no schema/settings, mas
  `advanced.*` é `hidden` na GUI; o daemon só recebe `--verbose` e o nível não é aplicado de forma
  uniforme a GUI/CLI/QML.
- Não há `podstack system logs`, página de logs do app, retenção de 7 dias nem coleta dos logs do
  helper/pacote/update; o comando de logs de container permanece separado.

## 2. Arquitetura proposta

```mermaid
flowchart LR
  subgraph Sessão do usuário
    GUI[podstack-gui] -->|marcador gui-was-open, mostra update, inicia runner e sai| RUN[podstack update --install --runner --manifest]
    RUN -->|systemctl --user stop/mask| DS[podstack.socket / podstack.service]
    RUN -->|D-Bus + polkit: StageUpdate/InstallUpdate/GetUpdateStatus| H[podstack-helper]
    H -->|systemd-run unidade transiente| UNIT[podstack-update-uuid.service]
    UNIT -->|flock + podstack-update-exec| W[podstack-update-exec: redaction/estado/log]
    W -->|apt/dnf local| PKG[pacote .deb/.rpm]
    PKG -->|preinst/%pre| OLD[GUI/daemon/helper antigos]
    PKG -->|postinst/%post/%posttrans| NEWH[helper novo + socket]
    RUN -->|systemctl --user start + podstack-gui --relaunch (marcador)| GUI2[GUI/daemon novos]
  end
  subgraph Logs
    GUI --> LOG[~/.local/state/podstack/logs]
    DS --> LOG
    RUN --> LOG
    W --> VLOG[/var/log/podstack/jobs/uid/jobId.jsonl]
    H --> VLOG
    LOG --> REPORT[podstack report]
    VLOG -->|GetLogs escopado por uid| REPORT
  end
```

| Componente | Onde roda | Responsabilidade |
|---|---|---|
| `preinst` / `%pre` | root (pacote) | parar socket/service de todos os usuários logados, aplicar máscara runtime, encerrar GUI/daemon/helper antes do unpack; fallback `pkill -x podstackd` sem systemd; não parar o runner |
| `postinst` / `%post` | root (pacote) | recarregar units, habilitar socket, remover a máscara, **reiniciar** o helper; não iniciar GUI de usuário |
| `postrm` (Debian) | root (pacote) | `abort-upgrade`/`abort-install` restauram o helper e removem a máscara; remoção encerra os processos do app |
| `%preun` / `%posttrans` (RPM) | root (pacote) | `%preun` antigo roda antes da remoção dos arquivos; `%posttrans` restaura helper/socket, valida `is-active`/versão e só falha se não conseguir |
| `podstack-update-runner` | usuário (processo `podstack`) | orquestrar parada, `StageUpdate`/`InstallUpdate`, polling de `GetUpdateStatus`, religamento e falha |
| `podstack-helper` | root (D-Bus/polkit) | validar manifesto/artefato, staging root-owned, recusar unidade ativa, iniciar a unidade transiente, varrer estado no start e expor status/logs |
| `podstack-update-exec` | root (wrapper da unidade) | segurar `flock`, executar `apt-get`/`dnf`, filtrar stdout/stderr pela `Redaction`, gravar JSONL em `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` e `state.json` |
| `podstack-update-<uuid>.service` | root (unidade transiente) | executar o wrapper fora do cgroup do helper e da sessão; sobrevive ao restart do helper |
| `LogWriter`/`LogRotation` | daemon/GUI/CLI/helper | JSON Lines assíncrono, arquivo por processo, rotação diária/por tamanho, retenção 7 dias, redaction |
| `podstack system logs`/GUI | usuário | ler/filtrar/exportar/limpar os 7 dias; “Ver log” no setup/update |

## 3. Ciclo de atualização

### 3.1 Caminho manual `apt`/`dnf` (resolve o incidente 0.1.3)

O pacote **novo** roda seu `preinst` antes de desempacotar. É ele que encerra o app antigo; isso
funciona mesmo quando o usuário só roda `sudo apt install ./podstack_0.1.4_amd64.deb`.

```sh
# packaging/deb/preinst (novo; upgrades e reinstalações)
if [ "$1" = "upgrade" ] || [ "$1" = "install" ]; then
    if command -v systemctl >/dev/null 2>&1; then
        for uid in $(loginctl list-sessions --no-legend | awk '{print $2}' | sort -u); do
            # Máscara runtime impede socket activation durante o unpack; não persiste.
            systemctl --machine "user@${uid}.service" --user mask --runtime podstack.socket 2>/dev/null || true
            systemctl --machine "user@${uid}.service" --user stop podstack.socket podstack.service 2>/dev/null || true
            pkill -TERM -u "$uid" -x podstack-gui 2>/dev/null || true
            if systemctl --machine "user@${uid}.service" --user is-active --quiet podstack.service 2>/dev/null; then
                echo "PodStack: não foi possível parar o daemon do uid ${uid}; abortando o upgrade." >&2
                exit 1
            fi
        done
        sleep 3
        for uid in $(loginctl list-sessions --no-legend | awk '{print $2}' | sort -u); do
            pkill -KILL -u "$uid" -x podstack-gui 2>/dev/null || true
            if pgrep -u "$uid" -x podstack-gui >/dev/null 2>&1; then
                echo "PodStack: GUI do uid ${uid} não encerrou; abortando o upgrade." >&2
                exit 1
            fi
        done
        if systemctl is-active --quiet podstack-helper.service 2>/dev/null; then
            if ! systemctl stop podstack-helper.service 2>/dev/null; then
                echo "PodStack: não foi possível parar o helper; abortando o upgrade." >&2
                exit 1
            fi
            if systemctl is-active --quiet podstack-helper.service 2>/dev/null; then
                echo "PodStack: helper ainda ativo após stop; abortando o upgrade." >&2
                exit 1
            fi
        fi
    else
        # Sem systemd: fallback por nome de processo; não toca no runner `podstack`.
        pkill -TERM -x podstack-gui 2>/dev/null || true
        pkill -TERM -x podstackd 2>/dev/null || true
        sleep 3
        pkill -KILL -x podstack-gui 2>/dev/null || true
        pkill -KILL -x podstackd 2>/dev/null || true
        pgrep -x podstackd >/dev/null 2>&1 && { echo "PodStack: daemon não encerrou; abortando." >&2; exit 1; }
        pgrep -x podstack-gui >/dev/null 2>&1 && { echo "PodStack: GUI não encerrou; abortando." >&2; exit 1; }
    fi
fi
```

```sh
# packaging/deb/postinst (trecho que muda)
helper_rc=0
unmask_rc=0
systemctl daemon-reload || helper_rc=1
systemctl --global enable podstack.socket || helper_rc=1
systemctl enable podstack-helper.service || helper_rc=1
systemctl restart podstack-helper.service || helper_rc=1     # não enable --now
for uid in $(loginctl list-sessions --no-legend | awk '{print $2}' | sort -u); do
    systemctl --machine "user@${uid}.service" --user daemon-reload 2>/dev/null || true
    if ! systemctl --machine "user@${uid}.service" --user unmask --runtime podstack.socket 2>/dev/null; then
        # Uma tentativa extra; se falhar, informa a remediação do socket.
        systemctl --machine "user@${uid}.service" --user unmask --runtime podstack.socket 2>/dev/null || {
            unmask_rc=1
            echo "PodStack: socket do uid ${uid} continua mascarado; execute 'systemctl --user unmask --runtime podstack.socket' e reabra o app." >&2
        }
    fi
done
if [ "$helper_rc" -ne 0 ]; then
    systemctl start podstack-helper.service 2>/dev/null || true
    echo "PodStack: falha ao religar o helper; execute 'systemctl restart podstack-helper.service' e reabra o app." >&2
fi
[ "$helper_rc" -eq 0 ] && [ "$unmask_rc" -eq 0 ] || exit 1
```

```sh
# packaging/deb/postrm (trecho novo; casos exclusivos do Debian)
case "$1" in
    abort-upgrade|abort-install)
        systemctl daemon-reload >/dev/null 2>&1 || true
        systemctl enable --now podstack-helper.service >/dev/null 2>&1 || true
        for uid in $(loginctl list-sessions --no-legend | awk '{print $2}' | sort -u); do
            systemctl --machine "user@${uid}.service" --user unmask --runtime podstack.socket 2>/dev/null || true
        done
        echo "PodStack: instalação abortada; helper restaurado. Reabra o app." >&2
        ;;
esac
```

- O `preinst` **não** usa `pkill -x podstack`, para não matar o runner `podstack update --install`.
- O `preinst` **não** para o Podman, containers ou machines; o daemon é o único processo do app.
- Se `systemctl --machine` falhar ou o user manager não responder, o `preinst`/`%pre` aplica
  `pkill -TERM -x podstackd`/`pkill -TERM -x podstack-gui` para o uid, valida com `pgrep` e, se
  ainda houver processo, executa o **trap de limpeza** (desfaz a máscara runtime e religa o helper
  dos uids já tocados) antes de abortar.
- A máscara runtime é removida no `postinst`/`%posttrans` por usuário; o socket sobe na próxima
  conexão.
- O `postinst` não inicia a GUI de um usuário (root não deve abrir sessão gráfica). No `apt` manual,
  o usuário reabre; no runner, ele religa a GUI.
- O `postinst` tenta o `unmask` duas vezes; se ainda falhar, informa a remediação do socket, não só
  do helper.
- O `prerm` atual só desabilita o helper na remoção; será alterado para encerrar GUI/daemon/helper de
  todos os usuários logados antes de remover, com o mesmo cuidado do `preinst`.
- RPM equivalente: `%pre` (upgrade) para socket/service/GUI/daemon/helper; `%preun` antigo roda
  **antes** da remoção dos arquivos antigos e pode desabilitar o helper; `%post` novo reinicia o
  helper; `%posttrans` é a recuperação: loop `loginctl` + `systemctl --machine user@<uid>.service --user
  unmask --runtime podstack.socket`, validado por usuário (o manager de sistema não desmascara unit
  de usuário), `enable --now`, health check (`is-active` + versão), mensagem e exit não-zero só se
  não restaurar. O RPM não tem os casos de
  aborto do `postrm` Debian.
- **Cgroup do helper:** o helper inicia o gerenciador em unidade transiente de sistema
  (`systemd-run --unit=podstack-update-<uuid> --collect`), fora do cgroup do helper e da sessão. O
  `postinst` pode reiniciar o helper sem matar o `apt`/`dnf`; o app **não** usa o helper como
  processo pai do gerenciador.
- O `preinst` usa `pkill`; declarar `procps` (Debian) e `procps-ng` (RPM) ou trocar por
  `systemctl`/`loginctl` onde possível. Os paths do inventário seguem a unit/RPM
  (`/usr/bin/podstackd`, `/usr/bin/podstack-gui`); a divergência com `packaging.md`
  (`/usr/libexec`) é reconciliada na T-DOC-01.
- O primeiro upgrade 0.1.3→0.1.4 usa exatamente esse caminho; o runner só existe a partir da 0.1.4.

### 3.2 Caminho assistido (GUI/CLI)

1. `updates.check` (já existe) devolve versão, canal, notas e metadados do manifesto. O runner baixa
   o manifesto assinado de `updates.feedUrl` (mesma origem do check) e grava
   `~/.cache/podstack/updates/manifest.json` (`0600`, TTL de 6 h) e o artefato em cache
   `0700`/`0600`, com `.part` + rename atômico, limite de 300 MB, preflight de espaço, timeout de
   download de 15 min e limpeza de versões antigas. Valida canal, arquitetura, formato e recusa
   versão igual/downgrade. Se `updates.publicKey` estiver customizada, o install assistido é
   recusado (`PODSTACK_UPDATE_UNSUPPORTED`, `reason=custom_key`) e o caminho manual é indicado.
2. A GUI grava `~/.local/state/podstack/updates/gui-was-open` e inicia
   `podstack update --install --runner --manifest <path>` em **novo session/process group**
   (`setsid`, sobrevive ao fechamento da GUI e a SIGHUP) e **sai**. `--manifest` é interno/oculto;
   o runner lê o manifesto já baixado e o artefato do cache.
3. O runner grava `state.json` atomicamente (`QSaveFile`/rename) com `pid`, `boot_id` e timestamp
   (o `jobId`, a `unit` e o `stateDir` são acrescentados após o `StageUpdate`, antes do
   `InstallUpdate`), e **para** `podstack.socket`/`podstack.service` do usuário (a GUI já saiu); em
   seguida chama o helper `StageUpdate` com o manifesto e o caminho regular do artefato.
4. O helper autentica por polkit (`dev.podstack.helper.stage-update`, `auth_admin`), obtém o uid por
   `GetConnectionUnixUser`/polkit, confere que o arquivo é regular, sem symlink (`O_NOFOLLOW`), dono
   = uid do chamador e copia para staging root-owned `0700` em `/run/podstack/update/<uuid>/` com
   timeout de stage de 5 min. Dentro do helper, valida a assinatura Ed25519/minisign com a **chave de
   release embutida** (ou `/etc/podstack/update.pub` root-owned), compara o **SHA-256 do artefato**
   ao valor do manifesto assinado, confere nome/versão/arch (`dpkg-deb -f`/`rpm -qp`) e devolve
   `jobId`.
5. O runner chama `InstallUpdate(jobId)`; o helper serializa a admissão com
   `flock /run/podstack/update.admission.lock` (checagem de unidade ativa + criação do `systemd-run`
   atômicas), recusa se houver `podstack-update-*` ativa e inicia a unidade transiente de sistema:
   `systemd-run --unit=podstack-update-<uuid> --collect --property=RuntimeMaxSec=1800 --
   /usr/bin/flock /run/podstack/update.lock /usr/libexec/podstack/podstack-update-exec ...`
   (ou `dnf`). O **lock pertence à unidade**, não ao helper; o wrapper `podstack-update-exec`
   executa o gerenciador, filtra stdout/stderr pela `Redaction`, grava JSONL em
   `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (`0640`), espelha no journal e escreve
   `/run/podstack/update/<uuid>/state.json` (`0640`) com fase/percentual/exit/error. Não há
   redirecionamento `append` do systemd para stdout/stderr.
6. O progresso é lido por `GetUpdateStatus {jobId}` (ação
   `dev.podstack.helper.get-update-status`, `auth_admin_keep`), retornando
   `{jobId, phase, percent, message, exitCode, errorCode}`; `UpdateProgress`/`UpdateFinished` são
   best-effort. O runner faz **polling com retry/backoff** enquanto o helper estiver parado ou
   reiniciando. No start, o helper varre unidades `podstack-update-*` e `state.json` e reconstrói o
   estado; não confia em memória.
7. O `preinst`/`%pre` do pacote remove qualquer GUI/daemon remanescente e para o helper; o
   `postinst`/`%post` reinicia o helper e reabilita o socket, sem matar a unidade transiente. O
   runner espera o estado terminal por polling e confere a versão instalada.
8. Sucesso: `systemctl --user start podstack.socket` (o daemon sobe sob demanda); se o marcador
   `gui-was-open` existir, `podstack-gui --relaunch` e apaga o marcador; `state.json=done`;
   notificação nativa; log do update. Ao reabrir, a GUI mostra o resultado.
9. Falha: o helper é mantido ou restaurado ativo; o runner religa `podstack.socket`/`podstack-gui`
   da versão instalada, grava o erro em `state.json` e no log; o usuário vê “Atualização falhou”
   com “Ver log”. Não há rollback automático (D7); na próxima execução de `podstack update`/GUI, o
   `state.json` é lido e o usuário pode **retomar ou limpar**; o daemon não instala.

**CLI:** `podstack update --install [--yes] [--json]` faz o download/verificação e executa o runner
no terminal; sem `--yes` em TTY, pede confirmação; sem TTY, nunca prompt (exit 2 se faltar `--yes`).
`--runner`, `--relaunch` e `--manifest` são flags internas/ocultas usadas pela GUI, não documentadas
como API pública. O polkit pede a senha; o progresso sai em texto ou NDJSON com `--json`; `Ctrl+C`
antes do início da instalação cancela com 9 e os timeouts retornam 8 (download 15 min, stage 5 min,
install 30 min via `RuntimeMaxSec`). Depois que a unidade transiente começou, o cancelamento é
recusado com instrução (não se interrompe `apt`/`dnf` no meio da transação).

**exit codes do update:**

| Comando | 0 | 1 | 2 | 3 | 6 | 8 | 9 |
|---|---|---|---|---|---|---|---|
| `update --check` | atualizado | erro de rede/manifesto | uso | desatualizado | permissão/consentimento | timeout | cancelado |
| `update --install` | instalado | falha de download/instalação | uso | — | consentimento/permissão | timeout | cancelado |

`4` (não encontrado) e `7` (engine indisponível) **não** são usados para falha de update. O
`--install` usa **0/1/2/6/8/9**; `3` é exclusivo do `--check`. Com `--json`, o objeto tem `api: 1`,
`command`, `status` (`ok|outdated|failed|cancelled`), `version`, `phase`,
`error: {code, message, detail}`; `--check` inclui `available`/`notes`; `--install` inclui `jobId`.
Os códigos estáveis são `PODSTACK_UPDATE_MANIFEST_INVALID`, `PODSTACK_UPDATE_SIGNATURE_INVALID`,
`PODSTACK_UPDATE_PACKAGE_INVALID`, `PODSTACK_UPDATE_DOWNLOAD_FAILED`,
`PODSTACK_UPDATE_INSTALL_FAILED`, `PODSTACK_UPDATE_CONSENT_DENIED`,
`PODSTACK_UPDATE_VERSION_EQUAL`, `PODSTACK_UPDATE_LOCKED`, `PODSTACK_UPDATE_UNSUPPORTED`.
Versão igual usa `PODSTACK_UPDATE_VERSION_EQUAL`; regressão/downgrade usa
`PODSTACK_UPDATE_MANIFEST_INVALID` com `detail: version_regression`; chave customizada usa
`PODSTACK_UPDATE_UNSUPPORTED` com `reason=custom_key`.

`updates.mode=auto` significa **verificar e baixar**, nunca instalar silenciosamente
(`security.md`); a instalação continua exigindo consentimento. Em Flatpak/sandbox ou sem systemd de
usuário, o app detecta a condição, desabilita o update assistido com mensagem clara e oferece o
caminho manual, sem deixar o app parado.

**Primeiro runner:** a 0.1.3 não tem o comando; o usuário instala a 0.1.4 manualmente (o `preinst`
resolve o incidente). A partir da 0.1.4, o runner cuida das próximas versões.

### 3.3 Processos e ordem

| Processo | Dono | Unit/escopo | Antes do unpack | Depois |
|---|---|---|---|---|
| `podstack-gui` | usuário | sessão gráfica | `pkill -TERM` (SIGKILL após 3 s); handler de SIGTERM salva `UiState`; a GUI do runner sai antes | runner/manual: `podstack-gui` |
| `podstackd` | usuário | `podstack.service` | `systemctl --user stop` em todos os logados; `pkill -TERM` como fallback no `preinst` | ativação por `podstack.socket` |
| `podstack.socket` | usuário | socket | `mask --runtime` + `systemctl --user stop` em todos os logados | `unmask --runtime`; `systemctl --user start` no runner ou próxima conexão |
| `podstack-helper` | root | `podstack-helper.service` | `systemctl stop` no `preinst` | `systemctl restart` no `postinst`; varre `podstack-update-*`/`state.json` no start |
| runner `podstack update` | usuário | processo `podstack` | **não matar** | termina após religar; polling de `GetUpdateStatus` |
| job de update | root | `podstack-update-<uuid>.service` (transiente) | — | wrapper `podstack-update-exec` mantém `flock`, grava estado/log; sobrevive ao restart do helper; `--collect` limpa ao final |
| QEMU/virtiofsd | usuário | `systemd-run --user --scope` (preferencial) | **não parar** | vivos no scope; `KillMode=process` só evita a morte, não move de cgroup; reconciliação de órfãos cobre restart |
| Podman/containers | usuário | `podman.socket` | **não parar** | intactos |

Ordem do pacote Debian: `prerm` antigo (upgrade é no-op; remove) → `preinst` novo (socket/service/
GUI/daemon/helper) → unpack → `postinst` novo (socket/helper) → triggers. Ordem real do RPM no
upgrade: `%pre` novo → `%preun` antigo → remoção dos arquivos antigos → `%post` novo → `%posttrans`
novo.

### 3.4 Verificação e segurança

- Manifesto assinado (Ed25519/minisign); a **chave de release** é embutida no helper ou
  `/etc/podstack/update.pub` root-owned. `updates.publicKey` serve apenas para check/notificação;
  se estiver customizada, o install assistido é recusado com `PODSTACK_UPDATE_UNSUPPORTED`/
  `reason=custom_key` e o caminho manual é indicado.
- O helper valida a assinatura, o **SHA-256 do artefato** contra o valor do manifesto assinado e a
  identidade do pacote; o chamador não fornece SHA/versão confiáveis.
- O helper obtém o uid por `GetConnectionUnixUser`/polkit (D-Bus), copia o artefato para staging
  root-owned `0700` com `O_NOFOLLOW`, canonicalização e `fstat` de dono; rejeita symlink, arquivo
  não regular, outro dono e path fora do staging.
- A unidade transiente mantém o `flock` de `/run/podstack/update.lock`; o helper recusa
  `InstallUpdate` se houver `podstack-update-*` ativa e reconstrução de estado no start. Polkit
  `auth_admin` para stage/install, `auth_admin_keep` para status, rate limit (10/min por uid) e
  auditoria no journal.
- O wrapper `podstack-update-exec` filtra stdout/stderr pela `Redaction` e grava JSONL em
  `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (`0640`); não há redirecionamento `append` do systemd.
  O wrapper é `ExecStart` da unidade transiente, não é exposto por D-Bus e não aceita comandos
  arbitrários. `GetLogs`/`GetUpdateStatus` usam o uid do chamador via D-Bus/polkit.
- O update não toca em Podman/containers/machines; QEMU/virtiofsd ficam em scope próprio e o
  restart do daemon não os encerra.

## 4. Logs de debug

### 4.1 Núcleo

`src/core/Logging` passa a instalar um `qInstallMessageHandler` que **nunca faz I/O no thread que
emite a mensagem**:

- mantém o padrão atual no stderr/journal;
- enfileira em writer thread com fila limitada (8192 entradas), drop-oldest e contador `dropped`
  emitido periodicamente; se a fila encher, o app continua responsivo;
- escreve JSON Lines em `~/.local/state/podstack/logs/` (0600; dir 0700), em arquivo por processo:
  `podstackd.jsonl`, `podstack-gui-<pid>.jsonl`, `podstack-cli-<pid>.jsonl`, com `ts`, `level`,
  `category`, `process`, `pid`, `thread`, `message`, `context` (arquivo/linha/função);
- usa `O_APPEND`, flush a cada 1 s e imediatamente em warn/error+, e flush final no encerramento;
  um crash perde no máximo ~1 s de linhas info/debug;
- aplica `Redaction::text` **antes** de gravar (tokens, `Authorization`, senhas, URLs com
  credenciais, env), inclusive em logs root;
- respeita `advanced.logLevel` (default info; debug/trace opt-in, sem rebuild) e
  `PODSTACK_LOG_LEVEL`; a GUI/CLI/daemon/QML/Qt passam pelo mesmo sink; `--verbose` continua como
  atalho.

### 4.2 Rotação e retenção

- `LogRotation` (novo em `src/core`): no start e por um timer diário à meia-noite local, fecha o
  arquivo do dia e remove arquivos com mais de **7 dias** (configurável 1–30); aplica o limite por
  arquivo (`advanced.logMaxBytes`, default **10 MB**) e o teto total de **200 MB**, removendo o mais
  antigo primeiro. Nomes: o corrente é `<processo>.jsonl`/`<processo>-<pid>.jsonl` (FR-LOG-001); a
  rotação deriva `<processo>-YYYYMMDD.jsonl`/`<processo>-<pid>-YYYYMMDD.jsonl`, e a rotação por
  tamanho gera `…-YYYYMMDD-N.jsonl`. O follow reabre pelo nome; a rotação é um rename atômico sob
  `flock` por arquivo.
- Chaves em `engineering/settings.md`/schema: `advanced.logRetentionDays` (default **7**),
  `advanced.logMaxBytes` (default **10 MB**), expostas na GUI (Avançado) e na CLI;
  `advanced.logLevel` já existe e passa a ser exposto na GUI com `restartRequired: false`
  (aplicação em runtime). Desligar debug vale imediatamente; debug não exige rebuild. A promoção
  corrige o `restartRequired` atual (`sim`) em `settings.md`.
- O `system.report` lê os arquivos rotacionados necessários para os 7 dias, redige e grava o report
  0600 em JSON (`{path, bytes, sha256}`), **não** tar.gz; `diagnostics.includeLogs` passa a default
  **false** (opt-in) e os caps são 10.000 linhas/componente e 5 MB no total. A doc antiga que
  promete tar.gz/`podstackd.log` é corrigida na promoção.

### 4.3 Helper, pacote e update

- `podstack-update-exec` grava stdout/stderr redigidos em
  `/var/log/podstack/jobs/<uid>/<jobId>.jsonl` (root, dir `0750`, arquivo `0640`), com `uid`/`jobId`
  em cada linha/índice, e espelha no journal; `jobId` é `update-<uuid>` e o índice por update é o
  próprio `jobId` (não há diretório separado `update-<uuid>/`). Os scriptlets recebem
  `PODSTACK_UPDATE_JOB_ID`/`PODSTACK_UPDATE_UID` do wrapper e emitem mensagens estruturadas via
  `logger` com as chaves `JOB_ID`/`UID`, sem despejar saída crua; `GetLogs` filtra o journal por
  essas chaves e combina com o arquivo do job. O helper também escreve
  `/var/log/podstack/helper-YYYYMMDD.log`.
- `/var/log/podstack` é root:root `0750`, arquivos `0640`, com `tmpfiles.d`, logrotate/timer e
  política SELinux/AppArmor; a redaction é aplicada antes do append. O app **nunca lê esses arquivos
  diretamente do filesystem**.
- O helper expõe `GetLogs {jobId?}` allowlisted (`dev.podstack.helper.get-logs`, `auth_admin`, rate
  limit 10/min, auditoria): somente operações do uid do chamador (uid via
  `GetConnectionUnixUser`/polkit), cap 1 MiB, retorno `{lines, truncated}` redigido; logs de
  sistema/root fora dessas operações não são expostos. O `journalctl -u podstack-helper.service`
  continua para auditoria.
- O job de setup (`SetupManager::appendLog`) também emite uma linha `setup` com `jobId`, ação,
  progresso e erro no log do daemon; o painel da GUI continua; o report inclui as linhas.
- O update runner/`state.json` registra a fase e o erro no log; a GUI/CLI mostra “Ver log”.
- O legado `~/.local/state/podstack/logs/podstackd.log` é migrado para JSONL no primeiro start ou
  removido; `system.report` e a documentação deixam de lê-lo, e a unit não redireciona stdout para
  ele.

### 4.4 CLI e GUI

- `podstack system logs [--follow] [--since 7d] [--level] [--component] [--json] [--export PATH]
  [--clear]`: lê os arquivos do usuário, filtra e exporta; `--since` aceita `7d`/ISO 8601;
  `--component` aceita `daemon|gui|cli|qml|setup|update|helper|package` (`helper`/`package` usam
  `GetLogs` do helper, com polkit; `package` = lotes de update do uid); `--export` canonicaliza o
  destino, recusa symlink/path fora do destino e grava `0600`; `--json` emite
  `{api:1, lines:[{ts,level,component,message}], truncated, files}` e erro
  `{api:1, error:{code,message,detail}}`; filtro inválido retorna 2. O `--clear` apaga só logs do
  usuário; o backend/limpeza é de T-LOG-02 e a superfície CLI/GUI de T-LOG-03. O comando de logs de
  container permanece separado e inalterado.
- GUI: página/diálogo **Logs** (filtro por nível/componente, follow, abrir pasta, exportar,
  “Exportar diagnóstico”, limpar com confirmação); a leitura roda em worker/modelo virtualizado,
  sem I/O no thread da UI; o erro de setup/update ganha **Ver log** que abre o painel já filtrado;
  strings com `qsTr` e `Accessible.name`.
- Progresso do update: a GUI sai antes da instalação; o runner comunica por notificação nativa e
  `state.json`/`GetUpdateStatus`; ao reabrir, a GUI lê o resultado. O marcador `gui-was-open` decide
  o `--relaunch` e é apagado depois.
- O `report --include-logs` passa a incluir daemon/GUI/CLI/setup/update/helper/pacote, com redaction
  incondicional e sem upload; os logs root entram via `GetLogs` escopado; report vazio e volumetria
  têm teste.

## 5. Arquivos afetados

**Novos:** `docs/specs/002-update-seguro-logs-debug/*` (este delta); `src/core/LogWriter.*`;
`src/core/LogRotation.*`; `packaging/deb/preinst`; `packaging/deb/postrm`;
`packaging/podstack-update-exec` (instalado em `/usr/libexec/podstack/podstack-update-exec`);
`packaging/tmpfiles.d/podstack.conf`; `packaging/logrotate/podstack` (ou timer equivalente);
testes de rotação/helper/runner; ações polkit `stage-update`/`install-update`/`get-update-status`/
`get-logs` no `packaging/polkit/dev.podstack.helper.policy`.

**Alterados:** `src/core/Logging.{h,cpp}`; `src/core/Settings`/schema
(`advanced.logRetentionDays`/`MaxBytes`, exposição de `logLevel`); `src/daemon/main.cpp`/
`Daemon.cpp` (sink, report, setup); `src/daemon/SetupManager.cpp`; `src/daemon/UpdateChecker.*`
(download/verify/estado); `src/gui/main.cpp`/`MainWindow.cpp` (handler SIGTERM/SIGINT,
`saveUiState`, marcador `gui-was-open`)/`Backend`/QML (update/logs); `src/cli/main.cpp`
(`update --install`, `system logs`); `src/helper/main.cpp`/`HelperService` (`StageUpdate`/
`InstallUpdate`/`GetUpdateStatus`/`GetLogs`, varredura de unidades/estado, log file);
`src/machines/QemuRuntime.cpp` (scope de QEMU/virtiofsd); `packaging/deb/postinst`/`prerm`;
`packaging/rpm/podstack.spec`; `packaging/deb/control` e `packaging/rpm` (`procps`/`procps-ng`);
`packaging/systemd/podstack.service` (`KillMode=process` como defesa em profundidade, sem mover
QEMU); `packaging/systemd-system/podstack-helper.service`; `packaging/selinux/*`;
`scripts/build-deb.sh`/`build-rpm.sh` (instalar os novos arquivos); `scripts/clean-install-check.sh`
(upgrade real); `translations/*`; `CHANGELOG`; `docs/engineering/{packaging,settings,architecture,
security,cli-parity,ui-map}.md`; `docs/engineering/api-rpc.md` apenas para `system.report`
(`GetLogs` fica em `security.md`/contrato do helper); `docs/sdd/{PRD,spec,plan,tasks,testing}.md` na
promoção; `docs/README.md`/`AGENTS.md` (índice de ADR, incluindo 0010); `docs/user/*`;
`web/{install,onboarding,troubleshooting}.html` e `_data/site.json`; `CMakeLists.txt` (bump
0.1.3→0.1.4); `docs/adr/0012-update-logging.md` (proposto; ADR-0011 pertence à spec 001).

## 6. Alternativas

| Alternativa | Prós | Contras | Decisão |
|---|---|---|---|
| Só scripts do pacote (`preinst`/`postinst`) | resolve o `apt` manual e o incidente; simples | não há update assistido; o usuário reabre | **Fazer** (T-UPD-01/02) |
| Runner + helper `StageUpdate`/`InstallUpdate` + unidade transiente | update assistido; para/religa o app; valida assinatura no root; fora dos cgroups | bootstrap da 0.1.3; polkit; mais peças | **Fazer** (T-UPD-03/04) |
| Segunda fronteira root dedicada (script próprio) | aparentemente simples | viola a constituição/`security.md` (helper é a única fronteira root), sem allowlist/hardening/auditoria; roda no cgroup da sessão | **Rejeitar** |
| Helper como processo pai do gerenciador | reusa a allowlist | o `postinst` reinicia o helper e pode matar o `apt`/`dnf` filho | **Rejeitar** |
| Lock global mantido pelo helper | mais simples de implementar | restart do helper libera o lock e dois updates podem sobrepor; o lock não pertence ao job | **Rejeitar** |
| `podstack-update.service` de usuário | systemd supervisiona | sessões remotas sem systemd; mais complexo; a GUI ainda precisa sair | **Rejeitar no v1** |
| Logs só no journal | zero código | não há arquivo de 7 dias do app, setup/helper/update; o report fica incompleto | **Rejeitar** |
| Logs JSON Lines + retenção no app | consulta/export/report; 7 dias; redaction | mais código; limite de disco | **Fazer** (T-LOG-01/02/03) |
| Logging síncrono com mutex | menos código | I/O no thread da UI; contenção entre processos; não atende NFR-LOG-001 | **Rejeitar** |

## 7. Riscos e mitigação

| Risco | Mitigação |
|---|---|
| `preinst` mata a GUI/daemon no meio de uma operação | `SIGTERM` + handler que salva `UiState` + 3 s; containers/machines não param; o `preinst` aborta se não conseguir parar |
| Socket activation reativa o daemon antigo durante o unpack | `mask --runtime` no `podstack.socket` de todos os logados antes do stop; `unmask` no `postinst` com retry/remediação; teste de corrida |
| `postinst` reiniciar o helper e derrubar o `apt` pai | o helper nunca é pai do gerenciador; unidade transiente de sistema; o `preinst` para o helper antes |
| Assinatura/caminho/SHA spoofados ou TOCTOU | chave de release embutida/root-owned; SHA do artefato contra manifesto assinado; staging root-owned; `O_NOFOLLOW`/dono via `GetConnectionUnixUser`; identidade do pacote |
| `updates.publicKey` customizada usada para instalar | install assistido usa a chave de release; setting customizada recusa com `PODSTACK_UPDATE_UNSUPPORTED`/`reason=custom_key` |
| Lock no helper liberado no restart | lock mantido pela unidade transiente (`flock`); helper recusa unidade ativa e reconstrói estado no start |
| Progresso perdido no restart do helper | estado em `/run/podstack/update/<uuid>/state.json` escrito pelo wrapper; `GetUpdateStatus` lê o arquivo; runner faz polling com backoff; sinais best-effort |
| Dois usuários atualizam ao mesmo tempo | lock global na unidade; helper recusa `podstack-update-*` ativa; estados por uid e auditoria |
| Primeiro update (0.1.3) não tem runner | caminho manual documentado; o pacote novo tem `preinst`/`postinst`; o 0.1.4 passa a ter o runner |
| Falha após `%pre` no RPM | `%posttrans` restaura socket/helper, valida `is-active`/versão e só falha se não conseguir; teste dedicado |
| Polkit indisponível em SSH/Tuxee ou Flatpak/sem systemd | o CLI mostra o erro e o log; o app detecta e desabilita o update assistido; o caminho manual com `sudo apt`/`dnf` continua coberto |
| Logs com segredos ou disco cheio | `Redaction` no wrapper/Logging; 0600/0640; 7 dias; limites por arquivo/total; teste com token na saída do `apt`/`dnf` |
| `GetLogs` expõe operação de outro uid | logs por `/var/log/podstack/jobs/<uid>/<jobId>.jsonl`; `GetLogs {jobId?}` filtra pelo uid via `GetConnectionUnixUser`/polkit; teste uid A/B |
| Logging bloquear a UI | writer assíncrono, fila limitada/drop-oldest, flush periódico; teste de carga e `kill -9` |
| Retenção apagar log útil | 7 dias por padrão; configurável até 30; export/report antes; poda só do mais antigo |
| Timeout de download/stage/install | 15/5/30 min; `RuntimeMaxSec=1800` na unidade; exit 8 e teste de timeout |
| Regressão Fedora/Ubuntu/RPM/DEB | testes unitários; e2e nas VMs 24.04/26.04/Fedora; `packaging.yml`/clean-install com upgrade real |
| Update falhar e deixar app parado | helper é mantido/restaurado ativo; runner religa a GUI/daemon instalados; próximo `podstack update`/GUI lê `state.json` e oferece retomar/limpar |

## 8. Testes

**Unitários/integração:** rotação/retenção/redaction com nomes `-YYYYMMDD[-N].jsonl` e timer da
meia-noite; writer assíncrono com concorrência (daemon+CLI), fila cheia, `kill -9` e follow após
rename; parse do manifesto/artefato/SHA/versão/lock/estado (stale lock e JSON corrompido); download
interrompido, disco cheio, arch/formato errado, versão igual, canal beta/stable, manifesto em cache
e timeouts 15/5/30 min; validação do helper (assinatura com chave de release, `updates.publicKey`
customizada, path, dono via D-Bus, `O_NOFOLLOW`, identidade do pacote, TOCTOU); `InstallUpdate` com
unidade ativa e restart do helper no meio do `apt`; dois usuários + lock na unidade; `podstack system
logs` (filtros, `--json` schema `{api:1,lines,truncated,files}`, erro, `--export`, `--clear`, exit 2);
`GetLogs` uid A/B/negado e `jobId`; token na saída do `apt`/`dnf` redigido; scripts
`preinst`/`postinst`/`postrm`/`prerm` e RPM `%pre`/`%preun`/`%post`/`%posttrans` com processos falsos
em container; fallback sem systemd; falha após `%pre` no RPM; `unmask` com retry/remediação; corrida
de socket activation; detecção de Flatpak/sem systemd; marcador `gui-was-open`/`--relaunch`; report
vazio e com volumetria; `test_cli/goldenExitCodes` com check 0/1/2/3/6/8/9 e install 0/1/2/6/8/9;
`clean-install-check` com upgrade real. Scriptlets root são testados em container com justificativa
registrada em `testing.md`; gate de cobertura ≥ 80%.

**E2E (VMs 24.04/26.04/Fedora):**

1. 0.1.3 com GUI/daemon/helper abertos e **machine rodando** → `apt install
   ./podstack_0.1.4.deb`: PIDs antigos ausentes; helper novo; socket não reativa daemon antigo;
   `podstack setup --validate` OVMF `ok`; containers e QEMU/virtiofsd intactos.
2. 0.1.4 → GUI “Atualizar” 0.1.5: runner, polkit, `StageUpdate`/`InstallUpdate`, unidade transiente,
   stop/install/restart; GUI/daemon 0.1.5; log do update; restart do helper no meio do `apt` não
   mata a unidade e o polling de `GetUpdateStatus` continua.
3. Falha simulada (download/assinatura/unpack/`postinst`/após `%pre` RPM): helper/socket
   restaurados; GUI/daemon instalados religados; log com o erro; nenhum órfão; próximo
   `podstack update`/GUI oferece retomar/limpar.
4. Logs: 7 arquivos diários, 8º removido, rotação por tamanho, 0600/0640, debug, setup/helper/update,
   token redigido, `--json`/`--export`/`--clear`, `podstack system logs`/GUI/report, uid A não vê B.
5. SSH/Tuxee/Flatpak: documentar o caminho manual (`podstackd` no mesmo XDG), o erro claro do runner
   e a detecção que desabilita o update assistido.

**UI:** `test_gui`/`test_terminal` para a página de logs e o diálogo de update; leitura em
worker/modelo virtualizado; `qmllint` dos QML alterados; `Accessible.name`/i18n; `clang-format`/
`clang-tidy`; cobertura ≥ 80%.

## 9. Ordem de implementação e docs

1. Aprovar a spec e as decisões abertas (§9).
2. **T-UPD-00:** colocar QEMU/virtiofsd em scope próprio (`systemd-run --user --scope`;
   `KillMode=process` como defesa) e validar update com machine rodando. É pré-requisito do runner.
3. **T-UPD-01/02:** `preinst`/`postinst`/`postrm`/`prerm` Debian e `%pre`/`%preun`/`%post`/
   `%posttrans` RPM; socket activation, helper restart e handler SIGTERM da GUI; e2e do `apt`
   manual (o incidente 0.1.3). É o ganho imediato.
4. **T-LOG-01:** núcleo de logs assíncrono/retenção/redaction/settings; sink no daemon/GUI/CLI.
5. **T-UPD-03/04:** helper `StageUpdate`/`InstallUpdate`, runner/CLI/GUI, download/verificação,
   unidade transiente, estado/lock, bootstrap, falha, Flatpak/sem systemd.
6. **T-LOG-02/03:** logs root/helper/update/package, `GetLogs`, report, `podstack system logs`,
   GUI e limpeza.
7. **T-E2E-01/T-DOC-01:** VMs, docs, web, ADR, CHANGELOG, bump 0.1.3→0.1.4.

Docs: `engineering/packaging.md` (scriptlets/ordem/cgroup/logs root/wrapper), `settings.md`
(logLevel exposto/retenção/limites), `architecture.md` (update/log/report, cgroup de machines),
`security.md` (fronteira root única, `GetLogs`, SELinux/AppArmor, redaction, chave de release),
`cli-parity.md` (`update --install`, exit codes, `system logs`), `ui-map.md` (update/logs),
`api-rpc.md` apenas para `system.report` (`GetLogs` fica no contrato do helper/`security.md`),
`testing.md` (casos e cobertura), `sdd/PRD.md` (FR-UPD-006…016, FR-SYS-009, NFR-SEC-004,
NFR-OBS-001/004), `sdd/spec.md` (nova seção), `sdd/plan.md`, `sdd/tasks.md` (TX-15/TX-16),
`docs/README.md`/`AGENTS.md` (índice de ADR, incluindo 0010), `translations/`, `docs/user/*`,
`web/*`, `CHANGELOG`. **ADR-0012** é desta spec; **ADR-0011 está reservado à spec 001**.
