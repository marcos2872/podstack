#!/usr/bin/env python3
"""Confere se os métodos documentados em api-rpc.md existem no daemon.

Direção que importa: doc → código. Um método documentado e inexistente quebra
quem implementa cliente contra a doc (foi o caso de `system.shutdown`,
`domains.*`, `jobs.*` e `containers.exec.*`).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "docs/engineering/api-rpc.md"
DAEMON = ROOT / "src/daemon/Daemon.cpp"

ROW = re.compile(r"^\|\s*`([a-z][a-z0-9]*(?:\.[A-Za-z0-9_]+)+)`")
IMPL = re.compile(r'method\s*==\s*QStringLiteral\("([^"]+)"\)')
# Campos citados em tabela que não são métodos RPC.
IGNORE = {"error.code"}


def documented():
    text = DOC.read_text(encoding="utf-8")
    # §9 lista justamente o que NÃO existe.
    body = text.split("## 9. Fora da API atual")[0]
    seen = []
    for line in body.splitlines():
        m = ROW.match(line)
        if m:
            if m.group(1) not in IGNORE:
                seen.append(m.group(1))
    return seen


def implemented():
    text = DAEMON.read_text(encoding="utf-8")
    return set(IMPL.findall(text))


def main() -> int:
    doc = documented()
    impl = implemented()
    missing = [m for m in doc if m not in impl]
    if missing:
        print("documentados em api-rpc.md e ausentes em Daemon::handle:")
        for method in missing:
            print(f"  - {method}")
        return 1
    print(f"api-rpc: ok ({len(doc)} métodos documentados, todos implementados)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
