# Plano técnico — 002 Atualização segura e logs de debug

> Derivado de [`spec.md`](spec.md). Não altera [`../../sdd/plan.md`](../../sdd/plan.md); é o delta proposto.
> Escopo: **TX-12** (atualização segura) e **TX-13** (logs de debug/retenção).

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
  `Type=dbus` no systemd de sistema. Nenhum é encerrado no upgrade.
- O incidente 0.1.2→0.1.3: GUI/daemon/helper antigos continuaram; o check de OVMF do daemon 0.1.2
  não via `/usr/share/OVMF/OVMF_CODE_4M.fd`; o helper 0.1.3 (allowlist com `ovmf`) não estava
  carregado; o usuário viu o mesmo erro até reiniciar os três.

**Logs (T7-08/FR-SYS-009, incompleto).**

- `src/core/Logging.cpp` só chama `qSetMessagePattern`; não há file sink, rotação, retenção nem
  redaction no caminho das mensagens.
- `Daemon::systemReport` lê `~/.local/state/podstack/logs/podstackd.log` (200 linhas) e aplica
  `Redaction`; o arquivo depende do redirecionamento do systemd e não é rotacionado pelo app.
- O job de setup (`SetupManager::appendLog`) vive em memória/GUI; ao reiniciar o daemon, o log e o
  erro do helper se perdem.
- O helper grava no journal do sistema (`journalctl -u podstack-helper.service`), não em arquivo
  próprio; o `report` não o inclui.
- `advanced.logLevel` (error/warn/info/debug/trace, default info) já existe no schema/settings, mas
  o daemon só recebe `--verbose`; o nível não é aplicado de forma uniforme a GUI/CLI/QML.
- Não há `podstack logs`, página de logs do app, retenção de 7 dias nem coleta dos logs do
  helper/pacote/update.

## 2. Arquitetura proposta

```mermaid
flowchart LR
  subgraph Sessão do usuário
    GUI[podstack-gui] -->|mostra update, inicia runner e sai| RUN[podstack update --install --runner]
    RUN -->|systemctl --user stop| DS[podstack.socket / podstack.service]
    RUN -->|pkexec| ROOT[podstack-update-root]
    ROOT -->|apt/dnf local| PKG[pacote .deb/.rpm]
    PKG -->|preinst/%pre| OLD[GUI/daemon/helper antigos]
    PKG -->|postinst/%post| NEWH[helper novo + socket]
    RUN -->|systemctl --user start + podstack-gui| GUI2[GUI/daemon novos]
  end
  subgraph Logs
    GUI --> LOG[~/.local/state/podstack/logs]
    DS --> LOG
    RUN --> LOG
    ROOT --> VLOG[/var/log/podstack/update]
    PKG --> VLOG
    NEWH --> VLOG
    LOG --> REPORT[podstack report]
    VLOG --> REPORT
  end
```

| Componente | Onde roda | Responsabilidade |
|---|---|---|
| `preinst` / `%pre` | root (pacote) | parar GUI/daemon/helper antigos antes do unpack; não parar o runner |
| `postinst` / `%post` | root (pacote) | recarregar units, habilitar socket, **reiniciar** o helper; não iniciar GUI de usuário |
| `podstack-update-runner` | usuário (processo `podstack`) | orquestrar parada, instalação, religamento e falha |
| `podstack-update-root` | root (pkexec) | validar caminho/SHA, chamar apt/dnf, registrar em `/var/log/podstack` |
| `LogFile`/`LogRotation` | daemon/GUI/CLI/helper | JSON Lines, rotação diária, retenção 7 dias, redaction |
| `podstack logs`/GUI | usuário | ler/filtrar/exportar os 7 dias; “Ver log” no setup/update |

## 3. Ciclo de atualização

### 3.1 Caminho manual `apt`/`dnf` (resolve o incidente 0.1.3)

O pacote **novo** roda seu `preinst` antes de desempacotar. É ele que encerra o app antigo; isso
funciona mesmo quando o usuário só roda `sudo apt install ./podstack_0.1.4_amd64.deb`.

