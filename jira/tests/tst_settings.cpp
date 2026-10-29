// The jira module's General Configuration screen: the site's base URL and
// its token, and reading a pasted link with the site of a profile.
#include "../JiraSettings.h"

#include "items/FieldValidation.h"
#include "items/ItemTypeRegistry.h"

#include <QJsonObject>
#include <QTest>

namespace mirante::modules::jira {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::modules::jira::baseUrlOf;

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void registersTheGeneralScreen()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);

        QCOMPARE(registry.moduleName(QStringLiteral("jira")), QStringLiteral("Jira"));
        const std::vector<const mirante::SettingsScreen *> screens = registry.settingsScreens(QStringLiteral("jira"));
        QCOMPARE(screens.size(), std::size_t(1));
        QCOMPARE(screens.front()->id(), QStringLiteral("general"));
        QCOMPARE(screens.front()->name(), QStringLiteral("General Configuration"));

        const std::vector<mirante::Field> fields = screens.front()->fields();
        QCOMPARE(fields.size(), std::size_t(2));
        QCOMPARE(fields[0].key, QStringLiteral("baseUrl"));
        QCOMPARE(fields[0].type, mirante::Field::Type::Url);
        QVERIFY(fields[0].defaultValue.isNull() || fields[0].defaultValue.isUndefined());   // no default
        QCOMPARE(fields[1].key, QStringLiteral("token"));
        QCOMPARE(fields[1].type, mirante::Field::Type::Secret);
        QVERIFY(mirante::validate(fields, {}).empty());

        // Every Jira type uses a profile, through the screen.
        for (const mirante::ItemType *type : registry.types())
            QVERIFY2(registry.usesProfile(type->id()), qPrintable(type->id()));
    }

    void baseUrl()
    {
        QVERIFY(baseUrlOf({}).isEmpty());
        QVERIFY(baseUrlOf({{QStringLiteral("baseUrl"), QStringLiteral("not a site")}}).isEmpty());
        QVERIFY(baseUrlOf({{QStringLiteral("baseUrl"), QStringLiteral("ftp://acme.example")}}).isEmpty());
        QCOMPARE(baseUrlOf({{QStringLiteral("baseUrl"), QStringLiteral(" https://acme.atlassian.net ")}}),
                 QUrl(QStringLiteral("https://acme.atlassian.net")));
    }

    // A link on a self-hosted site at a path is read with the site of the
    // profile the new item would use.
    void fromUrlReadsTheProfilesSite()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);
        registry.setSettingsReader([](const QString &profileId, const QString &moduleId, const QString &screenId) {
            if (profileId == QLatin1String("work") && moduleId == QLatin1String("jira")
                && screenId == QLatin1String("general"))
                return QJsonObject{{QStringLiteral("baseUrl"), QStringLiteral("https://tracker.example.net/jira")}};
            return QJsonObject();
        });
        const mirante::ItemType &page = registry.type(QStringLiteral("jira.issue.page"));
        const QUrl link(QStringLiteral("https://tracker.example.net/jira/browse/OPS-7"));
        const auto inWork = page.fromUrl(link, QStringLiteral("work"));
        QVERIFY(inWork);
        QCOMPARE(inWork->config.value(QStringLiteral("key")).toString(), QStringLiteral("OPS-7"));
        QCOMPARE(inWork->baseUrl, QUrl(QStringLiteral("https://tracker.example.net/jira")));
    }
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
