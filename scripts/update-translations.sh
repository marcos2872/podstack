#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Atualiza o catálogo en-US da UI. Requer lupdate/lrelease (Qt Linguist tools)
# no PATH, por exemplo: sudo dnf install qt6-linguist
set -euo pipefail

cd "$(dirname "$0")/.."

lupdate_bin="${LUPDATE:-lupdate}"
lrelease_bin="${LRELEASE:-lrelease}"

"$lupdate_bin" \
    src/gui/qml/Main.qml \
    src/gui/qml/SetupWindow.qml \
    src/gui/Tray.cpp \
    src/gui/Notifications.cpp \
    src/gui/MainWindow.cpp \
    -ts translations/podstack_en_US.ts

"$lrelease_bin" translations/podstack_en_US.ts -qm translations/podstack_en_US.qm
