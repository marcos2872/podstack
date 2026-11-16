// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>

namespace PodStack::ContainerTrust {

/// Caminhos de âncora de CA usados pelas distros base dos containers.
/// Fedora/RHEL usa `/etc/pki/ca-trust/source/anchors`; Debian/Ubuntu usa
/// `/usr/local/share/ca-certificates`. O PodStack monta a CA local nos dois.
inline QStringList caAnchorPaths()
{
    return {
        QStringLiteral("/etc/pki/ca-trust/source/anchors"),
        QStringLiteral("/usr/local/share/ca-certificates"),
    };
}

} // namespace PodStack::ContainerTrust
