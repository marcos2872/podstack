#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gera um SBOM CycloneDX 1.5 a partir do banco RPM do ambiente de build.
# O foco são as bibliotecas/runtime que o PodStack usa (Qt 6, libcurl, OpenSSL,
# SQLite, libarchive, qtermwidget, Podman, systemd, Caddy); se o filtro não
# encontrar nada, todos os pacotes do ambiente entram no SBOM.
#
# Uso: scripts/generate-sbom.sh
# Saída: build/sbom/podstack-<version>.cdx.json
#
# Requer rpm e python3 (não usa jq). SOURCE_DATE_EPOCH fixa o timestamp.
set -euo pipefail

cd "$(dirname "$0")/.."

version="$(sed -n 's/^project(PodStack VERSION \([^ ]*\).*/\1/p' CMakeLists.txt | head -1)"
if [ -z "$version" ]; then
    echo "Não foi possível ler a versão em CMakeLists.txt." >&2
    exit 1
fi
if ! command -v rpm >/dev/null 2>&1; then
    echo "rpm não encontrado; o SBOM é gerado a partir do banco RPM (Fedora)." >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 não encontrado; necessário para montar o JSON do SBOM." >&2
    exit 1
fi

out="build/sbom/podstack-${version}.cdx.json"
mkdir -p build/sbom

packages="$(mktemp)"
trap 'rm -f "$packages"' EXIT

# NAME<TAB>VERSION<TAB>RELEASE<TAB>ARCH<TAB>LICENSE
if ! rpm -qa --qf '%{NAME}\t%{VERSION}\t%{RELEASE}\t%{ARCH}\t%{LICENSE}\n' >"$packages"; then
    echo "Falha ao consultar os pacotes com rpm -qa." >&2
    exit 1
fi

python3 - "$version" "$packages" "$out" <<'PY'
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote

version, packages_path, output_path = sys.argv[1:4]

filters = (
    "qt6",
    "libcurl",
    "curl",
    "openssl",
    "libssl",
    "sqlite",
    "libarchive",
    "qtermwidget",
    "podman",
    "systemd",
    "caddy",
)
applications = {"podman", "conmon", "crun", "caddy", "systemd"}

rows = []
with open(packages_path, encoding="utf-8", errors="replace") as handle:
    for line in handle:
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 4:
            continue
        name, package_version, release, arch = parts[:4]
        license_name = parts[4] if len(parts) > 4 else ""
        if not name or name == "(none)":
            continue
        rows.append((name, package_version, release, arch, license_name))

if not rows:
    sys.exit("rpm -qa não retornou nenhum pacote; SBOM não gerado.")

selected = [row for row in rows if any(item in row[0].lower() for item in filters)]
if selected:
    note = f"filtro de runtime: {len(selected)} de {len(rows)} pacotes"
else:
    selected = rows
    note = f"filtro sem resultados; incluindo os {len(rows)} pacotes do ambiente"

selected.sort(key=lambda row: (row[0].lower(), row[1], row[2], row[3]))

components = []
for name, package_version, release, arch, license_name in selected:
    full_version = f"{package_version}-{release}"
    component = {
        "type": "application" if name in applications else "library",
        "name": name,
        "version": full_version,
        "purl": f"pkg:rpm/fedora/{quote(name)}@{quote(full_version)}?arch={quote(arch)}",
        "properties": [{"name": "podstack:arch", "value": arch}],
    }
    if license_name and license_name != "(none)":
        component["licenses"] = [{"license": {"name": license_name}}]
    components.append(component)

epoch = os.environ.get("SOURCE_DATE_EPOCH")
if epoch:
    timestamp = datetime.fromtimestamp(int(epoch), tz=timezone.utc)
else:
    timestamp = datetime.now(timezone.utc)

bom = {
    "bomFormat": "CycloneDX",
    "specVersion": "1.5",
    "version": 1,
    "metadata": {
        "timestamp": timestamp.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "tools": [
            {
                "vendor": "PodStack",
                "name": "generate-sbom.sh",
                "version": version,
            }
        ],
        "component": {
            "type": "application",
            "name": "podstack",
            "version": version,
            "purl": f"pkg:generic/podstack@{quote(version)}",
        },
    },
    "components": components,
}

Path(output_path).write_text(
    json.dumps(bom, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
)
print(f"SBOM: {output_path} ({note})")
PY
