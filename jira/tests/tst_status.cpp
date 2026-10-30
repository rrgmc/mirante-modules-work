// The status of the Jira issue types, from recorded responses of Jira Cloud
// and Jira Data Center in tests/data/. See README.md#status.
#include "items/ItemTypeRegistry.h"

#include <mirante/testing/FakeStatusJob.h>

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
        // Lists report no status in M5.
        QVERIFY(!type(QStringLiteral("jira.list")).hasStatus());
    }

    void openIssueOnCloud_data()
    {
        QTest::addColumn<QString>("typeId");
        QTest::newRow("summary") << QStringLiteral("jira.issue.summary");
        QTest::newRow("page") << QStringLiteral("jira.issue.page");
    }

    void openIssueOnCloud()
    {
        QFETCH(QString, typeId);
        FakeStatusJob job;
        setCloud(job, QStringLiteral("OPS-7"));
        QVERIFY(job.client.setJsonFile(issueKey(QStringLiteral("rest/api/3"), QStringLiteral("OPS-7")),
                                       QFINDTESTDATA("data/issue-cloud.json")));

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
