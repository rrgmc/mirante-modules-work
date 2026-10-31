// GitHubClient: the paths, the encoding of repositories, branches and search
// queries, the paging options of lists and the bodies of the actions.
#include "../GitHubClient.h"

#include <mirante/testing/FakeApiClient.h>

#include <QJsonObject>
#include <QObject>
#include <QTest>

using mirante::ApiReply;
using mirante::modules::github::GitHubClient;
using mirante::testing::FakeApiClient;

class TestClient : public QObject
{
    Q_OBJECT

private:
    // A client over a synchronous fake, and a callback that counts answers.
    struct Fixture
    {
        Fixture() { fake.setSynchronous(true); }
        FakeApiClient fake;
        QObject receiver;
        int answers = 0;
        GitHubClient client() { return GitHubClient(fake, &receiver); }
        GitHubClient::Done done()
        {
            return [this](const ApiReply &) { ++answers; };
        }
        const FakeApiClient::Request &last() const { return fake.requests().constLast(); }
    };

private slots:
    void encodes()
    {
        QCOMPARE(GitHubClient::encode(QStringLiteral("feature/a+b&c d")), QStringLiteral("feature%2Fa%2Bb%26c%20d"));
        QCOMPARE(GitHubClient::repositoryPath(QStringLiteral("acme/backend")), QStringLiteral("repos/acme/backend"));
        QCOMPARE(GitHubClient::repositoryPath(QStringLiteral("my org/my repo.js")),
                 QStringLiteral("repos/my%20org/my%20repo.js"));
        // The repository part is encoded on its own: a stray slash stays in it.
        QCOMPARE(GitHubClient::repositoryPath(QStringLiteral("acme/a/b")), QStringLiteral("repos/acme/a%2Fb"));
        QCOMPARE(GitHubClient::repositoryPath(QStringLiteral("acme")), QStringLiteral("repos/acme"));
    }

    void repositoryOfApiUrl()
    {
        QCOMPARE(GitHubClient::repositoryOfApiUrl(QStringLiteral("https://api.github.com/repos/cli/cli")),
                 QStringLiteral("cli/cli"));
        QCOMPARE(GitHubClient::repositoryOfApiUrl(
                     QStringLiteral("https://github.example.com/api/v3/repos/Acme/Backend/issues/7")),
                 QStringLiteral("Acme/Backend"));
        QVERIFY(GitHubClient::repositoryOfApiUrl(QStringLiteral("https://api.github.com/repos/cli")).isEmpty());
        QVERIFY(GitHubClient::repositoryOfApiUrl(QStringLiteral("https://api.github.com/user")).isEmpty());
        QVERIFY(GitHubClient::repositoryOfApiUrl(QString()).isEmpty());
    }

    // Single objects are plain GETs, without paging.
    void singleObjects()
    {
        Fixture f;
        const QString repository = QStringLiteral("acme/backend");
        f.client().currentUser(f.done());
        f.client().repository(repository, f.done());
        f.client().pullRequest(repository, 42, f.done());
        f.client().issue(repository, 7, f.done());
        f.client().combinedStatus(repository, QStringLiteral("abc123"), f.done());
        QCOMPARE(f.fake.requestKeys(),
                 (QStringList{QStringLiteral("GET user"), QStringLiteral("GET repos/acme/backend"),
                              QStringLiteral("GET repos/acme/backend/pulls/42"),
                              QStringLiteral("GET repos/acme/backend/issues/7"),
                              QStringLiteral("GET repos/acme/backend/commits/abc123/status")}));
        for (const FakeApiClient::Request &request : f.fake.requests()) {
            QVERIFY(!request.options.followLinks);
            QVERIFY(request.options.arrayKey.isEmpty());
        }
        QCOMPARE(f.answers, 5);
    }

