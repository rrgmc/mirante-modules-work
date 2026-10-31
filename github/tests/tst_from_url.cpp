// Items from a pasted link: what each type makes of a GitHub page, for every
// row of README.md#items-from-a-link, on github.com and on GitHub Enterprise
// Server, and the pages that are refused.
#include "../GitHubUrls.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/ItemType.h>

#include <QJsonObject>
#include <QTest>

#include <optional>

namespace mirante::modules::github {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::UrlMatch;
using mirante::modules::github::matchUrl;

namespace {

const QStringList kAllTypes{
    QStringLiteral("github.pr.summary"),    QStringLiteral("github.pr.page"),    QStringLiteral("github.pr.list"),
    QStringLiteral("github.run"),           QStringLiteral("github.run.list"),   QStringLiteral("github.issue.summary"),
    QStringLiteral("github.issue.page"),    QStringLiteral("github.issue.list"),
};

QUrl url(const char *text) { return QUrl(QString::fromUtf8(text)); }

// The ids of the types that take the address, in kAllTypes' order.
QStringList typesTaking(const QUrl &address, const QList<QUrl> &webBases = {})
{
    QStringList ids;
    for (const QString &type : kAllTypes) {
        if (matchUrl(type, address, webBases))
            ids << type;
    }
    return ids;
}

} // namespace

class TestFromUrl : public QObject
{
    Q_OBJECT

private slots:
    void pullRequest()
    {
        const QUrl page = url("https://github.com/acme/backend/pull/42");
        for (const char *type : {"github.pr.summary", "github.pr.page"}) {
            const std::optional<UrlMatch> match = matchUrl(QString::fromUtf8(type), page, {});
            QVERIFY(match);
            QCOMPARE(match->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                                 {QStringLiteral("number"), 42}}));
            QCOMPARE(match->name, QStringLiteral("acme/backend#42"));
            QCOMPARE(match->baseUrl, url("https://github.com"));
            QVERIFY(!match->generic);
        }
        QCOMPARE(typesTaking(page), (QStringList{QStringLiteral("github.pr.summary"), QStringLiteral("github.pr.page"),
                                                 QStringLiteral("github.run")}));
    }

    // A tab of the page, a query and a fragment do not change the item. The
    // repository keeps its case.
    void pullRequestTabs()
    {
        const auto match = matchUrl(QStringLiteral("github.pr.page"),
                                    url("https://github.com/Acme/Backend/pull/7/files?diff=split#r123"), {});
        QVERIFY(match);
        QCOMPARE(match->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("Acme/Backend")},
                                             {QStringLiteral("number"), 7}}));
        QCOMPARE(match->name, QStringLiteral("Acme/Backend#7"));
        QVERIFY(matchUrl(QStringLiteral("github.pr.summary"), url("https://github.com/acme/backend/pull/7/checks"), {}));
    }

    // A pull request's run follows that pull request.
    void runOfPullRequest()
    {
        const auto match =
            matchUrl(QStringLiteral("github.run"), url("https://github.com/acme/backend/pull/42/commits"), {});
        QVERIFY(match);
        QCOMPARE(match->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                             {QStringLiteral("branchSource"), QStringLiteral("pullRequest")},
                                             {QStringLiteral("number"), 42}}));
        QCOMPARE(match->name, QStringLiteral("acme/backend#42 CI"));
    }

    void issue()
    {
        const QUrl page = url("https://github.com/acme/backend/issues/7#issuecomment-1");
        for (const char *type : {"github.issue.summary", "github.issue.page"}) {
            const auto match = matchUrl(QString::fromUtf8(type), page, {});
            QVERIFY(match);
            QCOMPARE(match->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                                 {QStringLiteral("number"), 7}}));
            QCOMPARE(match->name, QStringLiteral("acme/backend#7"));
        }
        // An issue has no run, and is not a pull request.
        QCOMPARE(typesTaking(page),
                 (QStringList{QStringLiteral("github.issue.summary"), QStringLiteral("github.issue.page")}));
    }

    void repositoryLists()
    {
        const QUrl pullsPage = url("https://github.com/acme/backend/pulls?q=is%3Aopen");
        const auto pulls = matchUrl(QStringLiteral("github.pr.list"), pullsPage, {});
        QVERIFY(pulls);
        QCOMPARE(pulls->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                             {QStringLiteral("scope"), QStringLiteral("repository")},
                                             {QStringLiteral("preset"), QStringLiteral("allOpen")}}));
        QCOMPARE(pulls->name, QStringLiteral("acme/backend PRs"));
        QCOMPARE(typesTaking(pullsPage), QStringList{QStringLiteral("github.pr.list")});

        const QUrl issuesPage = url("https://github.com/acme/backend/issues");
        const auto issues = matchUrl(QStringLiteral("github.issue.list"), issuesPage, {});
        QVERIFY(issues);
        QCOMPARE(issues->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                              {QStringLiteral("scope"), QStringLiteral("repository")},
                                              {QStringLiteral("preset"), QStringLiteral("allOpen")}}));
        QCOMPARE(issues->name, QStringLiteral("acme/backend issues"));
        QCOMPARE(typesTaking(issuesPage), QStringList{QStringLiteral("github.issue.list")});
    }

    // The Actions page gives the default branch's runs and the list of runs.
    void actions()
    {
        const QUrl page = url("https://github.com/acme/backend/actions");
        const auto run = matchUrl(QStringLiteral("github.run"), page, {});
        QVERIFY(run);
        QCOMPARE(run->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                           {QStringLiteral("branchSource"), QStringLiteral("fixed")},
                                           {QStringLiteral("branch"), QString()}}));
        QCOMPARE(run->name, QStringLiteral("acme/backend CI"));

        const auto list = matchUrl(QStringLiteral("github.run.list"), page, {});
        QVERIFY(list);
        QCOMPARE(list->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                            {QStringLiteral("source"), QStringLiteral("all")}}));
        QCOMPARE(list->name, QStringLiteral("acme/backend runs"));

        QCOMPARE(typesTaking(page), (QStringList{QStringLiteral("github.run"), QStringLiteral("github.run.list")}));
    }

    void workflow()
    {
        const QUrl page = url("https://github.com/acme/backend/actions/workflows/ci.yml?query=branch%3Amain");
        const auto run = matchUrl(QStringLiteral("github.run"), page, {});
        QVERIFY(run);
        QCOMPARE(run->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                           {QStringLiteral("branchSource"), QStringLiteral("fixed")},
                                           {QStringLiteral("branch"), QString()},
                                           {QStringLiteral("workflow"), QStringLiteral("ci.yml")}}));
        QCOMPARE(run->name, QStringLiteral("acme/backend ci.yml CI"));

        const auto list = matchUrl(QStringLiteral("github.run.list"), page, {});
        QVERIFY(list);
        QCOMPARE(list->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                            {QStringLiteral("source"), QStringLiteral("all")},
                                            {QStringLiteral("workflow"), QStringLiteral("ci.yml")}}));
        QCOMPARE(list->name, QStringLiteral("acme/backend ci.yml runs"));

        // The workflows page without a file is not one.
        QVERIFY(typesTaking(url("https://github.com/acme/backend/actions/workflows")).isEmpty());
    }

    // One run gives the list of runs only; its id is not kept.
    void oneRun()
    {
        for (const char *text : {"https://github.com/acme/backend/actions/runs/123456",
                                 "https://github.com/acme/backend/actions/runs/123456/job/789",
                                 "https://github.com/acme/backend/actions/runs/123456/attempts/2"}) {
            const QUrl page = url(text);
            QCOMPARE(typesTaking(page), QStringList{QStringLiteral("github.run.list")});
            const auto list = matchUrl(QStringLiteral("github.run.list"), page, {});
            QCOMPARE(list->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                                {QStringLiteral("source"), QStringLiteral("all")}}));
            QCOMPARE(list->name, QStringLiteral("acme/backend runs"));
        }
    }

    // A branch's tree follows that branch, slashes included.
    void tree()
    {
        const auto plain = matchUrl(QStringLiteral("github.run"), url("https://github.com/acme/backend/tree/main"), {});
        QVERIFY(plain);
        QCOMPARE(plain->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                                             {QStringLiteral("branchSource"), QStringLiteral("fixed")},
                                             {QStringLiteral("branch"), QStringLiteral("main")}}));
        QCOMPARE(plain->name, QStringLiteral("acme/backend main CI"));

        const QUrl slashed = url("https://github.com/acme/backend/tree/feature/login/v2");
        const auto match = matchUrl(QStringLiteral("github.run"), slashed, {});
        QVERIFY(match);
        QCOMPARE(match->config.value(QStringLiteral("branch")).toString(), QStringLiteral("feature/login/v2"));
        QCOMPARE(match->name, QStringLiteral("acme/backend feature/login/v2 CI"));
        QCOMPARE(typesTaking(slashed), QStringList{QStringLiteral("github.run")});

        // An encoded slash and an encoded percent sign are decoded once.
        const auto encoded =
            matchUrl(QStringLiteral("github.run"), url("https://github.com/acme/backend/tree/fix%2F100%2525"), {});
        QVERIFY(encoded);
        QCOMPARE(encoded->config.value(QStringLiteral("branch")).toString(), QStringLiteral("fix/100%25"));
    }

    // On the web base itself, without an owner: the user's own lists in all
    // repositories.
    void ownLists_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("name");
        QTest::newRow("pulls") << QStringLiteral("https://github.com/pulls") << QStringLiteral("github.pr.list")
                               << QStringLiteral("createdByMe") << QStringLiteral("My PRs");
        QTest::newRow("pulls assigned") << QStringLiteral("https://github.com/pulls/assigned")
                                        << QStringLiteral("github.pr.list") << QStringLiteral("assignedToMe")
                                        << QStringLiteral("PRs assigned to me");
        QTest::newRow("pulls to review") << QStringLiteral("https://github.com/pulls/review-requested?q=x")
                                         << QStringLiteral("github.pr.list") << QStringLiteral("reviewRequested")
                                         << QStringLiteral("PRs to review");
        QTest::newRow("issues") << QStringLiteral("https://github.com/issues/") << QStringLiteral("github.issue.list")
                                << QStringLiteral("createdByMe") << QStringLiteral("My issues");
        QTest::newRow("issues assigned") << QStringLiteral("https://github.com/issues/assigned")
                                         << QStringLiteral("github.issue.list") << QStringLiteral("assignedToMe")
                                         << QStringLiteral("Issues assigned to me");
        QTest::newRow("enterprise pulls") << QStringLiteral("https://example.org/github/pulls/assigned")
                                          << QStringLiteral("github.pr.list") << QStringLiteral("assignedToMe")
                                          << QStringLiteral("PRs assigned to me");
    }

    void ownLists()
    {
        QFETCH(QString, address);
        QFETCH(QString, type);
        QFETCH(QString, preset);
        QFETCH(QString, name);
        const QList<QUrl> bases{url("https://example.org/github/")};
        const auto match = matchUrl(type, QUrl(address), bases);
        QVERIFY(match);
        QCOMPARE(match->config, (QJsonObject{{QStringLiteral("scope"), QStringLiteral("all")},
                                             {QStringLiteral("preset"), preset}}));
        QCOMPARE(match->name, name);
        QCOMPARE(typesTaking(QUrl(address), bases), QStringList{type});
    }

    // GitHub Enterprise Server at the profile's web base, at a host's root or
    // at a path, which is not taken for an owner.
    void enterprise()
    {
        const QList<QUrl> atRoot{url("https://github.example.com")};
        const auto root = matchUrl(QStringLiteral("github.pr.summary"),
                                   url("https://github.example.com/ops/infra/pull/3"), atRoot);
        QVERIFY(root);
        QCOMPARE(root->config.value(QStringLiteral("repository")).toString(), QStringLiteral("ops/infra"));
        QCOMPARE(root->baseUrl, url("https://github.example.com"));

        // The longest base the address is below wins.
        const QList<QUrl> bases{url("https://example.org"), url("https://example.org/github/")};
        const auto atPath = matchUrl(QStringLiteral("github.issue.page"),
                                     url("https://example.org/github/ops/infra/issues/9"), bases);
        QVERIFY(atPath);
        QCOMPARE(atPath->config, (QJsonObject{{QStringLiteral("repository"), QStringLiteral("ops/infra")},
                                              {QStringLiteral("number"), 9}}));
        QCOMPARE(atPath->name, QStringLiteral("ops/infra#9"));
        QCOMPARE(atPath->baseUrl, url("https://example.org/github/"));

        const auto tree = matchUrl(QStringLiteral("github.run"),
                                   url("https://example.org/github/ops/infra/tree/release/1.0"), bases);
        QVERIFY(tree);
        QCOMPARE(tree->config.value(QStringLiteral("branch")).toString(), QStringLiteral("release/1.0"));

        // github.com is read whatever the profile's server.
        const auto public_ = matchUrl(QStringLiteral("github.pr.page"), url("https://github.com/a/b/pull/1"), bases);
        QVERIFY(public_);
        QCOMPARE(public_->baseUrl, url("https://github.com"));
        QVERIFY(matchUrl(QStringLiteral("github.pr.page"), url("https://www.github.com/a/b/pull/1"), bases));

        // Another server, not the profile's, is not GitHub as far as we know.
        QVERIFY(typesTaking(url("https://github.other.net/ops/infra/pull/3"), atRoot).isEmpty());
        // Nor is a page of the server outside its path.
        QVERIFY(typesTaking(url("https://example.org/ops/infra/pull/3"), {url("https://example.org/github/")})
                    .isEmpty());
    }

    void refused_data()
    {
        QTest::addColumn<QString>("address");
        QTest::newRow("repository home") << QStringLiteral("https://github.com/acme/backend");
        QTest::newRow("owner") << QStringLiteral("https://github.com/acme");
        QTest::newRow("web base") << QStringLiteral("https://github.com/");
        QTest::newRow("compare") << QStringLiteral("https://github.com/acme/backend/compare/main...feature");
        QTest::newRow("new pull request") << QStringLiteral("https://github.com/acme/backend/pull/new/feature");
        QTest::newRow("pull request zero") << QStringLiteral("https://github.com/acme/backend/pull/0");
        QTest::newRow("pull without number") << QStringLiteral("https://github.com/acme/backend/pull");
        QTest::newRow("new issue") << QStringLiteral("https://github.com/acme/backend/issues/new");
        QTest::newRow("pulls with more") << QStringLiteral("https://github.com/acme/backend/pulls/x");
        QTest::newRow("actions other") << QStringLiteral("https://github.com/acme/backend/actions/new");
        QTest::newRow("run without id") << QStringLiteral("https://github.com/acme/backend/actions/runs/x");
        QTest::newRow("badge") << QStringLiteral("https://github.com/acme/backend/actions/workflows/ci.yml/badge.svg");
        QTest::newRow("tree without branch") << QStringLiteral("https://github.com/acme/backend/tree");
        QTest::newRow("blob") << QStringLiteral("https://github.com/acme/backend/blob/main/README.md");
        QTest::newRow("commit") << QStringLiteral("https://github.com/acme/backend/commit/abc123");
        QTest::newRow("issues review requested") << QStringLiteral("https://github.com/issues/review-requested");
        QTest::newRow("pulls other") << QStringLiteral("https://github.com/pulls/mentioned");
        QTest::newRow("pulls too deep") << QStringLiteral("https://github.com/pulls/assigned/x");
        QTest::newRow("other server") << QStringLiteral("https://gitlab.com/acme/backend/pull/1");
        QTest::newRow("not the web") << QStringLiteral("ftp://github.com/acme/backend/pull/1");
        QTest::newRow("empty") << QString();
    }

    void refused()
    {
        QFETCH(QString, address);
        QVERIFY(typesTaking(QUrl(address)).isEmpty());
        QVERIFY(!matchUrl(QStringLiteral("github.unknown"), url("https://github.com/a/b/pull/1"), {}));
    }

    // The registered types answer through fromUrl(), and the item dialog,
    // which lists types by name, offers the run before the run list.
    void registeredTypes()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("github"), &mirante::modules::github::registerModule);
        const auto taking = [&registry](const QUrl &address) {
            QStringList ids;
            for (const mirante::ItemType *type : registry.types()) {
                if (type->fromUrl(address, {}))
                    ids << type->id();
            }
            return ids;
        };
        QCOMPARE(taking(url("https://github.com/acme/backend/actions")),
                 (QStringList{QStringLiteral("github.run"), QStringLiteral("github.run.list")}));
        QCOMPARE(taking(url("https://github.com/acme/backend/actions/workflows/ci.yml")),
                 (QStringList{QStringLiteral("github.run"), QStringLiteral("github.run.list")}));

        QStringList pull = taking(url("https://github.com/acme/backend/pull/42"));
        pull.sort();
        QCOMPARE(pull, (QStringList{QStringLiteral("github.pr.page"), QStringLiteral("github.pr.summary"),
                                    QStringLiteral("github.run")}));
    }

    // fromUrl() reads the web base of the profile the new item would use.
    void fromUrlReadsTheProfilesServer()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("github"), &mirante::modules::github::registerModule);
        registry.setSettingsReader([](const QString &profileId, const QString &, const QString &) {
            if (profileId == QLatin1String("work"))
                return QJsonObject{{QStringLiteral("baseUrl"), QStringLiteral("https://example.org/github/")}};
            return QJsonObject();
        });
        const mirante::ItemType &summary = registry.type(QStringLiteral("github.pr.summary"));
        const QUrl link(QStringLiteral("https://example.org/github/acme/app/pull/3"));
        const auto inWork = summary.fromUrl(link, QStringLiteral("work"));
        QVERIFY(inWork);
        QCOMPARE(inWork->config.value(QStringLiteral("repository")).toString(), QStringLiteral("acme/app"));
        QCOMPARE(inWork->baseUrl, QUrl(QStringLiteral("https://example.org/github/")));
        // A profile on github.com does not read another server.
        QVERIFY(!summary.fromUrl(link, QStringLiteral("default")));
        QVERIFY(summary.fromUrl(QUrl(QStringLiteral("https://github.com/acme/app/pull/3")), QStringLiteral("work")));
    }
};

QTEST_MAIN(TestFromUrl)
#include "tst_from_url.moc"
