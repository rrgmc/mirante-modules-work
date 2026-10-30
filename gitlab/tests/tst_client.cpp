// GitLabClient: the paths, the encoding of project paths and branches, the
// paging options of lists and the bodies of the light actions.
#include "../GitLabClient.h"

#include <mirante/testing/FakeApiClient.h>

#include <QJsonObject>
#include <QObject>
#include <QTest>

using mirante::ApiReply;
using mirante::modules::gitlab::GitLabClient;
using mirante::testing::FakeApiClient;

class TestClient : public QObject
{
    Q_OBJECT

private:
    // A client over a synchronous fake, and a callback that counts answers.
    struct Fixture
    {
        Fixture()
        {
            fake.setSynchronous(true);
        }
        FakeApiClient fake;
        QObject receiver;
        int answers = 0;
        GitLabClient client() { return GitLabClient(fake, &receiver); }
        GitLabClient::Done done()
        {
            return [this](const ApiReply &) { ++answers; };
        }
        const FakeApiClient::Request &last() const { return fake.requests().constLast(); }
    };

private slots:
    void encodesPaths()
    {
        QCOMPARE(GitLabClient::encodePath(QStringLiteral("acme/backend")), QStringLiteral("acme%2Fbackend"));
        QCOMPARE(GitLabClient::encodePath(QStringLiteral("acme/sub group/app")),
                 QStringLiteral("acme%2Fsub%20group%2Fapp"));
        QCOMPARE(GitLabClient::encodePath(QStringLiteral("acme")), QStringLiteral("acme"));
    }

    void singleObjects()
    {
        Fixture f;
        const QString project = QStringLiteral("acme/sub/backend");
        f.client().currentUser(f.done());
        f.client().mergeRequest(project, 42, f.done());
        f.client().approvals(project, 42, f.done());
        f.client().issue(project, 7, f.done());
        QCOMPARE(f.fake.requestKeys(),
                 (QStringList{QStringLiteral("GET api/v4/user"),
                              QStringLiteral("GET api/v4/projects/acme%2Fsub%2Fbackend/merge_requests/42"),
                              QStringLiteral("GET api/v4/projects/acme%2Fsub%2Fbackend/merge_requests/42/approvals"),
                              QStringLiteral("GET api/v4/projects/acme%2Fsub%2Fbackend/issues/7")}));
        for (const FakeApiClient::Request &request : f.fake.requests())
            QVERIFY(!request.options.followLinks);
        QCOMPARE(f.answers, 4);
    }

