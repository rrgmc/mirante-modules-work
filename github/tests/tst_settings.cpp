// The github module's General Configuration screen, its server type, the
// web and API addresses, and the repositories it offers in the "repository"
// field of every type.
#include "../GitHubSettings.h"

#include "items/FieldValidation.h"
#include "items/ItemTypeRegistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QTest>

namespace mirante::modules::github {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::modules::github::apiBaseOf;
using mirante::modules::github::GitHubServerType;
using mirante::modules::github::repositoryNames;
using mirante::modules::github::webBaseOf;

namespace {

QJsonObject withBase(const QString &baseUrl)
{
    return {{QStringLiteral("baseUrl"), baseUrl}};
}

} // namespace

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void registersTheGeneralScreen()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("github"), &mirante::modules::github::registerModule);

        const std::vector<const mirante::SettingsScreen *> screens =
            registry.settingsScreens(QStringLiteral("github"));
        QCOMPARE(screens.size(), std::size_t(1));
        QCOMPARE(screens.front()->id(), QStringLiteral("general"));
        QCOMPARE(screens.front()->name(), QStringLiteral("General Configuration"));
        QVERIFY(!screens.front()->createEditor(nullptr));

        const std::vector<mirante::Field> fields = screens.front()->fields();
        QCOMPARE(fields.size(), std::size_t(3));
        QCOMPARE(fields[0].key, QStringLiteral("baseUrl"));
        QCOMPARE(fields[0].type, mirante::Field::Type::Url);
        QCOMPARE(fields[0].defaultValue.toString(), QStringLiteral("https://github.com"));
        QCOMPARE(fields[1].key, QStringLiteral("token"));
        QCOMPARE(fields[1].type, mirante::Field::Type::Secret);
        QCOMPARE(fields[2].key, QStringLiteral("repositories"));
        QCOMPARE(fields[2].type, mirante::Field::Type::TextList);
        // Nothing is required, so empty settings are valid.
        QVERIFY(mirante::validate(fields, {}).empty());
    }

    void registersServerAndProjectSetting()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("github"), &mirante::modules::github::registerModule);

        const mirante::ServerType *server = registry.serverType(QStringLiteral("github"));
        QVERIFY(server);
        QCOMPARE(server->screenId(), QStringLiteral("general"));
        QCOMPARE(server->tokenKey(), QStringLiteral("token"));
        QCOMPARE(server->baseUrl({}), QUrl(QStringLiteral("https://api.github.com/")));
        QVERIFY(server->problem({}).isEmpty());

        const std::vector<mirante::Field> settings = registry.projectSettings(QStringLiteral("github"));
        QCOMPARE(settings.size(), std::size_t(1));
        QCOMPARE(settings.front().key, QStringLiteral("repository"));
        QCOMPARE(settings.front().label, QStringLiteral("GitHub repository"));
    }

    // The web address defaults to github.com; a bad one gives none.
    void webBase()
    {
        QCOMPARE(webBaseOf({}), QUrl(QStringLiteral("https://github.com")));
        QCOMPARE(webBaseOf(withBase(QStringLiteral("  "))), QUrl(QStringLiteral("https://github.com")));
        QCOMPARE(webBaseOf(withBase(QStringLiteral(" https://github.example.com/ "))),
                 QUrl(QStringLiteral("https://github.example.com/")));
        QVERIFY(webBaseOf(withBase(QStringLiteral("github"))).isEmpty());
        QVERIFY(webBaseOf(withBase(QStringLiteral("ftp://github.example.com"))).isEmpty());
    }

    void apiBase_data()
    {
        QTest::addColumn<QString>("baseUrl");
        QTest::addColumn<QString>("api");
        QTest::newRow("default") << QString() << QStringLiteral("https://api.github.com/");
        QTest::newRow("github.com") << QStringLiteral("https://github.com/")
                                    << QStringLiteral("https://api.github.com/");
        QTest::newRow("www") << QStringLiteral("https://www.GitHub.com") << QStringLiteral("https://api.github.com/");
        QTest::newRow("enterprise") << QStringLiteral("https://github.example.com")
                                    << QStringLiteral("https://github.example.com/api/v3/");
        QTest::newRow("enterprise with slash") << QStringLiteral("https://github.example.com/")
                                               << QStringLiteral("https://github.example.com/api/v3/");
        QTest::newRow("at a path") << QStringLiteral("http://example.org:8080/github/")
                                   << QStringLiteral("http://example.org:8080/github/api/v3/");
        QTest::newRow("bad") << QStringLiteral("github") << QString();
    }

    void apiBase()
    {
        QFETCH(QString, baseUrl);
        QFETCH(QString, api);
        const QJsonObject values = baseUrl.isEmpty() ? QJsonObject() : withBase(baseUrl);
        QCOMPARE(apiBaseOf(values), QUrl(api));
        QCOMPARE(GitHubServerType().baseUrl(values), QUrl(api));
    }

    // A bearer token, GitHub's media type and API version, and a user agent,
    // without which GitHub refuses the request.
    void authorizeSetsHeaders()
    {
        QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/user")));
        GitHubServerType().authorize(request, QStringLiteral("ghp_secret"), {});
        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer ghp_secret"));
        QCOMPARE(request.rawHeader("Accept"), QByteArray("application/vnd.github+json"));
        QCOMPARE(request.rawHeader("X-GitHub-Api-Version"), QByteArray("2022-11-28"));
        QCOMPARE(request.rawHeader("User-Agent"), QByteArray("Mirante"));
    }

    // Trimmed, without slashes at the ends, without empty or repeated ones.
    void repositoryNamesAreCleaned()
    {
        const QJsonObject values{{QStringLiteral("repositories"),
                                  QJsonArray{QStringLiteral(" acme/backend "), QStringLiteral("/acme/web/"),
                                             QString(), QStringLiteral("acme/backend"), QStringLiteral("  ")}}};
        QCOMPARE(repositoryNames(values), (QStringList{QStringLiteral("acme/backend"), QStringLiteral("acme/web")}));
        QVERIFY(repositoryNames({}).isEmpty());
    }
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
