// Quick add for Jira: the handler on addresses and bare keys, and the finder on
// keys in text, from recorded responses of Jira Cloud and Jira Data Center in
// tests/data/. See README.md#quick-add.
#include "../JiraQuickAdd.h"

#include <mirante/testing/FakeQuickAdd.h>

#include <QJsonObject>
#include <QTest>

using mirante::ApiReply;
using mirante::QuickAddItem;
using mirante::modules::jira::JiraQuickAddHandler;
using mirante::modules::jira::JiraRelatedItemFinder;
using mirante::testing::FakeApiClient;
using mirante::testing::FakeQuickAddRequest;
using mirante::testing::FakeRelatedItemRequest;

namespace {

const QString kCloudSite = QStringLiteral("https://acme.atlassian.net");
const QString kDcSite = QStringLiteral("https://jira.example.org/jira");

// The request for an issue's summary.
QString summaryKey(const QString &root, const QString &key)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("summary"));
    return FakeApiClient::key(QStringLiteral("GET"), root + QStringLiteral("/issue/") + key, query);
}

QJsonObject general(const QString &site)
{
    QJsonObject values{{QStringLiteral("baseUrl"), site}};
    if (site == kCloudSite)
        values.insert(QStringLiteral("email"), QStringLiteral("someone@example.com"));
    return values;
}

// The deployments, with the issue each one has recorded.
void addDeployments()
{
    QTest::addColumn<QString>("site");
    QTest::addColumn<QString>("root");
    QTest::addColumn<QString>("key");
    QTest::addColumn<QString>("summary");
    QTest::addColumn<QString>("file");
    QTest::newRow("cloud") << kCloudSite << QStringLiteral("rest/api/3") << QStringLiteral("OPS-7")
                           << QStringLiteral("Rotate the build server's certificates")
                           << QFINDTESTDATA("tests/data/issue-summary-cloud.json");
    QTest::newRow("dc") << kDcSite << QStringLiteral("rest/api/2") << QStringLiteral("INFRA-1203")
                        << QStringLiteral("Upgrade the staging database")
                        << QFINDTESTDATA("tests/data/issue-summary-dc.json");
}

QuickAddItem summaryItem(const QString &key)
{
    return {QStringLiteral("jira.issue.summary"), key, QJsonObject{{QStringLiteral("key"), key}}};
}

bool sameItems(const std::vector<QuickAddItem> &a, const std::vector<QuickAddItem> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].typeId != b[i].typeId || a[i].name != b[i].name || a[i].config != b[i].config)
            return false;
    }
    return true;
}

QString prefixOf(const QString &key) { return key.section(QLatin1Char('-'), 0, 0); }

} // namespace

class TestQuickAdd : public QObject
{
    Q_OBJECT

private:
    // Runs the handler on text and waits for its answer.
    void propose(FakeQuickAddRequest &request, const QString &text)
    {
        request.pasted = text;
        JiraQuickAddHandler().propose(request);
        QTRY_COMPARE(request.finishCount, 1);
        QTest::qWait(0);
        QCOMPARE(request.finishCount, 1);
    }

    // Runs the finder on texts and waits for its answer.
    void find(FakeRelatedItemRequest &request, const QStringList &texts)
    {
        request.searched = texts;
        JiraRelatedItemFinder().find(request);
        QTRY_COMPARE(request.finishCount, 1);
        QTest::qWait(0);
        QCOMPARE(request.finishCount, 1);
    }

private slots:
    void address_data() { addDeployments(); }

    // An issue's address on the profile's site proposes its summary item.
    void address()
    {
        QFETCH(QString, site);
        QFETCH(QString, root);
        QFETCH(QString, key);
        QFETCH(QString, summary);
        QFETCH(QString, file);

        FakeQuickAddRequest request;
        request.screens.insert(QStringLiteral("general"), general(site));
        QVERIFY(request.client.setJsonFile(summaryKey(root, key), file));
        propose(request, site + QStringLiteral("/browse/") + key);

        QCOMPARE(request.client.requestKeys(), QStringList{summaryKey(root, key)});
        QVERIFY(request.finished);
        QCOMPARE(request.finished->title, summary);
        QVERIFY(sameItems(request.finished->items, {summaryItem(key)}));
        QVERIFY(request.finished->project);
        QCOMPARE(request.finished->project->key, QStringLiteral("projectKey"));
        QCOMPARE(request.finished->project->value.toString(), prefixOf(key));
        QCOMPARE(request.finished->texts, QStringList{key});
    }

