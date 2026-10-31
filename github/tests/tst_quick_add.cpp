// Quick add: the handler on a pull request's address, and the finder on Jira
// keys, on the replies in tests/data/quick_add_*.json, written by hand in the
// shape of GitHub's API.
#include "../GitHubClient.h"
#include "../GitHubQuickAdd.h"

#include <mirante/testing/FakeQuickAdd.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using mirante::ApiReply;
using mirante::QuickAddItem;
using mirante::modules::github::GitHubClient;
using mirante::testing::FakeQuickAddRequest;
using mirante::testing::FakeRelatedItemRequest;
using namespace mirante::modules::github;

namespace {

const QString kPullRequest = QStringLiteral("GET repos/acme/backend/pulls/42");
const QString kOpenOfRepository =
    QStringLiteral("GET repos/acme/backend/pulls?state=open&sort=updated&direction=desc&per_page=100");

// The key of a search for query, as GitHubClient::searchIssues() sends it.
QString searchKey(const QString &query)
{
    return QStringLiteral("GET search/issues?q=") + GitHubClient::encode(query)
        + QStringLiteral("&sort=updated&order=desc&per_page=100");
}

QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("tests/data/") + name);
}

QJsonObject general(const QString &baseUrl)
{
    return QJsonObject{{QStringLiteral("baseUrl"), baseUrl}};
}

