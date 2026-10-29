// Registers the jira module into a fresh registry and checks its types.
#include "items/FieldValidation.h"
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
    void fieldKeys()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        QMap<QString, QStringList> expected;
        expected.insert(QStringLiteral("jira.issue.summary"), {QStringLiteral("key")});
        expected.insert(QStringLiteral("jira.issue.page"), {QStringLiteral("key")});
        expected.insert(QStringLiteral("jira.list"), {QStringLiteral("preset"), QStringLiteral("jql")});

        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            const mirante::ItemType &type = registry.type(it.key());
            QStringList keys;
            for (const mirante::Field &field : type.fields())
                keys << field.key;
            QCOMPARE(keys, it.value());
            QVERIFY(type.pastedImageField().isEmpty());
        }
    }

    // Keys and labels are set and keys are unique. A new item's defaults
    // validate, except that each required field without a default is reported.
    void fieldsValidateTheirDefaults()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        for (const mirante::ItemType *type : registry.types()) {
            const std::vector<mirante::Field> fields = type->fields();
            QStringList keys;
            QStringList missing;
            for (const mirante::Field &field : fields) {
                const QString where = type->id() + QLatin1Char(' ') + field.key;
                QVERIFY2(!field.key.isEmpty(), qPrintable(type->id()));
                QVERIFY2(!keys.contains(field.key), qPrintable(where));
                QVERIFY2(!field.label.isEmpty(), qPrintable(where));
                if (field.type == mirante::Field::Type::Choice) {
                    QVERIFY2(!field.choices.isEmpty(), qPrintable(where));
                    QVERIFY2(field.choiceLabels.size() == field.choices.size(), qPrintable(where));
                }
                keys << field.key;
                if (field.required && (field.defaultValue.isNull() || field.defaultValue.isUndefined()))
                    missing << field.key;
            }

            QStringList reported;
            for (const mirante::FieldProblem &problem : mirante::validate(fields, mirante::defaultConfig(fields)))
                reported << problem.key;
            QCOMPARE(reported, missing);
        }
    }
};

QTEST_MAIN(TestRegister)
#include "tst_register.moc"
