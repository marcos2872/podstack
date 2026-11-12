# Site público (GitHub Pages) — PodStack

> **URL:** https://marcos2872.github.io/podstack/
> **Fonte:** [`../web/`](../web/) — 16 páginas HTML + CSS/JS/SVG + `_data/`, geradas por
> [`../scripts/web_build.py`](../scripts/web_build.py) e commitadas. O Pages publica o HTML estático.
> **Workflow:** [`.github/workflows/pages.yml`](../.github/workflows/pages.yml)

O site é a vitrine pública do PodStack: páginas estáticas por área do produto (containers,
images, volumes, networks, pods, compose, builds, machines, kubernetes, domains, settings,
CLI, troubleshooting, instalação e onboarding). O HTML/CSS/JS é estático e sem dependências
externas (sem CDN, sem fontes remotas, sem backend). O único gerador é local/CI: ele injeta
topbar, sidebar, TOC, pager e o índice de busca, e o resultado é commitado em `web/`.

---

## 1. Estrutura de `web/`

```
web/
├── _data/
│   ├── nav.json          # árvore: grupos, páginas, ícones, ordem e estados
│   └── site.json         # versão, URL do GitHub e licença
├── index.html            # visão geral (gerado)
├── install.html          # instalação (gerado)
├── onboarding.html       # 7 telas do setup (gerado)
├── containers.html       # + images, volumes, networks, pods (gerados)
├── compose.html          # + builds (gerados)
├── machines.html         # + kubernetes, domains (gerados)
├── settings.html         # + cli, troubleshooting (gerados)
└── assets/
    ├── style.css         # tokens de tema (claro/escuro) e componentes
    ├── docs-next.css     # shell de 3 colunas, sidebar, TOC e onboarding
    ├── app.js            # tema, busca, copiar código
    ├── docs-next.js      # drawer, accordion e scrollspy
    ├── search-index.js   # gerado a partir de nav.json + headings
    ├── img/setup/        # 7 capturas WebP do SetupWindow
    └── podstack-capsule.svg

scripts/web_build.py      # gera o chrome, o pager e o índice de busca
```

Regras que o site já cumpre e que **não devem ser quebradas**:

| Regra | Por quê |
|---|---|
| Todos os links e assets são **relativos** (`cli.html`, `assets/style.css`) | O site é servido em subpath (`/podstack/`), não na raiz do domínio. Caminhos absolutos (`/assets/...`) quebrariam. |
| Sem `<base>` | `app.js` resolve a página atual por `location.pathname`; a lógica já trata o caso vazio (`/podstack/` → `index.html`). |
| HTML gerado e commitado | O Pages publica `web/` estático; `scripts/web_build.py` roda local/CI e `--check` impede divergência. Sem `node_modules`, sem CDN, sem fontes remotas. |
| `nav.json` é a fonte da árvore | Sidebar, pager e índice de busca saem do mesmo arquivo; não edite a navegação à mão nas 16 páginas. |
| Sem backend | Não há `<form>`, `fetch` ou XHR — nada de endpoint para hospedar. |

---

## 2. Publicação

O deploy usa **GitHub Actions** (`actions/configure-pages` + `upload-pages-artifact` +
`deploy-pages`), com o artefato apontando para a pasta `web`.

| Item | Valor |
|---|---|
| Workflow | `.github/workflows/pages.yml` |
| Artefato | `web/` empacotado como `github-pages` |
| Environment | `github-pages` (criado automaticamente no primeiro deploy) |
| Permissões | `contents: read`, `pages: write`, `id-token: write` |
| Concorrência | `group: pages`, `cancel-in-progress: false` (um deploy por vez, sem abortar o atual) |
| Runner | `ubuntu-latest` |
| Duração | ~20–40 s (checkout + tar + deploy) |

### 2.1 Por que não "Deploy from a branch"

A opção *Settings → Pages → Source: Deploy from a branch* só aceita a **raiz** do repositório
ou a pasta **`/docs`**. Este projeto usa `/docs` para a especificação SDD em Markdown, então
publicar por branch serviria arquivos errados. **A fonte precisa ser "GitHub Actions"**, que
aceita qualquer diretório.

### 2.2 Habilitação (uma vez por repositório)

Settings → Pages → **Build and deployment → Source: GitHub Actions**.

Equivalente por CLI:

```bash
gh api -X POST repos/marcos2872/podstack/pages -f build_type=workflow
gh api repos/marcos2872/podstack/pages --jq '{url: .html_url, build_type, status}'
```

Sem esse passo, o job `deploy` falha com `Get Pages site failed` / `Not Found`.

---

## 3. Gatilhos

| Gatilho | Quando roda | Uso |
|---|---|---|
| `push` em `main` com mudança em `web/**` ou no próprio workflow | Automático | Publicação normal após merge. |
| `push` de tag `web-*` | Automático | Marco versionado do site; permite republicar/auditar um estado exato. |
| `workflow_dispatch` | Manual | Republicar sem commit nem tag nova. |

> **Filtros de path não são avaliados em pushes de tag.** Por isso uma tag `web-*` publica
> mesmo que o commit tagueado não altere `web/` — o que é o comportamento desejado para
> um marco de deploy.

### 3.1 Publicar manualmente

Interface: **Actions → Pages → Run workflow** (permite escolher branch ou tag no campo
*Use workflow from*).

CLI:

```bash
gh workflow run pages.yml --ref main
gh run watch "$(gh run list --workflow=pages.yml --limit=1 --json databaseId --jq '.[0].databaseId')"
```

Nada é commitado e nenhuma tag é criada — o site é apenas reenviado a partir do ref escolhido.

### 3.2 Versionar o site com tag

