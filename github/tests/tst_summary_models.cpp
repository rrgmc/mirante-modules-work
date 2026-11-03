// The models of the three GitHub summaries, which every layout draws. They run
// whichever layouts the build compiled in, so an app's layout keeps them. The
// default layouts' own tests are in tst_summary_views.cpp.
#include "../GitHubIssueLayout.h"
#include "../GitHubIssueView.h"
#include "../GitHubPullRequestLayout.h"
#include "../GitHubPullRequestView.h"
#include "../GitHubRunLayout.h"
#include "../GitHubRunView.h"
#include "SummaryTestSupport.h"

#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <memory>

using mirante::widgets::StatusRole;
using namespace summarytest;

namespace {

QStringList titles(const QList<RunModel::Run> &runs)
{
    QStringList list;
    for (const RunModel::Run &run : runs)
        list << run.title;
    return list;
}

QTimer *refreshTimer(const RunModel &model)
{
    return model.findChild<QTimer *>(QStringLiteral("run-refresh"));
}

} // namespace

class TestSummaryModels : public QObject
{
    Q_OBJECT

private slots:
    void withoutDataItShowsLoadingOrTheMessage()
    {
        FakeContext context;
        IssueModel model(context);
        QSignalSpy changed(&model, &SummaryModel::changed);
        model.follow(&context.source);
        QVERIFY(!model.hasData());
        QCOMPARE(model.message(), QStringLiteral("Loading…"));

        context.source.change(issue());
        QVERIFY(model.hasData());
        QVERIFY(model.message().isEmpty());

        // An error keeps the old data, and says nothing new.
        const qsizetype before = changed.size();
        ItemStatus error;
        error.state = ItemStatus::State::Error;
        error.message = QStringLiteral("Server error");
        context.source.change(error);
        QVERIFY(model.hasData());
        QCOMPARE(changed.size(), before);

        // A message without an error replaces it.
        ItemStatus none;
        none.message = QStringLiteral("Nothing to show");
        context.source.change(none);
        QVERIFY(!model.hasData());
        QCOMPARE(model.message(), QStringLiteral("Nothing to show"));
    }

    void withoutAStatusSourceItStaysAtLoading()
    {
        FakeContext context;
        IssueModel model(context);
        model.follow(nullptr);
        QVERIFY(!model.hasData());
        QCOMPARE(model.message(), QStringLiteral("Loading…"));
    }

