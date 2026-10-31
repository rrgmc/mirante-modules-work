// The addresses of GitHub pages built from an item's configuration, and the
// reading of a pasted address.
#include "../GitHubUrls.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/ItemContext.h>
#include <mirante/ItemType.h>

#include <QJsonObject>
#include <QTest>

namespace mirante::modules::github {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using namespace mirante::modules::github;

namespace {

// Records what a page type asks the core to show.
class PageContext : public mirante::ItemContext
{
public:
    QJsonObject itemConfig;
    QJsonObject general;
    QJsonValue repositorySetting;
    QUrl webPage;
    QString message;

    QString itemName() const override { return QStringLiteral("Item"); }
    QJsonObject config() const override { return itemConfig; }
    QJsonValue projectSetting(const QString &key) const override
    {
        return key == QLatin1String("repository") ? repositorySetting : QJsonValue();
    }
    QJsonObject moduleSettings(const QString &screenId) const override
    {
        return screenId == QLatin1String("general") ? general : QJsonObject();
    }
    QString folderPath(const QString &) const override { return {}; }
    QWidget *createWebView(const QUrl &url, QWidget *) override
    {
        webPage = url;
        return nullptr;
    }
    QWebEngineProfile *profile() override { return nullptr; }
    QWidget *createMessageView(const QString &text, QWidget *) override
    {
        message = text;
        return nullptr;
    }
    void updateConfig(const QJsonObject &config) override { itemConfig = config; }
};

const QUrl kGitHub(QStringLiteral("https://github.com"));
const QUrl kEnterprise(QStringLiteral("https://example.org/github/"));

QJsonObject item(const QString &repository, const QJsonValue &number)
{
    return {{QStringLiteral("repository"), repository}, {QStringLiteral("number"), number}};
}

} // namespace

class TestUrls : public QObject
{
    Q_OBJECT

private slots:
    void configReaders()
    {
        QCOMPARE(repositoryOf(item(QStringLiteral(" /acme/backend/ "), 1), {}), QStringLiteral("acme/backend"));
        QCOMPARE(repositoryOf({}, QStringLiteral("acme/web")), QStringLiteral("acme/web"));
        QVERIFY(repositoryOf({}, {}).isEmpty());

        QCOMPARE(itemNumber(item({}, 42)), qint64(42));
        QCOMPARE(itemNumber(item({}, QStringLiteral(" 42 "))), qint64(42));
        QCOMPARE(itemNumber(item({}, -1)), qint64(0));
        QCOMPARE(itemNumber({}), qint64(0));

        QCOMPARE(configuredBranch({{QStringLiteral("branch"), QStringLiteral(" main ")}}), QStringLiteral("main"));
        QVERIFY(configuredBranch({}).isEmpty());
        QCOMPARE(configuredWorkflow({{QStringLiteral("workflow"), QStringLiteral(" ci.yml ")}}),
                 QStringLiteral("ci.yml"));
    }

    void pageAddresses()
    {
        QCOMPARE(pullRequestUrl(kGitHub, item(QStringLiteral("acme/backend"), 42), {}),
                 QUrl(QStringLiteral("https://github.com/acme/backend/pull/42")));
        QCOMPARE(issueUrl(kEnterprise, item({}, 7), QStringLiteral("acme/backend")),
                 QUrl(QStringLiteral("https://example.org/github/acme/backend/issues/7")));
        QVERIFY(pullRequestUrl(kGitHub, item({}, 42), {}).isEmpty());
        QVERIFY(issueUrl(kGitHub, item(QStringLiteral("acme/backend"), 0), {}).isEmpty());
        QCOMPARE(webPageUrl(kEnterprise, QStringLiteral("/acme/backend/actions")),
                 QUrl(QStringLiteral("https://example.org/github/acme/backend/actions")));
    }

