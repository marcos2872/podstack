#!/usr/bin/env python3
"""Gera o chrome do site PodStack Docs (piloto F0/F1).

Lê `web/_data/site.json` e `web/_data/nav.json`, extrai os títulos das páginas
publicadas e injeta topbar, sidebar, breadcrumb e TOC nas páginas indicadas.

Uso:
    python3 scripts/web_build.py --pages index.html containers.html machines.html
    python3 scripts/web_build.py --check --pages index.html containers.html machines.html

O HTML gerado é estático e continua compatível com GitHub Pages.
"""

from __future__ import annotations

import argparse
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
DATA = WEB / "_data"

HEADING_RE = re.compile(r"<(h2|h3)\b([^>]*)>(.*?)</\1>", re.S | re.I)
TAG_RE = re.compile(r"<[^>]+>")
WS_RE = re.compile(r"\s+")


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def clean_text(value: str) -> str:
    return html.unescape(WS_RE.sub(" ", TAG_RE.sub("", value))).strip()


def extract_headings(source: str) -> list[dict]:
    headings: list[dict] = []
    for match in HEADING_RE.finditer(source):
        attrs = match.group(2)
        id_match = re.search(r'\bid="([^"]+)"', attrs)
        if not id_match:
            continue
        headings.append(
            {
                "tag": match.group(1).lower(),
                "id": id_match.group(1),
                "text": clean_text(match.group(3)),
            }
        )
    return headings


def published_pages(nav: dict) -> list[dict]:
    pages: list[dict] = []
    for group in nav["groups"]:
        for page in group["pages"]:
            if page.get("status", "published") == "published":
                pages.append({**page, "group": group["title"]})
    return pages


def find_page(nav: dict, filename: str) -> dict | None:
    for group in nav["groups"]:
        for page in group["pages"]:
            if page["file"] == filename:
                return {**page, "group": group["title"]}
    return None


def render_sidebar(nav: dict, active_file: str, headings_by_page: dict[str, list[dict]], site: dict) -> str:
    excluded = set(nav.get("sidebarExclude", []))
    output = [
        '<aside class="dx-sidebar" id="dx-sidebar" aria-label="Navegação da documentação">',
        '  <nav class="dx-nav" aria-label="Seções da documentação">',
    ]

    for group in nav["groups"]:
        pages = [p for p in group["pages"] if p.get("status", "published") == "published"]
        if not pages:
            continue
        output.append('    <div class="dx-nav-group">')
        output.append(f'      <p class="dx-nav-label">{html.escape(group["title"])}</p>')
        output.append('      <ul class="dx-nav-list">')

        for page in pages:
            filename = page["file"]
            title = page["title"]
            icon = page.get("icon", "i-box")
            is_active = filename == active_file
            sections = [
                heading
                for heading in headings_by_page.get(filename, [])
                if heading["tag"] == "h2" and heading["id"] not in excluded
            ]
            child_class = " dx-nav-page--child" if page.get("parent") else ""
            page_classes = "dx-nav-page" + child_class + (" is-active" if is_active else "")
            output.append(f'        <li class="{page_classes}">')
            output.append('          <div class="dx-nav-row">')
            current = ' aria-current="page"' if is_active else ""
            output.append(
                f'            <a class="dx-nav-link" href="{filename}"{current}>'
                f'<svg class="icon" aria-hidden="true"><use href="#{icon}"></use></svg>'
                f'<span>{html.escape(title)}</span></a>'
            )
            if sections:
                sub_id = "dx-sub-" + filename.removesuffix(".html")
                expanded = "true" if is_active else "false"
                action = "Recolher" if is_active else "Expandir"
                output.append(
                    f'            <button class="dx-nav-chevron" type="button" data-dx-chevron '
                    f'aria-expanded="{expanded}" aria-controls="{sub_id}" '
                    f'aria-label="{action} seção {html.escape(title)}">'
                    f'<span class="dx-chevron" aria-hidden="true"></span></button>'
                )
            output.append('          </div>')
            if sections:
                hidden = "" if is_active else " hidden"
                output.append(f'          <ul class="dx-nav-sub" id="{sub_id}"{hidden}>')
                for heading in sections:
                    output.append(
                        f'            <li><a href="{filename}#{heading["id"]}">'
                        f'{html.escape(heading["text"])}</a></li>'
                    )
                output.append('          </ul>')
            output.append('        </li>')

        output.append('      </ul>')
        output.append('    </div>')

    output.extend(
        [
            '  </nav>',
            '  <div class="dx-sidebar-foot">',
            f'    <p>Versão {html.escape(site["version"])} · {html.escape(site["license"])}</p>',
            f'    <p><a href="{html.escape(site["github"])}" target="_blank" rel="noopener noreferrer">Repositório no GitHub</a></p>',
            '  </div>',
            '</aside>',
        ]
    )
    return "\n".join(output)