```sh
# packaging/deb/preinst (novo; upgrades e reinstalações)
if [ "$1" = "upgrade" ] || [ "$1" = "install" ]; then
    pkill -TERM -x podstack-gui 2>/dev/null || true
    pkill -TERM -x podstackd 2>/dev/null || true
    sleep 3
    pkill -KILL -x podstack-gui 2>/dev/null || true
    pkill -KILL -x podstackd 2>/dev/null || true
    systemctl stop podstack-helper.service 2>/dev/null || true
fi
```

```sh
# packaging/deb/postinst (trecho que muda)
systemctl daemon-reload
systemctl --global enable podstack.socket
systemctl enable podstack-helper.service
systemctl restart podstack-helper.service     # não enable --now
```

- O `preinst` **não** usa `pkill -x podstack`, para não matar o runner `podstack update --install`.
- O `preinst` **não** para o Podman, containers ou machines; o daemon é o único processo do app.
- O `postinst` não inicia a GUI de um usuário (root não deve abrir sessão gráfica). No `apt` manual,
  o usuário reabre; no runner, ele religa a GUI.
- `prerm` (remove) encerra GUI/daemon/helper e desabilita o helper, como hoje, com o mesmo cuidado.
- RPM equivalente: `%pre` (upgrade), `%post` (`systemctl restart podstack-helper.service` +
  `%systemd_post`), `%preun` (remove) e o `%posttrans` do SELinux preservado.
- **Cgroup do helper:** o `pkexec`/script root instala o pacote; o `preinst` pode parar o helper sem
  matar o `apt`. O app **não** usa `podstack-helper InstallPackages` para instalar o próprio pacote.
  Isso evita que o `postinst` (`systemctl restart podstack-helper.service`) derrube o processo pai do
  `apt` (o helper).
- O primeiro upgrade 0.1.3→0.1.4 usa exatamente esse caminho; o runner só existe a partir da 0.1.4.

### 3.2 Caminho assistido (GUI/CLI)

1. `updates.check` (já existe) devolve versão, canal, notas e artefatos (`rpm`/`deb`, `url`,
   `sha256`). A GUI/CLI exige consentimento e baixa o artefato da arquitetura atual para
   `~/.cache/podstack/updates/<version>/`, 0600; verifica SHA-256 e a assinatura do manifesto.
2. A GUI mostra o progresso e, ao confirmar, inicia
   `podstack update --install --runner --package <path> --sha256 <sha> --version <v>` e **sai**.
   O processo do runner é `podstack` (o `preinst` não o mata).
3. O runner adquire `~/.local/state/podstack/updates/update.lock` (O_EXCL), grava `state.json`
   (`queued/stopping/installing/restarting/done/failed`) e chama
   `systemctl --user stop podstack.service podstack.socket`; a GUI já saiu; o daemon para.
4. O runner executa `pkexec /usr/libexec/podstack/podstack-update-root --package <path>
   --sha256 <sha> --version <v>`. O script root:
   - recusa caminho não absoluto, symlink, arquivo não regular ou de outro dono;
   - confere o SHA-256 e a versão (`dpkg-query`/`rpm -q`), recusa downgrade;
   - chama `apt-get install -y <path>` (Debian/Ubuntu) ou `dnf install -y <path>` (Fedora);
   - registra stdout/stderr em `/var/log/podstack/update-YYYYMMDD.log` (root 0600, 7 dias);
   - retorna um código estável (0 instalado; 2 validação; 3 gerenciador; 4 versão).
5. O `preinst`/`%pre` do pacote remove qualquer GUI/daemon remanescente e para o helper; o
   `postinst`/`%post` reinicia o helper e o socket. O runner espera o `pkexec` terminar e confere a
   versão instalada.
6. Sucesso: `systemctl --user start podstack.socket` (o daemon sobe sob demanda) e
   `podstack-gui` em background; `state.json=done`; log do update; notificação. O app reabre na
   versão nova.
7. Falha: o runner não altera o pacote (ou o gerenciador já o fez); religa
   `podstack.socket`/`podstack-gui` da versão instalada, grava o erro em `state.json` e no log; o
   usuário vê “Atualização falhou” com “Ver log”. Não há rollback automático (D7).

