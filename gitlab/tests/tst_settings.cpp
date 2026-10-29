// The gitlab module's General Configuration screen, and the project paths it
// offers in the "project" field of every type.
#include "../GitLabSettings.h"

#include "items/FieldValidation.h"
#include "items/ItemTypeRegistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

namespace mirante::modules::gitlab {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::modules::gitlab::projectPaths;

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void registersTheGeneralScreen()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        QCOMPARE(registry.moduleName(QStringLiteral("gitlab")), QStringLiteral("GitLab"));
        const std::vector<const mirante::SettingsScreen *> screens =
            registry.settingsScreens(QStringLiteral("gitlab"));
        QCOMPARE(screens.size(), std::size_t(1));
        QCOMPARE(screens.front()->id(), QStringLiteral("general"));
        QCOMPARE(screens.front()->name(), QStringLiteral("General Configuration"));
        QVERIFY(!screens.front()->createEditor(nullptr));

        const std::vector<mirante::Field> fields = screens.front()->fields();
        QCOMPARE(fields.size(), std::size_t(1));
        QCOMPARE(fields.front().key, QStringLiteral("projects"));
        QCOMPARE(fields.front().type, mirante::Field::Type::TextList);
        QVERIFY(!fields.front().label.isEmpty());
        // Nothing is required, so empty settings are valid.
        QVERIFY(mirante::validate(fields, {}).empty());
    }

    void projectPathsAreCleaned()
    {
        const QJsonObject general{
            {QStringLiteral("projects"),
             QJsonArray{QStringLiteral(" acme/backend "), QStringLiteral("/acme/web/"), QString(),
                        QStringLiteral("acme/backend"), 42, QStringLiteral("acme/docs")}}};
        QCOMPARE(projectPaths(general), (QStringList{QStringLiteral("acme/backend"), QStringLiteral("acme/web"),
                                                    QStringLiteral("acme/docs")}));
        QVERIFY(projectPaths({}).isEmpty());
        QVERIFY(projectPaths(QJsonObject{{QStringLiteral("projects"), QStringLiteral("acme/x")}}).isEmpty());
    }

    // Every type's "project" field offers the configured paths, read when the
    // fields are asked for, so a change shows in the next form.
    void projectFieldsOfferTheConfiguredPaths()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        QJsonObject general;
        registry.setSettingsReader([&general](const QString &moduleId, const QString &screenId) {
            return moduleId == QLatin1String("gitlab") && screenId == QLatin1String("general") ? general
                                                                                               : QJsonObject();
        });

        const auto suggestionsOf = [&registry](const QString &typeId) {
            for (const mirante::Field &field : registry.type(typeId).fields()) {
                if (field.key == QLatin1String("project"))
                    return field.suggestions;
            }
            return QStringList{QStringLiteral("no project field")};
        };

        for (const mirante::ItemType *type : registry.types())
            QVERIFY2(suggestionsOf(type->id()).isEmpty(), qPrintable(type->id()));

        general.insert(QStringLiteral("projects"),
                       QJsonArray{QStringLiteral("acme/backend"), QStringLiteral("acme/web")});
        const QStringList expected{QStringLiteral("acme/backend"), QStringLiteral("acme/web")};
        for (const mirante::ItemType *type : registry.types())
            QCOMPARE(suggestionsOf(type->id()), expected);

        // Other fields offer nothing.
        for (const mirante::Field &field : registry.type(QStringLiteral("gitlab.mr.list")).fields()) {
            if (field.key != QLatin1String("project"))
                QVERIFY2(field.suggestions.isEmpty(), qPrintable(field.key));
        }
    }
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