    void readsPageAddresses()
    {
        std::optional<PageAddress> address =
            readPageAddress(QUrl(QStringLiteral("https://github.com/Acme/Backend/pull/42/files?x=1#y")), {});
        QVERIFY(address);
        QCOMPARE(address->webBase, kGitHub);
        QCOMPARE(address->repository, QStringLiteral("Acme/Backend"));
        QCOMPARE(address->kind, QStringLiteral("pull"));
        QCOMPARE(address->number, qint64(42));

        address = readPageAddress(QUrl(QStringLiteral("https://example.org/github/acme/backend/tree/feature/a-b")),
                                  {kEnterprise});
        QVERIFY(address);
        QCOMPARE(address->webBase, kEnterprise);
        QCOMPARE(address->kind, QStringLiteral("tree"));
        QCOMPARE(address->branch, QStringLiteral("feature/a-b"));

        address = readPageAddress(QUrl(QStringLiteral("https://github.com/acme/backend/actions/workflows/ci.yml")), {});
        QVERIFY(address);
        QCOMPARE(address->kind, QStringLiteral("actions"));
        QCOMPARE(address->workflow, QStringLiteral("ci.yml"));

        address = readPageAddress(QUrl(QStringLiteral("https://github.com/acme/backend/actions/runs/123/job/4")), {});
        QVERIFY(address);
        QCOMPARE(address->number, qint64(123));

        address = readPageAddress(QUrl(QStringLiteral("https://github.com/pulls/review-requested")), {});
        QVERIFY(address);
        QVERIFY(address->repository.isEmpty());
        QCOMPARE(address->kind, QStringLiteral("pulls"));
        QCOMPARE(address->filter, QStringLiteral("review-requested"));
    }

    void refusesOtherPages_data()
    {
        QTest::addColumn<QString>("url");
        QTest::newRow("home page") << QStringLiteral("https://github.com/acme/backend");
        QTest::newRow("new pull request") << QStringLiteral("https://github.com/acme/backend/pull/new/feature");
        QTest::newRow("compare") << QStringLiteral("https://github.com/acme/backend/compare/main...feature");
        QTest::newRow("new issue") << QStringLiteral("https://github.com/acme/backend/issues/new");
        QTest::newRow("other server") << QStringLiteral("https://gitlab.com/acme/backend/pull/1");
        QTest::newRow("issues review requested") << QStringLiteral("https://github.com/issues/review-requested");
        QTest::newRow("not the web") << QStringLiteral("ftp://github.com/acme/backend/pull/1");
    }

    void refusesOtherPages()
    {
        QFETCH(QString, url);
        QVERIFY(!readPageAddress(QUrl(url), {kEnterprise}));
    }

    // A base with or without a slash at its end, and a page with one at its
    // start, give one slash between them.
    void noDoubledSlash()
    {
        const QUrl expected(QStringLiteral("https://github.example.com/acme/backend/pull/1"));
        QCOMPARE(pullRequestUrl(QUrl(QStringLiteral("https://github.example.com")),
                                item(QStringLiteral("acme/backend"), 1), {}),
                 expected);
        QCOMPARE(pullRequestUrl(QUrl(QStringLiteral("https://github.example.com/")),
                                item(QStringLiteral("/acme/backend/"), 1), {}),
                 expected);
        QCOMPARE(webPageUrl(QUrl(QStringLiteral("https://github.example.com//")), QStringLiteral("//pulls")),
                 QUrl(QStringLiteral("https://github.example.com/pulls")));
        // A query or a fragment of the base is not kept.
        QCOMPARE(webPageUrl(QUrl(QStringLiteral("https://github.com/?tab=1#top")), QStringLiteral("acme/backend")),
                 QUrl(QStringLiteral("https://github.com/acme/backend")));
        // The item's repository wins over the project's.
        QCOMPARE(issueUrl(kGitHub, item(QStringLiteral("acme/backend"), 3), QStringLiteral("acme/web")),
                 QUrl(QStringLiteral("https://github.com/acme/backend/issues/3")));
    }