def render_toc(headings: list[dict]) -> str:
    items = []
    for heading in headings:
        extra = ' class="dx-toc-sub"' if heading["tag"] == "h3" else ""
        items.append(f'    <li{extra}><a href="#{heading["id"]}">{html.escape(heading["text"])}</a></li>')
    if not items:
        return ""
    return "\n".join(
        [
            '<aside class="dx-toc" aria-label="Nesta página">',
            '  <p class="dx-toc-title">Nesta página</p>',
            '  <ul>',
            *items,
            '  </ul>',
            '</aside>',
        ]
    )


def page_description(source: str) -> str:
    match = re.search(r'<meta name="description" content="([^"]*)"', source)
    return html.unescape(match.group(1)) if match else ""


def build_search_index(nav: dict, headings_by_page: dict[str, list[dict]], descriptions: dict[str, str]) -> str:
    entries = []
    for page in published_pages(nav):
        filename = page["file"]
        headings = [
            {
                "title": heading["text"],
                "url": f"{filename}#{heading['id']}",
                "level": int(heading["tag"][1]),
            }
            for heading in headings_by_page.get(filename, [])
        ]
        description = descriptions.get(filename, "")
        keys = " ".join(
            [page["title"], description] + [heading["text"] for heading in headings_by_page.get(filename, [])]
        )
        entries.append(
            {
                "title": page["title"],
                "url": filename,
                "description": description,
                "keys": keys,
                "headings": headings,
            }
        )
    return "window.PODSTACK_SEARCH = " + json.dumps(entries, ensure_ascii=False, indent=2) + ";\n"


def render_topbar(site: dict, nav: dict) -> str:
    install = find_page(nav, "install.html")
    cta = ""
    if install and install.get("status", "published") == "published":
        cta = '\n      <a class="dx-cta" href="install.html">Instalar</a>'

    return f"""<header class="dx-topbar">
    <div class="dx-topbar-left">
      <button class="dx-icon-btn dx-nav-toggle" type="button" data-dx-nav-toggle aria-controls="dx-sidebar" aria-expanded="false" aria-label="Abrir navegação">
        <svg class="icon" aria-hidden="true"><use href="#i-menu"></use></svg>
      </button>
      <a class="dx-brand" href="index.html">
        <img src="assets/podstack-capsule.svg" alt="" width="26" height="26">
        <span class="dx-brand-name">PodStack</span>
        <span class="dx-brand-tag">Docs</span>
      </a>
      <span class="dx-version">{html.escape(site["version"])}</span>
    </div>

    <div class="search dx-search" role="search">
      <svg class="icon" aria-hidden="true"><use href="#i-search"></use></svg>
      <input id="site-search" type="search" role="combobox" aria-label="Buscar na documentação"
             aria-controls="search-results" aria-expanded="false" aria-autocomplete="list"
             placeholder="Buscar na documentação…" autocomplete="off" spellcheck="false">
      <kbd aria-hidden="true">/</kbd>
      <div class="search-results" id="search-results" role="listbox" aria-label="Resultados da busca" hidden></div>
    </div>

    <div class="dx-topbar-actions">{cta}
      <a class="dx-icon-btn" href="{html.escape(site["github"])}" target="_blank" rel="noopener noreferrer" aria-label="Repositório do PodStack no GitHub" title="Repositório no GitHub">
        <svg class="icon" aria-hidden="true"><use href="#i-github"></use></svg>
      </a>
      <button class="dx-icon-btn" id="theme-toggle" type="button" aria-label="Alternar tema">
        <svg class="icon" aria-hidden="true"><use href="#i-moon"></use></svg>
      </button>
    </div>
  </header>"""


def render_breadcrumb(nav: dict, active_file: str) -> str:
    page = find_page(nav, active_file)
    if not page:
        return ""
    parts = ['<a href="index.html">Documentação</a>']
    if active_file != "index.html":
        parts.append('<span class="sep" aria-hidden="true">›</span>')
        parts.append(f'<span>{html.escape(page["group"])}</span>')
    parts.append('<span class="sep" aria-hidden="true">›</span>')
    parts.append(f'<span aria-current="page">{html.escape(page["title"])}</span>')
    return '<nav class="breadcrumb" aria-label="Trilha de navegação">' + "".join(parts) + "</nav>"


