// The CI health list: its query and variables, its status, its model and its
// default layout, on ci_health.json, written by hand in the shape of GitHub's
// GraphQL answer. See README.md#ci-health.
#include "../GitHubLists.h"
#include "../GitHubRepositoryListLayout.h"
#include "../GitHubRepositoryListView.h"
#include "../GitHubViewSupport.h"
#include "ListTestSupport.h"

#include <mirante/testing/FakeStatusJob.h>
#include <mirante/widgets/ItemListView.h>

#include <QAbstractItemModel>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QTableView>
#include <QTest>
#include <QWidget>

#include <memory>

using mirante::widgets::StatusRole;
using mirante::testing::FakeStatusJob;
using namespace listtest;

namespace {

const QStringList kRepositories{QStringLiteral("acme/backend"), QStringLiteral("acme/web"),
                                QStringLiteral("acme/docs"), QStringLiteral("acme/empty")};

QJsonArray ciRows()
{
    return readJson(QStringLiteral("ci_health.json"))
        .object()
        .value(QStringLiteral("data"))
        .toObject()
        .value(QStringLiteral("o0"))
        .toObject()
        .value(QStringLiteral("repositories"))
        .toObject()
        .value(QStringLiteral("nodes"))
        .toArray();
}

QStringList names(const QJsonArray &rows)
{
    QStringList result;
    for (const QJsonValue &row : rows)
        result << row.toObject().value(QStringLiteral("nameWithOwner")).toString();
    return result;
}

QJsonObject ciConfig()
{
    return {{QStringLiteral("owner"), QStringLiteral("acme")}};
}

#define WAIT_FOR_FINISH(job)                                                                                         \
    do {                                                                                                             \
        QTRY_COMPARE((job).finishCount, 1);                                                                          \
        QCoreApplication::processEvents();                                                                           \
    } while (false)

using View = std::unique_ptr<QWidget>;

QTableView *table(const View &view)
{
    return view->findChild<QTableView *>();
}

QString cellText(const View &view, int row, int column)
{
    return table(view)->model()->index(row, column).data().toString();
}

enum Column { RepositoryColumn, ChecksColumn, BranchColumn, CommitColumn, PushedColumn, LinksColumn };

} // namespace

class TestCiHealth : public QObject
{
    Q_OBJECT

private slots:
    void ownersAreSplitAndTrimmed()
    {
        QCOMPARE(configuredOwners({{QStringLiteral("owner"), QStringLiteral("  @acme/ ")}}),
                 QStringList{QStringLiteral("acme")});
        QCOMPARE(configuredOwners({{QStringLiteral("owner"), QStringLiteral("acme, acme-labs  /tools/,ACME,,")}}),
                 (QStringList{QStringLiteral("acme"), QStringLiteral("acme-labs"), QStringLiteral("tools")}));
        QVERIFY(configuredOwners({}).isEmpty());
        QVERIFY(configuredOwners({{QStringLiteral("owner"), QStringLiteral(" , @ ")}}).isEmpty());
    }

    void queryHasOneAliasPerOwner()
    {
        const QString query = ciHealthQuery(2);
        QVERIFY(query.contains(QStringLiteral("$o0: String!, $o1: String!")));
        QVERIFY(query.contains(QStringLiteral("o0: repositoryOwner(login: $o0) { ...Repositories }")));
        QVERIFY(query.contains(QStringLiteral("o1: repositoryOwner(login: $o1) { ...Repositories }")));
        QVERIFY(!query.contains(QStringLiteral("$o2")));
        QVERIFY(query.contains(QStringLiteral("fragment Repositories on RepositoryOwner")));
    }

    void batchesStayWithinTheBudget()
    {
        QStringList owners;
        for (int index = 0; index < 90; ++index)
            owners << QStringLiteral("owner%1").arg(index);
        // 4000 rows a request: 40 owners of 100 rows.
        const QList<QStringList> batches = ciHealthBatches(owners, 100);
        QCOMPARE(batches.size(), 3);
        QCOMPARE(batches.at(0).size(), 40);
        QCOMPARE(batches.at(2), owners.mid(80));
        QCOMPARE(ciHealthBatches(owners, 20).size(), 1);
        QVERIFY(ciHealthBatches({}, 100).isEmpty());
    }