    // The two-part reading of a repository and one of its pages on the
    // web base, with and without a number.
    void readsListsAndRuns()
    {
        std::optional<PageAddress> address =
            readPageAddress(QUrl(QStringLiteral("https://github.com/acme/backend/issues")), {});
        QVERIFY(address);
        QCOMPARE(address->kind, QStringLiteral("issues"));
        QCOMPARE(address->number, qint64(0));

        address = readPageAddress(QUrl(QStringLiteral("https://github.com/acme/backend/pulls")), {});
        QVERIFY(address);
        QCOMPARE(address->kind, QStringLiteral("pulls"));
        QCOMPARE(address->repository, QStringLiteral("acme/backend"));

        address = readPageAddress(QUrl(QStringLiteral("https://github.com/acme/backend/actions")), {});
        QVERIFY(address);
        QCOMPARE(address->kind, QStringLiteral("actions"));
        QVERIFY(address->workflow.isEmpty());
        QCOMPARE(address->number, qint64(0));

        // On the web base itself, without a filter.
        address = readPageAddress(QUrl(QStringLiteral("https://example.org/github/issues")), {kEnterprise});
        QVERIFY(address);
        QCOMPARE(address->webBase, kEnterprise);
        QVERIFY(address->repository.isEmpty());
        QVERIFY(address->filter.isEmpty());
    }

    // The page types show the page on the profile's web base, or a message
    // when the server or the configuration is missing.
    void pageViews_data()
    {
        QTest::addColumn<QString>("typeId");
        QTest::addColumn<QString>("kind");
        QTest::newRow("pull request") << QStringLiteral("github.pr.page") << QStringLiteral("pull");
        QTest::newRow("issue") << QStringLiteral("github.issue.page") << QStringLiteral("issues");
    }

    void pageViews()
    {
        QFETCH(QString, typeId);
        QFETCH(QString, kind);
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("github"), &registerModule);
        const mirante::ItemType &type = registry.type(typeId);

        // github.com without settings.
        PageContext context;
        context.itemConfig = item(QStringLiteral("acme/backend"), 42);
        type.createView(context, nullptr);
        QCOMPARE(context.webPage, QUrl(QStringLiteral("https://github.com/acme/backend/") + kind
                                       + QStringLiteral("/42")));
        QVERIFY(context.message.isEmpty());

        // An Enterprise server at a path, and the project's repository.
        PageContext enterprise;
        enterprise.general = {{QStringLiteral("baseUrl"), QStringLiteral("https://example.org/github")}};
        enterprise.itemConfig = item({}, 7);
        enterprise.repositorySetting = QStringLiteral("ops/infra");
        type.createView(enterprise, nullptr);
        QCOMPARE(enterprise.webPage, QUrl(QStringLiteral("https://example.org/github/ops/infra/") + kind
                                          + QStringLiteral("/7")));

        PageContext badBase;
        badBase.general = {{QStringLiteral("baseUrl"), QStringLiteral("ftp://example.org")}};
        badBase.itemConfig = item(QStringLiteral("acme/backend"), 42);
        type.createView(badBase, nullptr);
        QVERIFY(badBase.webPage.isEmpty());
        QCOMPARE(badBase.message, QStringLiteral("Set the GitHub base URL in this profile's Modules > GitHub > "
                                                 "General Configuration, in the Profiles menu."));

        const QString incomplete = QStringLiteral("Set the repository and number in the item's settings.");
        PageContext noRepository;
        noRepository.itemConfig = item({}, 42);
        type.createView(noRepository, nullptr);
        QVERIFY(noRepository.webPage.isEmpty());
        QCOMPARE(noRepository.message, incomplete);

        PageContext noNumber;
        noNumber.itemConfig = item(QStringLiteral("acme/backend"), {});
        type.createView(noNumber, nullptr);
        QCOMPARE(noNumber.message, incomplete);
    }
};

QTEST_MAIN(TestUrls)
#include "tst_urls.moc"
