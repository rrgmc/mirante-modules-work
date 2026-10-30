// The jira module's General Configuration screen: the site's base URL, its
// deployment, the email and the token; the server type and its
// authentication; the project setting; and reading a pasted link with the
// site of a profile.
#include "../JiraServerType.h"
#include "../JiraSettings.h"

#include "items/FieldValidation.h"
#include "items/ItemTypeRegistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTest>

#include <memory>
#include <vector>

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
        QCOMPARE(fields.size(), std::size_t(5));
        QCOMPARE(fields[0].key, QStringLiteral("baseUrl"));
        QCOMPARE(fields[0].type, mirante::Field::Type::Url);
        QVERIFY(fields[0].defaultValue.isNull() || fields[0].defaultValue.isUndefined());   // no default
        QCOMPARE(fields[1].key, QStringLiteral("deployment"));
        QCOMPARE(fields[1].type, mirante::Field::Type::Choice);
        QCOMPARE(fields[1].choices, QStringList({QStringLiteral("auto"), QStringLiteral("cloud"), QStringLiteral("dc")}));
        QCOMPARE(fields[1].choiceLabels,
                 QStringList({QStringLiteral("Automatic"), QStringLiteral("Jira Cloud"),
                              QStringLiteral("Jira Data Center")}));
        QCOMPARE(fields[1].defaultValue.toString(), QStringLiteral("auto"));
        QCOMPARE(fields[2].key, QStringLiteral("email"));
        QCOMPARE(fields[2].type, mirante::Field::Type::Text);
        QCOMPARE(fields[3].key, QStringLiteral("token"));
        QCOMPARE(fields[3].type, mirante::Field::Type::Secret);
        QCOMPARE(fields[4].key, QStringLiteral("extraFields"));
        QCOMPARE(fields[4].label, QStringLiteral("Extra fields"));
        QCOMPARE(fields[4].type, mirante::Field::Type::TextList);
        QVERIFY(mirante::validate(fields, {}).empty());

        // The server is on that screen, with that token.
        const mirante::ServerType *server = registry.serverType(QStringLiteral("jira"));
        QVERIFY(server);
        QCOMPARE(server->screenId(), QStringLiteral("general"));
        QCOMPARE(server->tokenKey(), QStringLiteral("token"));

        // Every Jira type uses a profile, through the screen.
        for (const mirante::ItemType *type : registry.types())
            QVERIFY2(registry.usesProfile(type->id()), qPrintable(type->id()));
    }

    void extraFields()
    {
        using mirante::modules::jira::extraFieldsOf;
        QVERIFY(extraFieldsOf({}).isEmpty());
        QVERIFY(extraFieldsOf({{QStringLiteral("extraFields"), QStringLiteral("Team")}}).isEmpty());
        const QJsonArray names{QStringLiteral(" Team "), QStringLiteral(""), QStringLiteral("  "),
                               QStringLiteral("Fix version"), QStringLiteral("team"), 5};
        QCOMPARE(extraFieldsOf({{QStringLiteral("extraFields"), names}}),
                 QStringList({QStringLiteral("Team"), QStringLiteral("Fix version")}));
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

    // Automatic guesses from the host; "cloud" and "dc" are followed as
    // chosen. See decision 0060.
    void deployment_data()
    {
        QTest::addColumn<QString>("baseUrl");
        QTest::addColumn<QString>("deployment");
        QTest::addColumn<bool>("cloud");
        QTest::newRow("atlassian.net") << "https://acme.atlassian.net" << "auto" << true;
        QTest::newRow("upper case") << "https://ACME.Atlassian.NET" << "auto" << true;
        QTest::newRow("jira.com") << "https://acme.jira.com" << "auto" << true;
        QTest::newRow("own host") << "https://jira.example.org/jira" << "auto" << false;
        QTest::newRow("look-alike") << "https://atlassian.net.example.org" << "auto" << false;
        QTest::newRow("no setting") << "https://acme.atlassian.net" << "" << true;
        QTest::newRow("unknown value") << "https://jira.example.org" << "other" << false;
        QTest::newRow("no site") << "" << "auto" << false;
        QTest::newRow("cloud chosen") << "https://jira.example.org" << "cloud" << true;
        QTest::newRow("dc chosen") << "https://acme.atlassian.net" << "dc" << false;
    }

    void deployment()
    {
        QFETCH(QString, baseUrl);
        QFETCH(QString, deployment);
        QFETCH(bool, cloud);
        QJsonObject values{{QStringLiteral("baseUrl"), baseUrl}};
        if (!deployment.isEmpty())
            values.insert(QStringLiteral("deployment"), deployment);
        QCOMPARE(mirante::modules::jira::deploymentOf(values) == mirante::modules::jira::Deployment::Cloud, cloud);
    }

    void serverType()
    {
        const mirante::modules::jira::JiraServerType server;
        const QJsonObject cloud{{QStringLiteral("baseUrl"), QStringLiteral("https://acme.atlassian.net")},
                                {QStringLiteral("email"), QStringLiteral(" someone@example.com ")}};
        QCOMPARE(server.baseUrl(cloud), QUrl(QStringLiteral("https://acme.atlassian.net")));
        QVERIFY(server.baseUrl({}).isEmpty());
        QVERIFY(server.problem(cloud).isEmpty());

        // Basic with "email:token", the email trimmed.
        QNetworkRequest request;
        server.authorize(request, QStringLiteral("secret-token"), cloud);
        QCOMPARE(request.rawHeader("Authorization"),
                 QByteArray("Basic ") + QByteArray("someone@example.com:secret-token").toBase64());

        // Bearer on Data Center, where the email is not used.
        const QJsonObject dc{{QStringLiteral("baseUrl"), QStringLiteral("https://jira.example.org/jira")},
                             {QStringLiteral("email"), QStringLiteral("someone@example.com")}};
        QNetworkRequest dcRequest;
        server.authorize(dcRequest, QStringLiteral("secret-token"), dc);
        QCOMPARE(dcRequest.rawHeader("Authorization"), QByteArray("Bearer secret-token"));
        QVERIFY(server.problem(dc).isEmpty());
        QVERIFY(server.problem({{QStringLiteral("baseUrl"), QStringLiteral("https://jira.example.org")}}).isEmpty());
    }

    void cloudWithoutEmailIsAProblem()
    {
        const mirante::modules::jira::JiraServerType server;
        const QString expected = QStringLiteral("Set the email for Jira Cloud in the profile's Jira settings.");
        QCOMPARE(server.problem({{QStringLiteral("baseUrl"), QStringLiteral("https://acme.atlassian.net")}}),
                 expected);
        QCOMPARE(server.problem({{QStringLiteral("baseUrl"), QStringLiteral("https://acme.atlassian.net")},
                                 {QStringLiteral("email"), QStringLiteral("  ")}}),
                 expected);
        QCOMPARE(server.problem({{QStringLiteral("baseUrl"), QStringLiteral("https://jira.example.org")},
                                 {QStringLiteral("deployment"), QStringLiteral("cloud")}}),
                 expected);
    }

    // The module gives projects one setting, the Jira project key.
    void projectSetting()
    {
        RecordingRegistrar registrar;
        mirante::modules::jira::registerModule(registrar);
        QCOMPARE(registrar.projectSettings.size(), std::size_t(1));
        const mirante::Field &field = registrar.projectSettings.front();
        QCOMPARE(field.key, QStringLiteral("projectKey"));
        QCOMPARE(field.label, QStringLiteral("Jira project key"));
        QCOMPARE(field.type, mirante::Field::Type::Text);
        QVERIFY(!field.required);
        QVERIFY(registrar.server);
    }

private:
    // Keeps what a module registers, for what the registry does not give
    // back yet.
    class RecordingRegistrar : public mirante::ModuleRegistrar
    {
    public:
        class NoSettings : public mirante::ModuleSettings
        {
        public:
            QJsonObject values(const QString &, const QString &) const override { return {}; }
        };

        void addItemType(std::unique_ptr<mirante::ItemType>) override {}
        void setModuleName(const QString &) override {}
        void setModuleDescription(const QString &) override {}
        void addSettingsScreen(std::unique_ptr<mirante::SettingsScreen>) override {}
        const mirante::ModuleSettings &settings() const override { return m_settings; }
        void setServerType(std::unique_ptr<mirante::ServerType> type) override { server = std::move(type); }
        void addProjectSetting(const mirante::Field &field) override { projectSettings.push_back(field); }

        std::unique_ptr<mirante::ServerType> server;
        std::vector<mirante::Field> projectSettings;

    private:
        NoSettings m_settings;
    };
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
