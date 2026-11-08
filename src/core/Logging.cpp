#include "core/Logging.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcCore, "podstack.core")
Q_LOGGING_CATEGORY(lcPodman, "podstack.podman")
Q_LOGGING_CATEGORY(lcRpc, "podstack.rpc")

namespace PodStack::Logging {

void init(bool verbose)
{
    qSetMessagePattern(QStringLiteral(
        "%{time yyyy-MM-ddTHH:mm:ss.zzz} [%{category}] %{message}"));
    if (verbose) {
        QLoggingCategory::setFilterRules(QStringLiteral("podstack.*.debug=true"));
    } else {
        QLoggingCategory::setFilterRules(QStringLiteral("podstack.*.debug=false"));
    }
}

} // namespace PodStack::Logging
