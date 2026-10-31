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
        // A value that is not a list, or entries that are not text, give nothing.
        QVERIFY(repositoryNames({{QStringLiteral("repositories"), QStringLiteral("acme/x")}}).isEmpty());
        QCOMPARE(repositoryNames({{QStringLiteral("repositories"), QJsonArray{42, QStringLiteral("acme/x")}}}),
                 QStringList{QStringLiteral("acme/x")});
    }

    // The labels and placeholders of the screen and the project setting.
    void screenTexts()
    {
        const std::vector<mirante::Field> fields = mirante::modules::github::makeGeneralScreen()->fields();
        QCOMPARE(fields[0].label, QStringLiteral("Base URL"));
        QCOMPARE(fields[0].placeholder, QStringLiteral("https://github.com"));
        QCOMPARE(fields[1].label, QStringLiteral("Token"));
        QCOMPARE(fields[2].label, QStringLiteral("Repositories"));
        for (const mirante::Field &field : fields)
            QVERIFY2(!field.required, qPrintable(field.key));

        const mirante::Field setting = mirante::modules::github::makeProjectSetting();
        QCOMPARE(setting.type, mirante::Field::Type::Text);
        QCOMPARE(setting.placeholder, QStringLiteral("owner/repo"));
        QVERIFY(!setting.required);
    }

    // The server's address follows the screen's values; the headers do not
    // depend on them, and the token field is the screen's Secret field.
    void serverOfEnterprise()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("github"), &mirante::modules::github::registerModule);
        const mirante::ServerType *server = registry.serverType(QStringLiteral("github"));
        QVERIFY(server);

        const QJsonObject values = withBase(QStringLiteral("https://example.org/github"));
        QCOMPARE(server->baseUrl(values), QUrl(QStringLiteral("https://example.org/github/api/v3/")));
        QVERIFY(server->problem(values).isEmpty());
        QVERIFY(server->baseUrl(withBase(QStringLiteral("ftp://example.org"))).isEmpty());

        QNetworkRequest request(QUrl(QStringLiteral("https://example.org/github/api/v3/user")));
        server->authorize(request, QStringLiteral("ghp_x"), values);
        QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer ghp_x"));
        QCOMPARE(request.rawHeader("User-Agent"), QByteArray("Mirante"));
        QVERIFY(!request.hasRawHeader("PRIVATE-TOKEN"));

        bool found = false;
        for (const mirante::Field &field : registry.settingsScreens(QStringLiteral("github")).front()->fields())
            found = found || (field.key == server->tokenKey() && field.type == mirante::Field::Type::Secret);
        QVERIFY(found);
    }

    // The web address keeps a path and a port, and the API address is built
    // from it without doubling a slash.
    void webBaseWithPath()
    {
        const QJsonObject values = withBase(QStringLiteral("http://example.org:8080/tools/github"));
        QCOMPARE(webBaseOf(values), QUrl(QStringLiteral("http://example.org:8080/tools/github")));
        QCOMPARE(apiBaseOf(values), QUrl(QStringLiteral("http://example.org:8080/tools/github/api/v3/")));
        // A query or a fragment of the web address is not kept in the API's.
        QCOMPARE(apiBaseOf(withBase(QStringLiteral("https://github.example.com/?a=1#b"))),
                 QUrl(QStringLiteral("https://github.example.com/api/v3/")));
        // github.com with a path is still github.com.
        QCOMPARE(apiBaseOf(withBase(QStringLiteral("https://github.com/acme"))),
                 QUrl(QStringLiteral("https://api.github.com/")));
    }

    // Every type's "repository" field offers the repositories configured in
    // the profile the item uses, read when the fields are asked for, so a
    // change shows in the next form. fields() alone, without a profile,
    // offers none.
    void repositoryFieldsOfferTheConfiguredRepositories()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("github"), &mirante::modules::github::registerModule);

        QJsonObject work;
        QJsonObject standard;
        registry.setSettingsReader([&](const QString &profileId, const QString &moduleId, const QString &screenId) {
            if (moduleId != QLatin1String("github") || screenId != QLatin1String("general"))
                return QJsonObject();
            if (profileId == QLatin1String("work"))
                return work;
            return profileId == QLatin1String("default") ? standard : QJsonObject();
        });

        const auto suggestionsOf = [&registry](const QString &typeId, const QString &profileId) {
            for (const mirante::Field &field : registry.type(typeId).fieldsForProfile(profileId)) {
                if (field.key == QLatin1String("repository"))
                    return field.suggestions;
            }
            return QStringList{QStringLiteral("no repository field")};
        };

        for (const mirante::ItemType *type : registry.types())
            QVERIFY2(suggestionsOf(type->id(), QStringLiteral("work")).isEmpty(), qPrintable(type->id()));

        work.insert(QStringLiteral("repositories"),
                    QJsonArray{QStringLiteral("acme/backend"), QStringLiteral(" acme/web/ ")});
        standard.insert(QStringLiteral("repositories"), QJsonArray{QStringLiteral("me/dotfiles")});
        const QStringList expected{QStringLiteral("acme/backend"), QStringLiteral("acme/web")};
        for (const mirante::ItemType *type : registry.types()) {
            QCOMPARE(suggestionsOf(type->id(), QStringLiteral("work")), expected);
            QCOMPARE(suggestionsOf(type->id(), QStringLiteral("default")), QStringList{QStringLiteral("me/dotfiles")});
            QVERIFY(suggestionsOf(type->id(), QStringLiteral("other")).isEmpty());
            for (const mirante::Field &field : type->fields())
                QVERIFY2(field.suggestions.isEmpty(), qPrintable(type->id()));
        }

        // Other fields offer nothing.
        for (const mirante::Field &field :
             registry.type(QStringLiteral("github.pr.list")).fieldsForProfile(QStringLiteral("work"))) {
            if (field.key != QLatin1String("repository"))
                QVERIFY2(field.suggestions.isEmpty(), qPrintable(field.key));
        }
    }
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