    void sectionsFollowTheirFields()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("showLabels"), false);
        IssueModel model(context);
        QVERIFY(!model.isSectionShown(QStringLiteral("labels")));
        QVERIFY(model.isSectionShown(QStringLiteral("comments")));
    }

    void issueReadsItsStateCommentsAndPage()
    {
        FakeContext context;
        context.source.status = issue({comment(QStringLiteral("Newest")), comment(QStringLiteral("Older"))});
        IssueModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.issue().value(QStringLiteral("number")).toInt(), 7);
        QCOMPARE(model.state().text, QStringLiteral("Open"));
        QCOMPARE(model.state().role, StatusRole::Good);
        QCOMPARE(model.commentsState(), IssueModel::Comments::Shown);
        QCOMPARE(model.comments().size(), 2);
        QCOMPARE(model.pageAddress(), QUrl(kIssueAddress));
    }

    // The status leaves the comments out both when there are none and when
    // they could not be read.
    void issueTellsNoCommentsFromUnreadable()
    {
        FakeContext context;
        QJsonObject object = readObject(QStringLiteral("issue.json"));
        object.insert(QStringLiteral("comments"), 0);
        context.source.status = issue({}, object);
        IssueModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.commentsState(), IssueModel::Comments::None);

        object.insert(QStringLiteral("comments"), 3);
        context.source.change(issue({}, object));
        QCOMPARE(model.commentsState(), IssueModel::Comments::Unreadable);
    }

    void pullRequestReadsChecksAndReviewers()
    {
        FakeContext context;
        context.source.status = pullRequest();
        PullRequestModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.state().text, QStringLiteral("Open"));
        QVERIFY(!model.isDraft());
        QCOMPARE(model.pageAddress(), QUrl(kPrAddress));

        // Every check run, then every status, each with its own id.
        const QList<PullRequestModel::Check> checks = model.checkList();
        QVERIFY(!checks.isEmpty());
        QSet<QString> ids;
        for (const PullRequestModel::Check &check : checks) {
            QVERIFY(check.id.startsWith(QLatin1String("check-details-"))
                    || check.id.startsWith(QLatin1String("status-details-")));
            QVERIFY(check.details.isEmpty() || check.details.scheme().startsWith(QLatin1String("http")));
            ids.insert(check.id);
        }
        QCOMPARE(ids.size(), checks.size());
        const bool allPassed = std::all_of(checks.cbegin(), checks.cend(),
                                           [](const PullRequestModel::Check &check) { return check.passed; });
        QCOMPARE(model.checks().role == StatusRole::Good, allPassed);

        // Requested reviewers come after the latest reviews, as Waiting.
        const QList<PullRequestModel::Reviewer> reviewers = model.reviewers();
        QVERIFY(!reviewers.isEmpty());
        bool waiting = false;
        for (const PullRequestModel::Reviewer &reviewer : reviewers) {
            if (reviewer.state.text == QLatin1String("Waiting"))
                waiting = true;
            else
                QVERIFY2(!waiting, "A review comes after a waiting reviewer.");
        }
    }

    void pullRequestDraftIsOpenAndDraft()
    {
        FakeContext context;
        QJsonObject object = readObject(QStringLiteral("pull_request.json"));
        object.insert(QStringLiteral("draft"), true);
        context.source.status = pullRequest(object);
        PullRequestModel model(context);
        model.follow(&context.source);
        QVERIFY(model.isDraft());

        object.insert(QStringLiteral("state"), QStringLiteral("closed"));
        context.source.change(pullRequest(object));
        QVERIFY(!model.isDraft());
    }

    // Newest first, the lead shown, and the steps move through all.
    void runsAreSortedAndTheLeadIsShown()
    {
        FakeContext context;
        context.source.status = failedGroup();
        RunModel model(context);
        model.follow(&context.source);
        QVERIFY(model.hasRuns());
        QVERIFY(model.hasGroup());
        QCOMPARE(model.title(), QStringLiteral("CI #812"));
        QCOMPARE(model.shownRun().id, 9103);
        QCOMPARE(model.pageAddress(), QUrl(kRunAddress));
        QCOMPARE(model.jobsState(), RunModel::Jobs::Shown);
        QVERIFY(!model.jobs().isEmpty());

        const QStringList shown = titles(model.runs());
        QCOMPARE(shown.size(), 4);
        QVERIFY(shown.contains(QStringLiteral("Lint #520")));
        QCOMPARE(model.runs().constLast().id, 9090);
    }

    void showingARunClearsTheErrorAndStays()
    {
        FakeContext context;
        context.source.status = failedGroup();
        RunModel model(context);
        model.follow(&context.source);
        context.client.setReply(postKey(9103, QStringLiteral("rerun")),
                                failure(ApiReply::Error::Http, 500, QStringLiteral("Boom")));
        model.rerun();
        QTRY_COMPARE(model.error(), QStringLiteral("Boom"));

        qsizetype lint = -1;
        for (qsizetype i = 0; i < model.runs().size(); ++i) {
            if (model.runs().at(i).id == 9102)
                lint = i;
        }
        QSignalSpy changed(&model, &SummaryModel::changed);
        model.showRun(lint);
        QCOMPARE(changed.size(), 1);
        QVERIFY(model.error().isEmpty());
        QCOMPARE(model.title(), QStringLiteral("Lint #520"));

        // Through a new status while it is still there.
        context.source.change(failedGroup());
        QCOMPARE(model.shownRun().id, 9102);
        context.source.change(singleRun(QStringLiteral("completed"), QStringLiteral("success")));
        QCOMPARE(model.title(), QStringLiteral("CI #812"));

        model.showRun(99);
        QCOMPARE(model.title(), QStringLiteral("CI #812"));
    }

    void jobsNotInTheStatusAreRead()
    {
        FakeContext context;
        context.source.status = failedGroup(false);
        QVERIFY(context.client.setJsonFile(jobsKey(9102), dataFile(QStringLiteral("run_jobs_lint.json"))));
        RunModel model(context);
        model.follow(&context.source);
        for (qsizetype i = 0; i < model.runs().size(); ++i) {
            if (model.runs().at(i).id == 9102)
                model.showRun(i);
        }
        QCOMPARE(model.jobsState(), RunModel::Jobs::Loading);
        QCOMPARE(context.client.requestKeys(), QStringList{jobsKey(9102)});
        QTRY_COMPARE(model.jobsState(), RunModel::Jobs::Shown);
        QVERIFY(!model.jobs().isEmpty());

        // A run whose jobs cannot be read says so.
        for (qsizetype i = 0; i < model.runs().size(); ++i) {
            if (model.runs().at(i).id == 9101)
                model.showRun(i);
        }
        QTRY_COMPARE(model.jobsState(), RunModel::Jobs::Unreadable);
    }

    void actionsFollowTheShownRun_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<QString>("conclusion");
        QTest::addColumn<bool>("rerunFailed");
        QTest::addColumn<bool>("rerun");
        QTest::addColumn<bool>("cancel");

        QTest::newRow("failed") << QStringLiteral("completed") << QStringLiteral("failure") << true << true << false;
        QTest::newRow("passed") << QStringLiteral("completed") << QStringLiteral("success") << false << true << false;
        QTest::newRow("running") << QStringLiteral("in_progress") << QString() << false << false << true;
        QTest::newRow("queued") << QStringLiteral("queued") << QString() << false << false << true;
    }

    void actionsFollowTheShownRun()
    {
        QFETCH(QString, state);
        QFETCH(QString, conclusion);
        QFETCH(bool, rerunFailed);
        QFETCH(bool, rerun);
        QFETCH(bool, cancel);

        FakeContext context;
        context.source.status = singleRun(state, conclusion);
        RunModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.canRerunFailed(), rerunFailed);
        QCOMPARE(model.canRerun(), rerun);
        QCOMPARE(model.canCancel(), cancel);
    }

    void actionsPostAndRefresh()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("in_progress"), {});
        const QString cancelKey = postKey(9103, QStringLiteral("cancel"));
        const QString jobKey = QStringLiteral("POST repos/acme/widgets/actions/jobs/91032/rerun");
        succeed(context, cancelKey);
        succeed(context, jobKey);
        RunModel model(context);
        model.follow(&context.source);
        QSignalSpy busy(&model, &SummaryModel::busyChanged);

        model.cancel();
        QVERIFY(model.isBusy(RunModel::CancelAction));
        QCOMPARE(context.client.requestKeys(), QStringList{cancelKey});
        QTRY_COMPARE(context.source.refreshes, 1);
        QVERIFY(!model.isBusy(RunModel::CancelAction));
        QCOMPARE(busy.size(), 2);

        model.rerunJob(91032);
        QVERIFY(model.isBusy(RunModel::rerunJobAction(91032)));
        QTRY_COMPARE(context.source.refreshes, 2);
        QCOMPARE(context.client.requestKeys(), (QStringList{cancelKey, jobKey}));

        // An action that does not apply sends nothing.
        model.rerun();
        QCOMPARE(context.client.requestKeys().size(), 2);
    }

    void forbiddenNeedsWriteAccess()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("completed"), QStringLiteral("failure"));
        context.client.setReply(postKey(9103, QStringLiteral("rerun-failed-jobs")),
                                failure(ApiReply::Error::Forbidden, 403, QStringLiteral("Resource not accessible")));
        RunModel model(context);
        model.follow(&context.source);
        QSignalSpy error(&model, &SummaryModel::errorChanged);
        model.rerunFailed();
        QTRY_COMPARE(model.error(), kTokenMessage);
        QCOMPARE(error.size(), 1);
        model.clearError();
        QVERIFY(model.error().isEmpty());
    }

    // While the shown run runs and the view is visible.
    void refreshFollowsTheRunAndTheView()
    {
        FakeContext context;
        context.source.status = singleRun(QStringLiteral("in_progress"), {});
        auto *model = new RunModel(context);
        model->follow(&context.source);
        QWidget view;
        model->attach(&view);
        QCOMPARE(model->parent(), &view);
        QVERIFY(!refreshTimer(*model)->isActive());

        view.show();
        QVERIFY(refreshTimer(*model)->isActive());
        refreshTimer(*model)->setInterval(1);
        QTRY_VERIFY(context.source.refreshes >= 1);

        view.hide();
        QVERIFY(!refreshTimer(*model)->isActive());
        view.show();
        context.source.change(singleRun(QStringLiteral("completed"), QStringLiteral("success")));
        QVERIFY(!refreshTimer(*model)->isActive());
    }

    void branchWithoutRuns()
    {
        FakeContext context;
        context.source.status = runStatus(QStringLiteral("main"), {}, {});
        RunModel model(context);
        model.follow(&context.source);
        QVERIFY(model.hasData());
        QVERIFY(!model.hasRuns());
        QCOMPARE(model.title(), context.source.status.summary);
        QVERIFY(model.pageAddress().isEmpty());
        QVERIFY(!model.canRerun() && !model.canCancel() && !model.canRerunFailed());
    }

    // Whichever layouts the build compiled in: each view is made, draws every
    // kind of status, shows and hides, and goes.
    void everyLayoutDrawsEveryState()
    {
        FakeContext context;
        ItemStatus none;
        none.message = QStringLiteral("Nothing to show");

        std::unique_ptr<QWidget> issueView(createIssueView(context, nullptr));
        QVERIFY(issueView);
        issueView->show();
        context.source.change(issue({comment(QStringLiteral("A comment"))}));
        context.source.change(none);
        context.source.change(issue());
        issueView.reset();

        std::unique_ptr<QWidget> pullRequestView(createPullRequestView(context, nullptr));
        QVERIFY(pullRequestView);
        pullRequestView->show();
        context.source.change(pullRequest());
        context.source.change(none);
        pullRequestView.reset();

        std::unique_ptr<QWidget> runView(createRunView(context, nullptr));
        QVERIFY(runView);
        runView->show();
        context.source.change(failedGroup(false));
        context.source.change(passedGroup());
        context.source.change(runStatus(QStringLiteral("main"), {}, {}));
        context.source.change(singleRun(QStringLiteral("in_progress"), {}));
        context.source.change(none);
        QCoreApplication::processEvents();
        runView->hide();
        runView.reset();
        // Answers that come after the view is gone are dropped.
        QCoreApplication::processEvents();
    }

    void openPageShowsItInMirante()
    {
        FakeContext context;
        context.source.status = issue();
        IssueModel model(context);
        model.follow(&context.source);
        model.openPage();
        QCOMPARE(context.pages, QList<QUrl>{QUrl(kIssueAddress)});
    }
};

QTEST_MAIN(TestSummaryModels)
#include "tst_summary_models.moc"