    void newestRepositoriesComeFirst()
    {
        const QJsonObject old{{QStringLiteral("nameWithOwner"), QStringLiteral("a/old")},
                              {QStringLiteral("pushedAt"), QStringLiteral("2026-01-01T00:00:00Z")}};
        const QJsonObject recent{{QStringLiteral("nameWithOwner"), QStringLiteral("b/recent")},
                                 {QStringLiteral("pushedAt"), QStringLiteral("2026-11-01T00:00:00Z")}};
        const QJsonObject never{{QStringLiteral("nameWithOwner"), QStringLiteral("c/never")},
                                {QStringLiteral("pushedAt"), QJsonValue()}};
        QCOMPARE(newestRepositories({never, old, recent}, 10), (QJsonArray{recent, old, never}));
        QCOMPARE(newestRepositories({never, old, recent}, 1), QJsonArray{recent});
    }

    void organizationLoginsOfTheAnswer()
    {
        QCOMPARE(viewerOrganizationsQuery(),
                 QStringLiteral("query { viewer { organizations(first: 100) { nodes { login } } } }"));
        const QJsonObject answer = readJson(QStringLiteral("ci_health_organizations.json")).object();
        QCOMPARE(organizationLogins(answer), (QStringList{QStringLiteral("acme"), QStringLiteral("acme-labs")}));
        QVERIFY(organizationLogins({}).isEmpty());
    }

    void variablesLeaveOutForksAndArchivedByDefault()
    {
        const QJsonObject variables =
            ciHealthVariables(ciConfig(), {QStringLiteral("acme"), QStringLiteral("acme-labs")});
        QCOMPARE(variables.value(QStringLiteral("o0")).toString(), QStringLiteral("acme"));
        QCOMPARE(variables.value(QStringLiteral("o1")).toString(), QStringLiteral("acme-labs"));
        QCOMPARE(variables.value(QStringLiteral("first")).toInt(), 100);
        QCOMPARE(variables.value(QStringLiteral("isFork")), QJsonValue(false));
        QCOMPARE(variables.value(QStringLiteral("isArchived")), QJsonValue(false));

        QJsonObject config = ciConfig();
        config.insert(QStringLiteral("includeForks"), true);
        config.insert(QStringLiteral("includeArchived"), true);
        config.insert(QStringLiteral("limit"), 250);
        const QJsonObject all = ciHealthVariables(config, {QStringLiteral("acme")});
        QVERIFY(all.value(QStringLiteral("isFork")).isNull());
        QVERIFY(all.value(QStringLiteral("isArchived")).isNull());
        // One page of GitHub's GraphQL API at most.
        QCOMPARE(all.value(QStringLiteral("first")).toInt(), 100);

        config.insert(QStringLiteral("limit"), 20);
        QCOMPARE(ciHealthVariables(config, {QStringLiteral("acme")}).value(QStringLiteral("first")).toInt(), 20);
    }

    void checksOfEachRepository()
    {
        const QJsonArray rows = ciRows();
        QCOMPARE(repositoryChecks(rows.at(0).toObject()), ChecksState::Failed);
        QCOMPARE(repositoryChecks(rows.at(1).toObject()), ChecksState::Passed);
        QCOMPARE(repositoryChecks(rows.at(2).toObject()), ChecksState::None);
        QCOMPARE(repositoryChecks(rows.at(3).toObject()), ChecksState::None);
    }

    void summaryCountsTheFailing()
    {
        const QJsonArray rows = ciRows();
        QCOMPARE(ciHealthSummary(rows, 100), QStringLiteral("4 repository(s) · 1 failing"));
        QCOMPARE(ciHealthSummary(rows, 4), QStringLiteral("4+ repository(s) · 1 failing"));
        QCOMPARE(ciHealthSummary(QJsonArray{rows.at(1)}, 100), QStringLiteral("1 repository(s)"));
    }

