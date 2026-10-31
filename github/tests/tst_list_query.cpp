// The GitHub lists: their search queries, their sizes, their addresses on the
// site, their descriptions and their status, on responses in tests/data/ written
// by hand in the shape of GitHub's API.
#include "../GitHubLists.h"

#include <mirante/testing/FakeStatusJob.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeStatusJob;
using namespace mirante::modules::github;

namespace {

const QString kUser = QStringLiteral("GET user");
const QString kRepository = QStringLiteral("GET repos/acme/backend");
const QString kPullRequest = QStringLiteral("GET repos/acme/backend/pulls/42");
const QString kRuns = QStringLiteral("GET repos/acme/backend/actions/runs");
const QString kSearch = QStringLiteral("GET search/issues");

QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("tests/data/") + name);
}

QJsonObject readObject(const QString &name)
{
    QFile file(dataFile(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll()).object();
}

// So many rows, wrapped under key as GitHub answers them.
QJsonDocument wrapped(const QString &key, int count)
{
    QJsonArray array;
    for (int i = 0; i < count; ++i)
        array.append(QJsonObject{{QStringLiteral("number"), i + 1}});
    return QJsonDocument(QJsonObject{{QStringLiteral("total_count"), count * 3}, {key, array}});
}

// The request of a search, with GitHub's text encoding: "is:pr" is "is%3Apr".
QString searchKey(const QString &encodedQuery, int perPage = 100)
{
    return kSearch + QStringLiteral("?q=") + encodedQuery + QStringLiteral("&sort=updated&order=desc&per_page=")
        + QString::number(perPage);
}

QJsonObject listConfig(const QString &scope, const QString &preset, const QString &filter = {})
{
    QJsonObject config{{QStringLiteral("scope"), scope}, {QStringLiteral("preset"), preset}};
    if (scope == QLatin1String("repository"))
        config.insert(QStringLiteral("repository"), QStringLiteral("acme/backend"));
    else if (scope == QLatin1String("owner"))
        config.insert(QStringLiteral("owner"), QStringLiteral("acme"));
    if (!filter.isEmpty())
        config.insert(QStringLiteral("filter"), filter);
    return config;
}

QJsonObject runList(const QString &source)
{
    return QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")},
                       {QStringLiteral("source"), source}};
}

ApiReply forbiddenReply()
{
    ApiReply reply;
    reply.error = ApiReply::Error::Forbidden;
    reply.httpStatus = 403;
    reply.message = QStringLiteral("Forbidden (403)");
    return reply;
}

// Waits for the job to finish, and checks it finished once.
#define WAIT_FOR_FINISH(job)                                                                                         \
    do {                                                                                                             \
        QTRY_COMPARE((job).finishCount, 1);                                                                          \
        QCoreApplication::processEvents();                                                                           \
        QCOMPARE((job).finishCount, 1);                                                                              \
    } while (false)

} // namespace

class TestListQuery : public QObject
{
    Q_OBJECT

private slots:
    // Sizes.

    void sizes_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<int>("number");

