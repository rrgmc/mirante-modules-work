// Quick add: the handler on a merge request address, and the finder on Jira
// keys, on the replies in tests/data/. merge_request.json was recorded from a
// GitLab server; the merge_requests_*.json lists were written by hand in the
// shape of GitLab's API.
#include "../GitLabQuickAdd.h"

#include <mirante/testing/FakeQuickAdd.h>

#include <QCoreApplication>
#include <QJsonObject>
#include <QTest>

using mirante::ApiReply;
using mirante::QuickAddItem;
using mirante::testing::FakeQuickAddRequest;
using mirante::testing::FakeRelatedItemRequest;
using namespace mirante::modules::gitlab;

namespace {

const QString kMergeRequest = QStringLiteral("GET api/v4/projects/acme%2Fbackend/merge_requests/42");
const QString kOpenOfProject = QStringLiteral(
    "GET api/v4/projects/acme%2Fbackend/merge_requests?state=opened&order_by=updated_at&sort=desc&per_page=100");

QString searchKey(const QString &key)
{
    return QStringLiteral("GET api/v4/merge_requests?scope=all&state=opened&search=") + key
        + QStringLiteral("&in=title&per_page=100");
}

QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("tests/data/") + name);
}

QJsonObject general(const QString &baseUrl)
{
    return QJsonObject{{QStringLiteral("baseUrl"), baseUrl}};
}

// "typeId name config" per item, so a failed comparison shows them all.
QStringList describe(const std::vector<QuickAddItem> &items)
{
    QStringList lines;
    for (const QuickAddItem &item : items) {
        QStringList keys;
        for (auto it = item.config.begin(); it != item.config.end(); ++it)
            keys << it.key() + QLatin1Char('=') + it.value().toVariant().toString();
        lines << item.typeId + QLatin1Char(' ') + item.name + QLatin1Char(' ') + keys.join(QLatin1Char(','));
    }
    return lines;
}

// Lets queued replies arrive, then checks nothing finishes twice.
#define WAIT_FOR_FINISH(request)                                                                                     \
    do {                                                                                                             \
        QTRY_COMPARE((request).finishCount, 1);                                                                      \
        QCoreApplication::processEvents();                                                                           \
        QCOMPARE((request).finishCount, 1);                                                                          \
    } while (false)

} // namespace

class TestQuickAdd : public QObject
{
    Q_OBJECT

private slots:
    // Handler.

    void mergeRequestAddress()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("https://gitlab.example/acme/backend/-/merge_requests/42/diffs");
        request.screens.insert(QStringLiteral("general"), general(QStringLiteral("https://gitlab.example")));
        QVERIFY(request.client.setJsonFile(kMergeRequest, dataFile(QStringLiteral("merge_request.json"))));

        GitLabQuickAddHandler().propose(request);
        WAIT_FOR_FINISH(request);

