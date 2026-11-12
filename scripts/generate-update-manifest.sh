#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gera o manifesto podstack-update.json (schemaVersion 1) a partir dos pacotes
# empacotados para uma release.
#
# Uso:
#   scripts/generate-update-manifest.sh <version> <tag> <notes-file> \
#       <artifact-dir> <output.json> [base-url]
#
# <base-url> é a base das releases (default:
# https://github.com/marcos2872/podstack/releases). O releaseUrl aponta para
# <base-url>/tag/<tag> e cada artefato para
# <base-url>/download/<tag>/<arquivo>.
#
# Variáveis:
#   CHANNEL=stable|beta  além de stable, publica o mesmo release em beta
#                        quando CHANNEL=beta (default: stable)
#   SOURCE_DATE_EPOCH    fixa publishedAt para builds reprodutíveis
#
# O JSON é determinístico: pacotes ordenados por nome e chaves em ordem fixa.
set -euo pipefail

if [ "$#" -lt 5 ] || [ "$#" -gt 6 ]; then
    echo "Uso: $0 <version> <tag> <notes-file> <artifact-dir> <output.json> [base-url]" >&2
    exit 2
fi

version="$1"
tag="$2"
notes_file="$3"
artifact_dir="$4"
output="$5"
base_url="${6:-https://github.com/marcos2872/podstack/releases}"

if [ ! -f "$notes_file" ]; then
    echo "Arquivo de notas não encontrado: $notes_file" >&2
    exit 1
fi
if [ ! -d "$artifact_dir" ]; then
    echo "Diretório de artefatos não encontrado: $artifact_dir" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 não encontrado; necessário para montar o manifesto." >&2
    exit 1
fi

python3 - "$version" "$tag" "$notes_file" "$artifact_dir" "$output" "$base_url" <<'PY'
import hashlib
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote

version, tag, notes_file, artifact_dir, output, base_url = sys.argv[1:7]
version = version[1:] if version.startswith(("v", "V")) else version
base_url = base_url.rstrip("/")

channel = os.environ.get("CHANNEL", "stable").strip().lower() or "stable"
if channel not in ("stable", "beta"):
    sys.exit(f"CHANNEL inválido: {channel!r} (use stable ou beta).")

notes = Path(notes_file).read_text(encoding="utf-8")

directory = Path(artifact_dir)
files = sorted(
    path
    for path in directory.iterdir()
    if path.is_file() and path.suffix.lower() in (".rpm", ".deb")
)
if not files:
    sys.exit(f"Nenhum artefato .rpm/.deb encontrado em {artifact_dir}.")

arch_map = {
    "amd64": "x86_64",
    "arm64": "aarch64",
    "i386": "i686",
    "all": "noarch",
}


def artifact_arch(fmt, filename):
    stem = filename[: -len(fmt) - 1]
    if fmt == "rpm":
        return arch_map.get(stem.rsplit(".", 1)[-1], stem.rsplit(".", 1)[-1])
    parts = stem.split("_")
    if len(parts) < 3:
        return "unknown"
    return arch_map.get(parts[-1], parts[-1])


artifacts = []
for path in files:
    fmt = path.suffix.lower().lstrip(".")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    artifacts.append(
        {
            "arch": artifact_arch(fmt, path.name),
            "format": fmt,
            "url": f"{base_url}/download/{quote(tag)}/{quote(path.name)}",
            "sha256": digest,
        }
    )

epoch = os.environ.get("SOURCE_DATE_EPOCH")
if epoch:
    published_at = datetime.fromtimestamp(int(epoch), tz=timezone.utc)
else:
    published_at = datetime.now(timezone.utc)

release = {
    "version": version,
    "publishedAt": published_at.strftime("%Y-%m-%dT%H:%M:%SZ"),
    "releaseUrl": f"{base_url}/tag/{quote(tag)}",
    "notes": notes,
    "artifacts": artifacts,
}
channels = {"stable": release}
if channel == "beta":
    channels["beta"] = release

manifest = {
    "schemaVersion": 1,
    "channels": channels,
}

output_path = Path(output)
if output_path.parent:
    output_path.parent.mkdir(parents=True, exist_ok=True)
output_path.write_text(
    json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
)

channel_names = ", ".join(channels)
print(f"Manifesto: {output} ({len(artifacts)} artefato(s), canal(is): {channel_names})")
PY
