// The status of the Jira types, from recorded responses of Jira Cloud and Jira
// Data Center in tests/data/. See README.md#status.
#include "../JiraStatus.h"
#include "items/ItemTypeRegistry.h"

#include <mirante/testing/FakeStatusJob.h>

#include <QJsonArray>
#include <QTest>

namespace mirante::modules::jira {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeApiClient;
using mirante::testing::FakeStatusJob;

namespace {

const QString kFields = QStringLiteral("summary,status,assignee,priority,issuetype,updated");

// The request of a summary: all fields, with the names of the fields.
QString summaryKey(const QString &root, const QString &key)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("expand"), QStringLiteral("renderedFields,names"));
    return FakeApiClient::key(QStringLiteral("GET"), root + QStringLiteral("/issue/") + key, query);
}

// The first request of a list's search.
QString searchKey(const QString &path, const QString &jql, bool startAt)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("jql"), jql);
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("summary,status,assignee,updated"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
    if (startAt)
        query.addQueryItem(QStringLiteral("startAt"), QStringLiteral("0"));
    return FakeApiClient::key(QStringLiteral("GET"), path, query);
}

QString issueKey(const QString &root, const QString &key)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"), kFields);
    query.addQueryItem(QStringLiteral("expand"), QStringLiteral("renderedFields"));
    return FakeApiClient::key(QStringLiteral("GET"), root + QStringLiteral("/issue/") + key, query);
}

void setCloud(FakeStatusJob &job, const QString &key)
{
    job.configValues = {{QStringLiteral("key"), key}};
    job.screens.insert(QStringLiteral("general"),
                       {{QStringLiteral("baseUrl"), QStringLiteral("https://acme.atlassian.net")},
                        {QStringLiteral("email"), QStringLiteral("someone@example.com")}});
}

void setDataCenter(FakeStatusJob &job, const QString &key)
{
    job.configValues = {{QStringLiteral("key"), key}};
    job.screens.insert(QStringLiteral("general"),
                       {{QStringLiteral("baseUrl"), QStringLiteral("https://jira.example.org/jira")}});
}

} // namespace

class TestStatus : public QObject
{
    Q_OBJECT

private:
    mirante::ItemTypeRegistry m_registry;

    const mirante::ItemType &type(const QString &id) { return m_registry.type(id); }

private slots:
    void initTestCase()
    {
        m_registry.registerModule(QStringLiteral("jira"), &mirante::modules::jira::registerModule);
    }

    void issueTypesHaveStatus()
    {
        QVERIFY(type(QStringLiteral("jira.issue.summary")).hasStatus());
        QVERIFY(type(QStringLiteral("jira.issue.page")).hasStatus());
        QVERIFY(type(QStringLiteral("jira.list")).hasStatus());
    }

    void openIssueOnCloud_data()
    {
        QTest::addColumn<QString>("typeId");
        QTest::addColumn<QString>("request");
        QTest::addColumn<QString>("file");
        QTest::newRow("summary") << QStringLiteral("jira.issue.summary")
                                 << summaryKey(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7"))
                                 << QFINDTESTDATA("data/issue-full-cloud.json");
        QTest::newRow("page") << QStringLiteral("jira.issue.page")
                              << issueKey(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7"))
                              << QFINDTESTDATA("data/issue-cloud.json");
    }

    void openIssueOnCloud()
    {
        QFETCH(QString, typeId);
        QFETCH(QString, request);
        QFETCH(QString, file);
        FakeStatusJob job;
        setCloud(job, QStringLiteral("OPS-7"));
        QVERIFY(job.client.setJsonFile(request, file));

        type(typeId).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requests().size(), 1);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("In Review ") + QChar(0x00B7) + QStringLiteral(" assigned to Ana Example"));
        QCOMPARE(status.closed, std::optional<bool>(false));
        QCOMPARE(status.data.value(QStringLiteral("key")).toString(), QStringLiteral("OPS-7"));
        QCOMPARE(status.data.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("Rotate the build server's certificates"));
        QCOMPARE(status.data.value(QStringLiteral("statusId")).toString(), QStringLiteral("10003"));
        QCOMPARE(status.data.value(QStringLiteral("priority")).toString(), QStringLiteral("Medium"));
        QCOMPARE(status.data.value(QStringLiteral("issueType")).toString(), QStringLiteral("Task"));

        QCOMPARE(status.attention.size(), 1);
        QCOMPARE(status.attention.at(0).key, QStringLiteral("status:10003"));
        QCOMPARE(status.attention.at(0).text, QStringLiteral("Status changed to In Review"));
        QVERIFY(status.attention.at(0).clearsOnView);
        QVERIFY(!status.attention.at(0).notify);
    }

