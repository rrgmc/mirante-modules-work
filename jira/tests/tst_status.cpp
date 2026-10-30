// The status of the Jira types, from recorded responses of Jira Cloud and Jira
// Data Center in tests/data/. See README.md#status.
#include "../JiraFields.h"
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

// The search for a summary's children.
QString childrenKey(const QString &path, const QString &jql, const QString &maxResults, bool startAt)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("jql"), jql);
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("summary,status"));
    query.addQueryItem(QStringLiteral("maxResults"), maxResults);
    if (startAt)
        query.addQueryItem(QStringLiteral("startAt"), QStringLiteral("0"));
    return FakeApiClient::key(QStringLiteral("GET"), path, query);
}

const QString kCloudChildrenPath = QStringLiteral("rest/api/3/search/jql");
const QString kDcChildrenPath = QStringLiteral("rest/api/2/search");
const QString kCloudChildrenJql = QStringLiteral("parent = \"OPS-7\"");
const QString kDcChildrenJql = QStringLiteral("\"Epic Link\" = \"INFRA-1203\" OR parent = \"INFRA-1203\"");

QStringList keysOf(const QJsonArray &issues)
{
    QStringList keys;
    for (const QJsonValue &issue : issues)
        keys << issue.toObject().value(QStringLiteral("key")).toString();
    return keys;
}

