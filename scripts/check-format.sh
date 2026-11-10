#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Verifica o clang-format apenas nas linhas alteradas em relação a uma base.
# Reformata o repositório inteiro de uma vez criaria um diff enorme; o gate
# protege o código novo sem obrigar a um "big bang" de formatação.
set -euo pipefail

base="${1:-origin/main}"
if ! git rev-parse --verify --quiet "${base}^{commit}" >/dev/null; then
    base="HEAD~1"
fi

diff="$(git diff -U0 --no-color "${base}" -- '*.cpp' '*.h')"
if [ -z "$diff" ]; then
    exit 0
fi

formatter="$(command -v clang-format-diff || command -v clang-format-diff-22 || true)"
if [ -z "$formatter" ]; then
    echo "clang-format-diff não encontrado; instale clang-tools-extra." >&2
    exit 2
fi

# clang-format-diff imprime o diff em stderr e retorna 1 quando há violações;
# capture os dois fluxos sem deixar o `set -e` abortar antes da mensagem.
violations="$(printf '%s\n' "$diff" | "$formatter" -p1 -style=file 2>&1)" || true
if [ -n "$violations" ]; then
    printf '%s\n' "$violations"
    echo "clang-format: formate as linhas acima com clang-format -i." >&2
    exit 1
fi