        QCOMPARE(request.client.requestKeys(), QStringList{kMergeRequest});
        QVERIFY(request.finished);
        const mirante::QuickAddProposal &proposal = *request.finished;
        QCOMPARE(proposal.title, QStringLiteral("Add the retry button to the pipeline panel"));
        QCOMPARE(describe(proposal.items),
                 (QStringList{QStringLiteral("gitlab.mr.summary !42 iid=42,project=acme/backend"),
                              QStringLiteral("gitlab.pipeline !42 CI branchSource=mergeRequest,iid=42,"
                                             "project=acme/backend")}));
        QVERIFY(proposal.project);
        QCOMPARE(proposal.project->key, QStringLiteral("project"));
        QCOMPARE(proposal.project->value.toString(), QStringLiteral("acme/backend"));
        QCOMPARE(proposal.texts, (QStringList{QStringLiteral("Add the retry button to the pipeline panel"),
                                              QStringLiteral("feature/retry-button")}));
    }

    // Without a base URL the profile's server is gitlab.com.
    void defaultServerIsGitLabCom()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("https://gitlab.com/acme/backend/-/merge_requests/42");
        QVERIFY(request.client.setJsonFile(kMergeRequest, dataFile(QStringLiteral("merge_request.json"))));

        GitLabQuickAddHandler().propose(request);
        WAIT_FOR_FINISH(request);
        QVERIFY(request.finished);
    }

    void addressOnAnotherServer()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("https://gitlab.com/acme/backend/-/merge_requests/42");
        request.screens.insert(QStringLiteral("general"), general(QStringLiteral("https://gitlab.example")));

        GitLabQuickAddHandler().propose(request);
        QCOMPARE(request.finishCount, 1);
        QVERIFY(!request.finished);
        QVERIFY(request.client.requests().isEmpty());
    }

    void otherTextAsksNothing()
    {
        const QStringList texts{
            QStringLiteral("ABC-123"),
            QStringLiteral("https://gitlab.example/acme/backend/-/issues/7"),
            QStringLiteral("https://gitlab.example/acme/backend/-/merge_requests"),
            QStringLiteral("https://gitlab.example/acme/backend/-/merge_requests/new"),
            QStringLiteral("https://gitlab.example/acme/backend"),
        };
        for (const QString &text : texts) {
            FakeQuickAddRequest request;
            request.pasted = text;
            request.screens.insert(QStringLiteral("general"), general(QStringLiteral("https://gitlab.example")));
            GitLabQuickAddHandler().propose(request);
            QCOMPARE(request.finishCount, 1);
            QVERIFY2(!request.finished, qPrintable(text));
            QVERIFY2(request.client.requests().isEmpty(), qPrintable(text));
        }
    }

    void failedRequestProposesNothing()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("https://gitlab.example/acme/backend/-/merge_requests/42");
        request.screens.insert(QStringLiteral("general"), general(QStringLiteral("https://gitlab.example")));
        request.client.failAll(ApiReply::Error::NoToken, QStringLiteral("Token not set on this device"));

        GitLabQuickAddHandler().propose(request);
        WAIT_FOR_FINISH(request);
        QCOMPARE(request.client.requestKeys(), QStringList{kMergeRequest});
        QVERIFY(!request.finished);
    }

    // Finder.

    // With a project setting, one list serves every key, and a key in a
    // branch, in any case, finds its merge request.
    void keyInABranchWithAProject()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("Log in with SSO"), QStringLiteral("feature/ABC-123-login")};
        request.projectSettings = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};
        QVERIFY(request.client.setJsonFile(kOpenOfProject, dataFile(QStringLiteral("merge_requests_open.json"))));

        GitLabRelatedItemFinder().find(request);
        WAIT_FOR_FINISH(request);

        QCOMPARE(request.client.requestKeys(), QStringList{kOpenOfProject});
        QCOMPARE(describe(request.found),
                 (QStringList{QStringLiteral("gitlab.mr.summary !51 iid=51,project=acme/backend"),
                              QStringLiteral("gitlab.mr.summary !48 iid=48,project=acme/backend")}));
    }

    // Without one, GitLab searches the titles, one request per key.
    void keyWithoutAProject()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("ABC-123")};
        QVERIFY(request.client.setJsonFile(searchKey(QStringLiteral("ABC-123")),
                                           dataFile(QStringLiteral("merge_requests_search_abc_123.json"))));

        GitLabRelatedItemFinder().find(request);
        WAIT_FOR_FINISH(request);

        QCOMPARE(request.client.requestKeys(), QStringList{searchKey(QStringLiteral("ABC-123"))});
        // "Notes for ABC 123" is GitLab's match, not the key's.
        QCOMPARE(describe(request.found),
                 (QStringList{QStringLiteral("gitlab.mr.summary acme/backend!51 iid=51,project=acme/backend"),
                              QStringLiteral("gitlab.mr.summary acme/website!9 iid=9,project=acme/website")}));
    }

    void noKeysAskNothing()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("Add the retry button"), QStringLiteral("feature/retry-button")};
        request.projectSettings = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};

        GitLabRelatedItemFinder().find(request);
        QCOMPARE(request.finishCount, 1);
        QVERIFY(request.found.empty());
        QVERIFY(request.client.requests().isEmpty());
    }

    // A merge request found by two keys is offered once, and a key written
    // twice, in any case, is searched once.
    void duplicatesRemoved()
    {
        FakeRelatedItemRequest withProject;
        withProject.searched = QStringList{QStringLiteral("feature/abc-123-login"), QStringLiteral("ABC-124 and abc-123")};
        withProject.projectSettings = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};
        QVERIFY(withProject.client.setJsonFile(kOpenOfProject, dataFile(QStringLiteral("merge_requests_open.json"))));
        GitLabRelatedItemFinder().find(withProject);
        WAIT_FOR_FINISH(withProject);
        QCOMPARE(describe(withProject.found),
                 (QStringList{QStringLiteral("gitlab.mr.summary !51 iid=51,project=acme/backend"),
                              QStringLiteral("gitlab.mr.summary !48 iid=48,project=acme/backend"),
                              QStringLiteral("gitlab.mr.summary !50 iid=50,project=acme/backend")}));

        FakeRelatedItemRequest searched;
        searched.searched = QStringList{QStringLiteral("abc-123 login"), QStringLiteral("feature/ABC-123-abc-124")};
        QVERIFY(searched.client.setJsonFile(searchKey(QStringLiteral("ABC-123")),
                                            dataFile(QStringLiteral("merge_requests_search_abc_123.json"))));
        QVERIFY(searched.client.setJsonFile(searchKey(QStringLiteral("ABC-124")),
                                            dataFile(QStringLiteral("merge_requests_search_abc_124.json"))));
        GitLabRelatedItemFinder().find(searched);
        WAIT_FOR_FINISH(searched);
        QCOMPARE(searched.client.requestKeys(),
                 (QStringList{searchKey(QStringLiteral("ABC-123")), searchKey(QStringLiteral("ABC-124"))}));
        QCOMPARE(describe(searched.found),
                 (QStringList{QStringLiteral("gitlab.mr.summary acme/backend!51 iid=51,project=acme/backend"),
                              QStringLiteral("gitlab.mr.summary acme/website!9 iid=9,project=acme/website")}));
    }

    // At most five keys are searched.
    void fiveKeysAtMost()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("A1-1 AB-2 AB-3 AB-4 AB-5 AB-6")};
        GitLabRelatedItemFinder().find(request);
        QCOMPARE(request.client.requests().size(), 5);
        QVERIFY(!request.client.requestKeys().join(QLatin1Char(' ')).contains(QStringLiteral("AB-6")));
        WAIT_FOR_FINISH(request);
        QVERIFY(request.found.empty());
    }

    // A failed search leaves its key out; the others still count.
    void failedSearchFindsNothing()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("ABC-123 XYZ-9")};
        QVERIFY(request.client.setJsonFile(searchKey(QStringLiteral("ABC-123")),
                                           dataFile(QStringLiteral("merge_requests_search_abc_123.json"))));
        GitLabRelatedItemFinder().find(request);
        WAIT_FOR_FINISH(request);
        QCOMPARE(request.found.size(), size_t(2));
    }
};

QTEST_MAIN(TestQuickAdd)
#include "tst_quick_add.moc"