    void bareKey_data() { addDeployments(); }

    // A key, typed in lower case with spaces around it, is read in upper case.
    void bareKey()
    {
        QFETCH(QString, site);
        QFETCH(QString, root);
        QFETCH(QString, key);
        QFETCH(QString, summary);
        QFETCH(QString, file);

        FakeQuickAddRequest request;
        request.screens.insert(QStringLiteral("general"), general(site));
        QVERIFY(request.client.setJsonFile(summaryKey(root, key), file));
        propose(request, QStringLiteral("  ") + key.toLower() + QStringLiteral(" "));

        QCOMPARE(request.client.requestKeys(), QStringList{summaryKey(root, key)});
        QVERIFY(request.finished);
        QCOMPARE(request.finished->title, summary);
        QVERIFY(sameItems(request.finished->items, {summaryItem(key)}));
        QCOMPARE(request.finished->project->value.toString(), prefixOf(key));
    }

    void keyNotFound_data() { addDeployments(); }

    // Text that only looks like a key: the server answers 404.
    void keyNotFound()
    {
        QFETCH(QString, site);
        QFETCH(QString, root);

        FakeQuickAddRequest request;
        request.screens.insert(QStringLiteral("general"), general(site));
        propose(request, QStringLiteral("UTF-8"));
        QCOMPARE(request.client.requestKeys(), QStringList{summaryKey(root, QStringLiteral("UTF-8"))});
        QVERIFY(!request.finished);
    }

    // Any other failure also finishes with nothing.
    void failure()
    {
        FakeQuickAddRequest request;
        request.screens.insert(QStringLiteral("general"), general(kCloudSite));
        request.client.failAll(ApiReply::Error::Network, QStringLiteral("offline"));
        propose(request, QStringLiteral("OPS-7"));
        QCOMPARE(request.client.requests().size(), 1);
        QVERIFY(!request.finished);
    }