Convenção: **`web-<AAAA>.<MM>`** (ex.: `web-2026.11`); se houver mais de um marco no mês,
acrescente um sufixo (`web-2026.11.2`).

```bash
git tag web-2026.11                       # marca o commit atual
git push origin web-2026.11               # dispara o deploy
git ls-remote --tags origin 'web-*'       # conferir tags publicadas
```

A tag é um marcador imutável: **não mova nem reaproveite** (mover exigiria force-push de tag).
Para um novo estado, crie a próxima tag.

### 3.3 Rollback para uma versão anterior

1. Identifique a tag do estado bom: `git tag -l 'web-*' --sort=-creatordate`.
2. **Actions → Pages → Run workflow → Use workflow from: `web-2026.10`** (ou
   `gh workflow run pages.yml --ref web-2026.10`).
3. Confirme que a URL pública voltou ao estado esperado.

O rollback republica o conteúdo da tag **sem** mexer na branch `main`: o código continua
no estado atual e só o site é revertido. Requisito: a tag escolhida precisa conter o
próprio `.github/workflows/pages.yml` (ou seja, ser posterior à criação deste workflow).

---

## 4. Sem Jekyll, sem `.nojekyll`

O deploy por Actions **não** executa o Jekyll: o artefato é servido como está. Não há
arquivos com prefixo `_` em `web/`, portanto nada seria processado de qualquer forma.
`.nojekyll` é desnecessário — e, na prática, `upload-pages-artifact` **exclui dotfiles**
por padrão (`include-hidden-files: false`), então esse arquivo nem entraria no artefato.

---

## 5. Validação local

Servir o diretório e testar sob um subpath, que é como o Pages entrega:

```bash
python3 -m http.server -d web 8080
# http://localhost:8080/            (equivale a /podstack/)
```

Checklist antes de publicar:

- [ ] Rodar `python3 scripts/web_build.py --check` (HTML e `search-index.js` em dia).
- [ ] Abrir todas as páginas pelo menu e conferir navegação/âncoras.
- [ ] Testar a busca (`app.js`) em pelo menos duas páginas; buscar `Instalação` e `Onboarding` e conferir as subseções.
- [ ] Conferir as 7 capturas em `web/assets/img/setup/` e o link de abrir em tamanho real.
- [ ] Conferir tema claro/escuro e layout em 1280 px e 390 px de largura.
- [ ] Verificar que nenhum link relativo ficou quebrado:

```bash
for f in web/*.html; do
  grep -ohE 'href="[^"#:]+\.html' "$f" | sed 's/href="//' | sort -u |
    while read -r l; do [ -f "web/$l" ] || echo "quebrado: $f -> $l"; done
done
```

- [ ] Nenhum `console.log` de depuração em `web/assets/app.js`.

---

## 6. Limitações conhecidas

| Item | Situação |
|---|---|
| `web/404.html` | Não existe. Um caminho inválido exibe o **404 genérico do GitHub**. Se o site precisar de 404 com a identidade visual, adicione `web/404.html` (o Pages o usa automaticamente). |
| Domínio próprio | Não configurado. A URL é `marcos2872.github.io/podstack/`. Um domínio exige DNS (CNAME/A) e o arquivo `CNAME` no artefato, além de habilitar *Custom domain* nas settings. |
| Links para o GitHub | Instalação e licença apontam para `github.com/marcos2872/podstack` (`docs/user/install.md`, `LICENSE`). Dependem de o caminho existir em `main`. |
| Limites do Pages | 1 GB de site, ~100 GB/mês de banda, ~10 builds/hora. Irrelevantes para o tamanho atual (menos de 1 MB). |
| Validação no CI | O `--check` valida links/âncoras internas e o índice de busca. A validação visual, o contraste e o Lighthouse continuam manuais. |
| Capturas do onboarding | Foram feitas em ambiente isolado; estados de checks (socket/systemd) variam por host. |

---

## 7. Troubleshooting

| Sintoma | Causa provável | Correção |
|---|---|---|
| `Get Pages site failed` / `Not Found` no job | Pages não habilitado, ou fonte ainda em "Deploy from a branch" | §2.2 — Source = **GitHub Actions**. |
| Workflow não aparece na aba Actions | `pages.yml` não está na branch default (`main`) | Mergear em `main`; gatilhos por tag/path só existem depois disso. |
| Tag pushed mas nenhum deploy | Tag sem `web-` no nome, ou commit da tag anterior ao workflow | Usar `web-*` e taguear um commit que contenha `pages.yml`. |
| `Resource not accessible by integration` | `permissions` incompletas | Exigir `pages: write` e `id-token: write` (§2). |
| Site no ar mas CSS/JS não carregam | Caminho absoluto (`/assets/...`) ou `<base>` indevido | Usar caminhos relativos (§1). |
| Deploy verde, conteúdo antigo | Cache do CDN do Pages (minutos) | Aguardar e recarregar com cache limpo; conferir `page_url` no job. |

---

## 8. Arquivos relacionados

- [`../web/`](../web/) — fonte do site.
- [`../web/_data/nav.json`](../web/_data/nav.json) — árvore da navegação.
- [`../web/_data/site.json`](../web/_data/site.json) — versão, GitHub e licença.
- [`../scripts/web_build.py`](../scripts/web_build.py) — gerador do chrome, pager e busca.
- [`../.github/workflows/pages.yml`](../.github/workflows/pages.yml) — pipeline de publicação.
- [`packaging.md`](packaging.md) — empacotamento de binários (RPM/DEB/Flatpak), que é independente do site.
- [`branding.md`](branding.md) — paleta, marca e assets usados pelo site.
- [`tasks.md`](tasks.md) — backlog; publicação do site não pertence a nenhum marco M0–M8.
