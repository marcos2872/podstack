#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Roda o clang-tidy nos fontes alterados em relação a uma base. Exige um
# diretório de build com compile_commands.json (preset coverage/dev).
set -euo pipefail

base="${1:-origin/main}"
build_dir="${PODSTACK_BUILD_DIR:-build/coverage}"

if ! git rev-parse --verify --quiet "${base}^{commit}" >/dev/null; then
    base="HEAD~1"
fi
if [ ! -f "${build_dir}/compile_commands.json" ]; then
    echo "compile_commands.json ausente em ${build_dir}; configure o preset." >&2
    exit 2
fi

files=()
while IFS= read -r file; do
    [ -n "$file" ] && files+=("$file")
done < <(git diff --name-only "${base}" -- 'src/*.cpp')

if [ "${#files[@]}" -eq 0 ]; then
    exit 0
fi

clang-tidy -p "${build_dir}" "${files[@]}"