**CLI:** `podstack update --install [--yes] [--json]` faz o download/verificação e executa o runner
no terminal; sem `--yes`, pede confirmação; o `pkexec` pede a senha; exit 0/2/3/4. A GUI usa o
mesmo caminho com `--runner`.

**Primeiro runner:** a 0.1.3 não tem o comando; o usuário instala a 0.1.4 manualmente (o `preinst`
resolve o incidente). A partir da 0.1.4, o runner cuida das próximas versões.

### 3.3 Processos e ordem

| Processo | Dono | Unit/escopo | Antes do unpack | Depois |
|---|---|---|---|---|
| `podstack-gui` | usuário | sessão gráfica | `pkill -TERM` (SIGKILL após 3 s); a GUI do runner sai antes | runner/manual: `podstack-gui` |
| `podstackd` | usuário | `podstack.service` | `systemctl --user stop`; `pkill -TERM` como fallback no `preinst` | ativação por `podstack.socket` |
| `podstack.socket` | usuário | socket | `systemctl --user stop` no runner | `systemctl --user start` |
| `podstack-helper` | root | `podstack-helper.service` | `systemctl stop` no `preinst` | `systemctl restart` no `postinst` |
| runner `podstack update` | usuário | processo `podstack` | **não matar** | termina após religar |
| `podstack-update-root` | root | `pkexec` | — | — |
| Podman/containers | usuário | `podman.socket` | **não parar** | intactos |

Ordem do pacote: `prerm` antigo (remove) → `preinst` novo (kill/stop) → unpack → `postinst` novo
(socket/helper) → triggers. No RPM: `%pre` novo → unpack → `%post`/`%posttrans`.

### 3.4 Verificação e segurança

- Manifesto assinado (Ed25519/minisign, `updates.publicKey`); SHA-256 do artefato; canal; versão
  maior que a instalada; nada é instalado sem consentimento.
- O runner baixa e verifica antes do `pkexec`; o script root confere novamente caminho/SHA/versão.
- Sem segredos no argv/log; o `pkexec` autentica; o script root não aceita caminho fora do cache de
  updates nem symlink; o log é 0600.
- O update não toca em Podman/containers/machines; apenas o daemon/GUI/helper do app.

## 4. Logs de debug

### 4.1 Núcleo

`src/core/Logging` passa a instalar um `qInstallMessageHandler` que:

- mantém o padrão atual no stderr/journal;
- escreve JSON Lines em `~/.local/state/podstack/logs/<process>-YYYYMMDD.jsonl` (0600; dir 0700),
  com `ts`, `level`, `category`, `process`, `pid`, `thread`, `message`, `context` (arquivo/linha/
  função quando houver);
- aplica `Redaction::text` em cada linha (tokens, `Authorization`, senhas, URLs com credenciais, env);
- usa mutex + append; a rotação/fechamento é atômica; o handler nunca bloqueia a UI (NFR-LOG-001);
- respeita `advanced.logLevel` (default info; debug/trace opt-in) e `PODSTACK_LOG_LEVEL`; a GUI/CLI/
  daemon/QML/Qt passam pelo mesmo sink; `--verbose` continua como atalho.

### 4.2 Rotação e retenção

- `LogRotation` (novo em `src/core`): no start e uma vez por dia, fecha o arquivo do dia, renomeia
  para `<process>-YYYYMMDD.jsonl`, remove arquivos com mais de **7 dias** (configurável 1–30) e
  aplica o limite total (ex. 100 MB) e por arquivo (ex. 25 MB), removendo o mais antigo primeiro.
- Chaves novas em `engineering/settings.md`/schema: `advanced.logRetentionDays` (default **7**),
  `advanced.logMaxBytes` (default 100 MB); `advanced.logLevel` já existe. Mudança de nível em
  runtime é aplicada sem restart (ideal) ou no próximo start (fallback documentado).
- O `system.report` lê o arquivo do dia + os rotacionados necessários para as últimas 200 linhas,
  redige e grava o report 0600; o `diagnostics.includeLogs` continua.

### 4.3 Helper, pacote e update