    void notJira_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("words") << QStringLiteral("Fix the login page");
        QTest::newRow("key in words") << QStringLiteral("Fix OPS-7 today");
        QTest::newRow("no number") << QStringLiteral("OPS-");
        QTest::newRow("one letter") << QStringLiteral("A-1");
        QTest::newRow("merge request") << QStringLiteral("https://gitlab.com/acme/backend/-/merge_requests/42");
        QTest::newRow("search") << kCloudSite + QStringLiteral("/issues/?jql=project%20%3D%20OPS");
        QTest::newRow("other site") << QStringLiteral("https://other.atlassian.net/browse/OPS-7");
        QTest::newRow("other server") << QStringLiteral("https://jira.example.org/browse/OPS-7");
    }

    // Text that is not a key or an issue on the profile's site finishes at
    // once, without a request.
    void notJira()
    {
        QFETCH(QString, text);
        FakeQuickAddRequest request;
        request.screens.insert(QStringLiteral("general"), general(kCloudSite));
        request.pasted = text;
        JiraQuickAddHandler().propose(request);
        QCOMPARE(request.finishCount, 1);
        QVERIFY(!request.finished);
        QVERIFY(request.client.requests().isEmpty());
    }

    // Without a site in the profile there is no server to ask.
    void noSite()
    {
        FakeQuickAddRequest request;
        request.pasted = QStringLiteral("OPS-7");
        JiraQuickAddHandler().propose(request);
        QCOMPARE(request.finishCount, 1);
        QVERIFY(!request.finished);
        QVERIFY(request.client.requests().isEmpty());
    }

    void keysInBranch_data() { addDeployments(); }

    // A key inside a branch name, in lower case, offers its issue.
    void keysInBranch()
    {
        QFETCH(QString, site);
        QFETCH(QString, root);
        QFETCH(QString, key);
        QFETCH(QString, file);

        FakeRelatedItemRequest request;
        request.screens.insert(QStringLiteral("general"), general(site));
        request.projectSettings.insert(QStringLiteral("projectKey"), prefixOf(key).toLower());
        QVERIFY(request.client.setJsonFile(summaryKey(root, key), file));
        find(request, {QStringLiteral("feature/") + key.toLower() + QStringLiteral("-login")});

        QCOMPARE(request.client.requestKeys(), QStringList{summaryKey(root, key)});
        QVERIFY(sameItems(request.found, {summaryItem(key)}));
    }

    // A key of another Jira project is not asked about.
    void otherPrefix()
    {
        FakeRelatedItemRequest request;
        request.screens.insert(QStringLiteral("general"), general(kCloudSite));
        request.projectSettings.insert(QStringLiteral("projectKey"), QStringLiteral("OPS"));
        request.searched = {QStringLiteral("INFRA-1203: upgrade"), QStringLiteral("hotfix/ops2-4")};
        JiraRelatedItemFinder().find(request);
        QCOMPARE(request.finishCount, 1);
        QVERIFY(request.found.empty());
        QVERIFY(request.client.requests().isEmpty());
    }

    // Without a project key every key is asked about. Only those found are
    // offered, in the order of the texts.
    void anyPrefixWithoutProjectKey()
    {
        FakeRelatedItemRequest request;
        request.screens.insert(QStringLiteral("general"), general(kDcSite));
        QVERIFY(request.client.setJsonFile(summaryKey(QStringLiteral("rest/api/2"), QStringLiteral("INFRA-1203")),
                                           QFINDTESTDATA("tests/data/issue-summary-dc.json")));
        find(request, {QStringLiteral("OPS-9 and INFRA-1203"), QStringLiteral("Upgrade the staging database")});

        QCOMPARE(request.client.requestKeys(),
                 QStringList({summaryKey(QStringLiteral("rest/api/2"), QStringLiteral("OPS-9")),
                              summaryKey(QStringLiteral("rest/api/2"), QStringLiteral("INFRA-1203"))}));
        QVERIFY(sameItems(request.found, {summaryItem(QStringLiteral("INFRA-1203"))}));
    }

    // A key found several times, in any case, is asked about once.
    void duplicates()
    {
        FakeRelatedItemRequest request;
        request.screens.insert(QStringLiteral("general"), general(kCloudSite));
        request.projectSettings.insert(QStringLiteral("projectKey"), QStringLiteral("OPS"));
        QVERIFY(request.client.setJsonFile(summaryKey(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7")),
                                           QFINDTESTDATA("tests/data/issue-summary-cloud.json")));
        find(request, {QStringLiteral("OPS-7"), QStringLiteral("feature/ops-7-certs"),
                       QStringLiteral("Rotate certificates (OPS-7, Ops-7)")});

        QCOMPARE(request.client.requestKeys(),
                 QStringList{summaryKey(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7"))});
        QVERIFY(sameItems(request.found, {summaryItem(QStringLiteral("OPS-7"))}));
    }

    // At most five keys are asked about.
    void fiveKeysAtMost()
    {
        FakeRelatedItemRequest request;
        request.screens.insert(QStringLiteral("general"), general(kCloudSite));
        find(request, {QStringLiteral("OPS-1 OPS-2 OPS-3 OPS-4 OPS-5 OPS-6 OPS-7")});
        QCOMPARE(request.client.requests().size(), 5);
        QCOMPARE(request.client.requests().at(4).path, QStringLiteral("rest/api/3/issue/OPS-5"));
        QVERIFY(request.found.empty());
    }

    // Text without keys finishes at once, without a request.
    void noKeys()
    {
        FakeRelatedItemRequest request;
        request.screens.insert(QStringLiteral("general"), general(kCloudSite));
        request.searched = {QStringLiteral("feature/login"), QStringLiteral("Fix the login page"),
                            QStringLiteral("OPS-")};
        JiraRelatedItemFinder().find(request);
        QCOMPARE(request.finishCount, 1);
        QVERIFY(request.found.empty());
        QVERIFY(request.client.requests().isEmpty());
    }
};

QTEST_MAIN(TestQuickAdd)
#include "tst_quick_add.moc"