    void descriptionAndWebLink()
    {
        QCOMPARE(ciHealthDescription(ciConfig()), QStringLiteral("owner acme · without forks · without archived"));
        QJsonObject config = ciConfig();
        config.insert(QStringLiteral("includeForks"), true);
        QCOMPARE(ciHealthDescription(config), QStringLiteral("owner acme · without archived"));

        QCOMPARE(ciHealthWebUrl(QUrl(kBase), ciConfig()), QUrl(kBase + QStringLiteral("/acme")));
        QVERIFY(ciHealthWebUrl(QUrl(kBase), {}).isEmpty());

        // Several owners, or the user's organizations, have no page of their own.
        const QJsonObject several{{QStringLiteral("owner"), QStringLiteral("acme, acme-labs")}};
        QCOMPARE(ciHealthDescription(several), QStringLiteral("owners acme, acme-labs · without forks · without archived"));
        QVERIFY(ciHealthWebUrl(QUrl(kBase), several).isEmpty());
        const QJsonObject organizations{{QStringLiteral("scope"), QStringLiteral("myOrganizations")},
                                        {QStringLiteral("owner"), QStringLiteral("acme")}};
        QCOMPARE(ciHealthDescription(organizations), QStringLiteral("my organizations · without forks · without archived"));
        QVERIFY(ciHealthWebUrl(QUrl(kBase), organizations).isEmpty());
    }

