// Registers the gitlab module into a fresh registry and checks its types.
#include "items/ItemTypeRegistry.h"

#include <QIcon>
#include <QMap>
#include <QPixmap>
#include <QTest>

namespace mirante::modules::gitlab {
void registerModule(mirante::ModuleRegistrar &registrar);
}

class TestRegister : public QObject
{
    Q_OBJECT

private slots:
    void registersTypes()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        // Expected id and name of each type. QMap keeps the ids sorted.
        QMap<QString, QString> expected;
        expected.insert(QStringLiteral("gitlab.mr.summary"), QStringLiteral("GitLab MR (summary)"));
        expected.insert(QStringLiteral("gitlab.mr.page"), QStringLiteral("GitLab MR (page)"));
        expected.insert(QStringLiteral("gitlab.mr.list"), QStringLiteral("GitLab MR List"));
        expected.insert(QStringLiteral("gitlab.pipeline"), QStringLiteral("GitLab CI Pipeline"));
        expected.insert(QStringLiteral("gitlab.issue.summary"), QStringLiteral("GitLab Issue (summary)"));
        expected.insert(QStringLiteral("gitlab.issue.page"), QStringLiteral("GitLab Issue (page)"));
        expected.insert(QStringLiteral("gitlab.issue.list"), QStringLiteral("GitLab Issue List"));

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
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        for (const mirante::ItemType *type : registry.types()) {
            const QIcon icon = type->icon();
            QVERIFY2(!icon.isNull(), qPrintable(type->id()));
            QVERIFY2(!icon.pixmap(16, 16).isNull(), qPrintable(type->id()));
        }
    }
};

QTEST_MAIN(TestRegister)
#include "tst_register.moc"