QJsonArray numberedIssues(const QString &project, int count)
{
    QJsonArray issues;
    for (int i = 1; i <= count; ++i)
        issues.append(QJsonObject{{QStringLiteral("key"), QStringLiteral("%1-%2").arg(project).arg(1000 + i)}});
    return issues;
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
        QCOMPARE(job.client.requestKeys().first(), request);

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

    // The children come from a further search. A sub-task the issue already
    // lists is not listed twice.
    void childrenOnCloud()
    {
        FakeStatusJob job;
        setCloud(job, QStringLiteral("OPS-7"));
        QVERIFY(job.client.setJsonFile(summaryKey(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7")),
                                       QFINDTESTDATA("data/issue-full-cloud.json")));
        const QString request = childrenKey(kCloudChildrenPath, kCloudChildrenJql, QStringLiteral("100"), false);
        QVERIFY(job.client.setJsonFile(request, QFINDTESTDATA("data/search-children-cloud.json")));

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requests().size(), 2);
        QCOMPARE(job.client.requestKeys().last(), request);

        const ItemStatus &status = *job.finished;
        QCOMPARE(keysOf(status.data.value(QStringLiteral("children")).toArray()),
                 QStringList({QStringLiteral("OPS-11"), QStringLiteral("OPS-12")}));
        QCOMPARE(status.data.value(QStringLiteral("childrenTotal")).toInt(), 2);
    }

    void childrenOnDataCenter()
    {
        FakeStatusJob job;
        setDataCenter(job, QStringLiteral("INFRA-1203"));
        QVERIFY(job.client.setJsonFile(summaryKey(QStringLiteral("rest/api/2"), QStringLiteral("INFRA-1203")),
                                       QFINDTESTDATA("data/issue-full-dc.json")));
        const QString request = childrenKey(kDcChildrenPath, kDcChildrenJql, QStringLiteral("50"), true);
        QVERIFY(job.client.setJsonFile(request, QFINDTESTDATA("data/search-children-dc.json")));

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requests().size(), 2);

        const ItemStatus &status = *job.finished;
        QCOMPARE(keysOf(status.data.value(QStringLiteral("children")).toArray()),
                 QStringList({QStringLiteral("INFRA-1210"), QStringLiteral("INFRA-1211")}));
        QCOMPARE(status.data.value(QStringLiteral("childrenTotal")).toInt(), 2);
    }

    // Data Center's total counts all of them, from one request of 50.
    void childrenAreCappedOnDataCenter()
    {
        FakeStatusJob job;
        setDataCenter(job, QStringLiteral("INFRA-1203"));
        QVERIFY(job.client.setJsonFile(summaryKey(QStringLiteral("rest/api/2"), QStringLiteral("INFRA-1203")),
                                       QFINDTESTDATA("data/issue-full-dc.json")));
        job.client.setJson(childrenKey(kDcChildrenPath, kDcChildrenJql, QStringLiteral("50"), true),
                           QJsonDocument(QJsonObject{{QStringLiteral("issues"), numberedIssues(QStringLiteral("INFRA"), 50)},
                                                     {QStringLiteral("total"), 73}}));

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requests().size(), 2);
        QCOMPARE(job.finished->data.value(QStringLiteral("children")).toArray().size(), 50);
        QCOMPARE(job.finished->data.value(QStringLiteral("childrenTotal")).toInt(), 73);
    }

    // Cloud gives no total, so the rows are counted. OPS-9 is also a sub-task.
    void childrenAreCappedOnCloud()
    {
        FakeStatusJob job;
        setCloud(job, QStringLiteral("OPS-7"));
        QVERIFY(job.client.setJsonFile(summaryKey(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7")),
                                       QFINDTESTDATA("data/issue-full-cloud.json")));
        QJsonArray issues = numberedIssues(QStringLiteral("OPS"), 59);
        issues.prepend(QJsonObject{{QStringLiteral("key"), QStringLiteral("OPS-9")}});
        job.client.setJson(childrenKey(kCloudChildrenPath, kCloudChildrenJql, QStringLiteral("100"), false),
                           QJsonDocument(QJsonObject{{QStringLiteral("issues"), issues},
                                                     {QStringLiteral("isLast"), true}}));

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        const QJsonArray children = job.finished->data.value(QStringLiteral("children")).toArray();
        QCOMPARE(children.size(), 50);
        QVERIFY(!keysOf(children).contains(QStringLiteral("OPS-9")));
        QCOMPARE(job.finished->data.value(QStringLiteral("childrenTotal")).toInt(), 59);
    }

    // A site that does not know parent or Epic Link answers the search with an
    // error, and the status of the issue stands.
    void failedChildrenKeepTheStatus_data()
    {
        QTest::addColumn<bool>("cloud");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("issueRequest");
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("childrenRequest");
        QTest::newRow("cloud") << true << QStringLiteral("OPS-7")
                               << summaryKey(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7"))
                               << QFINDTESTDATA("data/issue-full-cloud.json")
                               << childrenKey(kCloudChildrenPath, kCloudChildrenJql, QStringLiteral("100"), false);
        QTest::newRow("data center") << false << QStringLiteral("INFRA-1203")
                                     << summaryKey(QStringLiteral("rest/api/2"), QStringLiteral("INFRA-1203"))
                                     << QFINDTESTDATA("data/issue-full-dc.json")
                                     << childrenKey(kDcChildrenPath, kDcChildrenJql, QStringLiteral("50"), true);
    }

    void failedChildrenKeepTheStatus()
    {
        QFETCH(bool, cloud);
        QFETCH(QString, key);
        QFETCH(QString, issueRequest);
        QFETCH(QString, file);
        QFETCH(QString, childrenRequest);
        FakeStatusJob job;
        if (cloud)
            setCloud(job, key);
        else
            setDataCenter(job, key);
        QVERIFY(job.client.setJsonFile(issueRequest, file));
        ApiReply refused;
        refused.error = ApiReply::Error::Http;
        refused.httpStatus = 400;
        refused.message = QStringLiteral("Field 'Epic Link' does not exist");
        job.client.setReply(childrenRequest, refused);

        type(QStringLiteral("jira.issue.summary")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requestKeys().last(), childrenRequest);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(status.message.isEmpty());
        QCOMPARE(status.data.value(QStringLiteral("key")).toString(), key);
        QVERIFY(status.data.contains(QStringLiteral("issue")));
        QVERIFY(!status.data.contains(QStringLiteral("children")));
        QVERIFY(!status.data.contains(QStringLiteral("childrenTotal")));
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

    // The item's limit is the rows asked for, and the summary's number. An absent
    // limit is covered above.
    void listLimitIsTheRowsAskedFor_data()
    {
        QTest::addColumn<QJsonValue>("limit");
        QTest::addColumn<QString>("maxResults");
        QTest::addColumn<QString>("summary");
        QTest::newRow("small") << QJsonValue(2) << QStringLiteral("2") << QStringLiteral("2+ issues");
        QTest::newRow("zero uses the default") << QJsonValue(0) << QStringLiteral("50") << QStringLiteral("2 issues");
        QTest::newRow("negative uses the default") << QJsonValue(-1) << QStringLiteral("50") << QStringLiteral("2 issues");
    }

    void listLimitIsTheRowsAskedFor()
    {
        QFETCH(QJsonValue, limit);
        QFETCH(QString, maxResults);
        QFETCH(QString, summary);
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("jql"), QStringLiteral("project = OPS"));
        query.addQueryItem(QStringLiteral("fields"), QStringLiteral("summary,status,assignee,updated"));
        query.addQueryItem(QStringLiteral("maxResults"), maxResults);
        query.addQueryItem(QStringLiteral("startAt"), QStringLiteral("0"));

        FakeStatusJob job;
        setDataCenter(job, QString());
        job.configValues = {{QStringLiteral("preset"), QStringLiteral("jql")},
                            {QStringLiteral("jql"), QStringLiteral("project = OPS")},
                            {QStringLiteral("limit"), limit}};
        const QJsonArray issues{QJsonObject{{QStringLiteral("key"), QStringLiteral("OPS-1")}},
                                QJsonObject{{QStringLiteral("key"), QStringLiteral("OPS-2")}}};
        job.client.setJson(FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/2/search"), query),
                           QJsonDocument(QJsonObject{{QStringLiteral("issues"), issues}, {QStringLiteral("total"), 2}}));

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->summary, summary);
    }

    void fieldValues_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<QString>("text");
        const auto object = [](const char *key, const QString &value) {
            return QJsonValue(QJsonObject{{QLatin1String(key), value}});
        };
        QTest::newRow("text") << QJsonValue(QStringLiteral("Build agents")) << QStringLiteral("Build agents");
        QTest::newRow("whole number") << QJsonValue(5) << QStringLiteral("5");
        QTest::newRow("large number") << QJsonValue(1200000) << QStringLiteral("1200000");
        QTest::newRow("fraction") << QJsonValue(5.5) << QStringLiteral("5.5");
        QTest::newRow("name") << object("name", QStringLiteral("Platform")) << QStringLiteral("Platform");
        QTest::newRow("value") << object("value", QStringLiteral("High")) << QStringLiteral("High");
        QTest::newRow("displayName") << object("displayName", QStringLiteral("Dora")) << QStringLiteral("Dora");
        QTest::newRow("name comes first")
            << QJsonValue(QJsonObject{{QStringLiteral("displayName"), QStringLiteral("B")},
                                      {QStringLiteral("name"), QStringLiteral("A")}})
            << QStringLiteral("A");
        QTest::newRow("list of objects")
            << QJsonValue(QJsonArray{object("name", QStringLiteral("2026.11")), object("name", QStringLiteral("2026.12"))})
            << QStringLiteral("2026.11, 2026.12");
        QTest::newRow("list of text") << QJsonValue(QJsonArray{QStringLiteral("a"), QStringLiteral("b")})
                                      << QStringLiteral("a, b");
        QTest::newRow("list with gaps") << QJsonValue(QJsonArray{QJsonValue(), QStringLiteral("b")}) << QStringLiteral("b");
        QTest::newRow("empty list") << QJsonValue(QJsonArray()) << QString();
        QTest::newRow("null") << QJsonValue() << QString();
        QTest::newRow("object without a name") << QJsonValue(QJsonObject{{QStringLiteral("type"), QStringLiteral("doc")}})
                                               << QString();
    }

    void fieldValues()
    {
        QFETCH(QJsonValue, value);
        QFETCH(QString, text);
        QCOMPARE(mirante::modules::jira::fieldText(value), text);
    }

    void fieldsByName_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("id");
        QTest::newRow("exact") << QStringLiteral("Team") << QStringLiteral("customfield_1");
        QTest::newRow("case") << QStringLiteral("tEAM") << QStringLiteral("customfield_1");
        QTest::newRow("spaces") << QStringLiteral(" Team ") << QStringLiteral("customfield_1");
        QTest::newRow("without the s") << QStringLiteral("Fix version") << QStringLiteral("fixVersions");
        QTest::newRow("with the s") << QStringLiteral("Fix versions") << QStringLiteral("fixVersions");
        QTest::newRow("data center's name") << QStringLiteral("Component") << QStringLiteral("components");
        QTest::newRow("an added s") << QStringLiteral("Teams") << QStringLiteral("customfield_1");
        QTest::newRow("exact comes first") << QStringLiteral("Label") << QStringLiteral("customfield_9");
        QTest::newRow("unknown") << QStringLiteral("Nothing here") << QString();
        QTest::newRow("empty") << QStringLiteral("  ") << QString();
    }

    void fieldsByName()
    {
        QFETCH(QString, name);
        QFETCH(QString, id);
        const QJsonObject names{{QStringLiteral("customfield_1"), QStringLiteral("Team")},
                                {QStringLiteral("fixVersions"), QStringLiteral("Fix versions")},
                                {QStringLiteral("components"), QStringLiteral("Component/s")},
                                {QStringLiteral("a_labels"), QStringLiteral("Labels")},
                                {QStringLiteral("customfield_9"), QStringLiteral("Label")}};
        QCOMPARE(mirante::modules::jira::fieldIdByName(names, name), id);
    }

    // The ids come from GET field, and the search asks for those fields.
    void listAsksForExtraFields_data()
    {
        QTest::addColumn<bool>("cloud");
        QTest::addColumn<QString>("root");
        QTest::addColumn<QString>("searchPath");
        QTest::addColumn<QString>("fields");
        QTest::addColumn<QString>("fieldsFile");
        QTest::addColumn<QString>("searchFile");
        QTest::addColumn<QStringList>("ids");
        QTest::newRow("cloud") << true << QStringLiteral("rest/api/3") << QStringLiteral("rest/api/3/search/jql")
                               << QStringLiteral("summary,status,assignee,updated,customfield_10030,fixVersions")
                               << QFINDTESTDATA("data/fields-cloud.json") << QFINDTESTDATA("data/search-extra-cloud.json")
                               << QStringList{QStringLiteral("customfield_10030"), QStringLiteral("fixVersions")};
        QTest::newRow("data center") << false << QStringLiteral("rest/api/2") << QStringLiteral("rest/api/2/search")
                                     << QStringLiteral("summary,status,assignee,updated,customfield_10130,fixVersions")
                                     << QFINDTESTDATA("data/fields-dc.json") << QFINDTESTDATA("data/search-extra-dc.json")
                                     << QStringList{QStringLiteral("customfield_10130"), QStringLiteral("fixVersions")};
    }

    void listAsksForExtraFields()
    {
        QFETCH(bool, cloud);
        QFETCH(QString, root);
        QFETCH(QString, searchPath);
        QFETCH(QString, fields);
        QFETCH(QString, fieldsFile);
        QFETCH(QString, searchFile);
        QFETCH(QStringList, ids);
        FakeStatusJob job;
        cloud ? setCloud(job, QString()) : setDataCenter(job, QString());
        job.configValues = {{QStringLiteral("preset"), QStringLiteral("jql")},
                            {QStringLiteral("jql"), QStringLiteral("project = OPS")}};
        QJsonObject general = job.screens.value(QStringLiteral("general"));
        general.insert(QStringLiteral("extraFields"),
                       QJsonArray{QStringLiteral("team"), QStringLiteral("Fix version"), QStringLiteral("Not on this site")});
        job.screens.insert(QStringLiteral("general"), general);

        const QString fieldsRequest = FakeApiClient::key(QStringLiteral("GET"), root + QStringLiteral("/field"));
        QVERIFY(job.client.setJsonFile(fieldsRequest, fieldsFile));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("jql"), QStringLiteral("project = OPS"));
        query.addQueryItem(QStringLiteral("fields"), fields);
        query.addQueryItem(QStringLiteral("expand"), QStringLiteral("names"));
        query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
        if (!cloud)
            query.addQueryItem(QStringLiteral("startAt"), QStringLiteral("0"));
        const QString searchRequest = FakeApiClient::key(QStringLiteral("GET"), searchPath, query);
        QVERIFY(job.client.setJsonFile(searchRequest, searchFile));

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.client.requestKeys(), QStringList({fieldsRequest, searchRequest}));

        const QJsonObject names = job.finished->data.value(QStringLiteral("names")).toObject();
        for (const QString &id : ids)
            QVERIFY2(names.contains(id), qPrintable(id));
        const QJsonObject first = job.finished->data.value(QStringLiteral("rows")).toArray().at(0).toObject();
        QVERIFY(first.value(QStringLiteral("fields")).toObject().contains(ids.at(0)));
    }

    // A server that ignores expand answers without names. The ids are known.
    void extraFieldsSurviveAnIgnoredExpand()
    {
        FakeStatusJob job;
        setCloud(job, QString());
        job.configValues = {{QStringLiteral("preset"), QStringLiteral("jql")},
                            {QStringLiteral("jql"), QStringLiteral("project = OPS")}};
        QJsonObject general = job.screens.value(QStringLiteral("general"));
        general.insert(QStringLiteral("extraFields"), QJsonArray{QStringLiteral("Team")});
        job.screens.insert(QStringLiteral("general"), general);
        QVERIFY(job.client.setJsonFile(FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/field")),
                                       QFINDTESTDATA("data/fields-cloud.json")));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("jql"), QStringLiteral("project = OPS"));
        query.addQueryItem(QStringLiteral("fields"), QStringLiteral("summary,status,assignee,updated,customfield_10030"));
        query.addQueryItem(QStringLiteral("expand"), QStringLiteral("names"));
        query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("50"));
        job.client.setJson(FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/search/jql"), query),
                           QJsonDocument(QJsonObject{{QStringLiteral("issues"), QJsonArray()},
                                                     {QStringLiteral("isLast"), true}}));

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->data.value(QStringLiteral("names")).toObject(),
                 (QJsonObject{{QStringLiteral("customfield_10030"), QStringLiteral("Team")}}));
    }

    // Without the ids, or with none that match, the list has its usual columns.
    void extraFieldsNeverBreakTheList_data()
    {
        QTest::addColumn<bool>("fieldsFail");
        QTest::newRow("GET field fails") << true;
        QTest::newRow("no name matches") << false;
    }

    void extraFieldsNeverBreakTheList()
    {
        QFETCH(bool, fieldsFail);
        FakeStatusJob job;
        setCloud(job, QString());
        job.configValues = {{QStringLiteral("preset"), QStringLiteral("myOpen")}};
        QJsonObject general = job.screens.value(QStringLiteral("general"));
        general.insert(QStringLiteral("extraFields"), QJsonArray{QStringLiteral("Not on this site")});
        job.screens.insert(QStringLiteral("general"), general);
        if (!fieldsFail) {
            QVERIFY(job.client.setJsonFile(FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/field")),
                                           QFINDTESTDATA("data/fields-cloud.json")));
        }
        const QString jql =
            QStringLiteral("assignee = currentUser() AND resolution = Unresolved ORDER BY updated DESC");
        QVERIFY(job.client.setJsonFile(searchKey(QStringLiteral("rest/api/3/search/jql"), jql, false),
                                       QFINDTESTDATA("data/search-list-cloud.json")));

        type(QStringLiteral("jira.list")).fetchStatus(job);
        QTRY_COMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->data.value(QStringLiteral("total")).toInt(), 3);
        QVERIFY(!job.finished->data.contains(QStringLiteral("names")));
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
