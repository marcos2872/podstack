// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest/quicktest.h>

// Módulos QML estáticos: no Qt 6.4 o registro não é automático.
Q_IMPORT_PLUGIN(PodStackPlugin)
Q_IMPORT_PLUGIN(PodStack_DesignPlugin)
Q_IMPORT_PLUGIN(PodStack_PagesPlugin)
Q_IMPORT_PLUGIN(PodStack_FeaturesPlugin)

QUICK_TEST_MAIN(PodStackQml)
