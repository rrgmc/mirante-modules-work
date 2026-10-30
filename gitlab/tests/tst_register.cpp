// Registers the gitlab module into a fresh registry and checks its types.
#include "items/FieldValidation.h"
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

    void fieldKeys()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        const QStringList single{QStringLiteral("project"), QStringLiteral("iid")};
        const QStringList sizes{QStringLiteral("limit"), QStringLiteral("pageSize")};
        const QStringList list = QStringList{QStringLiteral("scope"), QStringLiteral("project"),
                                             QStringLiteral("group"), QStringLiteral("preset"),
                                             QStringLiteral("filter")}
            + sizes;
        QMap<QString, QStringList> expected;
        expected.insert(QStringLiteral("gitlab.mr.summary"),
                        single
                            + QStringList{QStringLiteral("showPipeline"), QStringLiteral("showApprovals"),
                                          QStringLiteral("showPeople"), QStringLiteral("showLabels")});
        expected.insert(QStringLiteral("gitlab.mr.page"), single);
        expected.insert(QStringLiteral("gitlab.mr.list"),
                        list + QStringList{QStringLiteral("showPipeline"), QStringLiteral("showApprovals")});
        expected.insert(QStringLiteral("gitlab.pipeline"),
                        {QStringLiteral("project"), QStringLiteral("branchSource"), QStringLiteral("branch"),
                         QStringLiteral("iid")});
        expected.insert(QStringLiteral("gitlab.issue.summary"),
                        single
                            + QStringList{QStringLiteral("showDetails"), QStringLiteral("showLabels"),
                                          QStringLiteral("showComments")});
        expected.insert(QStringLiteral("gitlab.issue.page"), single);
        expected.insert(QStringLiteral("gitlab.issue.list"), list);

        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            const mirante::ItemType &type = registry.type(it.key());
            QStringList keys;
            for (const mirante::Field &field : type.fields())
                keys << field.key;
            QCOMPARE(keys, it.value());
            QVERIFY(type.pastedImageField().isEmpty());
        }
    }

    // A new pipeline item follows master, and the empty field says so.
    void pipelineBranchDefaultsToMaster_data()
    {
        QTest::addColumn<QString>("typeId");
        QTest::newRow("pipeline") << QStringLiteral("gitlab.pipeline");
    }

    void pipelineBranchDefaultsToMaster()
    {
        QFETCH(QString, typeId);
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        for (const mirante::Field &field : registry.type(typeId).fields()) {
            if (field.key == QLatin1String("branch")) {
                QCOMPARE(field.defaultValue.toString(), QStringLiteral("master"));
                QCOMPARE(field.placeholder, QStringLiteral("master"));
                return;
            }
        }
        QFAIL("No branch field");
    }

    void listsHaveSizes_data()
    {
        QTest::addColumn<QString>("typeId");
        QTest::newRow("merge requests") << QStringLiteral("gitlab.mr.list");
        QTest::newRow("issues") << QStringLiteral("gitlab.issue.list");
    }

    void listsHaveSizes()
    {
        QFETCH(QString, typeId);
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        const QJsonObject defaults = mirante::defaultConfig(registry.type(typeId).fields());
        QCOMPARE(defaults.value(QStringLiteral("limit")).toInt(), 100);
        QCOMPARE(defaults.value(QStringLiteral("pageSize")).toInt(), 50);
    }

    // Keys and labels are set and keys are unique. A new item's defaults
    // validate, except that each required field without a default is reported.
    void fieldsValidateTheirDefaults()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

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

    // The module has a name and a description, and so has every type, for
    // Help > Modules. See docs/decisions/0056-modules-window.md.
    void namesAndDescriptions()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        QCOMPARE(registry.moduleName(QStringLiteral("gitlab")), QStringLiteral("GitLab"));
        QVERIFY(!registry.moduleDescription(QStringLiteral("gitlab")).isEmpty());
        for (const mirante::ItemType *type : registry.types())
            QVERIFY2(!type->description().isEmpty(), qPrintable(type->id()));
    }
};

QTEST_MAIN(TestRegister)
#include "tst_register.moc"
