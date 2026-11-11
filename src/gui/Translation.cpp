// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/Translation.h"

#include <QCoreApplication>
#include <QTranslator>

#include <memory>

// O recurso está em uma biblioteca estática; sem esta chamada o linker pode
// descartar o qrc e o QTranslator não encontra o .qm.
void initTranslationResources()
{
    Q_INIT_RESOURCE(translations);
}

namespace PodStack {
namespace {

std::unique_ptr<QTranslator> g_translator;

} // namespace

bool installTranslation(QCoreApplication *application, const QString &language)
{
    if (!application) {
        return false;
    }
    initTranslationResources();

    if (g_translator) {
        application->removeTranslator(g_translator.get());
        g_translator.reset();
    }

    if (!language.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)) {
        return true;
    }

    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(QStringLiteral(":/i18n/podstack_en_US.qm"))) {
        return false;
    }
    application->installTranslator(translator.get());
    g_translator = std::move(translator);
    return true;
}

} // namespace PodStack