    // The latest pipeline names the branch in the query, encoded so that "+"
    // and "&" survive.
    void latestPipeline()
    {
        Fixture f;
        f.client().latestPipeline(QStringLiteral("acme/backend"), QStringLiteral("feature/a+b&c"), f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/projects/acme%2Fbackend/pipelines/latest"));
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("ref"), QUrl::FullyDecoded),
                 QStringLiteral("feature/a+b&c"));
        QCOMPARE(f.fake.requestKeys().constLast(),
                 QStringLiteral("GET api/v4/projects/acme%2Fbackend/pipelines/latest?ref=feature%2Fa%2Bb%26c"));
        QVERIFY(!f.last().options.followLinks);
        QCOMPARE(f.answers, 1);
    }

    // Lists follow the Link header up to maxRows, and ask for pages of at
    // most 100 rows unless the filter names its own page size.
    void listsFollowLinks()
    {
        Fixture f;
        QUrlQuery filter;
        filter.addQueryItem(QStringLiteral("state"), QStringLiteral("opened"));
        filter.addQueryItem(QStringLiteral("scope"), QStringLiteral("created_by_me"));

        f.client().mergeRequests(filter, 250, f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/merge_requests"));
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 250);
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("state")), QStringLiteral("opened"));
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("scope")), QStringLiteral("created_by_me"));
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("per_page")), QStringLiteral("100"));

        f.client().projectMergeRequests(QStringLiteral("acme/backend"), filter, 20, f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/projects/acme%2Fbackend/merge_requests"));
        QCOMPARE(f.last().options.maxRows, 20);
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("per_page")), QStringLiteral("20"));

        f.client().groupMergeRequests(QStringLiteral("acme/platform"), {}, 100, f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/groups/acme%2Fplatform/merge_requests"));
        QVERIFY(f.last().options.followLinks);

        QUrlQuery ownPage;
        ownPage.addQueryItem(QStringLiteral("per_page"), QStringLiteral("5"));
        f.client().issues(ownPage, 100, f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/issues"));
        QCOMPARE(f.last().query.allQueryItemValues(QStringLiteral("per_page")), QStringList{QStringLiteral("5")});

        f.client().pipelineJobs(QStringLiteral("acme/backend"), 9001, 100, f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/projects/acme%2Fbackend/pipelines/9001/jobs"));
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.answers, 5);
    }

    void onePipeline()
    {
        Fixture f;
        f.client().pipeline(QStringLiteral("acme/backend"), 9001, f.done());
        QCOMPARE(f.fake.requestKeys(), QStringList{QStringLiteral("GET api/v4/projects/acme%2Fbackend/pipelines/9001")});
        QVERIFY(!f.last().options.followLinks);
        QCOMPARE(f.answers, 1);
    }

    // The latest notes first, as a list up to maxRows.
    void issueNotes()
    {
        Fixture f;
        f.client().issueNotes(QStringLiteral("acme/backend"), 7, 20, f.done());
        QCOMPARE(f.fake.requestKeys(),
                 QStringList{QStringLiteral(
                     "GET api/v4/projects/acme%2Fbackend/issues/7/notes?sort=desc&order_by=created_at&per_page=20")});
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 20);
        QCOMPARE(f.answers, 1);
    }

    // Like the merge request lists.
    void issueLists()
    {
        Fixture f;
        QUrlQuery filter;
        filter.addQueryItem(QStringLiteral("state"), QStringLiteral("opened"));

        f.client().projectIssues(QStringLiteral("acme/backend"), filter, 250, f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/projects/acme%2Fbackend/issues"));
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 250);
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("state")), QStringLiteral("opened"));
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("per_page")), QStringLiteral("100"));

        f.client().groupIssues(QStringLiteral("acme/platform"), filter, 20, f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/groups/acme%2Fplatform/issues"));
        QVERIFY(f.last().options.followLinks);
        QCOMPARE(f.last().options.maxRows, 20);
        QCOMPARE(f.last().query.queryItemValue(QStringLiteral("per_page")), QStringLiteral("20"));
        QCOMPARE(f.answers, 2);
    }

    void issueStateIsPut()
    {
        Fixture f;
        f.client().setIssueState(QStringLiteral("acme/backend"), 7, QStringLiteral("close"), f.done());
        QCOMPARE(f.last().method, QStringLiteral("PUT"));
        QCOMPARE(f.last().path, QStringLiteral("api/v4/projects/acme%2Fbackend/issues/7"));
        QCOMPARE(f.last().body, QJsonDocument(QJsonObject{{QStringLiteral("state_event"), QStringLiteral("close")}}));

        f.client().setIssueState(QStringLiteral("acme/backend"), 7, QStringLiteral("reopen"), f.done());
        QCOMPARE(f.last().body, QJsonDocument(QJsonObject{{QStringLiteral("state_event"), QStringLiteral("reopen")}}));
        QCOMPARE(f.answers, 2);
    }

    void actionsPost()
    {
        Fixture f;
        const QString project = QStringLiteral("acme/backend");
        f.client().retryPipeline(project, 9001, f.done());
        f.client().cancelPipeline(project, 9001, f.done());
        f.client().retryJob(project, 70002, f.done());
        f.client().approve(project, 42, f.done());
        QCOMPARE(f.fake.requestKeys(),
                 (QStringList{QStringLiteral("POST api/v4/projects/acme%2Fbackend/pipelines/9001/retry"),
                              QStringLiteral("POST api/v4/projects/acme%2Fbackend/pipelines/9001/cancel"),
                              QStringLiteral("POST api/v4/projects/acme%2Fbackend/jobs/70002/retry"),
                              QStringLiteral("POST api/v4/projects/acme%2Fbackend/merge_requests/42/approve")}));
        for (const FakeApiClient::Request &request : f.fake.requests())
            QCOMPARE(request.body, QJsonDocument(QJsonObject()));
        QCOMPARE(f.answers, 4);
    }

    void commentsPostTheText()
    {
        Fixture f;
        const QString text = QStringLiteral("Looks good.\nOne question about `retry()`.");
        f.client().commentOnMergeRequest(QStringLiteral("acme/backend"), 42, text, f.done());
        QCOMPARE(f.last().method, QStringLiteral("POST"));
        QCOMPARE(f.last().path, QStringLiteral("api/v4/projects/acme%2Fbackend/merge_requests/42/notes"));
        QCOMPARE(f.last().body, QJsonDocument(QJsonObject{{QStringLiteral("body"), text}}));

        f.client().commentOnIssue(QStringLiteral("acme/backend"), 7, text, f.done());
        QCOMPARE(f.last().path, QStringLiteral("api/v4/projects/acme%2Fbackend/issues/7/notes"));
        QCOMPARE(f.last().body, QJsonDocument(QJsonObject{{QStringLiteral("body"), text}}));
        QCOMPARE(f.answers, 2);
    }

    // A reply reaches the callback unchanged, errors included.
    void passesRepliesOn()
    {
        Fixture f;
        ApiReply forbidden;
        forbidden.error = ApiReply::Error::Forbidden;
        forbidden.httpStatus = 403;
        forbidden.message = QStringLiteral("Forbidden (403)");
        f.fake.setReply(QStringLiteral("POST api/v4/projects/acme%2Fbackend/merge_requests/42/approve"), forbidden);

        std::optional<ApiReply> got;
        f.client().approve(QStringLiteral("acme/backend"), 42, [&got](const ApiReply &reply) { got = reply; });
        QVERIFY(got);
        QCOMPARE(got->error, ApiReply::Error::Forbidden);
        QCOMPARE(got->message, QStringLiteral("Forbidden (403)"));
    }
};

QTEST_GUILESS_MAIN(TestClient)
#include "tst_client.moc"
