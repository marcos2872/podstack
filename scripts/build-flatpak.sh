#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Gera o Flatpak com flatpak-builder. Requer flatpak-builder e o runtime
# org.kde.Platform 6.10 instalados. O resultado fica em build/flatpak/repo.
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v flatpak-builder >/dev/null 2>&1; then
    echo "flatpak-builder não encontrado; instale flatpak-builder." >&2
    exit 1
fi

mkdir -p build/flatpak
flatpak-builder --force-clean --repo=build/flatpak/repo \
    build/flatpak/build packaging/flatpak/dev.podstack.PodStack.yml

echo "Flatpak em build/flatpak/repo"