    // Reviews are an array answer, read up to 100.
    void pullRequestReviews()
    {
        Fixture f;
        f.client().pullRequestReviews(QStringLiteral("acme/backend"), 42, f.done());
        QCOMPARE(f.fake.requestKeys(),
                 QStringList{QStringLiteral("GET repos/acme/backend/pulls/42/reviews?per_page=100")});
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 100);
        QVERIFY(f.last().options.arrayKey.isEmpty());
        QCOMPARE(f.answers, 1);
    }

    // A repository's pull requests take the caller's query, and pages of at
    // most 100 rows and at most the limit.
    void repositoryPullRequests()
    {
        Fixture f;
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("state"), QStringLiteral("open"));
        query.addQueryItem(QStringLiteral("sort"), QStringLiteral("updated"));
        query.addQueryItem(QStringLiteral("direction"), QStringLiteral("desc"));

        f.client().repositoryPullRequests(QStringLiteral("acme/backend"), query, 250, f.done());
        QCOMPARE(f.fake.requestKeys().constLast(),
                 QStringLiteral("GET repos/acme/backend/pulls?state=open&sort=updated&direction=desc&per_page=100"));
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 250);
        QVERIFY(f.last().options.arrayKey.isEmpty());

        f.client().repositoryPullRequests(QStringLiteral("acme/backend"), query, 20, f.done());
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("per_page")), QStringLiteral("20"));
        QCOMPARE(f.last().options.maxRows, 20);

        // A page size in the query is kept, and a limit below 1 counts as 1.
        QUrlQuery ownPage;
        ownPage.addQueryItem(QStringLiteral("per_page"), QStringLiteral("5"));
        f.client().repositoryPullRequests(QStringLiteral("acme/backend"), ownPage, 0, f.done());
        QCOMPARE(f.last().query.allQueryItemValues(QStringLiteral("per_page")), QStringList{QStringLiteral("5")});
        QCOMPARE(f.last().options.maxRows, 1);
        QCOMPARE(f.answers, 3);
    }

    // Check runs wrap their rows in "check_runs".
    void checkRuns()
    {
        Fixture f;
        f.client().checkRuns(QStringLiteral("acme/backend"), QStringLiteral("abc123"), f.done());
        QCOMPARE(f.fake.requestKeys(),
                 QStringList{QStringLiteral("GET repos/acme/backend/commits/abc123/check-runs?per_page=100")});
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 100);
        QCOMPARE(f.last().options.arrayKey, QStringLiteral("check_runs"));
        QCOMPARE(f.answers, 1);
    }

    // One page of comments, without following links.
    void issueComments()
    {
        Fixture f;
        f.client().issueComments(QStringLiteral("acme/backend"), 7, 20, 3, f.done());
        QCOMPARE(f.fake.requestKeys(),
                 QStringList{QStringLiteral("GET repos/acme/backend/issues/7/comments?per_page=20&page=3")});
        QVERIFY(!f.last().options.followLinks);
        QVERIFY(f.last().options.arrayKey.isEmpty());
        QCOMPARE(f.answers, 1);
    }

    // The search names its query in "q", encoded so that "+", "&" and "#"
    // survive, and wraps its rows in "items".
    void searchIssues()
    {
        Fixture f;
        const QString query = QStringLiteral("is:pr is:open repo:acme/backend label:c++ #1&2");
        f.client().searchIssues(query, 250, f.done());
        QCOMPARE(f.last().path, QStringLiteral("search/issues"));
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), query);
        QCOMPARE(f.fake.requestKeys().constLast(),
                 QStringLiteral("GET search/issues?q=is%3Apr%20is%3Aopen%20repo%3Aacme%2Fbackend%20label%3Ac%2B%2B%20"
                                "%231%262&sort=updated&order=desc&per_page=100"));
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 250);
        QCOMPARE(f.last().options.arrayKey, QStringLiteral("items"));

        f.client().searchIssues(QStringLiteral("is:issue"), 30, f.done());
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("per_page")), QStringLiteral("30"));
        QCOMPARE(f.last().options.maxRows, 30);
        QCOMPARE(f.answers, 2);
    }

    // A repository's runs, or one workflow's, wrapped in "workflow_runs", with
    // the caller's query.
    void workflowRuns()
    {
        Fixture f;
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("branch"), GitHubClient::encode(QStringLiteral("feature/a+b&c")));

        f.client().workflowRuns(QStringLiteral("acme/backend"), {}, query, 20, f.done());
        QCOMPARE(f.fake.requestKeys().constLast(),
                 QStringLiteral("GET repos/acme/backend/actions/runs?branch=feature%2Fa%2Bb%26c&per_page=20"));
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("branch"), QUrl::FullyDecoded),
                 QStringLiteral("feature/a+b&c"));
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 20);
        QCOMPARE(f.last().options.arrayKey, QStringLiteral("workflow_runs"));

        f.client().workflowRuns(QStringLiteral("acme/backend"), QStringLiteral("ci.yml"), {}, 500, f.done());
        QCOMPARE(f.fake.requestKeys().constLast(),
                 QStringLiteral("GET repos/acme/backend/actions/workflows/ci.yml/runs?per_page=100"));
        QCOMPARE(f.last().options.maxRows, 500);
        QCOMPARE(f.last().options.arrayKey, QStringLiteral("workflow_runs"));
        QCOMPARE(f.answers, 2);
    }

    // The jobs of a run's latest attempt, wrapped in "jobs".
    void runJobs()
    {
        Fixture f;
        f.client().runJobs(QStringLiteral("acme/backend"), 9001, f.done());
        QCOMPARE(f.fake.requestKeys(),
                 QStringList{
                     QStringLiteral("GET repos/acme/backend/actions/runs/9001/jobs?filter=latest&per_page=100")});
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 100);
        QCOMPARE(f.last().options.arrayKey, QStringLiteral("jobs"));
        QCOMPARE(f.answers, 1);
    }

    void actionsPost()
    {
        Fixture f;
        const QString repository = QStringLiteral("acme/backend");
        f.client().rerunRun(repository, 9001, f.done());
        f.client().rerunFailedJobs(repository, 9001, f.done());
        f.client().cancelRun(repository, 9001, f.done());
        f.client().rerunJob(repository, 70002, f.done());
        QCOMPARE(f.fake.requestKeys(),
                 (QStringList{QStringLiteral("POST repos/acme/backend/actions/runs/9001/rerun"),
                              QStringLiteral("POST repos/acme/backend/actions/runs/9001/rerun-failed-jobs"),
                              QStringLiteral("POST repos/acme/backend/actions/runs/9001/cancel"),
                              QStringLiteral("POST repos/acme/backend/actions/jobs/70002/rerun")}));
        for (const FakeApiClient::Request &request : f.fake.requests())
            QCOMPARE(request.body, QJsonDocument(QJsonObject()));
        QCOMPARE(f.answers, 4);
    }

    // A reply reaches the callback unchanged, errors included.
    void passesRepliesOn()
    {
        Fixture f;
        ApiReply forbidden;
        forbidden.error = ApiReply::Error::Forbidden;
        forbidden.httpStatus = 403;
        forbidden.message = QStringLiteral("Forbidden (403)");
        f.fake.setReply(QStringLiteral("POST repos/acme/backend/actions/runs/9001/cancel"), forbidden);

        std::optional<ApiReply> got;
        f.client().cancelRun(QStringLiteral("acme/backend"), 9001, [&got](const ApiReply &reply) { got = reply; });
        QVERIFY(got);
        QCOMPARE(got->error, ApiReply::Error::Forbidden);
        QCOMPARE(got->message, QStringLiteral("Forbidden (403)"));
    }
};

QTEST_GUILESS_MAIN(TestClient)
#include "tst_client.moc"