def render_pager(nav: dict, active_file: str) -> str:
    pages = published_pages(nav)
    index = next((i for i, page in enumerate(pages) if page["file"] == active_file), -1)
    if index < 0:
        return ""

    parts = ['<nav class="pager" aria-label="Navegação entre páginas">']
    if index > 0:
        previous = pages[index - 1]
        parts.append(
            f'<a class="prev" href="{previous["file"]}">'
            '<span class="pager-label"><svg class="icon" aria-hidden="true"><use href="#i-arrow-left"></use></svg> Anterior</span>'
            f'<span class="pager-title">{html.escape(previous["title"])}</span></a>'
        )
    if index + 1 < len(pages):
        following = pages[index + 1]
        parts.append(
            f'<a class="next" href="{following["file"]}">'
            '<span class="pager-label">Próxima <svg class="icon" aria-hidden="true"><use href="#i-arrow-right"></use></svg></span>'
            f'<span class="pager-title">{html.escape(following["title"])}</span></a>'
        )
    parts.append("</nav>")
    return "".join(parts)


def transform(
    source: str,
    active_file: str,
    nav: dict,
    site: dict,
    headings_by_page: dict[str, list[dict]],
) -> str:
    source = re.sub(
        r"<title>(.*?) · PodStack [^<]+</title>",
        lambda match: f'<title>{match.group(1)} · PodStack {html.escape(site["version"])}</title>',
        source,
        count=1,
    )
    source = re.sub(
        r'<span class="badge badge-accent">PodStack [^<]+</span>',
        lambda _match: f'<span class="badge badge-accent">PodStack {html.escape(site["version"])}</span>',
        source,
        count=1,
    )
    source = re.sub(
        r"<p>PodStack [^<]+ · Licença GPL-3.0-or-later",
        lambda _match: f'<p>PodStack {html.escape(site["version"])} · Licença GPL-3.0-or-later',
        source,
        count=1,
    )

    if "<body>" in source:
        source = source.replace("<body>", '<body class="docs-next">', 1)

    if "assets/docs-next.css" not in source:
        source = source.replace(
            '<link rel="stylesheet" href="assets/style.css">',
            '<link rel="stylesheet" href="assets/style.css">\n  <link rel="stylesheet" href="assets/docs-next.css">',
            1,
        )

    if "assets/docs-next.js" not in source:
        source = source.replace(
            '<script src="assets/app.js" defer></script>',
            '<script src="assets/app.js" defer></script>\n  <script src="assets/docs-next.js" defer></script>',
            1,
        )

    if "assets/search-index.js" not in source:
        source = source.replace(
            '<script src="assets/app.js" defer></script>',
            '<script src="assets/search-index.js" defer></script>\n  <script src="assets/app.js" defer></script>',
            1,
        )

    topbar = render_topbar(site, nav)
    if "<!-- dx:topbar:start -->" in source:
        source = re.sub(
            r"<!-- dx:topbar:start -->.*?<!-- dx:topbar:end -->",
            lambda _match: f"<!-- dx:topbar:start -->\n{topbar}\n<!-- dx:topbar:end -->",
            source,
            count=1,
            flags=re.S,
        )
    else:
        source = re.sub(
            r'<header class="topbar">.*?</header>',
            lambda _match: f"<!-- dx:topbar:start -->\n{topbar}\n<!-- dx:topbar:end -->",
            source,
            count=1,
            flags=re.S,
        )

    source = source.replace(
        '<div class="nav-overlay" data-nav-overlay aria-hidden="true"></div>',
        '<div class="dx-overlay" data-dx-overlay aria-hidden="true"></div>',
    )

    breadcrumb = render_breadcrumb(nav, active_file)
    source = re.sub(
        r'<nav class="breadcrumb" aria-label="Trilha de navegação">.*?</nav>',
        lambda _match: breadcrumb,
        source,
        count=1,
        flags=re.S,
    )

    source = re.sub(r"\s*<nav class=\"page-toc\" data-toc[^>]*></nav>", "", source, count=1)

    pager = render_pager(nav, active_file)
    if pager:
        if re.search(r'<nav class="pager" aria-label="Navegação entre páginas">', source):
            source = re.sub(
                r'<nav class="pager" aria-label="Navegação entre páginas">.*?</nav>',
                lambda _match: pager,
                source,
                count=1,
                flags=re.S,
            )
        else:
            source = source.replace('<footer class="site-footer">', f"{pager}\n\n      <footer class=\"site-footer\">", 1)

    sidebar = render_sidebar(nav, active_file, headings_by_page, site)
    toc = render_toc(headings_by_page.get(active_file, []))

    if '<div class="dx-shell">' not in source:
        source = re.sub(
            r'<aside class="sidebar" id="sidebar".*?</aside>',
            lambda _match: (
                '<div class="dx-shell">\n'
                "<!-- dx:sidebar:start -->\n"
                f"{sidebar}\n"
                "<!-- dx:sidebar:end -->"
            ),
            source,
            count=1,
            flags=re.S,
        )
        source = source.replace('<div class="main">', '<div class="main dx-main">', 1)
        source = source.replace('<main class="page" id="conteudo">', '<main class="page dx-page" id="conteudo">', 1)
        source = re.sub(
            r"(\n[ \t]*)</div>(\s*)</body>",
            lambda match: (
                f"{match.group(1)}</div>\n"
                "  <!-- dx:toc:start -->\n"
                f"{toc}\n"
                "  <!-- dx:toc:end -->\n"
                "</div>"
                f"{match.group(2)}</body>"
            ),
            source,
            count=1,
            flags=re.S,
        )
    else:
        source = re.sub(
            r"<!-- dx:sidebar:start -->.*?<!-- dx:sidebar:end -->",
            lambda _match: f"<!-- dx:sidebar:start -->\n{sidebar}\n<!-- dx:sidebar:end -->",
            source,
            count=1,
            flags=re.S,
        )
        source = re.sub(
            r"<!-- dx:toc:start -->.*?<!-- dx:toc:end -->",
            lambda _match: f"<!-- dx:toc:start -->\n{toc}\n  <!-- dx:toc:end -->",
            source,
            count=1,
            flags=re.S,
        )

    return source


