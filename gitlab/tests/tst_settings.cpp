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

using mirante::modules::gitlab::baseUrlOf;
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
        QCOMPARE(fields.size(), std::size_t(3));
        QCOMPARE(fields[0].key, QStringLiteral("baseUrl"));
        QCOMPARE(fields[0].type, mirante::Field::Type::Url);
        QCOMPARE(fields[0].defaultValue.toString(), QStringLiteral("https://gitlab.com"));
        QCOMPARE(fields[1].key, QStringLiteral("token"));
        QCOMPARE(fields[1].type, mirante::Field::Type::Secret);
        QCOMPARE(fields[2].key, QStringLiteral("projects"));
        QCOMPARE(fields[2].type, mirante::Field::Type::TextList);
        QVERIFY(!fields[2].label.isEmpty());
        // Nothing is required, so empty settings are valid.
        QVERIFY(mirante::validate(fields, {}).empty());
    }

    // The base URL defaults to gitlab.com; a bad one gives none.
    void baseUrl()
    {
        QCOMPARE(baseUrlOf({}), QUrl(QStringLiteral("https://gitlab.com")));
        QCOMPARE(baseUrlOf({{QStringLiteral("baseUrl"), QStringLiteral(" https://example.org/gitlab/ ")}}),
                 QUrl(QStringLiteral("https://example.org/gitlab/")));
        QVERIFY(baseUrlOf({{QStringLiteral("baseUrl"), QStringLiteral("gitlab")}}).isEmpty());
    }

    // A pasted link on a self-hosted server at a path is read with the base
    // URL of the profile the new item would use.
    void fromUrlReadsTheProfilesServer()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);
        registry.setSettingsReader([](const QString &profileId, const QString &, const QString &) {
            if (profileId == QLatin1String("work"))
                return QJsonObject{{QStringLiteral("baseUrl"), QStringLiteral("https://example.org/gitlab/")}};
            return QJsonObject();
        });
        const mirante::ItemType &summary = registry.type(QStringLiteral("gitlab.mr.summary"));
        const QUrl link(QStringLiteral("https://example.org/gitlab/acme/app/-/merge_requests/3"));
        const auto inWork = summary.fromUrl(link, QStringLiteral("work"));
        QVERIFY(inWork);
        QCOMPARE(inWork->config.value(QStringLiteral("project")).toString(), QStringLiteral("acme/app"));
        // Without that server the path is read from the server root.
        const auto elsewhere = summary.fromUrl(link, QStringLiteral("default"));
        QVERIFY(elsewhere);
        QCOMPARE(elsewhere->config.value(QStringLiteral("project")).toString(), QStringLiteral("gitlab/acme/app"));
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

    // Every type's "project" field offers the paths configured in the
    // profile the item uses, read when the fields are asked for, so a change
    // shows in the next form. fields() alone, without a profile, offers none.
    void projectFieldsOfferTheConfiguredPaths()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        QJsonObject work;
        QJsonObject standard;
        registry.setSettingsReader([&](const QString &profileId, const QString &moduleId, const QString &screenId) {
            if (moduleId != QLatin1String("gitlab") || screenId != QLatin1String("general"))
                return QJsonObject();
            if (profileId == QLatin1String("work"))
                return work;
            return profileId == QLatin1String("default") ? standard : QJsonObject();
        });

        const auto suggestionsOf = [&registry](const QString &typeId, const QString &profileId) {
            for (const mirante::Field &field : registry.type(typeId).fieldsForProfile(profileId)) {
                if (field.key == QLatin1String("project"))
                    return field.suggestions;
            }
            return QStringList{QStringLiteral("no project field")};
        };

        for (const mirante::ItemType *type : registry.types())
            QVERIFY2(suggestionsOf(type->id(), QStringLiteral("work")).isEmpty(), qPrintable(type->id()));

        work.insert(QStringLiteral("projects"), QJsonArray{QStringLiteral("acme/backend"), QStringLiteral("acme/web")});
        standard.insert(QStringLiteral("projects"), QJsonArray{QStringLiteral("me/dotfiles")});
        const QStringList expected{QStringLiteral("acme/backend"), QStringLiteral("acme/web")};
        for (const mirante::ItemType *type : registry.types()) {
            QCOMPARE(suggestionsOf(type->id(), QStringLiteral("work")), expected);
            QCOMPARE(suggestionsOf(type->id(), QStringLiteral("default")), QStringList{QStringLiteral("me/dotfiles")});
            QVERIFY(suggestionsOf(type->id(), QStringLiteral("other")).isEmpty());
            // Without a profile there is nothing to offer.
            for (const mirante::Field &field : type->fields())
                QVERIFY2(field.suggestions.isEmpty(), qPrintable(type->id()));
        }

        // Other fields offer nothing.
        for (const mirante::Field &field :
             registry.type(QStringLiteral("gitlab.mr.list")).fieldsForProfile(QStringLiteral("work"))) {
            if (field.key != QLatin1String("project"))
                QVERIFY2(field.suggestions.isEmpty(), qPrintable(field.key));
        }
    }
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