        QTest::newRow("absent") << QJsonValue(QJsonValue::Undefined) << 0;
        QTest::newRow("a number") << QJsonValue(30) << 30;
        QTest::newRow("one") << QJsonValue(1) << 1;
        QTest::newRow("zero") << QJsonValue(0) << 0;
        QTest::newRow("below zero") << QJsonValue(-5) << 0;
        QTest::newRow("null") << QJsonValue() << 0;
        QTest::newRow("as text") << QJsonValue(QStringLiteral(" 30 ")) << 30;
        QTest::newRow("other text") << QJsonValue(QStringLiteral("many")) << 0;
    }

    // Zero stands for the default, which differs per field.
    void sizes()
    {
        QFETCH(QJsonValue, value);
        QFETCH(int, number);

        QJsonObject config;
        if (!value.isUndefined()) {
            config.insert(QStringLiteral("limit"), value);
            config.insert(QStringLiteral("pageSize"), value);
        }
        QCOMPARE(listLimit(config), number > 0 ? number : 100);
        QCOMPARE(listPageSize(config), number > 0 ? number : 50);
    }

    // The search query.

    void searchQueries_data()
    {
        QTest::addColumn<QString>("scope");
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("pullRequests");
        QTest::addColumn<QString>("issues");

        // Issues have no reviewRequested, so it counts as their first preset.
        QTest::newRow("repository, created by me")
            << "repository" << "createdByMe" << "is:pr is:open repo:acme/backend author:@me"
            << "is:issue is:open repo:acme/backend author:@me";
        QTest::newRow("repository, review requested")
            << "repository" << "reviewRequested" << "is:pr is:open repo:acme/backend review-requested:@me"
            << "is:issue is:open repo:acme/backend assignee:@me";
        QTest::newRow("repository, assigned to me")
            << "repository" << "assignedToMe" << "is:pr is:open repo:acme/backend assignee:@me"
            << "is:issue is:open repo:acme/backend assignee:@me";
        QTest::newRow("repository, all open") << "repository" << "allOpen" << "is:pr is:open repo:acme/backend"
                                              << "is:issue is:open repo:acme/backend";
        QTest::newRow("owner, created by me") << "owner" << "createdByMe" << "is:pr is:open user:acme author:@me"
                                              << "is:issue is:open user:acme author:@me";
        QTest::newRow("owner, review requested")
            << "owner" << "reviewRequested" << "is:pr is:open user:acme review-requested:@me"
            << "is:issue is:open user:acme assignee:@me";
        QTest::newRow("owner, assigned to me") << "owner" << "assignedToMe" << "is:pr is:open user:acme assignee:@me"
                                               << "is:issue is:open user:acme assignee:@me";
        QTest::newRow("owner, all open") << "owner" << "allOpen" << "is:pr is:open user:acme"
                                         << "is:issue is:open user:acme";
        QTest::newRow("all, created by me") << "all" << "createdByMe" << "is:pr is:open author:@me"
                                            << "is:issue is:open author:@me";
        QTest::newRow("all, review requested") << "all" << "reviewRequested" << "is:pr is:open review-requested:@me"
                                               << "is:issue is:open assignee:@me";
        QTest::newRow("all, assigned to me") << "all" << "assignedToMe" << "is:pr is:open assignee:@me"
                                             << "is:issue is:open assignee:@me";
        QTest::newRow("all, all open") << "all" << "allOpen" << "is:pr is:open involves:@me"
                                       << "is:issue is:open involves:@me";
        QTest::newRow("no preset") << "repository" << "" << "is:pr is:open repo:acme/backend author:@me"
                                   << "is:issue is:open repo:acme/backend assignee:@me";
        QTest::newRow("unknown preset") << "all" << "starred" << "is:pr is:open author:@me"
                                        << "is:issue is:open assignee:@me";
    }

    void searchQueries()
    {
        QFETCH(QString, scope);
        QFETCH(QString, preset);
        QFETCH(QString, pullRequests);
        QFETCH(QString, issues);
        const QJsonObject config = listConfig(scope, preset);
        QCOMPARE(searchQuery(ListKind::PullRequests, config, QJsonValue()), pullRequests);
        QCOMPARE(searchQuery(ListKind::Issues, config, QJsonValue()), issues);
    }

    // The filter comes last, trimmed and otherwise as typed.
    void filterComesLast()
    {
        const QJsonObject config =
            listConfig(QStringLiteral("owner"), QStringLiteral("reviewRequested"), QStringLiteral("  label:bug  base:main "));
        QCOMPARE(searchQuery(ListKind::PullRequests, config, QJsonValue()),
                 QStringLiteral("is:pr is:open user:acme review-requested:@me label:bug  base:main"));
        const QJsonObject all = listConfig(QStringLiteral("all"), QStringLiteral("allOpen"), QStringLiteral("   "));
        QCOMPARE(searchQuery(ListKind::Issues, all, QJsonValue()), QStringLiteral("is:issue is:open involves:@me"));
    }

    // An absent scope is a repository, and an empty repository uses the
    // project's setting. Slashes and spaces around the names go.
    void scopeDefaults()
    {
        const QJsonValue setting(QStringLiteral("acme/website"));
        QCOMPARE(searchQuery(ListKind::PullRequests, {}, setting),
                 QStringLiteral("is:pr is:open repo:acme/website author:@me"));
        QCOMPARE(searchQuery(ListKind::Issues, QJsonObject{{QStringLiteral("repository"), QStringLiteral(" ")}}, setting),
                 QStringLiteral("is:issue is:open repo:acme/website assignee:@me"));
        QCOMPARE(searchQuery(ListKind::Issues,
                             QJsonObject{{QStringLiteral("repository"), QStringLiteral("/acme/backend/")}}, setting),
                 QStringLiteral("is:issue is:open repo:acme/backend assignee:@me"));
        QCOMPARE(searchQuery(ListKind::PullRequests,
                             QJsonObject{{QStringLiteral("scope"), QStringLiteral("owner")},
                                         {QStringLiteral("owner"), QStringLiteral(" /acme/ ")}},
                             setting),
                 QStringLiteral("is:pr is:open user:acme author:@me"));
        QCOMPARE(scopeProblem({}, setting), QString());
    }

    void incompleteScopes_data()
    {
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("message");

        const QString noRepository = QStringLiteral("Set the repository in the item's settings.");
        QTest::newRow("no scope") << QJsonObject() << noRepository;
        QTest::newRow("no repository")
            << QJsonObject{{QStringLiteral("scope"), QStringLiteral("repository")}} << noRepository;
        QTest::newRow("blank repository") << QJsonObject{{QStringLiteral("repository"), QStringLiteral(" / ")}}
                                          << noRepository;
        QTest::newRow("no owner") << QJsonObject{{QStringLiteral("scope"), QStringLiteral("owner")},
                                                 {QStringLiteral("repository"), QStringLiteral("acme/backend")}}
                                  << QStringLiteral("Set the owner in the item's settings.");
        QTest::newRow("unknown scope") << QJsonObject{{QStringLiteral("scope"), QStringLiteral("space")}}
                                       << QStringLiteral("Set the scope in the item's settings.");
    }

    // Without a repository or an owner there is no query, no Web link and no
    // request.
    void incompleteScopes()
    {
        QFETCH(QJsonObject, config);
        QFETCH(QString, message);
        const QJsonValue none;

        QCOMPARE(scopeProblem(config, none), message);
        QVERIFY(searchQuery(ListKind::PullRequests, config, none).isEmpty());
        QVERIFY(searchQuery(ListKind::Issues, config, none).isEmpty());
        QVERIFY(listWebUrl(ListKind::PullRequests, QUrl(QStringLiteral("https://github.com")), config, none).isEmpty());

        FakeStatusJob pullRequests;
        pullRequests.configValues = config;
        fetchPullRequestListStatus(pullRequests);
        QCOMPARE(pullRequests.finishCount, 1);
        QCOMPARE(pullRequests.finished->state, ItemStatus::State::None);
        QCOMPARE(pullRequests.finished->message, message);
        QVERIFY(!pullRequests.finished->closed.has_value());
        QVERIFY(pullRequests.client.requests().isEmpty());

        FakeStatusJob issues;
        issues.configValues = config;
        fetchIssueListStatus(issues);
        QCOMPARE(issues.finishCount, 1);
        QCOMPARE(issues.finished->state, ItemStatus::State::None);
        QCOMPARE(issues.finished->message, message);
        QVERIFY(issues.client.requests().isEmpty());
    }

    // Web links.

    void webAddresses_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("pullRequests");
        QTest::addColumn<QString>("issues");

        QTest::newRow("repository")
            << "https://github.com" << listConfig(QStringLiteral("repository"), QStringLiteral("allOpen"))
            << "https://github.com/acme/backend/pulls?q=is%3Apr%20is%3Aopen%20repo%3Aacme%2Fbackend"
            << "https://github.com/acme/backend/issues?q=is%3Aissue%20is%3Aopen%20repo%3Aacme%2Fbackend";
        QTest::newRow("owner")
            << "https://github.com" << listConfig(QStringLiteral("owner"), QStringLiteral("assignedToMe"))
            << "https://github.com/pulls?q=is%3Apr%20is%3Aopen%20user%3Aacme%20assignee%3A%40me"
            << "https://github.com/issues?q=is%3Aissue%20is%3Aopen%20user%3Aacme%20assignee%3A%40me";
        QTest::newRow("all") << "https://github.com" << listConfig(QStringLiteral("all"), QStringLiteral("allOpen"))
                             << "https://github.com/pulls?q=is%3Apr%20is%3Aopen%20involves%3A%40me"
                             << "https://github.com/issues?q=is%3Aissue%20is%3Aopen%20involves%3A%40me";
        QTest::newRow("filter with + and &")
            << "https://github.com/"
            << listConfig(QStringLiteral("all"), QStringLiteral("createdByMe"), QStringLiteral("label:\"a&b\" c+d"))
            << "https://github.com/pulls?q=is%3Apr%20is%3Aopen%20author%3A%40me%20label%3A%22a%26b%22%20c%2Bd"
            << "https://github.com/issues?q=is%3Aissue%20is%3Aopen%20author%3A%40me%20label%3A%22a%26b%22%20c%2Bd";
        QTest::newRow("server with a path")
            << "https://git.example.org/github/" << listConfig(QStringLiteral("repository"), QStringLiteral("allOpen"))
            << "https://git.example.org/github/acme/backend/pulls?q=is%3Apr%20is%3Aopen%20repo%3Aacme%2Fbackend"
            << "https://git.example.org/github/acme/backend/issues?q=is%3Aissue%20is%3Aopen%20repo%3Aacme%2Fbackend";
        QTest::newRow("repository from the setting")
            << "https://github.com" << QJsonObject{{QStringLiteral("preset"), QStringLiteral("allOpen")}}
            << "https://github.com/acme/website/pulls?q=is%3Apr%20is%3Aopen%20repo%3Aacme%2Fwebsite"
            << "https://github.com/acme/website/issues?q=is%3Aissue%20is%3Aopen%20repo%3Aacme%2Fwebsite";
    }

    void webAddresses()
    {
        QFETCH(QString, base);
        QFETCH(QJsonObject, config);
        QFETCH(QString, pullRequests);
        QFETCH(QString, issues);
        const QJsonValue setting(QStringLiteral("acme/website"));
        QCOMPARE(listWebUrl(ListKind::PullRequests, QUrl(base), config, setting).toString(QUrl::FullyEncoded),
                 pullRequests);
        QCOMPARE(listWebUrl(ListKind::Issues, QUrl(base), config, setting).toString(QUrl::FullyEncoded), issues);
    }

    void noWebAddressWithoutAServer()
    {
        QVERIFY(listWebUrl(ListKind::Issues, QUrl(), listConfig(QStringLiteral("all"), QStringLiteral("allOpen")),
                           QJsonValue())
                    .isEmpty());
        QVERIFY(runListWebUrl(QUrl(), runList(QStringLiteral("all")), QJsonValue()).isEmpty());
    }

    void runWebAddresses()
    {
        const QUrl base(QStringLiteral("https://github.com/"));
        QCOMPARE(runListWebUrl(base, runList(QStringLiteral("all")), QJsonValue()).toString(),
                 QStringLiteral("https://github.com/acme/backend/actions"));
        QJsonObject workflow = runList(QStringLiteral("branch"));
        workflow.insert(QStringLiteral("workflow"), QStringLiteral(" ci.yml "));
        QCOMPARE(runListWebUrl(base, workflow, QJsonValue()).toString(),
                 QStringLiteral("https://github.com/acme/backend/actions/workflows/ci.yml"));
        QCOMPARE(runListWebUrl(QUrl(QStringLiteral("https://git.example.org/github")), {},
                               QJsonValue(QStringLiteral("acme/website")))
                     .toString(),
                 QStringLiteral("https://git.example.org/github/acme/website/actions"));
        QVERIFY(runListWebUrl(base, QJsonObject{{QStringLiteral("workflow"), QStringLiteral("ci.yml")}}, QJsonValue())
                    .isEmpty());
    }

    // Descriptions.

    void descriptions()
    {
        const QJsonValue none;
        QCOMPARE(listDescription(ListKind::PullRequests,
                                 listConfig(QStringLiteral("repository"), QStringLiteral("reviewRequested")), none),
                 QStringLiteral("Review requested from me · acme/backend"));
        QCOMPARE(listDescription(ListKind::Issues,
                                 listConfig(QStringLiteral("owner"), QStringLiteral(""), QStringLiteral(" label:bug ")),
                                 none),
                 QStringLiteral("Assigned to me · owner acme · label:bug"));
        QCOMPARE(listDescription(ListKind::PullRequests, listConfig(QStringLiteral("all"), QStringLiteral("allOpen")),
                                 none),
                 QStringLiteral("All open · all repositories"));
        QCOMPARE(listDescription(ListKind::PullRequests, {}, QJsonValue(QStringLiteral("acme/website"))),
                 QStringLiteral("Created by me · acme/website"));
        QCOMPARE(listDescription(ListKind::Issues, QJsonObject{{QStringLiteral("preset"), QStringLiteral("reviewRequested")}},
                                 none),
                 QStringLiteral("Assigned to me"));
        QCOMPARE(listDescription(ListKind::Issues, listConfig(QStringLiteral("all"), QStringLiteral("createdByMe")), none),
                 QStringLiteral("Created by me · all repositories"));
    }

    void runDescriptions()
    {
        const QJsonValue none;
        QJsonObject pullRequest = runList(QStringLiteral("pullRequest"));
        pullRequest.insert(QStringLiteral("number"), 42);
        QCOMPARE(runListDescription(pullRequest, none), QStringLiteral("Of pull request #42 · acme/backend"));

        QJsonObject branch = runList(QStringLiteral("branch"));
        branch.insert(QStringLiteral("branch"), QStringLiteral("feature/login"));
        branch.insert(QStringLiteral("workflow"), QStringLiteral("ci.yml"));
        QCOMPARE(runListDescription(branch, none), QStringLiteral("Of branch feature/login · ci.yml · acme/backend"));

        QCOMPARE(runListDescription(QJsonObject{{QStringLiteral("source"), QStringLiteral("branch")}},
                                    QJsonValue(QStringLiteral("acme/website"))),
                 QStringLiteral("Of the default branch · acme/website"));
        QCOMPARE(runListDescription(runList(QStringLiteral("mine")), none),
                 QStringLiteral("Started by me · acme/backend"));
        QCOMPARE(runListDescription({}, none), QStringLiteral("All runs"));
    }

    // Pull request and issue lists.

    // Three rows, as GitHub gave them. No attention, and closed unset.
    void pullRequestList()
    {
        const QString key = searchKey(QStringLiteral("is%3Apr%20is%3Aopen%20review-requested%3A%40me"));
        FakeStatusJob job;
        job.configValues = listConfig(QStringLiteral("all"), QStringLiteral("reviewRequested"));
        QVERIFY(job.client.setJsonFile(key, dataFile(QStringLiteral("pr_search.json"))));
        fetchPullRequestListStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(status.message.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("3 pull request(s)"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!status.closed.has_value());
        QVERIFY(status.updatedAt.isValid());
        QCOMPARE(status.data.value(QStringLiteral("rows")).toArray(),
                 readObject(QStringLiteral("pr_search.json")).value(QStringLiteral("items")).toArray());
        QCOMPARE(status.data.value(QStringLiteral("total")).toInt(), 3);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
        const auto &options = job.client.requests().constLast().options;
        QVERIFY(options.followLinks);
        QCOMPARE(options.maxRows, 100);
        QCOMPARE(options.arrayKey, QStringLiteral("items"));
    }

    void issueList()
    {
        const QString key = searchKey(QStringLiteral("is%3Aissue%20is%3Aopen%20repo%3Aacme%2Fbackend%20assignee%3A%40me"));
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("")}};
        job.projectSettings = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")}};
        QVERIFY(job.client.setJsonFile(key, dataFile(QStringLiteral("issue_search.json"))));
        fetchIssueListStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("2 issue(s)"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!status.closed.has_value());
        QCOMPARE(status.data.value(QStringLiteral("rows")).toArray(),
                 readObject(QStringLiteral("issue_search.json")).value(QStringLiteral("items")).toArray());
        QCOMPARE(status.data.value(QStringLiteral("total")).toInt(), 2);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
    }

    // The filter reaches the request, encoded whole.
    void filterReachesTheRequest()
    {
        const QString key =
            searchKey(QStringLiteral("is%3Apr%20is%3Aopen%20user%3Aacme%20label%3A%22a%26b%22%20c%2Bd"));
        FakeStatusJob job;
        job.configValues =
            listConfig(QStringLiteral("owner"), QStringLiteral("allOpen"), QStringLiteral(" label:\"a&b\" c+d"));
        job.client.setJson(key, wrapped(QStringLiteral("items"), 1));
        fetchPullRequestListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->summary, QStringLiteral("1 pull request(s)"));
        QCOMPARE(job.client.requestKeys(), QStringList{key});
    }

    void summaries_data()
    {
        QTest::addColumn<int>("limit");
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("pullRequests");
        QTest::addColumn<QString>("issues");
        QTest::addColumn<QString>("runs");

        QTest::newRow("none") << 100 << 0 << "0 pull request(s)" << "0 issue(s)" << "0 run(s)";
        QTest::newRow("one") << 100 << 1 << "1 pull request(s)" << "1 issue(s)" << "1 run(s)";
        QTest::newRow("some") << 100 << 12 << "12 pull request(s)" << "12 issue(s)" << "12 run(s)";
        QTest::newRow("below the limit") << 100 << 99 << "99 pull request(s)" << "99 issue(s)" << "99 run(s)";
        QTest::newRow("at the limit") << 100 << 100 << "100+ pull request(s)" << "100+ issue(s)" << "100+ run(s)";
        QTest::newRow("at another limit") << 30 << 30 << "30+ pull request(s)" << "30+ issue(s)" << "30+ run(s)";
    }

    // The limit reaches each request, and "+" follows it. total is the number
    // of rows, not GitHub's total_count.
    void summaries()
    {
        QFETCH(int, limit);
        QFETCH(int, count);
        QFETCH(QString, pullRequests);
        QFETCH(QString, issues);
        QFETCH(QString, runs);

        FakeStatusJob pullRequestJob;
        pullRequestJob.configValues = listConfig(QStringLiteral("all"), QStringLiteral("createdByMe"));
        pullRequestJob.configValues.insert(QStringLiteral("limit"), limit);
        pullRequestJob.client.setJson(searchKey(QStringLiteral("is%3Apr%20is%3Aopen%20author%3A%40me"), limit),
                                      wrapped(QStringLiteral("items"), count));
        fetchPullRequestListStatus(pullRequestJob);
        WAIT_FOR_FINISH(pullRequestJob);
        QCOMPARE(pullRequestJob.finished->summary, pullRequests);
        QCOMPARE(pullRequestJob.finished->data.value(QStringLiteral("total")).toInt(), count);
        QCOMPARE(pullRequestJob.client.requests().constLast().options.maxRows, limit);

        FakeStatusJob issueJob;
        issueJob.configValues = listConfig(QStringLiteral("all"), QStringLiteral("assignedToMe"));
        issueJob.configValues.insert(QStringLiteral("limit"), limit);
        issueJob.client.setJson(searchKey(QStringLiteral("is%3Aissue%20is%3Aopen%20assignee%3A%40me"), limit),
                                wrapped(QStringLiteral("items"), count));
        fetchIssueListStatus(issueJob);
        WAIT_FOR_FINISH(issueJob);
        QCOMPARE(issueJob.finished->summary, issues);
        QCOMPARE(issueJob.finished->data.value(QStringLiteral("total")).toInt(), count);

        FakeStatusJob runJob;
        runJob.configValues = runList(QStringLiteral("all"));
        runJob.configValues.insert(QStringLiteral("limit"), limit);
        runJob.client.setJson(kRuns + QStringLiteral("?per_page=%1").arg(limit),
                              wrapped(QStringLiteral("workflow_runs"), count));
        fetchRunListStatus(runJob);
        WAIT_FOR_FINISH(runJob);
        QCOMPARE(runJob.finished->summary, runs);
        QCOMPARE(runJob.finished->data.value(QStringLiteral("total")).toInt(), count);
        QCOMPARE(runJob.client.requests().constLast().options.maxRows, limit);
    }

    // More than a page of GitHub's allows, so the client follows the links.
    void limitAboveAPage()
    {
        FakeStatusJob job;
        job.configValues = listConfig(QStringLiteral("all"), QStringLiteral("createdByMe"));
        job.configValues.insert(QStringLiteral("limit"), 250);
        job.client.setJson(searchKey(QStringLiteral("is%3Apr%20is%3Aopen%20author%3A%40me")),
                           wrapped(QStringLiteral("items"), 100));
        fetchPullRequestListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.client.requests().constLast().options.maxRows, 250);
        QCOMPARE(job.finished->summary, QStringLiteral("100 pull request(s)"));
    }

    void failedReply()
    {
        FakeStatusJob pullRequests;
        pullRequests.configValues = listConfig(QStringLiteral("all"), QStringLiteral("createdByMe"));
        pullRequests.client.setReply(searchKey(QStringLiteral("is%3Apr%20is%3Aopen%20author%3A%40me")),
                                     forbiddenReply());
        fetchPullRequestListStatus(pullRequests);
        WAIT_FOR_FINISH(pullRequests);
        QCOMPARE(pullRequests.finished->state, ItemStatus::State::Error);
        QCOMPARE(pullRequests.finished->message, QStringLiteral("Forbidden (403)"));
        QVERIFY(pullRequests.finished->data.isEmpty());

        FakeStatusJob issues;
        issues.configValues = listConfig(QStringLiteral("all"), QStringLiteral("allOpen"));
        issues.client.failAll(ApiReply::Error::NoToken, QStringLiteral("Token not set on this device"));
        fetchIssueListStatus(issues);
        WAIT_FOR_FINISH(issues);
        QCOMPARE(issues.finished->state, ItemStatus::State::NoToken);
        QCOMPARE(issues.finished->message, QStringLiteral("Token not set on this device"));
    }

    // The run list.

    void runListSources_data()
    {
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("first");     // the request before the runs, or empty
        QTest::addColumn<QString>("firstData"); // its answer, a file in tests/data or inline JSON
        QTest::addColumn<QString>("key");

        QJsonObject branch = runList(QStringLiteral("branch"));
        branch.insert(QStringLiteral("branch"), QStringLiteral(" feature/a+b&c "));
        QJsonObject workflow = runList(QStringLiteral("all"));
        workflow.insert(QStringLiteral("workflow"), QStringLiteral("ci.yml"));
        QJsonObject pullRequest = runList(QStringLiteral("pullRequest"));
        pullRequest.insert(QStringLiteral("number"), 42);

        QTest::newRow("all") << runList(QStringLiteral("all")) << "" << "" << kRuns + "?per_page=100";
        QTest::newRow("absent source") << QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")}}
                                       << "" << "" << kRuns + "?per_page=100";
        QTest::newRow("workflow") << workflow << "" << ""
                                  << "GET repos/acme/backend/actions/workflows/ci.yml/runs?per_page=100";
        QTest::newRow("mine") << runList(QStringLiteral("mine")) << kUser << R"({"login": "ana dev", "id": 1001})"
                              << kRuns + "?actor=ana%20dev&per_page=100";
        QTest::newRow("branch") << branch << "" << "" << kRuns + "?branch=feature%2Fa%2Bb%26c&per_page=100";
        QTest::newRow("default branch") << runList(QStringLiteral("branch")) << kRepository
                                        << R"({"full_name": "acme/backend", "default_branch": "trunk"})"
                                        << kRuns + "?branch=trunk&per_page=100";
        QTest::newRow("pull request")
            << pullRequest << kPullRequest
            << R"({"number": 42, "head": {"ref": "feature/upload-retries", "sha": "9f2c"}, "base": {"ref": "main"}})"
            << kRuns + "?branch=feature%2Fupload-retries&event=pull_request&per_page=100";
    }

    // Four runs, as GitHub gave them, after the request each source needs first.
    void runListSources()
    {
        QFETCH(QJsonObject, config);
        QFETCH(QString, first);
        QFETCH(QString, firstData);
        QFETCH(QString, key);

        FakeStatusJob job;
        job.configValues = config;
        if (!first.isEmpty())
            job.client.setJson(first, QJsonDocument::fromJson(firstData.toUtf8()));
        QVERIFY(job.client.setJsonFile(key, dataFile(QStringLiteral("run_list.json"))));
        fetchRunListStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QStringList keys;
        if (!first.isEmpty())
            keys << first;
        keys << key;
        QCOMPARE(job.client.requestKeys(), keys);
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("4 run(s)"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!status.closed.has_value());
        QVERIFY(status.updatedAt.isValid());
        QCOMPARE(status.data.value(QStringLiteral("rows")).toArray(),
                 readObject(QStringLiteral("run_list.json")).value(QStringLiteral("workflow_runs")).toArray());
        QCOMPARE(status.data.value(QStringLiteral("total")).toInt(), 4);
        const auto &options = job.client.requests().constLast().options;
        QVERIFY(options.followLinks);
        QCOMPARE(options.arrayKey, QStringLiteral("workflow_runs"));
    }

    void runListRepositoryFromTheSetting()
    {
        const QString key = kRuns + QStringLiteral("?per_page=100");
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("source"), QStringLiteral("all")}};
        job.projectSettings = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")}};
        job.client.setJson(key, wrapped(QStringLiteral("workflow_runs"), 0));
        fetchRunListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
    }

    // A failed first request asks for no runs.
    void runListFirstRequestFails_data()
    {
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("first");

        QJsonObject pullRequest = runList(QStringLiteral("pullRequest"));
        pullRequest.insert(QStringLiteral("number"), 42);
        QTest::newRow("mine") << runList(QStringLiteral("mine")) << kUser;
        QTest::newRow("pull request") << pullRequest << kPullRequest;
        QTest::newRow("default branch") << runList(QStringLiteral("branch")) << kRepository;
    }

    void runListFirstRequestFails()
    {
        QFETCH(QJsonObject, config);
        QFETCH(QString, first);

        FakeStatusJob job;
        job.configValues = config;
        job.client.setReply(first, forbiddenReply());
        fetchRunListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Error);
        QCOMPARE(job.finished->message, QStringLiteral("Forbidden (403)"));
        QVERIFY(job.finished->data.isEmpty());
        QCOMPARE(job.client.requestKeys(), QStringList{first});
    }

    void runListWithoutADefaultBranch()
    {
        FakeStatusJob job;
        job.configValues = runList(QStringLiteral("branch"));
        job.client.setJson(kRepository, QJsonDocument(QJsonObject{{QStringLiteral("full_name"), QStringLiteral("acme/backend")}}));
        fetchRunListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Error);
        QCOMPARE(job.finished->message, QStringLiteral("The repository's default branch could not be read."));
        QCOMPARE(job.client.requestKeys(), QStringList{kRepository});
    }

    void runListIncomplete_data()
    {
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("message");

        QTest::newRow("no repository") << QJsonObject{{QStringLiteral("source"), QStringLiteral("all")}}
                                       << QStringLiteral("Set the repository in the item's settings.");
        QTest::newRow("no number") << runList(QStringLiteral("pullRequest"))
                                   << QStringLiteral("Set the pull request number in the item's settings.");
        QTest::newRow("unknown source") << runList(QStringLiteral("everyone"))
                                        << QStringLiteral("Set the source in the item's settings.");
    }

    void runListIncomplete()
    {
        QFETCH(QJsonObject, config);
        QFETCH(QString, message);

        FakeStatusJob job;
        job.configValues = config;
        fetchRunListStatus(job);
        QCOMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::None);
        QCOMPARE(job.finished->message, message);
        QVERIFY(!job.finished->closed.has_value());
        QVERIFY(job.client.requests().isEmpty());
    }

    void runListFailedReply()
    {
        FakeStatusJob job;
        job.configValues = runList(QStringLiteral("all"));
        job.client.setReply(kRuns + QStringLiteral("?per_page=100"), forbiddenReply());
        fetchRunListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Error);
        QVERIFY(job.finished->data.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestListQuery)
#include "tst_list_query.moc"