- `podstack-helper` passa a escrever `/var/log/podstack/helper-YYYYMMDD.log` (root 0600) além do
  journal, com a saída do `apt`/`dnf` no `InstallPackages`; 7 dias/limite; redaction.
- `podstack-update-root` e os scripts de pacote escrevem `/var/log/podstack/update-YYYYMMDD.log` e
  `/var/log/podstack/package-YYYYMMDD.log` (root 0600); o runner grava o seu em
  `~/.local/state/podstack/logs/update-YYYYMMDD.jsonl`.
- O helper expõe um `GetLogs` allowlisted (redigido, últimas N linhas, 7 dias) para o app/report; o
  `journalctl -u podstack-helper.service` continua para auditoria.
- O job de setup (`SetupManager::appendLog`) também emite uma linha `setup` com `jobId`, ação,
  progresso e erro no log do daemon; o painel da GUI continua; o report inclui as linhas.
- O update runner/`state.json` registra a fase e o erro no log; a GUI/CLI mostra “Ver log”.

### 4.4 CLI e GUI

- `podstack logs [--follow] [--since 7d] [--level] [--component] [--json] [--export PATH]
  [--helper]`: lê os arquivos do usuário, filtra e exporta; `--helper` pede ao helper `GetLogs`.
- GUI: página/diálogo **Logs** (filtro por nível/componente, follow, abrir pasta, exportar,
  “Exportar diagnóstico”); o erro de setup/update ganha **Ver log** que abre o painel já filtrado.
- O `report --include-logs` passa a incluir daemon/GUI/CLI/setup/update/helper/pacote, com redaction
  e sem upload.

## 5. Arquivos afetados

**Novos:** `docs/specs/002-update-seguro-logs-debug/*` (este delta); `src/core/LogRotation.*`;
`packaging/deb/preinst`; `packaging/polkit/dev.podstack.update.policy`;
`packaging/podstack-update-root` (instalado em `/usr/libexec/podstack/`); testes de rotação/runner.

**Alterados:** `src/core/Logging.{h,cpp}`; `src/core/Settings`/schema (advanced.logRetentionDays/
MaxBytes); `src/daemon/main.cpp`/`Daemon.cpp` (sink, report, setup); `src/daemon/SetupManager.cpp`;
`src/daemon/UpdateChecker.*` (download/verify/estado); `src/gui/main.cpp`/`Backend`/QML
(update/logs); `src/cli/main.cpp` (`update --install`, `logs`); `src/helper/main.cpp`/
`HelperService` (`GetLogs`, log file); `src/machines`? não (containers/machines intactos);
`packaging/deb/postinst`/`prerm`; `packaging/rpm/podstack.spec`;
`packaging/systemd/podstack.service`/`.socket`; `packaging/systemd-system/podstack-helper.service`;
`scripts/build-deb.sh`/`build-rpm.sh` (instalar os novos arquivos); `packaging/dev.podstack...`
(nova versão); `CHANGELOG`; `docs/engineering/{packaging,settings,architecture,security,cli-parity,
ui-map}.md`; `docs/user/*`; `web/{install,onboarding,troubleshooting}.html` e `_data/site.json`;
`docs/adr/0011-update-logging.md` (proposto).

## 6. Alternativas

| Alternativa | Prós | Contras | Decisão |
|---|---|---|---|
| Só scripts do pacote (`preinst`/`postinst`) | resolve o `apt` manual e o incidente; simples | não há update assistido; o usuário reabre | **Fazer** (T-UPD-01/02) |
| Runner + `pkexec`/script root | update assistido; para/religa o app; sem cgroup do helper | bootstrap da 0.1.3; polkit; mais peças | **Fazer** (T-UPD-03/04) |
| Helper instala o próprio pacote | reusa a allowlist | o `postinst` reinicia o helper e pode matar o `apt` filho; o helper antigo não tem `InstallUpdate` | **Rejeitar** |
| `podstack-update.service` de usuário | systemd supervisiona | sessões remotas sem systemd; mais complexo; a GUI ainda precisa sair | **Rejeitar no v1** |
| Logs só no journal | zero código | não há arquivo de 7 dias do app, setup/helper/update; o report fica incompleto | **Rejeitar** |
| Logs JSON Lines + retenção no app | consulta/export/report; 7 dias; redaction | mais código; limite de disco | **Fazer** (T-LOG-01/02/03) |

