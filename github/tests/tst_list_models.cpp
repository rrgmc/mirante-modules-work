// The models of the three GitHub lists, which every list layout draws. They run
// whichever layouts the build compiled in, so an app's layout keeps them. The
// default layouts' own tests are in tst_list_views.cpp.
#include "../GitHubIssueListLayout.h"
#include "../GitHubIssueListView.h"
#include "../GitHubListSupport.h"
#include "../GitHubPullRequestListLayout.h"
#include "../GitHubPullRequestListView.h"
#include "../GitHubRunListLayout.h"
#include "../GitHubRunListView.h"
#include "ListTestSupport.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <algorithm>
#include <memory>

using mirante::widgets::StatusRole;
using namespace listtest;

namespace {

// The keys of the rows, as a layout that shows them all in order would give.
QStringList allKeys(const ListModel &model)
{
    return model.keys();
}

} // namespace

class TestListModels : public QObject
{
    Q_OBJECT

public slots:
    // Takes the place of the browser.
    void openUrl(const QUrl &url) { m_opened << url; }

private slots:
    void init()
    {
        m_opened.clear();
        QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "openUrl");
        QDesktopServices::setUrlHandler(QStringLiteral("http"), this, "openUrl");
    }

    void cleanup()
    {
        QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
        QDesktopServices::unsetUrlHandler(QStringLiteral("http"));
    }

    void rowRepositoryReadsTheApiAddress()
    {
        const QJsonObject row{
            {QStringLiteral("repository_url"), QStringLiteral("https://api.github.com/repos/cli/cli")}};
        QCOMPARE(rowRepository(row), QStringLiteral("cli/cli"));
        QCOMPARE(rowRepository(QJsonObject()), QString());
    }

    void withoutRowsItShowsLoadingOrTheMessage()
    {
        FakeContext context;
        IssueListModel model(context);
        model.follow(&context.source);
        QVERIFY(!model.hasData());
        QCOMPARE(model.message(), QStringLiteral("Loading…"));

        ItemStatus failed;
        failed.state = ItemStatus::State::NoToken;
        failed.message = QStringLiteral("Token not set");
        context.source.change(failed);
        QCOMPARE(model.message(), QStringLiteral("Token not set"));

        context.source.change(issues());
        QVERIFY(model.hasData());

        // Old rows stay through an error.
        context.source.change(failed);
        QVERIFY(model.hasData());
        QCOMPARE(model.keys().size(), 2);
    }

    void issueRowsReadTheData()
    {
        FakeContext context;
        context.source.status = issues();
        IssueListModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.emptyText(), QStringLiteral("No issues"));
        QVERIFY(!model.description().isEmpty());

        const QList<IssueListModel::Issue> rows = model.issues();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).ref, QStringLiteral("backend#51"));
        QCOMPARE(rows.at(0).title, QStringLiteral("Uploads stop after the token expires"));
        QCOMPARE(rows.at(0).labels, (QStringList{QStringLiteral("bug"), QStringLiteral("priority: high")}));
        QCOMPARE(rows.at(1).assignees, (QStringList{QStringLiteral("ana-dev"), QStringLiteral("bruno-ux")}));
        QVERIFY(rows.at(0).updated.isValid());
        QVERIFY(rows.at(0).canAdd);
        QVERIFY(!rows.at(0).address.isEmpty());
    }

    void pullRequestRowsReadTheData()
    {
        FakeContext context;
        context.source.status = pullRequests();
        PullRequestListModel model(context);
        model.follow(&context.source);
        const QList<PullRequestListModel::PullRequest> rows = model.pullRequests();
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).key, QStringLiteral("acme/backend#42"));
        QCOMPARE(rows.at(0).ref, QStringLiteral("backend#42"));
        QCOMPARE(rows.at(0).author, QStringLiteral("ana-dev"));
        QCOMPARE(rows.at(1).ref, QStringLiteral("website#318"));
        // Nothing more until the layout shows rows and the view is visible.
        QVERIFY(rows.at(0).checks.text.isEmpty());
        QVERIFY(!rows.at(0).reviews.answered);
        QVERIFY(context.client.requests().isEmpty());
    }

    void visibleRowsGetChecksAndReviews()
    {
        FakeContext context;
        context.source.status = pullRequests();
        setUpBackend42(context);
        auto *model = new PullRequestListModel(context);
        model->follow(&context.source);
        QWidget view;
        model->attach(&view);
        QSignalSpy changed(model, &ListModel::rowChanged);

        // Hidden, it only remembers.
        model->setVisibleRows({QStringLiteral("acme/backend#42")});
        QVERIFY(context.client.requests().isEmpty());

        view.show();
        QCOMPARE(context.client.requestKeys(), (QStringList{pullKey(42), reviewsKey(42)}));
        QTRY_VERIFY(model->pullRequest(QStringLiteral("acme/backend#42")).checks.role == StatusRole::Bad);
        QTRY_VERIFY(model->pullRequest(QStringLiteral("acme/backend#42")).reviews.answered);
        QCOMPARE(model->pullRequest(QStringLiteral("acme/backend#42")).checks.text, QStringLiteral("Failed"));
        QCOMPARE(model->pullRequest(QStringLiteral("acme/backend#42")).reviews.approvals, 1);
        QVERIFY(changed.contains(QVariantList{QStringLiteral("acme/backend#42")}));

        // A row that leaves the shown ones drops its answers.
        model->setVisibleRows({});
        QVERIFY(model->pullRequest(QStringLiteral("acme/backend#42")).checks.text.isEmpty());
        QVERIFY(!model->pullRequest(QStringLiteral("acme/backend#42")).reviews.answered);
    }

    void onlyTheFirst25RowsAreAskedFor()
    {
        FakeContext context;
        context.source.status = statusOfRows(manyPullRequests(30));
        auto *model = new PullRequestListModel(context);
        model->follow(&context.source);
        QWidget view;
        model->attach(&view);
        view.show();
        // The layout shows the newest first.
        QStringList keys = allKeys(*model);
        std::reverse(keys.begin(), keys.end());
        model->setVisibleRows(keys);
        QCOMPARE(context.client.requestKeys().size(), 2 * PullRequestListModel::AskedRows);
        QVERIFY(context.client.requestKeys().contains(pullKey(6)));
        QVERIFY(!context.client.requestKeys().contains(pullKey(5)));
    }

    void columnFieldsTurnRequestsOff()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("showChecks"), false);
        context.source.status = pullRequests();
        auto *model = new PullRequestListModel(context);
        model->follow(&context.source);
        QWidget view;
        model->attach(&view);
        view.show();
        QVERIFY(!model->isColumnShown(QStringLiteral("checks")));
        model->setVisibleRows({QStringLiteral("acme/backend#42")});
        QCOMPARE(context.client.requestKeys(), QStringList{reviewsKey(42)});
    }

    void runRowsReadTheData()
    {
        FakeContext context;
        context.itemConfig = runConfig();
        context.source.status = runs();
        RunListModel model(context);
        model.follow(&context.source);
        const QList<RunListModel::Run> rows = model.runs();
        QCOMPARE(rows.size(), 4);
        bool sawRunning = false;
        for (const RunListModel::Run &run : rows) {
            QCOMPARE(run.key, QString::number(run.id));
            if (run.number == 1207) {
                sawRunning = true;
                QCOMPARE(run.workflow, QStringLiteral("CI"));
                QCOMPARE(run.state.text, QStringLiteral("Running"));
                QCOMPARE(run.branch, QStringLiteral("feature/upload-retries"));
                QCOMPARE(run.actor, QStringLiteral("ana-dev"));
                QVERIFY(run.started.isValid());
                QVERIFY(run.canAdd);
            }
        }
        QVERIFY(sawRunning);
    }

    void rowActions()
    {
        FakeContext context;
        context.source.status = issues();
        IssueListModel model(context);
        model.follow(&context.source);
        const IssueListModel::Issue first = model.issues().first();

        model.openPage(first.key);
        QCOMPARE(context.pages, QList<QUrl>{first.address});
        model.openWeb(first.key);
        QCOMPARE(m_opened, QList<QUrl>{first.address});
        model.copyLink(first.key);
        QCOMPARE(QGuiApplication::clipboard()->text(), first.address.toString());
        model.addToTask(first.key);
        QCOMPARE(context.added.size(), 1);
        QCOMPARE(context.added.first().typeId, QStringLiteral("github.issue.summary"));
        QCOMPARE(context.added.first().name, QStringLiteral("acme/backend#51"));

        // A key that is not there does nothing.
        model.openWeb(QStringLiteral("nothing"));
        model.addToTask(QStringLiteral("nothing"));
        QCOMPARE(m_opened.size(), 1);
        QCOMPARE(context.added.size(), 1);
    }

    // Whichever layouts the build compiled in: each view is made, draws every
    // kind of status, shows and hides, and goes.
    void everyLayoutDrawsEveryState()
    {
        FakeContext context;
        ItemStatus failed;
        failed.state = ItemStatus::State::NoToken;
        failed.message = QStringLiteral("Token not set");
        setUpBackend42(context);

        std::unique_ptr<QWidget> issueView(createIssueListView(context, nullptr));
        QVERIFY(issueView);
        issueView->show();
        context.source.change(failed);
        context.source.change(issues());
        context.source.change(statusOfRows({}));
        issueView.reset();

        std::unique_ptr<QWidget> pullRequestView(createPullRequestListView(context, nullptr));
        QVERIFY(pullRequestView);
        pullRequestView->show();
        context.source.change(pullRequests());
        context.source.change(statusOfRows(manyPullRequests(30)));
        QCoreApplication::processEvents();
        pullRequestView->hide();
        pullRequestView.reset();
        QCoreApplication::processEvents();

        context.itemConfig = runConfig();
        std::unique_ptr<QWidget> runView(createRunListView(context, nullptr));
        QVERIFY(runView);
        runView->show();
        context.source.change(runs());
        context.source.change(statusOfRows({}));
        runView.reset();
    }

private:
    QList<QUrl> m_opened;
};

QTEST_MAIN(TestListModels)
#include "tst_list_models.moc"