    void fetchReadsTheRepositories()
    {
        FakeStatusJob job;
        job.configValues = ciConfig();
        QVERIFY(job.client.setJsonFile(graphqlKey(), dataFile(QStringLiteral("ci_health.json"))));
        fetchCiHealthStatus(job);
        WAIT_FOR_FINISH(job);

        QCOMPARE(job.client.requestKeys(), QStringList{graphqlKey()});
        const QJsonObject body = job.client.requests().first().body.object();
        QCOMPARE(body.value(QStringLiteral("query")).toString(), ciHealthQuery(1));
        QCOMPARE(body.value(QStringLiteral("variables")).toObject(),
                 ciHealthVariables(ciConfig(), {QStringLiteral("acme")}));

        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->summary, QStringLiteral("4 repository(s) · 1 failing"));
        QCOMPARE(job.finished->data.value(QStringLiteral("total")).toInt(), 4);
        QCOMPARE(job.finished->data.value(QStringLiteral("rows")).toArray(), ciRows());
        QVERIFY(job.finished->attention.isEmpty());
    }

    // Two owners in one request; their rows are joined, newest push first.
    void fetchJoinsSeveralOwners()
    {
        FakeStatusJob job;
        job.configValues = {{QStringLiteral("owner"), QStringLiteral("acme, acme-labs")}};
        QVERIFY(job.client.setJsonFile(graphqlKey(), dataFile(QStringLiteral("ci_health_organizations.json"))));
        fetchCiHealthStatus(job);
        WAIT_FOR_FINISH(job);

        QCOMPARE(job.client.requestKeys(), QStringList{graphqlKey()});
        const QJsonObject body = job.client.requests().first().body.object();
        QCOMPARE(body.value(QStringLiteral("query")).toString(), ciHealthQuery(2));
        QCOMPARE(body.value(QStringLiteral("variables")).toObject().value(QStringLiteral("o1")).toString(),
                 QStringLiteral("acme-labs"));
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(names(job.finished->data.value(QStringLiteral("rows")).toArray()),
                 (QStringList{QStringLiteral("acme/backend"), QStringLiteral("acme-labs/robot"),
                              QStringLiteral("acme/web"), QStringLiteral("acme/docs"), QStringLiteral("acme/empty")}));
        QCOMPARE(job.finished->summary, QStringLiteral("5 repository(s) · 2 failing"));
    }

    // The organizations first, then their repositories. The fake answers
    // every GraphQL request with the same answer, which holds both parts.
    void fetchReadsMyOrganizations()
    {
        FakeStatusJob job;
        job.configValues = {{QStringLiteral("scope"), QStringLiteral("myOrganizations")},
                            {QStringLiteral("owner"), QStringLiteral("ignored")}};
        QVERIFY(job.client.setJsonFile(graphqlKey(), dataFile(QStringLiteral("ci_health_organizations.json"))));
        fetchCiHealthStatus(job);
        WAIT_FOR_FINISH(job);

        QCOMPARE(job.client.requestKeys(), (QStringList{graphqlKey(), graphqlKey()}));
        QCOMPARE(job.client.requests().at(0).body.object().value(QStringLiteral("query")).toString(),
                 viewerOrganizationsQuery());
        const QJsonObject variables =
            job.client.requests().at(1).body.object().value(QStringLiteral("variables")).toObject();
        QCOMPARE(variables.value(QStringLiteral("o0")).toString(), QStringLiteral("acme"));
        QCOMPARE(variables.value(QStringLiteral("o1")).toString(), QStringLiteral("acme-labs"));
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->data.value(QStringLiteral("total")).toInt(), 5);
    }

    void withoutOrganizationsTheListSaysSo()
    {
        FakeStatusJob job;
        job.configValues = {{QStringLiteral("scope"), QStringLiteral("myOrganizations")}};
        job.client.setJson(graphqlKey(),
                           QJsonDocument(QJsonObject{
                               {QStringLiteral("data"),
                                QJsonObject{{QStringLiteral("viewer"),
                                             QJsonObject{{QStringLiteral("organizations"),
                                                          QJsonObject{{QStringLiteral("nodes"), QJsonArray()}}}}}}}}));
        fetchCiHealthStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::None);
        QCOMPARE(job.finished->message, QStringLiteral("You belong to no organization on this server."));
        QCOMPARE(job.client.requests().size(), 1);
    }

    void fetchWithoutAnOwnerAsksNothing()
    {
        FakeStatusJob job;
        fetchCiHealthStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::None);
        QCOMPARE(job.finished->message, QStringLiteral("Set the owner in the item's settings."));
        QVERIFY(job.client.requests().isEmpty());
    }

    void anUnknownOwnerIsAnError()
    {
        FakeStatusJob job;
        job.configValues = ciConfig();
        job.client.setJson(graphqlKey(),
                           QJsonDocument(QJsonObject{
                               {QStringLiteral("data"), QJsonObject{{QStringLiteral("o0"), QJsonValue()}}},
                               {QStringLiteral("errors"),
                                QJsonArray{QJsonObject{{QStringLiteral("path"), QJsonArray{QStringLiteral("o0")}},
                                                       {QStringLiteral("message"),
                                                        QStringLiteral("Could not resolve to a RepositoryOwner with "
                                                                       "the login of 'acme'.")}}}}}));
        fetchCiHealthStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Error);
        QCOMPARE(job.finished->message,
                 QStringLiteral("Could not resolve to a RepositoryOwner with the login of 'acme'."));

        // Without GitHub's message, a message of its own.
        FakeStatusJob bare;
        bare.configValues = ciConfig();
        bare.client.setJson(graphqlKey(), QJsonDocument(QJsonObject{{QStringLiteral("data"), QJsonObject()}}));
        fetchCiHealthStatus(bare);
        WAIT_FOR_FINISH(bare);
        QCOMPARE(bare.finished->state, ItemStatus::State::Error);
        QCOMPARE(bare.finished->message, QStringLiteral("GitHub has no owner acme."));
    }

    void aFailedRequestGivesItsState()
    {
        FakeStatusJob job;
        job.configValues = ciConfig();
        // The fake answers 404 to a request it has no answer for.
        fetchCiHealthStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::NotFound);
    }

    void modelReadsTheRows()
    {
        FakeContext context;
        context.itemConfig = ciConfig();
        context.source.status = statusOfRows(ciRows());
        RepositoryListModel model(context);
        model.follow(&context.source);

        QCOMPARE(model.keys(), kRepositories);
        QCOMPARE(model.emptyText(), QStringLiteral("No repositories"));
        QCOMPARE(model.description(), QStringLiteral("owner acme · without forks · without archived"));
        QCOMPARE(model.pageAddress(), QUrl(kBase + QStringLiteral("/acme")));

        const RepositoryListModel::Repository backend = model.repository(QStringLiteral("acme/backend"));
        QCOMPARE(backend.repository, QStringLiteral("acme/backend"));
        QCOMPARE(backend.branch, QStringLiteral("main"));
        QCOMPARE(backend.checks.text, QStringLiteral("Failed"));
        QCOMPARE(backend.checks.role, StatusRole::Bad);
        QCOMPARE(backend.commit, QStringLiteral("Retry uploads after the token expires"));
        QVERIFY(backend.pushed.isValid());
        // The newest commit's page, which shows its checks.
        QCOMPARE(backend.address,
                 QUrl(QStringLiteral("https://github.example/acme/backend/commit/"
                                     "1111111111111111111111111111111111111111")));
        QVERIFY(backend.canAdd);

        QCOMPARE(model.repository(QStringLiteral("acme/web")).checks.text, QStringLiteral("Passed"));
        const RepositoryListModel::Repository docs = model.repository(QStringLiteral("acme/docs"));
        QCOMPARE(docs.checks.text, QStringLiteral("No checks"));
        QCOMPARE(docs.checks.role, StatusRole::Neutral);

        // Without a default branch: no checks at all, and the repository's page.
        const RepositoryListModel::Repository empty = model.repository(QStringLiteral("acme/empty"));
        QVERIFY(empty.checks.text.isEmpty());
        QVERIFY(empty.branch.isEmpty());
        QVERIFY(!empty.pushed.isValid());
        QCOMPARE(empty.address, QUrl(QStringLiteral("https://github.example/acme/empty")));
    }

    void addFollowsTheDefaultBranch()
    {
        FakeContext context;
        context.itemConfig = ciConfig();
        context.source.status = statusOfRows(ciRows());
        RepositoryListModel model(context);
        model.follow(&context.source);

        model.addToTask(QStringLiteral("acme/web"));
        model.addToTask(QStringLiteral("acme/empty"));
        QCOMPARE(context.added.size(), 2);
        QCOMPARE(context.added.at(0).typeId, QStringLiteral("github.run"));
        QCOMPARE(context.added.at(0).name, QStringLiteral("acme/web trunk CI"));
        // An empty branch is the default branch, so the item follows it.
        QCOMPARE(context.added.at(0).config,
                 (QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/web")},
                              {QStringLiteral("branchSource"), QStringLiteral("fixed")}}));
        QCOMPARE(context.added.at(1).name, QStringLiteral("acme/empty CI"));
    }

    void layoutDrawsTheRows()
    {
        FakeContext context;
        context.itemConfig = ciConfig();
        context.source.status = statusOfRows(ciRows());
        const View view(createCiHealthView(context, nullptr));
        if (view->objectName() != defaultLayoutName(QStringLiteral("repository-list")))
            QSKIP("The app replaces the layout of the CI health list.");
        view->resize(1200, 700);
        view->show();
        QCoreApplication::processEvents();

        QCOMPARE(table(view)->model()->rowCount(), 4);
        // Sorted by Pushed, the newest first, and the repository without a push
        // last.
        QCOMPARE(cellText(view, 0, RepositoryColumn), QStringLiteral("acme/backend"));
        QCOMPARE(cellText(view, 0, ChecksColumn), QStringLiteral("Failed"));
        QCOMPARE(cellText(view, 0, BranchColumn), QStringLiteral("main"));
        QCOMPARE(cellText(view, 0, CommitColumn), QStringLiteral("Retry uploads after the token expires"));
        QCOMPARE(cellText(view, 1, RepositoryColumn), QStringLiteral("acme/web"));
        QCOMPARE(cellText(view, 3, RepositoryColumn), QStringLiteral("acme/empty"));
        QCOMPARE(cellText(view, 3, ChecksColumn), kDash);
        QCOMPARE(cellText(view, 3, BranchColumn), kDash);

        // The Checks filter lists every state, whatever the rows have.
        auto *filter = view->findChild<QComboBox *>(QStringLiteral("filter.%1").arg(int(ChecksColumn)));
        QVERIFY(filter);
        QCOMPARE(filter->itemText(0), QStringLiteral("All"));
        for (const QString &text : RepositoryListModel::checksTexts())
            QVERIFY2(filter->findText(text) >= 0, qPrintable(text));

        filter->setCurrentIndex(filter->findText(QStringLiteral("Failed")));
        QCOMPARE(table(view)->model()->rowCount(), 1);
        QCOMPARE(cellText(view, 0, RepositoryColumn), QStringLiteral("acme/backend"));
        // The item keeps the choice for the next start.
        QCOMPARE(context.itemConfig.value(QStringLiteral("statusFilter")).toArray(),
                 QJsonArray{QStringLiteral("Failed")});
    }
};

QTEST_MAIN(TestCiHealth)
#include "tst_ci_health.moc"