    void doneIssueOnDataCenter()
    {
        FakeStatusJob job;
        setDataCenter(job, QStringLiteral("INFRA-1203"));
        QVERIFY(job.client.setJsonFile(issueKey(QStringLiteral("rest/api/2"), QStringLiteral("INFRA-1203")),
                                       QFINDTESTDATA("data/issue-dc-done.json")));

        type(QStringLiteral("jira.issue.page")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Done ") + QChar(0x00B7) + QStringLiteral(" unassigned"));
        QCOMPARE(status.closed, std::optional<bool>(true));
        QCOMPARE(status.attention.size(), 1);
        QCOMPARE(status.attention.at(0).key, QStringLiteral("status:10001"));
    }

    // A summary keeps the whole answer. A page has only its six fields.
    void summaryKeepsTheAnswer_data()
    {
        QTest::addColumn<bool>("cloud");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("sprint");
        QTest::addColumn<QString>("sprintField");
        QTest::newRow("cloud") << true << QStringLiteral("OPS-7") << QFINDTESTDATA("data/issue-full-cloud.json")
                               << QStringLiteral("Sprint 12") << QStringLiteral("customfield_10020");
        QTest::newRow("data center") << false << QStringLiteral("INFRA-1203")
                                     << QFINDTESTDATA("data/issue-full-dc.json")
                                     << QStringLiteral("Platform, week 44") << QStringLiteral("customfield_10105");
    }

    void summaryKeepsTheAnswer()
    {
        QFETCH(bool, cloud);
        QFETCH(QString, key);
        QFETCH(QString, file);
        QFETCH(QString, sprint);
        QFETCH(QString, sprintField);
        FakeStatusJob job;
        if (cloud)
            setCloud(job, key);
        else
            setDataCenter(job, key);
        const QString root = cloud ? QStringLiteral("rest/api/3") : QStringLiteral("rest/api/2");
        QVERIFY(job.client.setJsonFile(summaryKey(root, key), file));

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requests().size(), 1);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.data.value(QStringLiteral("key")).toString(), key);
        const QJsonObject issue = status.data.value(QStringLiteral("issue")).toObject();
        QCOMPARE(issue.value(QStringLiteral("key")).toString(), key);
        QVERIFY(issue.value(QStringLiteral("fields")).toObject().contains(sprintField));
        QCOMPARE(mirante::modules::jira::sprintOf(issue), sprint);
        // The attention rules are the page's.
        QCOMPARE(status.attention.size(), 1);
        QVERIFY(status.attention.at(0).key.startsWith(QStringLiteral("status:")));
    }

    void pageDoesNotKeepTheAnswer()
    {
        FakeStatusJob job;
        setDataCenter(job, QStringLiteral("INFRA-1203"));
        QVERIFY(job.client.setJsonFile(issueKey(QStringLiteral("rest/api/2"), QStringLiteral("INFRA-1203")),
                                       QFINDTESTDATA("data/issue-dc-done.json")));

        type(QStringLiteral("jira.issue.page")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requests().size(), 1);
        QVERIFY(!job.finished->data.contains(QStringLiteral("issue")));
    }

    // The sprint field is found by its name, and its id differs per site. An
    // open sprint wins over later ones, and otherwise the last one counts.
    void sprintOfAnIssue_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<QString>("sprint");
        const QJsonObject closed{{QStringLiteral("name"), QStringLiteral("Old")},
                                 {QStringLiteral("state"), QStringLiteral("closed")}};
        const QJsonObject active{{QStringLiteral("name"), QStringLiteral("Now")},
                                 {QStringLiteral("state"), QStringLiteral("active")}};
        const QJsonObject later{{QStringLiteral("name"), QStringLiteral("Next")},
                                {QStringLiteral("state"), QStringLiteral("future")}};
        QTest::newRow("none") << QJsonValue(QJsonArray()) << QString();
        QTest::newRow("null") << QJsonValue() << QString();
        QTest::newRow("open wins") << QJsonValue(QJsonArray{closed, active, later}) << QStringLiteral("Now");
        QTest::newRow("else the last") << QJsonValue(QJsonArray{closed, later}) << QStringLiteral("Next");
        QTest::newRow("text")
            << QJsonValue(QJsonArray{QStringLiteral("x.Sprint@1b[id=4,state=ACTIVE,name=Week, 44,goal=]")})
            << QStringLiteral("Week, 44");
        QTest::newRow("text at the end")
            << QJsonValue(QJsonArray{QStringLiteral("x.Sprint@1b[id=4,state=CLOSED,name=Week 44]")})
            << QStringLiteral("Week 44");
    }

    void sprintOfAnIssue()
    {
        QFETCH(QJsonValue, value);
        QFETCH(QString, sprint);
        const QJsonObject wrong{{QStringLiteral("name"), QStringLiteral("Wrong")}};
        const QJsonObject issue{
            {QStringLiteral("names"), QJsonObject{{QStringLiteral("customfield_1"), QStringLiteral("Story Points")},
                                                  {QStringLiteral("customfield_2"), QStringLiteral("Sprint")}}},
            {QStringLiteral("fields"), QJsonObject{{QStringLiteral("customfield_1"), QJsonArray{wrong}},
                                                   {QStringLiteral("customfield_2"), value}}},
        };
        QCOMPARE(mirante::modules::jira::sprintOf(issue), sprint);
    }

    void listReportsItsRows_data()
    {
        QTest::addColumn<bool>("cloud");
        QTest::addColumn<QString>("request");
        QTest::addColumn<QString>("file");
        const QString jql =
            QStringLiteral("assignee = currentUser() AND resolution = Unresolved ORDER BY updated DESC");
        QTest::newRow("cloud") << true << searchKey(QStringLiteral("rest/api/3/search/jql"), jql, false)
                               << QFINDTESTDATA("data/search-list-cloud.json");
        QTest::newRow("data center") << false << searchKey(QStringLiteral("rest/api/2/search"), jql, true)
                                     << QFINDTESTDATA("data/search-list-dc.json");
    }

    void listReportsItsRows()
    {
        QFETCH(bool, cloud);
        QFETCH(QString, request);
        QFETCH(QString, file);
        FakeStatusJob job;
        if (cloud)
            setCloud(job, QString());
        else
            setDataCenter(job, QString());
        job.configValues = {{QStringLiteral("preset"), QStringLiteral("myOpen")}};
        QVERIFY(job.client.setJsonFile(request, file));

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requests().size(), 1);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("3 issues"));
        QCOMPARE(status.data.value(QStringLiteral("total")).toInt(), 3);
        const QJsonArray rows = status.data.value(QStringLiteral("rows")).toArray();
        QCOMPARE(rows.size(), 3);
        const QJsonObject fields = rows.at(0).toObject().value(QStringLiteral("fields")).toObject();
        QVERIFY(fields.contains(QStringLiteral("summary")));
        QVERIFY(fields.contains(QStringLiteral("status")));
        QVERIFY(fields.contains(QStringLiteral("assignee")));
        QVERIFY(fields.contains(QStringLiteral("updated")));

        // A list raises nothing, and never counts towards a task being done.
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!status.closed.has_value());
    }

    void listSummaryCountsIssues_data()
    {
        QTest::addColumn<int>("rows");
        QTest::addColumn<QString>("summary");
        QTest::newRow("none") << 0 << QStringLiteral("0 issues");
        QTest::newRow("one") << 1 << QStringLiteral("1 issue");
        QTest::newRow("some") << 12 << QStringLiteral("12 issues");
        QTest::newRow("at the limit") << 50 << QStringLiteral("100+ issues");
    }

    // The last row covers a list that stops at the limit, over two pages.
    void listSummaryCountsIssues()
    {
        QFETCH(int, rows);
        QFETCH(QString, summary);
        QJsonArray issues;
        for (int i = 0; i < rows; ++i)
            issues.append(QJsonObject{{QStringLiteral("key"), QStringLiteral("OPS-%1").arg(i)}});

        const auto page = [](const QString &startAt) {
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("jql"), QStringLiteral("project = OPS"));
            query.addQueryItem(QStringLiteral("fields"), QStringLiteral("summary,status,assignee,updated"));
            query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
            query.addQueryItem(QStringLiteral("startAt"), startAt);
            return FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/2/search"), query);
        };

        FakeStatusJob job;
        setDataCenter(job, QString());
        job.configValues = {{QStringLiteral("preset"), QStringLiteral("jql")},
                            {QStringLiteral("jql"), QStringLiteral("project = OPS")}};
        const bool atLimit = rows == 50;
        const QJsonDocument answer(QJsonObject{{QStringLiteral("issues"), issues},
                                               {QStringLiteral("total"), atLimit ? 250 : rows}});
        job.client.setJson(page(QStringLiteral("0")), answer);
        job.client.setJson(page(QStringLiteral("50")), answer);

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->summary, summary);
        QCOMPARE(job.finished->data.value(QStringLiteral("total")).toInt(), atLimit ? 100 : rows);
    }

    void listWithoutWhatItNeedsSendsNothing_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("message");
        const QString project = QStringLiteral("Set the Jira project key in the project's settings.");
        QTest::newRow("sprint") << QStringLiteral("currentSprint") << project;
        QTest::newRow("recent") << QStringLiteral("recentlyUpdated") << project;
        QTest::newRow("jql") << QStringLiteral("jql") << QStringLiteral("Set the JQL query in the item's settings.");
    }

    void listWithoutWhatItNeedsSendsNothing()
    {
        QFETCH(QString, preset);
        QFETCH(QString, message);
        FakeStatusJob job;
        setCloud(job, QString());
        job.configValues = {{QStringLiteral("preset"), preset}};

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QCOMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::None);
        QCOMPARE(job.finished->message, message);
        QVERIFY(job.client.requests().isEmpty());
    }

    void listUsesTheProjectKey()
    {
        FakeStatusJob job;
        setCloud(job, QString());
        job.configValues = {{QStringLiteral("preset"), QStringLiteral("recentlyUpdated")}};
        job.projectSettings = {{QStringLiteral("projectKey"), QStringLiteral("OPS")}};
        job.client.setSynchronous(true);

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QCOMPARE(job.client.requests().at(0).query.queryItemValue(QStringLiteral("jql"), QUrl::FullyDecoded),
                 QStringLiteral("project = \"OPS\" AND updated >= -7d ORDER BY updated DESC"));
    }

    void listErrorsPassThrough()
    {
        FakeStatusJob job;
        setCloud(job, QString());
        job.configValues = {{QStringLiteral("preset"), QStringLiteral("myOpen")}};
        job.client.failAll(ApiReply::Error::NoToken, QStringLiteral("Token not set"));

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::NoToken);
        QCOMPARE(job.finished->message, QStringLiteral("Token not set"));
    }

    // "dc" is followed even on an atlassian.net address.
    void deploymentSettingChoosesTheRoot()
    {
        FakeStatusJob job;
        setCloud(job, QStringLiteral("OPS-7"));
        QJsonObject general = job.screens.value(QStringLiteral("general"));
        general.insert(QStringLiteral("deployment"), QStringLiteral("dc"));
        job.screens.insert(QStringLiteral("general"), general);
        job.client.setSynchronous(true);

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QCOMPARE(job.client.requests().at(0).path, QStringLiteral("rest/api/2/issue/OPS-7"));
    }

    void errorsPassThrough_data()
    {
        QTest::addColumn<int>("error");
        QTest::addColumn<int>("state");
        QTest::newRow("no token") << int(ApiReply::Error::NoToken) << int(ItemStatus::State::NoToken);
        QTest::newRow("no server") << int(ApiReply::Error::NoServer) << int(ItemStatus::State::NoServer);
        QTest::newRow("refused") << int(ApiReply::Error::Refused) << int(ItemStatus::State::Refused);
        QTest::newRow("network") << int(ApiReply::Error::Network) << int(ItemStatus::State::Error);
    }

    void errorsPassThrough()
    {
        QFETCH(int, error);
        QFETCH(int, state);
        FakeStatusJob job;
        setCloud(job, QStringLiteral("OPS-7"));
        job.client.failAll(ApiReply::Error(error), QStringLiteral("Something"));

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(int(job.finished->state), state);
        QCOMPARE(job.finished->message, QStringLiteral("Something"));
        QVERIFY(job.finished->attention.isEmpty());
    }

    void unknownIssueIsNotFound()
    {
        FakeStatusJob job;
        setCloud(job, QStringLiteral("OPS-999"));

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::NotFound);
    }

    void missingKeySendsNothing()
    {
        FakeStatusJob job;
        setCloud(job, QString());

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QCOMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::Error);
        QVERIFY(job.client.requests().isEmpty());
    }
};

QTEST_MAIN(TestStatus)
#include "tst_status.moc"