QJsonObject withRepositories(const QStringList &repositories)
{
    return QJsonObject{{QStringLiteral("repositories"), QJsonArray::fromStringList(repositories)}};
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

    // Without a web address the profile is github.com. A tab after the
    // number is part of the pull request's page.
    void pullRequestAddress()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("https://github.com/acme/backend/pull/42/files");
        QVERIFY(request.client.setJsonFile(kPullRequest, dataFile(QStringLiteral("quick_add_pull_request.json"))));

        GitHubQuickAddHandler().propose(request);
        WAIT_FOR_FINISH(request);

        QCOMPARE(request.client.requestKeys(), QStringList{kPullRequest});
        QVERIFY(request.finished);
        const mirante::QuickAddProposal &proposal = *request.finished;
        QCOMPARE(proposal.title, QStringLiteral("Add the retry button to the run panel"));
        QCOMPARE(describe(proposal.items),
                 (QStringList{QStringLiteral("github.pr.summary #42 number=42,repository=acme/backend"),
                              QStringLiteral("github.run #42 CI branchSource=pullRequest,number=42,"
                                             "repository=acme/backend")}));
        QVERIFY(proposal.project);
        QCOMPARE(proposal.project->key, QStringLiteral("repository"));
        QCOMPARE(proposal.project->value.toString(), QStringLiteral("acme/backend"));
        QCOMPARE(proposal.texts, (QStringList{QStringLiteral("Add the retry button to the run panel"),
                                              QStringLiteral("feature/ABC-123-retry-button")}));
    }

    // A pull request below a GitHub Enterprise Server's address, also one
    // installed at a path. The repository keeps the address's case; the core
    // compares the project setting without case.
    void enterpriseAddress()
    {
        const QString key = QStringLiteral("GET repos/Acme/Backend/pulls/42");
        const QList<QPair<QString, QString>> cases{
            {QStringLiteral("https://github.example.com"),
             QStringLiteral("https://github.example.com/Acme/Backend/pull/42")},
            {QStringLiteral("https://example.org/github/"),
             QStringLiteral("https://example.org/github/Acme/Backend/pull/42#discussion")},
        };
        for (const auto &[base, address] : cases) {
            FakeQuickAddRequest request;
            request.pasted = address;
            request.screens.insert(QStringLiteral("general"), general(base));
            QVERIFY(request.client.setJsonFile(key, dataFile(QStringLiteral("quick_add_pull_request.json"))));

            GitHubQuickAddHandler().propose(request);
            WAIT_FOR_FINISH(request);

            QCOMPARE(request.client.requestKeys(), QStringList{key});
            QVERIFY2(request.finished, qPrintable(address));
            QCOMPARE(request.finished->items.front().config.value(QStringLiteral("repository")).toString(),
                     QStringLiteral("Acme/Backend"));
            QCOMPARE(request.finished->project->value.toString(), QStringLiteral("Acme/Backend"));
        }
    }

    // github.com is read whatever the profile's web address is.
    void gitHubComWithAnEnterpriseProfile()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("https://www.github.com/acme/backend/pull/42");
        request.screens.insert(QStringLiteral("general"), general(QStringLiteral("https://github.example.com")));
        QVERIFY(request.client.setJsonFile(kPullRequest, dataFile(QStringLiteral("quick_add_pull_request.json"))));

        GitHubQuickAddHandler().propose(request);
        WAIT_FOR_FINISH(request);
        QCOMPARE(request.client.requestKeys(), QStringList{kPullRequest});
        QVERIFY(request.finished);
    }

    void addressOnAnotherHost()
    {
        const QList<QPair<QString, QString>> cases{
            {QStringLiteral("https://github.example.com"), QStringLiteral("https://gitlab.example/acme/backend/pull/42")},
            {QString(), QStringLiteral("https://github.example.com/acme/backend/pull/42")},
            {QStringLiteral("https://example.org/github"), QStringLiteral("https://example.org/acme/backend/pull/42")},
        };
        for (const auto &[base, address] : cases) {
            FakeQuickAddRequest request;
            request.pasted = address;
            if (!base.isEmpty())
                request.screens.insert(QStringLiteral("general"), general(base));
            GitHubQuickAddHandler().propose(request);
            QCOMPARE(request.finishCount, 1);
            QVERIFY2(!request.finished, qPrintable(address));
            QVERIFY2(request.client.requests().isEmpty(), qPrintable(address));
        }
    }

    void otherTextAsksNothing()
    {
        const QStringList texts{
            QString(),
            QStringLiteral("ABC-123"),
            QStringLiteral("ftp://github.com/acme/backend/pull/42"),
            QStringLiteral("https://github.com/acme/backend"),
            QStringLiteral("https://github.com/acme/backend/issues/7"),
            QStringLiteral("https://github.com/acme/backend/pulls"),
            QStringLiteral("https://github.com/acme/backend/pull/new/feature"),
            QStringLiteral("https://github.com/acme/backend/actions/runs/123"),
            QStringLiteral("https://github.com/pulls/review-requested"),
        };
        for (const QString &text : texts) {
            FakeQuickAddRequest request;
            request.pasted = text;
            GitHubQuickAddHandler().propose(request);
            QCOMPARE(request.finishCount, 1);
            QVERIFY2(!request.finished, qPrintable(text));
            QVERIFY2(request.client.requests().isEmpty(), qPrintable(text));
        }
    }

    void failedRequestProposesNothing()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("https://github.com/acme/backend/pull/42");
        request.client.failAll(ApiReply::Error::NoToken, QStringLiteral("Token not set on this device"));

        GitHubQuickAddHandler().propose(request);
        WAIT_FOR_FINISH(request);
        QCOMPARE(request.client.requestKeys(), QStringList{kPullRequest});
        QVERIFY(!request.finished);
    }

    // A pull request that GitHub does not know: a 404.
    void unknownPullRequestProposesNothing()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("https://github.com/acme/backend/pull/42");
        GitHubQuickAddHandler().propose(request);
        WAIT_FOR_FINISH(request);
        QVERIFY(!request.finished);
    }

    // Finder with the project setting.

    // One list serves every key, and a key in a branch or a title, in any
    // case, finds its pull request.
    void keyWithARepository()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("Log in with SSO"), QStringLiteral("feature/ABC-123-login")};
        request.projectSettings = QJsonObject{{QStringLiteral("repository"), QStringLiteral("/acme/backend/")}};
        request.screens.insert(QStringLiteral("general"), withRepositories({QStringLiteral("acme/website")}));
        QVERIFY(request.client.setJsonFile(kOpenOfRepository, dataFile(QStringLiteral("quick_add_open.json"))));

        GitHubRelatedItemFinder().find(request);
        WAIT_FOR_FINISH(request);

        QCOMPARE(request.client.requestKeys(), QStringList{kOpenOfRepository});
        QCOMPARE(request.client.requests().front().options.maxRows, 100);
        QCOMPARE(describe(request.found),
                 (QStringList{QStringLiteral("github.pr.summary #51 number=51,repository=acme/backend"),
                              QStringLiteral("github.pr.summary #48 number=48,repository=acme/backend")}));
    }

    // The items follow the keys' order, not the list's, and a pull request
    // found by two keys is offered once.
    void orderAndDuplicatesWithARepository()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("ABC-124 and abc-123"), QStringLiteral("feature/abc-123-login")};
        request.projectSettings = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")}};
        QVERIFY(request.client.setJsonFile(kOpenOfRepository, dataFile(QStringLiteral("quick_add_open.json"))));

        GitHubRelatedItemFinder().find(request);
        WAIT_FOR_FINISH(request);

        QCOMPARE(request.client.requestKeys(), QStringList{kOpenOfRepository});
        QCOMPARE(describe(request.found),
                 (QStringList{QStringLiteral("github.pr.summary #50 number=50,repository=acme/backend"),
                              QStringLiteral("github.pr.summary #51 number=51,repository=acme/backend"),
                              QStringLiteral("github.pr.summary #48 number=48,repository=acme/backend")}));
    }

    void failedListFindsNothing()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("ABC-123")};
        request.projectSettings = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")}};
        request.client.failAll(ApiReply::Error::Network, QStringLiteral("Connection refused"));

        GitHubRelatedItemFinder().find(request);
        WAIT_FOR_FINISH(request);
        QCOMPARE(request.client.requestKeys(), QStringList{kOpenOfRepository});
        QVERIFY(request.found.empty());
    }

    // Finder without the project setting.

    // One search for every key, over the profile's repositories. GitHub's
    // match "Notes for ABC 123" is not the key's, and a pull request found
    // by two keys is offered once, in the first key's place.
    void keysWithoutARepository()
    {
        const QString query = QStringLiteral(
            "is:pr is:open in:title (ABC-123 OR ABC-124) repo:acme/backend repo:acme/website");
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("abc-123 login"), QStringLiteral("feature/ABC-123-abc-124")};
        request.screens.insert(QStringLiteral("general"),
                               withRepositories({QStringLiteral("acme/backend"), QStringLiteral(" /acme/website/ "),
                                                 QStringLiteral("acme/backend")}));
        QVERIFY(request.client.setJsonFile(searchKey(query), dataFile(QStringLiteral("quick_add_search.json"))));

        GitHubRelatedItemFinder().find(request);
        WAIT_FOR_FINISH(request);

        QCOMPARE(request.client.requestKeys(), QStringList{searchKey(query)});
        QCOMPARE(request.client.requests().front().query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded),
                 query);
        QCOMPARE(request.client.requests().front().options.maxRows, 100);
        QCOMPARE(describe(request.found),
                 (QStringList{QStringLiteral("github.pr.summary acme/backend#51 number=51,repository=acme/backend"),
                              QStringLiteral("github.pr.summary acme/website#9 number=9,repository=acme/website"),
                              QStringLiteral("github.pr.summary acme/website#7 number=7,repository=acme/website")}));
    }

    // A search without "repo:" would cover all of GitHub.
    void noListedRepositoriesAskNothing()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("ABC-123")};
        request.screens.insert(QStringLiteral("general"), withRepositories({QStringLiteral(" "), QStringLiteral("/")}));

        GitHubRelatedItemFinder().find(request);
        QCOMPARE(request.finishCount, 1);
        QVERIFY(request.found.empty());
        QVERIFY(request.client.requests().isEmpty());
    }

    void failedSearchFindsNothing()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("ABC-123")};
        request.screens.insert(QStringLiteral("general"), withRepositories({QStringLiteral("acme/backend")}));
        request.client.failAll(ApiReply::Error::NoToken, QStringLiteral("Token not set on this device"));

        GitHubRelatedItemFinder().find(request);
        WAIT_FOR_FINISH(request);
        QCOMPARE(request.client.requests().size(), 1);
        QVERIFY(request.found.empty());
    }

    // Both.

    void noKeysAskNothing()
    {
        for (const bool withRepository : {true, false}) {
            FakeRelatedItemRequest request;
            request.searched = QStringList{QStringLiteral("Add the retry button"), QStringLiteral("feature/retry-button")};
            if (withRepository)
                request.projectSettings = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/backend")}};
            request.screens.insert(QStringLiteral("general"), withRepositories({QStringLiteral("acme/backend")}));

            GitHubRelatedItemFinder().find(request);
            QCOMPARE(request.finishCount, 1);
            QVERIFY(request.found.empty());
            QVERIFY(request.client.requests().isEmpty());
        }
    }

    // At most five distinct keys, in upper case, in the order they appear.
    void fiveKeysAtMost()
    {
        FakeRelatedItemRequest request;
        request.searched = QStringList{QStringLiteral("a1-1 AB-2 a1-1"), QStringLiteral("AB-3 ab-4 AB-5 AB-6")};
        request.screens.insert(QStringLiteral("general"), withRepositories({QStringLiteral("acme/backend")}));

        GitHubRelatedItemFinder().find(request);
        QCOMPARE(request.client.requests().size(), 1);
        QCOMPARE(request.client.requests().front().query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded),
                 QStringLiteral("is:pr is:open in:title (A1-1 OR AB-2 OR AB-3 OR AB-4 OR AB-5) repo:acme/backend"));
        WAIT_FOR_FINISH(request);
        QVERIFY(request.found.empty());
    }
};

QTEST_GUILESS_MAIN(TestQuickAdd)
#include "tst_quick_add.moc"
