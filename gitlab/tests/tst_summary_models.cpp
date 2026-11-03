// The models of the three GitLab summaries, which every layout draws. They run
// whichever layouts the build compiled in, so an app's layout keeps them. The
// default layouts' own tests are in tst_summary_views.cpp.
#include "../GitLabIssueLayout.h"
#include "../GitLabIssueView.h"
#include "../GitLabMergeRequestLayout.h"
#include "../GitLabMergeRequestView.h"
#include "../GitLabPipelineLayout.h"
#include "../GitLabPipelineView.h"
#include "SummaryTestSupport.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWidget>

#include <memory>

using mirante::widgets::StatusRole;
using namespace summarytest;

namespace {

QTimer *refreshTimer(const PipelineModel &model)
{
    return model.findChild<QTimer *>(QStringLiteral("pipeline-refresh"));
}

qsizetype indexOf(const PipelineModel &model, qint64 id)
{
    const QList<PipelineModel::Pipeline> pipelines = model.pipelines();
    for (qsizetype i = 0; i < pipelines.size(); ++i) {
        if (pipelines.at(i).id == id)
            return i;
    }
    return -1;
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
        model.follow(&context.source);
        QVERIFY(!model.hasData());
        QCOMPARE(model.message(), QStringLiteral("Loading…"));

        context.source.change(openIssue());
        QVERIFY(model.hasData());

        ItemStatus error;
        error.state = ItemStatus::State::Error;
        error.message = QStringLiteral("Server error");
        context.source.change(error);
        QVERIFY(model.hasData());

        ItemStatus none;
        none.message = QStringLiteral("Nothing to show");
        context.source.change(none);
        QVERIFY(!model.hasData());
        QCOMPARE(model.message(), QStringLiteral("Nothing to show"));
    }

    void sectionsFollowTheirFields()
    {
        FakeContext context;
        context.itemConfig.insert(QStringLiteral("showApprovals"), false);
        MergeRequestModel model(context);
        QVERIFY(!model.isSectionShown(QStringLiteral("approvals")));
        QVERIFY(model.isSectionShown(QStringLiteral("pipeline")));
    }

    // An empty list means no comments; a missing one, that they could not be read.
    void issueCommentsState()
    {
        FakeContext context;
        context.source.status = openIssue();
        IssueModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.state().text, QStringLiteral("Open"));
        QCOMPARE(model.commentsState(), IssueModel::Comments::Unreadable);
        QCOMPARE(model.pageAddress(), QUrl(kIssueAddress));

        context.source.change(openIssue(QJsonArray()));
        QCOMPARE(model.commentsState(), IssueModel::Comments::None);