## 7. Riscos e mitigação

| Risco | Mitigação |
|---|---|
| `preinst` mata a GUI/daemon no meio de uma operação | `SIGTERM` + 3 s; `UiState` salvo; containers/machines não param; o app só dispara o runner quando ocioso |
| `postinst` reiniciar o helper e derrubar o `apt` pai | install via `pkexec`/script root, nunca pelo helper; o `preinst` para o helper antes |
| Primeiro update (0.1.3) não tem runner | caminho manual documentado; o pacote novo tem `preinst`/`postinst`; o 0.1.4 passa a ter o runner |
| Polkit/`pkexec` indisponível em SSH/Tuxee | o CLI mostra o erro e o log; o caminho manual com `sudo apt` continua coberto pelos scripts; docs |
| Logs com segredos ou disco cheio | `Redaction` em todos os sinks; 0600/0700; 7 dias; limites por arquivo/total; teste |
| Retenção apagar log útil | 7 dias por padrão; configurável até 30; export/report antes |
| Regressão Fedora/Ubuntu/RPM/DEB | testes unitários; e2e nas VMs 24.04/26.04/Fedora; `packaging.yml`/clean-install |
| Update falhar e deixar app parado | runner religa a GUI/daemon; erro + log; estado recuperável no próximo start |

## 8. Testes

**Unitários/integração:** rotação/retenção/redaction; parse do manifesto/SHA/versão/lock/estado;
`podstack logs`; `GetLogs`; scripts `preinst`/`postinst` com processos falsos
(`podstack-gui`/`podstackd`/helper) em container; `clean-install-check` com upgrade real.

**E2E (VMs 24.04/26.04/Fedora):**

1. 0.1.3 com GUI/daemon/helper abertos → `apt install ./podstack_0.1.4.deb`: PIDs antigos ausentes;
   helper novo; `podstack setup --validate` OVMF `ok`; containers intactos.
2. 0.1.4 → GUI “Atualizar” 0.1.5: runner, polkit, stop/install/restart; GUI/daemon 0.1.5; log do
   update.
3. Falha simulada do gerenciador: GUI/daemon antigos religados; log com o erro; nenhum órfão.
4. Logs: 7 arquivos diários, 8º removido, 0600/0700, debug, setup/helper/update, redaction,
   `podstack logs`/GUI/report.
5. SSH/Tuxee: documentar o caminho manual (`podstackd` no mesmo XDG) e o erro claro do runner.

**UI:** `test_gui`/`test_terminal` para a página de logs e o diálogo de update; `qmllint` dos QML
alterados; `clang-format`/`clang-tidy`; cobertura ≥ 80%.

## 9. Ordem de implementação e docs

1. Aprovar a spec e as decisões abertas (§9).
2. **T-UPD-01/02:** `preinst`/`postinst`/`prerm` Debian e `%pre`/`%post`/`%preun` RPM; helper
   restart; e2e do `apt` manual (o incidente 0.1.3). É o ganho imediato.
3. **T-LOG-01:** núcleo de logs/retenção/redaction/settings; sink no daemon/GUI/CLI.
4. **T-UPD-03/04:** runner/CLI/GUI, download/verificação, pkexec/root, estado/lock, primeiro
   bootstrap, falha.
5. **T-LOG-02/03:** setup/helper/update/package logs, `GetLogs`, report, `podstack logs`, GUI.
6. **T-E2E-01/T-DOC-01:** VMs, docs, web, ADR, CHANGELOG.

Docs: `engineering/packaging.md` (scripts/ordem/cgroup), `settings.md` (logLevel/retenção),
`architecture.md` (update/log), `security.md` (pkexec/logs/redaction), `cli-parity.md`
(`update --install`, `logs`), `ui-map.md` (update/logs), `sdd/*` após aprovação, `docs/user/*`,
`web/*`, `CHANGELOG`. ADR-0011 proposto para a decisão de update (pkexec, stop/restart, logs).
