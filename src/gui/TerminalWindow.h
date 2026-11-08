#pragma once

#include <QStringList>
#include <QWidget>

namespace PodStack {

class TerminalWindow : public QWidget {
    Q_OBJECT

public:
    explicit TerminalWindow(const QString &containerId, const QString &program = {},
                            const QStringList &args = {}, QWidget *parent = nullptr);
};

} // namespace PodStack