def validate_site() -> list[str]:
    pages = sorted(WEB.glob("*.html"))
    ids_by_page: dict[str, set[str]] = {}
    problems: list[str] = []

    for path in pages:
        source = path.read_text(encoding="utf-8")
        ids = re.findall(r'\bid="([^"]+)"', source)
        ids_by_page[path.name] = set(ids)
        duplicates = sorted({value for value in ids if ids.count(value) > 1})
        for duplicate in duplicates:
            problems.append(f"{path.name}: id duplicado #{duplicate}")

    for path in pages:
        source = path.read_text(encoding="utf-8")
        for href in re.findall(r'href="([^"]+)"', source):
            if not href or href.startswith(("http://", "https://", "mailto:", "tel:", "data:")):
                continue
            if href.startswith("#"):
                target, anchor = path.name, href[1:]
            elif ".html" in href:
                target, _, anchor = href.partition("#")
            elif href.endswith((".css", ".js", ".svg", ".webp", ".png", ".ico", ".json")):
                if not (WEB / href).exists():
                    problems.append(f"{path.name}: asset ausente {href}")
                continue
            else:
                continue
            if not (WEB / target).exists():
                problems.append(f"{path.name}: arquivo ausente {target}")
            elif anchor and anchor not in ids_by_page.get(target, set()):
                problems.append(f"{path.name}: âncora ausente {target}#{anchor}")

        for src in re.findall(r'src="([^"]+)"', source):
            if src.startswith(("http://", "https://", "data:")):
                continue
            if not (WEB / src).exists():
                problems.append(f"{path.name}: src ausente {src}")

    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description="Gera o chrome do site PodStack Docs.")
    parser.add_argument("--pages", nargs="*", help="Páginas a processar (padrão: todas as publicadas).")
    parser.add_argument("--check", action="store_true", help="Não escreve; falha se o HTML estiver desatualizado.")
    args = parser.parse_args()

    site = load_json(DATA / "site.json")
    nav = load_json(DATA / "nav.json")

    pages = published_pages(nav)
    headings_by_page: dict[str, list[dict]] = {}
    descriptions: dict[str, str] = {}
    for page in pages:
        path = WEB / page["file"]
        if path.exists():
            source = path.read_text(encoding="utf-8")
            headings_by_page[page["file"]] = extract_headings(source)
            descriptions[page["file"]] = page_description(source)

    targets = args.pages or [page["file"] for page in pages]
    stale = False

    for filename in targets:
        path = WEB / filename
        if not path.exists():
            print(f"erro: página não encontrada: {path}", file=sys.stderr)
            return 2
        before = path.read_text(encoding="utf-8")
        after = transform(before, filename, nav, site, headings_by_page)
        if before == after:
            print(f"ok      {filename}")
            continue
        stale = True
        if args.check:
            print(f"stale   {filename}")
        else:
            path.write_text(after, encoding="utf-8")
            print(f"updated {filename}")

    index_source = build_search_index(nav, headings_by_page, descriptions)
    index_path = WEB / "assets" / "search-index.js"
    current_index = index_path.read_text(encoding="utf-8") if index_path.exists() else ""
    if current_index != index_source:
        if args.check:
            print("stale   assets/search-index.js")
            stale = True
        else:
            index_path.write_text(index_source, encoding="utf-8")
            print("updated assets/search-index.js")

    problems = validate_site()
    if problems:
        for problem in problems:
            print(f"link    {problem}", file=sys.stderr)
        return 1

    return 1 if (args.check and stale) else 0


if __name__ == "__main__":
    raise SystemExit(main())
