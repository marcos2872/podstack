#include "core/Config.h"
#include "core/Error.h"

#include <QJsonObject>
#include <QTest>

namespace {

class CoreTest : public QObject {
    Q_OBJECT

private slots:
    void errorSerializes();
    void configNestedSet();
    void configDefaults();
};

void CoreTest::errorSerializes()
{
    const PodStack::Error error =
        PodStack::Error::make(QStringLiteral("PODSTACK_CONFLICT"),
                              QStringLiteral("Nome em uso."), QStringLiteral("detail"),
                              QStringLiteral("Escolha outro nome."), false);
    const QJsonObject json = error.toJson();
    QCOMPARE(json.value(QStringLiteral("code")).toString(), QStringLiteral("PODSTACK_CONFLICT"));
    QCOMPARE(json.value(QStringLiteral("message")).toString(), QStringLiteral("Nome em uso."));
    QCOMPARE(json.value(QStringLiteral("retryable")).toBool(), false);
}

void CoreTest::configNestedSet()
{
    PodStack::Config config;
    config.set(QStringLiteral("app.theme"), QStringLiteral("dark"));
    config.set(QStringLiteral("network.subnet4"), QStringLiteral("10.0.0.0/8"));

    QCOMPARE(config.get(QStringLiteral("app.theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(config.get(QStringLiteral("network.subnet4")).toString(),
             QStringLiteral("10.0.0.0/8"));
}

void CoreTest::configDefaults()
{
    PodStack::Config config;
    QCOMPARE(config.get(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(config.get(QStringLiteral("engine.mode")).toString(), QStringLiteral("rootless"));
    QCOMPARE(config.get(QStringLiteral("telemetry.enabled")).toBool(), false);
}

} // namespace

QTEST_GUILESS_MAIN(CoreTest)
#include "test_core.moc"
