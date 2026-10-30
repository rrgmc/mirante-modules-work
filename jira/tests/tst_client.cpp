// JiraClient on both deployments: the API root, the paths and queries, and the
// two search paging styles. Answered by a FakeApiClient from recorded responses
// in tests/data/.
#include "../JiraClient.h"

#include <mirante/testing/FakeApiClient.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include <optional>

using mirante::ApiReply;
using mirante::modules::jira::Deployment;
using mirante::modules::jira::JiraClient;
using mirante::modules::jira::SearchOptions;
using mirante::testing::FakeApiClient;

namespace {

QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("tests/data/") + name);
}

// A query built in the order JiraClient adds its items.
QUrlQuery query(const QList<QPair<QString, QString>> &items)
{
    QUrlQuery result;
    for (const auto &item : items)
        result.addQueryItem(item.first, item.second);
    return result;
}

QStringList keysOf(const ApiReply &reply)
{
    QStringList keys;
    const QJsonArray issues = reply.json.object().value(QStringLiteral("issues")).toArray();
    for (const QJsonValue &issue : issues)
        keys << issue.toObject().value(QStringLiteral("key")).toString();
    return keys;
}

const QString kCloudToken = QStringLiteral("Ch0jU3RyaW5nJlQxQlRMVUM9JUludCZNVEF3TkRNPRAC+GA");

} // namespace

class TestClient : public QObject
{
    Q_OBJECT

private slots:
    void apiRoot()
    {
        FakeApiClient server;
        QCOMPARE(JiraClient(server, Deployment::Cloud).apiRoot(), QStringLiteral("rest/api/3"));
        QCOMPARE(JiraClient(server, Deployment::DataCenter).apiRoot(), QStringLiteral("rest/api/2"));
    }

