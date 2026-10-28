// Registers the jira module into a fresh registry and checks its types.
#include "items/ItemTypeRegistry.h"

#include <QIcon>
#include <QMap>
#include <QPixmap>
#include <QTest>

namespace mirante::modules::jira {
void registerModule(mirante::ModuleRegistrar &registrar);
}

class TestRegister : public QObject
{
    Q_OBJECT

private slots:
    void registersTypes()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        // Expected id and name of each type. QMap keeps the ids sorted.
        QMap<QString, QString> expected;
        expected.insert(QStringLiteral("jira.issue.summary"), QStringLiteral("Jira Issue (summary)"));
        expected.insert(QStringLiteral("jira.issue.page"), QStringLiteral("Jira Issue (page)"));
        expected.insert(QStringLiteral("jira.list"), QStringLiteral("Jira List"));

        QStringList ids;
        for (const mirante::ItemType *type : registry.types())
            ids << type->id();
        ids.sort();
        QCOMPARE(ids, QStringList(expected.keys()));

        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            QVERIFY2(registry.contains(it.key()), qPrintable(it.key()));
            QCOMPARE(registry.type(it.key()).name(), it.value());
        }
    }

    void iconsRender()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        for (const mirante::ItemType *type : registry.types()) {
            const QIcon icon = type->icon();
            QVERIFY2(!icon.isNull(), qPrintable(type->id()));
            QVERIFY2(!icon.pixmap(16, 16).isNull(), qPrintable(type->id()));
        }
    }
};

QTEST_MAIN(TestRegister)
#include "tst_register.moc"
