// The GitLab lists: their queries, their sizes, their addresses on the site and
// their status, on responses in tests/data/ written by hand in the shape of GitLab's
// API. "Me" is the user in data/user.json, id 1001.
#include "../GitLabLists.h"

#include <mirante/testing/FakeStatusJob.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeStatusJob;
using namespace mirante::modules::gitlab;

namespace {

const QString kQuery = QStringLiteral("?state=opened&scope=all&order_by=updated_at&per_page=100");
const QString kUser = QStringLiteral("GET api/v4/user");

QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("data/") + name);
}

QJsonArray readRows(const QString &name)
{
    QFile file(dataFile(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll()).array();
}

// So many rows, as GitLab would answer them.
QJsonDocument rows(int count)
{
    QJsonArray array;
    for (int i = 0; i < count; ++i)
        array.append(QJsonObject{{QStringLiteral("iid"), i + 1}});
    return QJsonDocument(array);
}

QString queryText(ListKind kind, const QString &preset, const QString &filter = {}, qint64 userId = 1001)
{
    return listQuery(kind, preset, userId, filter).toString(QUrl::FullyEncoded);
}

QJsonObject allOpen(const QString &scope = QStringLiteral("all"))
{
    return QJsonObject{{QStringLiteral("scope"), scope}, {QStringLiteral("preset"), QStringLiteral("allOpen")}};
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
    void presets()
    {
        const auto mergeRequests = ListKind::MergeRequests;
        QCOMPARE(queryText(mergeRequests, QStringLiteral("createdByMe")),
                 QStringLiteral("state=opened&scope=created_by_me&order_by=updated_at"));
        QCOMPARE(queryText(mergeRequests, QStringLiteral("assignedToMe")),
                 QStringLiteral("state=opened&scope=assigned_to_me&order_by=updated_at"));
        QCOMPARE(queryText(mergeRequests, QStringLiteral("reviewRequested")),
                 QStringLiteral("state=opened&scope=all&reviewer_id=1001&order_by=updated_at"));
        QCOMPARE(queryText(mergeRequests, QStringLiteral("allOpen")),
                 QStringLiteral("state=opened&scope=all&order_by=updated_at"));

        const auto issues = ListKind::Issues;
        QCOMPARE(queryText(issues, QStringLiteral("createdByMe")),
                 QStringLiteral("state=opened&scope=created_by_me&order_by=updated_at"));
        QCOMPARE(queryText(issues, QStringLiteral("assignedToMe")),
                 QStringLiteral("state=opened&scope=assigned_to_me&order_by=updated_at"));
        QCOMPARE(queryText(issues, QStringLiteral("allOpen")),
                 QStringLiteral("state=opened&scope=all&order_by=updated_at"));
    }

    // Any other preset is the first choice of the type's form. Issues have no
    // reviewRequested.
    void otherPresets()
    {
        QCOMPARE(queryText(ListKind::MergeRequests, QString()),
                 QStringLiteral("state=opened&scope=created_by_me&order_by=updated_at"));
        QCOMPARE(queryText(ListKind::Issues, QString()),
                 QStringLiteral("state=opened&scope=assigned_to_me&order_by=updated_at"));
        QCOMPARE(queryText(ListKind::Issues, QStringLiteral("reviewRequested")),
                 QStringLiteral("state=opened&scope=assigned_to_me&order_by=updated_at"));
    }

    void filterComesLast()
    {
        QCOMPARE(queryText(ListKind::MergeRequests, QStringLiteral("allOpen"),
                           QStringLiteral(" labels=bug&target_branch=main ")),
                 QStringLiteral("state=opened&scope=all&order_by=updated_at&labels=bug&target_branch=main"));
    }

    void filterReplacesParameters()
    {
        QCOMPARE(queryText(ListKind::MergeRequests, QStringLiteral("allOpen"),
                           QStringLiteral("state=merged&order_by=created_at")),
                 QStringLiteral("scope=all&state=merged&order_by=created_at"));
        QCOMPARE(queryText(ListKind::MergeRequests, QStringLiteral("reviewRequested"), QStringLiteral("reviewer_id=5")),
                 QStringLiteral("state=opened&scope=all&order_by=updated_at&reviewer_id=5"));
    }

    // A name given twice keeps both values, and a value keeps its encoding.
    void filterKeepsItsValues()
    {
        QCOMPARE(queryText(ListKind::Issues, QStringLiteral("allOpen"),
                           QStringLiteral("not[labels][]=a&not[labels][]=b")),
                 QStringLiteral("state=opened&scope=all&order_by=updated_at&not[labels][]=a&not[labels][]=b"));
        QCOMPARE(queryText(ListKind::Issues, QStringLiteral("allOpen"), QStringLiteral("labels=a%26b&search=x y")),
                 QStringLiteral("state=opened&scope=all&order_by=updated_at&labels=a%26b&search=x%20y"));
    }

    void webAddresses_data()
    {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("mergeRequests");
        QTest::addColumn<QString>("issues");

        const QJsonObject project{{QStringLiteral("scope"), QStringLiteral("project")},
                                  {QStringLiteral("project"), QStringLiteral("acme/backend")}};
        const QJsonObject group{{QStringLiteral("scope"), QStringLiteral("group")},
                                {QStringLiteral("group"), QStringLiteral("/acme/tools/")}};
        const QJsonObject all{{QStringLiteral("scope"), QStringLiteral("all")}};

        QTest::newRow("project") << "https://gitlab.example" << project
                                 << "https://gitlab.example/acme/backend/-/merge_requests"
                                 << "https://gitlab.example/acme/backend/-/issues";
        QTest::newRow("group") << "https://gitlab.example" << group
                               << "https://gitlab.example/groups/acme/tools/-/merge_requests"
                               << "https://gitlab.example/groups/acme/tools/-/issues";
        QTest::newRow("all") << "https://gitlab.example" << all << "https://gitlab.example/dashboard/merge_requests"
                             << "https://gitlab.example/dashboard/issues";
        QTest::newRow("slash at the end") << "https://gitlab.example/" << project
                                          << "https://gitlab.example/acme/backend/-/merge_requests"
                                          << "https://gitlab.example/acme/backend/-/issues";
        QTest::newRow("base with a path") << "https://example.org/gitlab/" << group
                                          << "https://example.org/gitlab/groups/acme/tools/-/merge_requests"
                                          << "https://example.org/gitlab/groups/acme/tools/-/issues";
        QTest::newRow("all, base with a path") << "https://example.org/gitlab" << all
                                               << "https://example.org/gitlab/dashboard/merge_requests"
                                               << "https://example.org/gitlab/dashboard/issues";
        QTest::newRow("project from the setting") << "https://gitlab.example" << QJsonObject()
                                                  << "https://gitlab.example/acme/website/-/merge_requests"
                                                  << "https://gitlab.example/acme/website/-/issues";
    }

    void webAddresses()
    {
        QFETCH(QString, base);
        QFETCH(QJsonObject, config);
        QFETCH(QString, mergeRequests);
        QFETCH(QString, issues);
        const QJsonValue defaultProject(QStringLiteral("acme/website"));
        QCOMPARE(listWebUrl(ListKind::MergeRequests, QUrl(base), config, defaultProject).toString(), mergeRequests);
        QCOMPARE(listWebUrl(ListKind::Issues, QUrl(base), config, defaultProject).toString(), issues);
    }

    void noWebAddressWhenIncomplete()
    {
        const QUrl base(QStringLiteral("https://gitlab.example"));
        const QJsonValue none;
        QVERIFY(listWebUrl(ListKind::MergeRequests, base, {}, none).isEmpty());
        QVERIFY(listWebUrl(ListKind::Issues, base, QJsonObject{{QStringLiteral("scope"), QStringLiteral("project")}}, none)
                    .isEmpty());
        QVERIFY(listWebUrl(ListKind::Issues, base, QJsonObject{{QStringLiteral("scope"), QStringLiteral("group")}},
                           QJsonValue(QStringLiteral("acme/website")))
                    .isEmpty());
        QVERIFY(listWebUrl(ListKind::Issues, base, QJsonObject{{QStringLiteral("scope"), QStringLiteral("space")}}, none)
                    .isEmpty());
        QVERIFY(listWebUrl(ListKind::Issues, QUrl(), QJsonObject{{QStringLiteral("scope"), QStringLiteral("all")}}, none)
                    .isEmpty());
    }

    void mergeRequestEndpoints_data()
    {
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("path");

        QJsonObject project = allOpen(QStringLiteral("project"));
        project.insert(QStringLiteral("project"), QStringLiteral("acme/backend"));
        QJsonObject group = allOpen(QStringLiteral("group"));
        group.insert(QStringLiteral("group"), QStringLiteral("acme/tools"));

        QTest::newRow("project") << project << "api/v4/projects/acme%2Fbackend/merge_requests";
        QTest::newRow("group") << group << "api/v4/groups/acme%2Ftools/merge_requests";
        QTest::newRow("all") << allOpen() << "api/v4/merge_requests";
    }

    // Three rows, as GitLab gave them. No attention, and closed unset.
    void mergeRequestEndpoints()
    {
        QFETCH(QJsonObject, config);
        QFETCH(QString, path);
        const QString key = QStringLiteral("GET ") + path + kQuery;

        FakeStatusJob job;
        job.configValues = config;
        QVERIFY(job.client.setJsonFile(key, dataFile(QStringLiteral("merge_request_list.json"))));
        fetchMergeRequestListStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(status.message.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("3 merge requests"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!status.closed.has_value());
        QVERIFY(status.updatedAt.isValid());
        QCOMPARE(status.data.value(QStringLiteral("rows")).toArray(), readRows(QStringLiteral("merge_request_list.json")));
        QCOMPARE(status.data.value(QStringLiteral("total")).toInt(), 3);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
        QVERIFY(job.client.requests().constLast().options.followLinks);
        QCOMPARE(job.client.requests().constLast().options.maxRows, 100);
    }

    void issueEndpoints_data()
    {
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("path");

        QJsonObject project = allOpen(QStringLiteral("project"));
        project.insert(QStringLiteral("project"), QStringLiteral("acme/backend"));
        QJsonObject group = allOpen(QStringLiteral("group"));
        group.insert(QStringLiteral("group"), QStringLiteral("acme/tools"));

        QTest::newRow("project") << project << "api/v4/projects/acme%2Fbackend/issues";
        QTest::newRow("group") << group << "api/v4/groups/acme%2Ftools/issues";
        QTest::newRow("all") << allOpen() << "api/v4/issues";
    }

    void issueEndpoints()
    {
        QFETCH(QJsonObject, config);
        QFETCH(QString, path);
        const QString key = QStringLiteral("GET ") + path + kQuery;

        FakeStatusJob job;
        job.configValues = config;
        QVERIFY(job.client.setJsonFile(key, dataFile(QStringLiteral("issue_list.json"))));
        fetchIssueListStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("2 issues"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!status.closed.has_value());
        QCOMPARE(status.data.value(QStringLiteral("rows")).toArray(), readRows(QStringLiteral("issue_list.json")));
        QCOMPARE(status.data.value(QStringLiteral("total")).toInt(), 2);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
    }

    // An empty project uses the project's setting, and an absent scope is a
    // project.
    void projectFromTheSetting()
    {
        const QString key = QStringLiteral("GET api/v4/projects/acme%2Fbackend/issues") + kQuery;
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("preset"), QStringLiteral("allOpen")},
                                       {QStringLiteral("project"), QStringLiteral("")}};
        job.projectSettings = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};
        QVERIFY(job.client.setJsonFile(key, dataFile(QStringLiteral("issue_list.json"))));
        fetchIssueListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
    }

    // The custom filter reaches the request.
    void filterReachesTheRequest()
    {
        const QString key = QStringLiteral("GET api/v4/merge_requests?scope=all&order_by=updated_at&state=merged"
                                           "&per_page=100");
        FakeStatusJob job;
        job.configValues = allOpen();
        job.configValues.insert(QStringLiteral("filter"), QStringLiteral("state=merged"));
        job.client.setJson(key, rows(1));
        fetchMergeRequestListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
    }

    // Only reviewRequested asks for the current user, and first.
    void reviewRequestedAsksForTheUser()
    {
        const QString key = QStringLiteral("GET api/v4/merge_requests?state=opened&scope=all&reviewer_id=1001"
                                           "&order_by=updated_at&per_page=100");
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("scope"), QStringLiteral("all")},
                                       {QStringLiteral("preset"), QStringLiteral("reviewRequested")}};
        QVERIFY(job.client.setJsonFile(kUser, dataFile(QStringLiteral("user.json"))));
        QVERIFY(job.client.setJsonFile(key, dataFile(QStringLiteral("merge_request_list.json"))));
        fetchMergeRequestListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->summary, QStringLiteral("3 merge requests"));
        QCOMPARE(job.client.requestKeys(), (QStringList{kUser, key}));
    }

    void otherPresetsAskForNoUser()
    {
        for (const QString &preset : {QStringLiteral("createdByMe"), QStringLiteral("assignedToMe"),
                                      QStringLiteral("allOpen"), QStringLiteral("reviewRequested")}) {
            FakeStatusJob issues;
            issues.configValues = QJsonObject{{QStringLiteral("scope"), QStringLiteral("all")},
                                              {QStringLiteral("preset"), preset}};
            fetchIssueListStatus(issues);
            WAIT_FOR_FINISH(issues);
            QCOMPARE(issues.client.requests().size(), 1);
            QVERIFY(!issues.client.requestKeys().contains(kUser));

            if (preset == QLatin1String("reviewRequested"))
                continue;
            FakeStatusJob mergeRequests;
            mergeRequests.configValues = issues.configValues;
            fetchMergeRequestListStatus(mergeRequests);
            WAIT_FOR_FINISH(mergeRequests);
            QCOMPARE(mergeRequests.client.requests().size(), 1);
            QVERIFY(!mergeRequests.client.requestKeys().contains(kUser));
        }
    }

    void summaries_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("mergeRequests");
        QTest::addColumn<QString>("issues");

        QTest::newRow("none") << 0 << "0 merge requests" << "0 issues";
        QTest::newRow("one") << 1 << "1 merge request" << "1 issue";
        QTest::newRow("some") << 12 << "12 merge requests" << "12 issues";
        QTest::newRow("below the limit") << 99 << "99 merge requests" << "99 issues";
        QTest::newRow("at the limit") << 100 << "100+ merge requests" << "100+ issues";
    }

    void summaries()
    {
        QFETCH(int, count);
        QFETCH(QString, mergeRequests);
        QFETCH(QString, issues);

        FakeStatusJob mergeRequestJob;
        mergeRequestJob.configValues = allOpen();
        mergeRequestJob.client.setJson(QStringLiteral("GET api/v4/merge_requests") + kQuery, rows(count));
        fetchMergeRequestListStatus(mergeRequestJob);
        WAIT_FOR_FINISH(mergeRequestJob);
        QCOMPARE(mergeRequestJob.finished->summary, mergeRequests);
        QCOMPARE(mergeRequestJob.finished->data.value(QStringLiteral("total")).toInt(), count);

        FakeStatusJob issueJob;
        issueJob.configValues = allOpen();
        issueJob.client.setJson(QStringLiteral("GET api/v4/issues") + kQuery, rows(count));
        fetchIssueListStatus(issueJob);
        WAIT_FOR_FINISH(issueJob);
        QCOMPARE(issueJob.finished->summary, issues);
        QCOMPARE(issueJob.finished->data.value(QStringLiteral("total")).toInt(), count);
    }

    // Without a project or a group nothing is asked.
    void incompleteScope_data()
    {
        QTest::addColumn<QJsonObject>("config");
        QTest::addColumn<QString>("message");

        const QString noProject = QStringLiteral("Set the project in the item's settings.");
        QTest::newRow("no scope") << QJsonObject() << noProject;
        QTest::newRow("no project") << allOpen(QStringLiteral("project")) << noProject;
        QTest::newRow("no group") << allOpen(QStringLiteral("group"))
                                  << QStringLiteral("Set the group in the item's settings.");
        QTest::newRow("unknown scope") << allOpen(QStringLiteral("space"))
                                       << QStringLiteral("Set the scope in the item's settings.");
    }

    void incompleteScope()
    {
        QFETCH(QJsonObject, config);
        QFETCH(QString, message);

        FakeStatusJob mergeRequests;
        mergeRequests.configValues = config;
        fetchMergeRequestListStatus(mergeRequests);
        QCOMPARE(mergeRequests.finishCount, 1);
        QCOMPARE(mergeRequests.finished->state, ItemStatus::State::None);
        QCOMPARE(mergeRequests.finished->message, message);
        QVERIFY(!mergeRequests.finished->closed.has_value());
        QVERIFY(mergeRequests.client.requests().isEmpty());

        FakeStatusJob issues;
        issues.configValues = config;
        fetchIssueListStatus(issues);
        QCOMPARE(issues.finishCount, 1);
        QCOMPARE(issues.finished->state, ItemStatus::State::None);
        QCOMPARE(issues.finished->message, message);
        QVERIFY(issues.client.requests().isEmpty());
    }

    // A scope with a group needs no project, even with the setting empty.
    void groupNeedsNoProject()
    {
        QJsonObject config = allOpen(QStringLiteral("group"));
        config.insert(QStringLiteral("group"), QStringLiteral("acme"));
        const QString key = QStringLiteral("GET api/v4/groups/acme/issues") + kQuery;

        FakeStatusJob job;
        job.configValues = config;
        job.client.setJson(key, rows(0));
        fetchIssueListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
    }

    void failedReply()
    {
        FakeStatusJob mergeRequests;
        mergeRequests.configValues = allOpen();
        mergeRequests.client.setReply(QStringLiteral("GET api/v4/merge_requests") + kQuery, forbiddenReply());
        fetchMergeRequestListStatus(mergeRequests);
        WAIT_FOR_FINISH(mergeRequests);
        QCOMPARE(mergeRequests.finished->state, ItemStatus::State::Error);
        QCOMPARE(mergeRequests.finished->message, QStringLiteral("Forbidden (403)"));
        QVERIFY(mergeRequests.finished->data.isEmpty());

        FakeStatusJob issues;
        issues.configValues = allOpen();
        issues.client.failAll(ApiReply::Error::NoToken, QStringLiteral("Token not set on this device"));
        fetchIssueListStatus(issues);
        WAIT_FOR_FINISH(issues);
        QCOMPARE(issues.finished->state, ItemStatus::State::NoToken);
        QCOMPARE(issues.finished->message, QStringLiteral("Token not set on this device"));
    }

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

    // The limit reaches the request, and "+" follows it.
    void limitReachesTheRequest()
    {
        const QString key = QStringLiteral("GET api/v4/issues?state=opened&scope=all&order_by=updated_at&per_page=30");
        FakeStatusJob job;
        job.configValues = allOpen();
        job.configValues.insert(QStringLiteral("limit"), 30);
        job.client.setJson(key, rows(30));
        fetchIssueListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.client.requestKeys(), QStringList{key});
        QCOMPARE(job.client.requests().constLast().options.maxRows, 30);
        QCOMPARE(job.finished->summary, QStringLiteral("30+ issues"));
    }

    // More than a page of GitLab's allows, so the client follows the links.
    void limitAboveAPage()
    {
        const QString key = QStringLiteral("GET api/v4/merge_requests") + kQuery;
        FakeStatusJob job;
        job.configValues = allOpen();
        job.configValues.insert(QStringLiteral("limit"), 250);
        job.client.setJson(key, rows(100));
        fetchMergeRequestListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.client.requests().constLast().options.maxRows, 250);
        QCOMPARE(job.finished->summary, QStringLiteral("100 merge requests"));
    }

    void limitBelowOneUsesTheDefault()
    {
        const QString key = QStringLiteral("GET api/v4/merge_requests") + kQuery;
        FakeStatusJob job;
        job.configValues = allOpen();
        job.configValues.insert(QStringLiteral("limit"), 0);
        job.client.setJson(key, rows(100));
        fetchMergeRequestListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.client.requests().constLast().options.maxRows, 100);
        QCOMPARE(job.finished->summary, QStringLiteral("100+ merge requests"));
    }

    // A failed request for the user asks for no list.
    void failedUserReply()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("scope"), QStringLiteral("all")},
                                       {QStringLiteral("preset"), QStringLiteral("reviewRequested")}};
        job.client.setReply(kUser, forbiddenReply());
        fetchMergeRequestListStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Error);
        QCOMPARE(job.client.requestKeys(), QStringList{kUser});
    }
};

QTEST_MAIN(TestListQuery)
#include "tst_list_query.moc"