    void issueWithOtherExpandValues()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        JiraClient(server, Deployment::Cloud)
            .issue(
                QStringLiteral("OPS-7"), {}, nullptr, [](const ApiReply &) {},
                {QStringLiteral("renderedFields"), QStringLiteral("names")});
        QCOMPARE(server.requestKeys(),
                 QStringList{FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/issue/OPS-7"),
                                                query({{QStringLiteral("expand"),
                                                        QStringLiteral("renderedFields,names")}}))});
    }

    void issue()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        JiraClient cloud(server, Deployment::Cloud);
        JiraClient dc(server, Deployment::DataCenter);

        std::optional<ApiReply> answer;
        cloud.issue(QStringLiteral("OPS-7"), {QStringLiteral("summary"), QStringLiteral("status")}, nullptr,
                    [&](const ApiReply &reply) { answer = reply; });
        dc.issue(QStringLiteral("INFRA-1203"), {}, nullptr, [](const ApiReply &) {});

        QVERIFY(answer);
        QCOMPARE(answer->error, ApiReply::Error::NotFound);   // nothing recorded
        QCOMPARE(server.requestKeys(),
                 QStringList({FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/issue/OPS-7"),
                                                 query({{QStringLiteral("fields"), QStringLiteral("summary,status")},
                                                        {QStringLiteral("expand"),
                                                         QStringLiteral("renderedFields")}})),
                              FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/2/issue/INFRA-1203"),
                                                 query({{QStringLiteral("expand"),
                                                         QStringLiteral("renderedFields")}}))}));
        // Jira pages itself; the client never follows Link headers.
        QVERIFY(!server.requests().at(0).options.followLinks);
    }

    // A key is one path segment, even if it holds a slash.
    void keyIsEncoded()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        JiraClient(server, Deployment::Cloud).issue(QStringLiteral("A/B 1"), {}, nullptr, [](const ApiReply &) {});
        QCOMPARE(server.requests().at(0).path, QStringLiteral("rest/api/3/issue/A%2FB%201"));
    }

    void searchCloudFollowsNextPageToken()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        const QString jql = QStringLiteral("project = OPS AND text ~ \"a+b\"");
        const QString path = QStringLiteral("rest/api/3/search/jql");
        const QString encodedJql = QStringLiteral("project = OPS AND text ~ \"a%2Bb\"");
        QVERIFY(server.setJsonFile(FakeApiClient::key(QStringLiteral("GET"), path,
                                                      query({{QStringLiteral("jql"), encodedJql},
                                                             {QStringLiteral("fields"), QStringLiteral("summary")},
                                                             {QStringLiteral("maxResults"), QStringLiteral("2")}})),
                                   dataFile(QStringLiteral("search-cloud-1.json"))));
        QString encodedToken = kCloudToken;
        encodedToken.replace(QLatin1Char('+'), QLatin1String("%2B"));
        QVERIFY(server.setJsonFile(FakeApiClient::key(QStringLiteral("GET"), path,
                                                      query({{QStringLiteral("jql"), encodedJql},
                                                             {QStringLiteral("fields"), QStringLiteral("summary")},
                                                             {QStringLiteral("maxResults"), QStringLiteral("2")},
                                                             {QStringLiteral("nextPageToken"), encodedToken}})),
                                   dataFile(QStringLiteral("search-cloud-2.json"))));

        SearchOptions options;
        options.fields = {QStringLiteral("summary")};
        options.pageSize = 2;
        std::optional<ApiReply> answer;
        JiraClient(server, Deployment::Cloud).search(jql, options, nullptr, [&](const ApiReply &reply) {
            answer = reply;
        });

        QVERIFY(answer);
        QVERIFY2(answer->ok(), qPrintable(server.requestKeys().join(QLatin1Char('\n'))));
        QCOMPARE(keysOf(*answer), QStringList({QStringLiteral("OPS-7"), QStringLiteral("OPS-8"),
                                               QStringLiteral("OPS-12")}));
        QCOMPARE(server.requests().size(), 2);
        // The server gets the text as typed, "+" included.
        QCOMPARE(server.requests().at(0).query.queryItemValue(QStringLiteral("jql"), QUrl::FullyDecoded), jql);
        QCOMPARE(server.requests().at(1).query.queryItemValue(QStringLiteral("nextPageToken"), QUrl::FullyDecoded),
                 kCloudToken);
        QVERIFY(!server.requests().at(0).query.hasQueryItem(QStringLiteral("startAt")));
    }

    void searchDataCenterFollowsStartAt()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        const QString path = QStringLiteral("rest/api/2/search");
        const auto pageKey = [&](const QString &startAt) {
            return FakeApiClient::key(QStringLiteral("GET"), path,
                                      query({{QStringLiteral("jql"), QStringLiteral("project = INFRA")},
                                             {QStringLiteral("maxResults"), QStringLiteral("2")},
                                             {QStringLiteral("startAt"), startAt}}));
        };
        QVERIFY(server.setJsonFile(pageKey(QStringLiteral("0")), dataFile(QStringLiteral("search-dc-1.json"))));
        QVERIFY(server.setJsonFile(pageKey(QStringLiteral("2")), dataFile(QStringLiteral("search-dc-2.json"))));

        SearchOptions options;
        options.pageSize = 2;
        std::optional<ApiReply> answer;
        JiraClient(server, Deployment::DataCenter)
            .search(QStringLiteral("project = INFRA"), options, nullptr, [&](const ApiReply &reply) {
                answer = reply;
            });

        QVERIFY(answer);
        QVERIFY2(answer->ok(), qPrintable(server.requestKeys().join(QLatin1Char('\n'))));
        QCOMPARE(keysOf(*answer), QStringList({QStringLiteral("INFRA-1203"), QStringLiteral("INFRA-1204"),
                                               QStringLiteral("INFRA-1212")}));
        // The total is reached, so there is no third request.
        QCOMPARE(server.requests().size(), 2);
    }

    // Data Center gives a total, and Cloud's search/jql does not.
    void searchPassesTheTotal()
    {
        const auto issues = QJsonArray{QJsonObject{{QStringLiteral("key"), QStringLiteral("OPS-1")}}};
        SearchOptions options;
        options.maxRows = 1;

        FakeApiClient server;
        server.setSynchronous(true);
        server.setJson(FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/2/search"),
                                          query({{QStringLiteral("jql"), QStringLiteral("a")},
                                                 {QStringLiteral("maxResults"), QStringLiteral("1")},
                                                 {QStringLiteral("startAt"), QStringLiteral("0")}})),
                       QJsonDocument(QJsonObject{{QStringLiteral("issues"), issues}, {QStringLiteral("total"), 7}}));
        server.setJson(FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/search/jql"),
                                          query({{QStringLiteral("jql"), QStringLiteral("a")},
                                                 {QStringLiteral("maxResults"), QStringLiteral("1")}})),
                       QJsonDocument(QJsonObject{{QStringLiteral("issues"), issues}, {QStringLiteral("isLast"), true}}));

        std::optional<ApiReply> answer;
        const auto keep = [&answer](const ApiReply &reply) { answer = reply; };
        JiraClient(server, Deployment::DataCenter).search(QStringLiteral("a"), options, nullptr, keep);
        QVERIFY(answer && answer->ok());
        QCOMPARE(answer->json.object().value(QStringLiteral("total")).toInt(), 7);

        JiraClient(server, Deployment::Cloud).search(QStringLiteral("a"), options, nullptr, keep);
        QVERIFY(answer->ok());
        QVERIFY(!answer->json.object().contains(QStringLiteral("total")));
    }

    void searchAsksForExpandAndKeepsTheNames()
    {
        SearchOptions options;
        options.fields = QStringList{QStringLiteral("summary"), QStringLiteral("customfield_1")};
        options.expand = QStringList{QStringLiteral("names")};
        const QJsonObject names{{QStringLiteral("customfield_1"), QStringLiteral("Team")}};

        FakeApiClient server;
        server.setSynchronous(true);
        server.setJson(FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/search/jql"),
                                          query({{QStringLiteral("jql"), QStringLiteral("a")},
                                                 {QStringLiteral("fields"), QStringLiteral("summary,customfield_1")},
                                                 {QStringLiteral("expand"), QStringLiteral("names")},
                                                 {QStringLiteral("maxResults"), QStringLiteral("50")}})),
                       QJsonDocument(QJsonObject{{QStringLiteral("issues"), QJsonArray()},
                                                 {QStringLiteral("isLast"), true},
                                                 {QStringLiteral("names"), names}}));

        std::optional<ApiReply> answer;
        JiraClient(server, Deployment::Cloud)
            .search(QStringLiteral("a"), options, nullptr, [&answer](const ApiReply &reply) { answer = reply; });
        QVERIFY(answer && answer->ok());
        QCOMPARE(answer->json.object().value(QStringLiteral("names")).toObject(), names);
    }

    void allFields()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        JiraClient(server, Deployment::Cloud).allFields(nullptr, [](const ApiReply &) {});
        JiraClient(server, Deployment::DataCenter).allFields(nullptr, [](const ApiReply &) {});
        QCOMPARE(server.requestKeys(),
                 QStringList({QStringLiteral("GET rest/api/3/field"), QStringLiteral("GET rest/api/2/field")}));
    }

    void searchStopsAtMaxRows()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        QVERIFY(server.setJsonFile(
            FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/2/search"),
                               query({{QStringLiteral("jql"), QStringLiteral("project = INFRA")},
                                      {QStringLiteral("maxResults"), QStringLiteral("2")},
                                      {QStringLiteral("startAt"), QStringLiteral("0")}})),
            dataFile(QStringLiteral("search-dc-1.json"))));

        SearchOptions options;
        options.maxRows = 2;
        options.pageSize = 50;
        std::optional<ApiReply> answer;
        JiraClient(server, Deployment::DataCenter)
            .search(QStringLiteral("project = INFRA"), options, nullptr, [&](const ApiReply &reply) {
                answer = reply;
            });

        QVERIFY(answer && answer->ok());
        QCOMPARE(keysOf(*answer).size(), 2);
        QCOMPARE(server.requests().size(), 1);
    }

    // An error on a later page answers with that error.
    void searchPassesAnError()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        QVERIFY(server.setJsonFile(
            FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/2/search"),
                               query({{QStringLiteral("jql"), QStringLiteral("x")},
                                      {QStringLiteral("maxResults"), QStringLiteral("2")},
                                      {QStringLiteral("startAt"), QStringLiteral("0")}})),
            dataFile(QStringLiteral("search-dc-1.json"))));

        SearchOptions options;
        options.pageSize = 2;
        std::optional<ApiReply> answer;
        JiraClient(server, Deployment::DataCenter).search(QStringLiteral("x"), options, nullptr,
                                                          [&](const ApiReply &reply) { answer = reply; });
        QVERIFY(answer);
        QCOMPARE(answer->error, ApiReply::Error::NotFound);
        QCOMPARE(server.requests().size(), 2);
    }

    // The search outlives the JiraClient that started it, and its pages
    // arrive later, as a real client's do.
    void searchOutlivesTheClient()
    {
        FakeApiClient server;
        QVERIFY(server.setJsonFile(
            FakeApiClient::key(QStringLiteral("GET"), QStringLiteral("rest/api/3/search/jql"),
                               query({{QStringLiteral("jql"), QStringLiteral("x")},
                                      {QStringLiteral("maxResults"), QStringLiteral("50")}})),
            dataFile(QStringLiteral("search-cloud-2.json"))));
        std::optional<ApiReply> answer;
        QObject receiver;
        {
            const JiraClient client(server, Deployment::Cloud);
            client.search(QStringLiteral("x"), SearchOptions(), &receiver,
                          [&](const ApiReply &reply) { answer = reply; });
        }
        QTRY_VERIFY(answer);
        QCOMPARE(keysOf(*answer), QStringList({QStringLiteral("OPS-12")}));
    }

    void myself()
    {
        FakeApiClient server;
        server.setSynchronous(true);
        JiraClient(server, Deployment::Cloud).myself(nullptr, [](const ApiReply &) {});
        JiraClient(server, Deployment::DataCenter).myself(nullptr, [](const ApiReply &) {});
        QCOMPARE(server.requestKeys(),
                 QStringList({QStringLiteral("GET rest/api/3/myself"), QStringLiteral("GET rest/api/2/myself")}));
    }
};

QTEST_MAIN(TestClient)
#include "tst_client.moc"