        context.source.change(openIssue(QJsonArray{note(QStringLiteral("Newest"))}));
        QCOMPARE(model.commentsState(), IssueModel::Comments::Shown);
        QCOMPARE(model.comments().size(), 1);
    }

    void mergeRequestReadsPipelineJobsAndApprovals()
    {
        FakeContext context;
        context.source.status = failedMergeRequest();
        MergeRequestModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.state().text, QStringLiteral("Open"));
        QCOMPARE(model.pageAddress(), QUrl(kMrAddress));
        QVERIFY(!model.pipeline().isEmpty());
        QVERIFY(model.canRetryPipeline());

        // The failed jobs, the allowed failure among them.
        const QList<MergeRequestModel::Job> jobs = model.failedJobs();
        QCOMPARE(jobs.size(), 2);
        QCOMPARE(jobs.at(0).id, 70002);
        QCOMPARE(jobs.at(0).state.role, StatusRole::Bad);
        QCOMPARE(jobs.at(1).state.text, QStringLiteral("Failed, allowed"));

        const MergeRequestModel::Approvals approvals = model.approvals();
        QCOMPARE(approvals.given, 2);
        QCOMPARE(approvals.required, 3);
        QCOMPARE(approvals.approvers,
                 (QStringList{QStringLiteral("Carla Reviewer"), QStringLiteral("Dora Maintainer")}));
    }

    void mergeRequestRetriesOnlyAFailedOrCanceledPipeline()
    {
        FakeContext context;
        context.source.status = mergeRequestWithPipeline(QStringLiteral("running"));
        MergeRequestModel model(context);
        model.follow(&context.source);
        QVERIFY(!model.canRetryPipeline());
        model.retryPipeline();
        QVERIFY(context.client.requestKeys().isEmpty());

        context.source.change(mergeRequestWithPipeline(QStringLiteral("canceled")));
        QVERIFY(model.canRetryPipeline());
    }

    void pipelinesAndTheLatestShown()
    {
        FakeContext context;
        context.source.status = groupStatus();
        PipelineModel model(context);
        model.follow(&context.source);
        QVERIFY(model.hasPipelines());
        QVERIFY(model.hasGroup());
        QCOMPARE(model.title(), QStringLiteral("Pipeline #9001"));
        QCOMPARE(model.shownIndex(), 0);
        QCOMPARE(model.pipelines().size(), 5);
        QCOMPARE(model.pageAddress(), QUrl(kPipelineAddress));
        QVERIFY(model.canTrigger());

        // Jobs in the order they were created, with their stages.
        QCOMPARE(model.jobsState(), PipelineModel::Jobs::Shown);
        const QList<PipelineModel::Job> jobs = model.jobs();
        for (qsizetype i = 1; i < jobs.size(); ++i)
            QVERIFY(jobs.at(i - 1).id < jobs.at(i).id);
        QCOMPARE(model.stages(), (QStringList{QStringLiteral("build"), QStringLiteral("test"),
                                             QStringLiteral("deploy")}));
        QVERIFY(jobs.at(0).canRetry);
        QVERIFY(!jobs.at(3).canRetry);
    }

    void showingAPipelineClearsTheErrorAndStays()
    {
        FakeContext context;
        context.source.status = groupStatus();
        PipelineModel model(context);
        model.follow(&context.source);
        context.client.setReply(QStringLiteral("POST ") + kPipeline + QStringLiteral("/retry"),
                                failure(ApiReply::Error::Http, 500, QStringLiteral("Boom")));
        model.retry();
        QTRY_COMPARE(model.error(), QStringLiteral("Boom"));

        QSignalSpy changed(&model, &SummaryModel::changed);
        model.showPipeline(indexOf(model, 9000));
        QCOMPARE(changed.size(), 1);
        QVERIFY(model.error().isEmpty());
        QCOMPARE(model.title(), QStringLiteral("Pipeline #9000"));

        context.source.change(groupStatus());
        QCOMPARE(model.shownPipeline().id, 9000);

        model.showPipeline(0);
        QCOMPARE(model.title(), QStringLiteral("Pipeline #9001"));
    }

    void jobsNotInTheStatusAreRead()
    {
        FakeContext context;
        context.source.status = groupStatus();
        const QString olderJobs = QStringLiteral("GET ") + kProject + QStringLiteral("/pipelines/8990/jobs?per_page=100");
        QVERIFY(context.client.setJsonFile(olderJobs, QFINDTESTDATA("tests/data/pipeline_jobs_sibling.json")));
        PipelineModel model(context);
        model.follow(&context.source);

        model.showPipeline(indexOf(model, 8990));
        QCOMPARE(model.jobsState(), PipelineModel::Jobs::Loading);
        QCOMPARE(context.client.requestKeys(), QStringList{olderJobs});
        QTRY_COMPARE(model.jobsState(), PipelineModel::Jobs::Shown);
        QVERIFY(!model.jobs().isEmpty());

        model.showPipeline(indexOf(model, 8950));
        QTRY_COMPARE(model.jobsState(), PipelineModel::Jobs::Unreadable);
    }

    void actionsFollowTheShownPipeline_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<bool>("retry");
        QTest::addColumn<bool>("cancel");

        QTest::newRow("failed") << QStringLiteral("failed") << true << false;
        QTest::newRow("canceled") << QStringLiteral("canceled") << true << false;
        QTest::newRow("running") << QStringLiteral("running") << false << true;
        QTest::newRow("success") << QStringLiteral("success") << false << false;
    }

    void actionsFollowTheShownPipeline()
    {
        QFETCH(QString, state);
        QFETCH(bool, retry);
        QFETCH(bool, cancel);

        FakeContext context;
        context.source.status = pipelineWithStatus(state);
        PipelineModel model(context);
        model.follow(&context.source);
        QCOMPARE(model.canRetry(), retry);
        QCOMPARE(model.canCancel(), cancel);
    }

    void actionsPostAndRefresh()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("running"));
        const QString cancelKey = QStringLiteral("POST ") + kPipeline + QStringLiteral("/cancel");
        const QString jobKey = QStringLiteral("POST ") + kProject + QStringLiteral("/jobs/70002/retry");
        const QString triggerKey =
            QStringLiteral("POST ") + kProject + QStringLiteral("/pipeline?ref=feature%2Fretry-button");
        succeed(context, cancelKey);
        succeed(context, jobKey);
        succeed(context, triggerKey);
        PipelineModel model(context);
        model.follow(&context.source);

        model.cancel();
        QVERIFY(model.isBusy(PipelineModel::CancelAction));
        QTRY_COMPARE(context.source.refreshes, 1);
        QVERIFY(!model.isBusy(PipelineModel::CancelAction));

        model.retryJob(70002);
        QVERIFY(model.isBusy(PipelineModel::retryJobAction(70002)));
        QTRY_COMPARE(context.source.refreshes, 2);

        model.trigger();
        QTRY_COMPARE(context.source.refreshes, 3);
        QCOMPARE(context.client.requestKeys(), (QStringList{cancelKey, jobKey, triggerKey}));

        // An action that does not apply sends nothing.
        model.retry();
        QCOMPARE(context.client.requestKeys().size(), 3);
    }

    void forbiddenNeedsTheApiScope()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("failed"));
        context.client.setReply(QStringLiteral("POST ") + kPipeline + QStringLiteral("/retry"),
                                failure(ApiReply::Error::Forbidden, 403, QStringLiteral("403 Forbidden")));
        PipelineModel model(context);
        model.follow(&context.source);
        model.retry();
        QTRY_COMPARE(model.error(), kScopeMessage);
    }

    void refreshFollowsThePipelineAndTheView()
    {
        FakeContext context;
        context.source.status = pipelineWithStatus(QStringLiteral("running"));
        auto *model = new PipelineModel(context);
        model->follow(&context.source);
        QWidget view;
        model->attach(&view);
        QVERIFY(!refreshTimer(*model)->isActive());

        view.show();
        QVERIFY(refreshTimer(*model)->isActive());
        view.hide();
        QVERIFY(!refreshTimer(*model)->isActive());
        view.show();
        context.source.change(pipelineWithStatus(QStringLiteral("manual")));
        QVERIFY(!refreshTimer(*model)->isActive());
    }

    void branchWithoutPipelines()
    {
        FakeContext context;
        context.source.status = pipelineStatus(QStringLiteral("main"), {}, {});
        PipelineModel model(context);
        model.follow(&context.source);
        QVERIFY(model.hasData());
        QVERIFY(!model.hasPipelines());
        QCOMPARE(model.title(), context.source.status.summary);
        QVERIFY(model.canTrigger());
        QVERIFY(!model.canRetry() && !model.canCancel());
        QVERIFY(model.pageAddress().isEmpty());
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
        context.source.change(openIssue(QJsonArray{note(QStringLiteral("A comment"))}));
        context.source.change(openIssue());
        context.source.change(none);
        issueView.reset();

        std::unique_ptr<QWidget> mergeRequestView(createMergeRequestView(context, nullptr));
        QVERIFY(mergeRequestView);
        mergeRequestView->show();
        context.source.change(failedMergeRequest());
        context.source.change(mergeRequestWithPipeline(QStringLiteral("running")));
        context.source.change(none);
        mergeRequestView.reset();

        std::unique_ptr<QWidget> pipelineView(createPipelineView(context, nullptr));
        QVERIFY(pipelineView);
        pipelineView->show();
        context.source.change(groupStatus());
        context.source.change(pipelineWithStatus(QStringLiteral("running")));
        context.source.change(pipelineStatus(QStringLiteral("main"), {}, {}));
        context.source.change(none);
        QCoreApplication::processEvents();
        pipelineView->hide();
        pipelineView.reset();
        QCoreApplication::processEvents();
    }

    void openPageShowsItInMirante()
    {
        FakeContext context;
        context.source.status = openIssue();
        IssueModel model(context);
        model.follow(&context.source);
        model.openPage();
        QCOMPARE(context.pages, QList<QUrl>{QUrl(kIssueAddress)});
    }
};

QTEST_MAIN(TestSummaryModels)
#include "tst_summary_models.moc"
