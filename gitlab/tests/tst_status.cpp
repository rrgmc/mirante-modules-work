// The status of GitLab items, on responses recorded from a GitLab server and
// trimmed by hand, in tests/data/. "Me" is the user in data/user.json, id 1001.
#include "../GitLabStatus.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/testing/FakeStatusJob.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace mirante::modules::gitlab {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeStatusJob;
using namespace mirante::modules::gitlab;

namespace {

const QString kMergeRequest = QStringLiteral("GET api/v4/projects/acme%2Fbackend/merge_requests/42");
const QString kApprovals = QStringLiteral("GET api/v4/projects/acme%2Fbackend/merge_requests/42/approvals");
const QString kUser = QStringLiteral("GET api/v4/user");
const QString kIssue = QStringLiteral("GET api/v4/projects/acme%2Fbackend/issues/7");
const QString kLatest =
    QStringLiteral("GET api/v4/projects/acme%2Fbackend/pipelines/latest?ref=feature%2Fretry-button");
const QString kJobs = QStringLiteral("GET api/v4/projects/acme%2Fbackend/pipelines/9001/jobs?per_page=100");
const QString kNotes =
    QStringLiteral("GET api/v4/projects/acme%2Fbackend/issues/7/notes?sort=desc&order_by=created_at&per_page=20");

QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("data/") + name);
}

QJsonDocument readJson(const QString &name)
{
    QFile file(dataFile(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll());
}

QStringList keysOf(const ItemStatus &status)
{
    QStringList keys;
    for (const mirante::AttentionReason &reason : status.attention)
        keys << reason.key;
    return keys;
}

const mirante::AttentionReason *findReason(const ItemStatus &status, const QString &key)
{
    for (const mirante::AttentionReason &reason : status.attention) {
        if (reason.key == key)
            return &reason;
    }
    return nullptr;
}

// A merge request job for acme/backend!42, its project taken from the
// project setting.
void setUpMergeRequest(FakeStatusJob &job, const QString &mergeRequest, const QString &approvals)
{
    job.configValues = QJsonObject{{QStringLiteral("iid"), 42}};
    job.projectSettings = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};
    QVERIFY(job.client.setJsonFile(kMergeRequest, dataFile(mergeRequest)));
    QVERIFY(job.client.setJsonFile(kApprovals, dataFile(approvals)));
    QVERIFY(job.client.setJsonFile(kUser, dataFile(QStringLiteral("user.json"))));
}

// An issue job for acme/backend#7.
void setUpIssue(FakeStatusJob &job)
{
    job.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                   {QStringLiteral("iid"), 7}};
    QVERIFY(job.client.setJsonFile(kIssue, dataFile(QStringLiteral("issue.json"))));
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

class TestStatus : public QObject
{
    Q_OBJECT

private slots:
    // Open, me a reviewer who has not approved, and a failed pipeline.
    void mergeRequestWithReviewRequested()
    {
        FakeStatusJob job;
        setUpMergeRequest(job, QStringLiteral("merge_request.json"), QStringLiteral("approvals.json"));
        fetchMergeRequestStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(status.message.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("Open · 2 of 3 approvals · pipeline failed"));
        QCOMPARE(keysOf(status), (QStringList{QStringLiteral("pipeline-failed"), QStringLiteral("review")}));
        QCOMPARE(findReason(status, QStringLiteral("pipeline-failed"))->text, QStringLiteral("Pipeline failed"));
        QVERIFY(findReason(status, QStringLiteral("pipeline-failed"))->notify);
        QVERIFY(!findReason(status, QStringLiteral("pipeline-failed"))->clearsOnView);
        QCOMPARE(findReason(status, QStringLiteral("review"))->text, QStringLiteral("Review requested"));
        QVERIFY(!findReason(status, QStringLiteral("review"))->notify);
        QVERIFY(status.closed.has_value());
        QVERIFY(!*status.closed);
        QVERIFY(status.updatedAt.isValid());
        QCOMPARE(status.data.value(QStringLiteral("mergeRequest")).toObject().value(QStringLiteral("iid")).toInt(), 42);
        QCOMPARE(status.data.value(QStringLiteral("approvals")).toObject().value(QStringLiteral("approvals_left")).toInt(),
                 1);
        QCOMPARE(status.data.value(QStringLiteral("user")).toObject().value(QStringLiteral("username")).toString(),
                 QStringLiteral("ana.reviewer"));
        QCOMPARE(job.client.requestKeys(), (QStringList{kMergeRequest, kApprovals, kUser}));
    }

    // A reviewer who has approved is not asked again.
    void reviewerWhoApproved()
    {
        QJsonObject approvals = readJson(QStringLiteral("approvals.json")).object();
        QJsonArray approvedBy = approvals.value(QStringLiteral("approved_by")).toArray();
        approvedBy.append(QJsonObject{{QStringLiteral("user"), QJsonObject{{QStringLiteral("id"), 1001}}}});
        approvals.insert(QStringLiteral("approved_by"), approvedBy);

        const ItemStatus status = mergeRequestStatus(readJson(QStringLiteral("merge_request.json")).object(),
                                                     approvals, readJson(QStringLiteral("user.json")).object());
        QCOMPARE(keysOf(status), QStringList{QStringLiteral("pipeline-failed")});
        QCOMPARE(status.summary, QStringLiteral("Open · 3 of 3 approvals · pipeline failed"));
    }

    // Someone who is neither author nor reviewer sees only the pipeline.
    void bystander()
    {
        const ItemStatus status =
            mergeRequestStatus(readJson(QStringLiteral("merge_request.json")).object(),
                               readJson(QStringLiteral("approvals.json")).object(),
                               QJsonObject{{QStringLiteral("id"), 9999}, {QStringLiteral("username"), QStringLiteral("x")}});
        QCOMPARE(keysOf(status), QStringList{QStringLiteral("pipeline-failed")});
    }

    // My own draft without a pipeline: approved, with conflicts.
    void myMergeRequestWithoutPipeline()
    {
        FakeStatusJob job;
        setUpMergeRequest(job, QStringLiteral("merge_request_no_pipeline.json"),
                          QStringLiteral("approvals_approved.json"));
        fetchMergeRequestStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Open · draft · 1 approval · has conflicts"));
        QCOMPARE(keysOf(status), (QStringList{QStringLiteral("approved"), QStringLiteral("conflicts")}));
        QCOMPARE(findReason(status, QStringLiteral("approved"))->text, QStringLiteral("Approved"));
        QVERIFY(findReason(status, QStringLiteral("approved"))->clearsOnView);
        QVERIFY(!findReason(status, QStringLiteral("approved"))->notify);
        QCOMPARE(findReason(status, QStringLiteral("conflicts"))->text, QStringLiteral("Has conflicts"));
        QVERIFY(!findReason(status, QStringLiteral("conflicts"))->clearsOnView);
        QVERIFY(!*status.closed);
    }

    // Conflicts are the author's to fix, so others are not told.
    void conflictsOnlyForTheAuthor()
    {
        const ItemStatus status = mergeRequestStatus(
            readJson(QStringLiteral("merge_request_no_pipeline.json")).object(),
            readJson(QStringLiteral("approvals_approved.json")).object(), QJsonObject{{QStringLiteral("id"), 3003}});
        QVERIFY(status.attention.isEmpty());
    }

    // A merged merge request is closed and needs nothing, even with a
    // failed pipeline.
    void mergedMergeRequest()
    {
        FakeStatusJob job;
        setUpMergeRequest(job, QStringLiteral("merge_request_merged.json"), QStringLiteral("approvals_approved.json"));
        fetchMergeRequestStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(status.closed.has_value());
        QVERIFY(*status.closed);
        QVERIFY(status.attention.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("Merged · 1 approval · pipeline failed"));

        QJsonObject closed = readJson(QStringLiteral("merge_request.json")).object();
        closed.insert(QStringLiteral("state"), QStringLiteral("closed"));
        const ItemStatus closedStatus = mergeRequestStatus(closed, {}, readJson(QStringLiteral("user.json")).object());
        QVERIFY(*closedStatus.closed);
        QVERIFY(closedStatus.attention.isEmpty());
        QCOMPARE(closedStatus.summary, QStringLiteral("Closed · pipeline failed"));
    }

    void issue()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("/acme/backend/")},
                                       {QStringLiteral("iid"), QStringLiteral("7")}};
        QVERIFY(job.client.setJsonFile(kIssue, dataFile(QStringLiteral("issue.json"))));
        fetchIssueStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Open · assigned to Ana Reviewer"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!*status.closed);
        QCOMPARE(status.data.value(QStringLiteral("issue")).toObject().value(QStringLiteral("iid")).toInt(), 7);
        QCOMPARE(job.client.requestKeys(), QStringList{kIssue});

        QJsonObject closed = readJson(QStringLiteral("issue.json")).object();
        closed.insert(QStringLiteral("state"), QStringLiteral("closed"));
        closed.insert(QStringLiteral("assignees"), QJsonArray());
        const ItemStatus closedStatus = issueStatus(closed);
        QCOMPARE(closedStatus.summary, QStringLiteral("Closed"));
        QVERIFY(*closedStatus.closed);
    }

    // A summary asks for the jobs of a failed head pipeline. The rest of the
    // status is that of the plain fetch.
    void mergeRequestSummaryWithFailedPipeline()
    {
        FakeStatusJob job;
        setUpMergeRequest(job, QStringLiteral("merge_request.json"), QStringLiteral("approvals.json"));
        QVERIFY(job.client.setJsonFile(kJobs, dataFile(QStringLiteral("pipeline_jobs.json"))));
        fetchMergeRequestSummaryStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Open · 2 of 3 approvals · pipeline failed"));
        QCOMPARE(keysOf(status), (QStringList{QStringLiteral("pipeline-failed"), QStringLiteral("review")}));
        QCOMPARE(status.data.value(QStringLiteral("mergeRequest")).toObject().value(QStringLiteral("iid")).toInt(), 42);
        const QJsonArray jobs = status.data.value(QStringLiteral("jobs")).toArray();
        QCOMPARE(jobs.size(), 4);
        QCOMPARE(jobs.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("test:unit"));
        QCOMPARE(job.client.requestKeys(), (QStringList{kMergeRequest, kApprovals, kUser, kJobs}));
        QVERIFY(job.client.requests().constLast().options.followLinks);
    }

    // Without a failed head pipeline, a summary asks for no jobs.
    void mergeRequestSummaryAsksForJobsOnlyWhenFailed()
    {
        FakeStatusJob withoutPipeline;
        setUpMergeRequest(withoutPipeline, QStringLiteral("merge_request_no_pipeline.json"),
                          QStringLiteral("approvals_approved.json"));
        fetchMergeRequestSummaryStatus(withoutPipeline);
        WAIT_FOR_FINISH(withoutPipeline);
        QCOMPARE(withoutPipeline.finished->state, ItemStatus::State::Ok);
        QVERIFY(!withoutPipeline.finished->data.contains(QStringLiteral("jobs")));
        QCOMPARE(withoutPipeline.client.requestKeys(), (QStringList{kMergeRequest, kApprovals, kUser}));

        for (const QString &pipelineState : {QStringLiteral("success"), QStringLiteral("canceled")}) {
            QJsonObject mergeRequest = readJson(QStringLiteral("merge_request.json")).object();
            QJsonObject pipeline = mergeRequest.value(QStringLiteral("head_pipeline")).toObject();
            pipeline.insert(QStringLiteral("status"), pipelineState);
            mergeRequest.insert(QStringLiteral("head_pipeline"), pipeline);

            FakeStatusJob job;
            setUpMergeRequest(job, QStringLiteral("merge_request.json"), QStringLiteral("approvals.json"));
            job.client.setJson(kMergeRequest, QJsonDocument(mergeRequest));
            fetchMergeRequestSummaryStatus(job);
            WAIT_FOR_FINISH(job);
            QCOMPARE(job.finished->state, ItemStatus::State::Ok);
            QVERIFY(!job.finished->data.contains(QStringLiteral("jobs")));
            QCOMPARE(job.client.requestKeys(), (QStringList{kMergeRequest, kApprovals, kUser}));
        }
    }

    // A failed jobs request keeps the status and leaves the jobs out.
    void mergeRequestSummaryWhenJobsFail()
    {
        FakeStatusJob job;
        setUpMergeRequest(job, QStringLiteral("merge_request.json"), QStringLiteral("approvals.json"));
        job.client.setReply(kJobs, forbiddenReply());
        fetchMergeRequestSummaryStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(status.message.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("Open · 2 of 3 approvals · pipeline failed"));
        QCOMPARE(keysOf(status), (QStringList{QStringLiteral("pipeline-failed"), QStringLiteral("review")}));
        QVERIFY(status.data.contains(QStringLiteral("mergeRequest")));
        QVERIFY(!status.data.contains(QStringLiteral("jobs")));
        QCOMPARE(job.client.requestKeys(), (QStringList{kMergeRequest, kApprovals, kUser, kJobs}));
    }

    // An unknown merge request is NotFound, and no jobs are asked for.
    void mergeRequestSummaryNotFound()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                       {QStringLiteral("iid"), 42}};
        QVERIFY(job.client.setJsonFile(kUser, dataFile(QStringLiteral("user.json"))));
        QVERIFY(job.client.setJsonFile(kApprovals, dataFile(QStringLiteral("approvals.json"))));
        fetchMergeRequestSummaryStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::NotFound);
        QCOMPARE(job.client.requestKeys(), (QStringList{kMergeRequest, kApprovals, kUser}));
    }

    // A summary has the latest notes, newest first, without system notes.
    void issueSummaryLeavesOutSystemNotes()
    {
        FakeStatusJob job;
        setUpIssue(job);
        QVERIFY(job.client.setJsonFile(kNotes, dataFile(QStringLiteral("issue_notes.json"))));
        fetchIssueSummaryStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Open · assigned to Ana Reviewer"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!*status.closed);
        QCOMPARE(status.data.value(QStringLiteral("issue")).toObject().value(QStringLiteral("iid")).toInt(), 7);

        QList<int> ids;
        for (const QJsonValue &note : status.data.value(QStringLiteral("notes")).toArray()) {
            QVERIFY(!note.toObject().value(QStringLiteral("system")).toBool());
            ids << note.toObject().value(QStringLiteral("id")).toInt();
        }
        QCOMPARE(ids, (QList<int>{880005, 880003, 880001}));
        QCOMPARE(job.client.requestKeys(), (QStringList{kIssue, kNotes}));
        QVERIFY(job.client.requests().constLast().options.followLinks);
        QCOMPARE(job.client.requests().constLast().options.maxRows, 20);
    }

    // A failed notes request keeps the status and leaves the notes out.
    void issueSummaryWhenNotesFail()
    {
        FakeStatusJob job;
        setUpIssue(job);
        job.client.setReply(kNotes, forbiddenReply());
        fetchIssueSummaryStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(status.message.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("Open · assigned to Ana Reviewer"));
        QVERIFY(status.data.contains(QStringLiteral("issue")));
        QVERIFY(!status.data.contains(QStringLiteral("notes")));
        QCOMPARE(job.client.requestKeys(), (QStringList{kIssue, kNotes}));
    }

    // An unknown issue is NotFound, and no notes are asked for.
    void issueSummaryNotFound()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                       {QStringLiteral("iid"), 7}};
        fetchIssueSummaryStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::NotFound);
        QCOMPARE(job.client.requestKeys(), QStringList{kIssue});
    }

    // The registered types: a summary asks for more, and a page for what it
    // asked for in M5, even with a failed pipeline.
    void pagesAskForLessThanSummaries()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);

        const auto runMergeRequest = [&registry](const QString &typeId, FakeStatusJob &job) {
            setUpMergeRequest(job, QStringLiteral("merge_request.json"), QStringLiteral("approvals.json"));
            QVERIFY(job.client.setJsonFile(kJobs, dataFile(QStringLiteral("pipeline_jobs.json"))));
            registry.type(typeId).fetchStatus(job);
            WAIT_FOR_FINISH(job);
        };
        FakeStatusJob mergeRequestPage;
        runMergeRequest(QStringLiteral("gitlab.mr.page"), mergeRequestPage);
        QCOMPARE(mergeRequestPage.client.requestKeys(), (QStringList{kMergeRequest, kApprovals, kUser}));
        QVERIFY(!mergeRequestPage.finished->data.contains(QStringLiteral("jobs")));

        FakeStatusJob mergeRequestSummary;
        runMergeRequest(QStringLiteral("gitlab.mr.summary"), mergeRequestSummary);
        QCOMPARE(mergeRequestSummary.client.requestKeys(), (QStringList{kMergeRequest, kApprovals, kUser, kJobs}));
        QVERIFY(mergeRequestSummary.finished->data.contains(QStringLiteral("jobs")));

        const auto runIssue = [&registry](const QString &typeId, FakeStatusJob &job) {
            setUpIssue(job);
            QVERIFY(job.client.setJsonFile(kNotes, dataFile(QStringLiteral("issue_notes.json"))));
            registry.type(typeId).fetchStatus(job);
            WAIT_FOR_FINISH(job);
        };
        FakeStatusJob issuePage;
        runIssue(QStringLiteral("gitlab.issue.page"), issuePage);
        QCOMPARE(issuePage.client.requestKeys(), QStringList{kIssue});
        QVERIFY(!issuePage.finished->data.contains(QStringLiteral("notes")));

        FakeStatusJob issueSummary;
        runIssue(QStringLiteral("gitlab.issue.summary"), issueSummary);
        QCOMPARE(issueSummary.client.requestKeys(), (QStringList{kIssue, kNotes}));
        QVERIFY(issueSummary.finished->data.contains(QStringLiteral("notes")));
    }

    // A fixed branch whose latest pipeline failed. A failed job that may
    // fail is not counted.
    void pipelineOfAFixedBranch()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                       {QStringLiteral("branchSource"), QStringLiteral("fixed")},
                                       {QStringLiteral("branch"), QStringLiteral(" feature/retry-button ")}};
        QVERIFY(job.client.setJsonFile(kLatest, dataFile(QStringLiteral("pipeline_latest.json"))));
        QVERIFY(job.client.setJsonFile(kJobs, dataFile(QStringLiteral("pipeline_jobs.json"))));
        fetchPipelineStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Failed · feature/retry-button · 1 failed job"));
        QCOMPARE(keysOf(status), QStringList{QStringLiteral("pipeline-failed:9001")});
        QCOMPARE(status.attention.first().text, QStringLiteral("Pipeline failed"));
        QVERIFY(status.attention.first().notify);
        QVERIFY(!status.attention.first().clearsOnView);
        QVERIFY(status.closed.has_value());
        QVERIFY(!*status.closed);
        QCOMPARE(status.data.value(QStringLiteral("branch")).toString(), QStringLiteral("feature/retry-button"));
        QCOMPARE(status.data.value(QStringLiteral("pipeline")).toObject().value(QStringLiteral("id")).toInt(), 9001);
        QCOMPARE(status.data.value(QStringLiteral("jobs")).toArray().size(), 4);
        QCOMPARE(job.client.requestKeys(), (QStringList{kLatest, kJobs}));
        QVERIFY(job.client.requests().constLast().options.followLinks);
    }

    // The branch of a linked merge request: the merge request first.
    void pipelineOfAMergeRequest()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("branchSource"), QStringLiteral("mergeRequest")},
                                       {QStringLiteral("iid"), 42}};
        job.projectSettings = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};
        QVERIFY(job.client.setJsonFile(kMergeRequest, dataFile(QStringLiteral("merge_request.json"))));
        QVERIFY(job.client.setJsonFile(kLatest, dataFile(QStringLiteral("pipeline_latest.json"))));
        QVERIFY(job.client.setJsonFile(kJobs, dataFile(QStringLiteral("pipeline_jobs.json"))));
        fetchPipelineStatus(job);
        WAIT_FOR_FINISH(job);

        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(keysOf(*job.finished), QStringList{QStringLiteral("pipeline-failed:9001")});
        QCOMPARE(job.client.requestKeys(), (QStringList{kMergeRequest, kLatest, kJobs}));
    }

    void passedPipeline()
    {
        QJsonObject pipeline = readJson(QStringLiteral("pipeline_latest.json")).object();
        pipeline.insert(QStringLiteral("status"), QStringLiteral("success"));
        const ItemStatus status = pipelineStatus(QStringLiteral("main"), pipeline, QJsonArray());
        QCOMPARE(status.summary, QStringLiteral("Passed · main"));
        QVERIFY(status.attention.isEmpty());

        pipeline.insert(QStringLiteral("status"), QStringLiteral("waiting_for_resource"));
        QCOMPARE(pipelineStatus(QStringLiteral("main"), pipeline, QJsonArray()).summary,
                 QStringLiteral("Waiting for resource · main"));
    }

    // GitLab answers 404 for a branch without pipelines.
    void branchWithoutPipeline()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                       {QStringLiteral("branch"), QStringLiteral("main")}};
        fetchPipelineStatus(job);
        WAIT_FOR_FINISH(job);

        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->summary, QStringLiteral("No pipeline on main"));
        QVERIFY(job.finished->attention.isEmpty());
        QCOMPARE(job.client.requestKeys(),
                 QStringList{QStringLiteral("GET api/v4/projects/acme%2Fbackend/pipelines/latest?ref=main")});
    }

    void mainFolderWaitsForM7()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                       {QStringLiteral("branchSource"), QStringLiteral("mainFolder")}};
        fetchPipelineStatus(job);
        QCOMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::None);
        QCOMPARE(job.finished->message, QStringLiteral("Follows the main folder from M7"));
        QVERIFY(job.client.requests().isEmpty());
    }

    // Without a project or a number nothing is asked.
    void incompleteConfiguration()
    {
        FakeStatusJob mergeRequest;
        mergeRequest.configValues = QJsonObject{{QStringLiteral("iid"), 42}};
        fetchMergeRequestStatus(mergeRequest);
        QCOMPARE(mergeRequest.finishCount, 1);
        QCOMPARE(mergeRequest.finished->state, ItemStatus::State::None);
        QCOMPARE(mergeRequest.finished->message, QStringLiteral("Set the project and number in the item's settings."));
        QVERIFY(mergeRequest.client.requests().isEmpty());

        FakeStatusJob mergeRequestSummary;
        mergeRequestSummary.configValues = QJsonObject{{QStringLiteral("iid"), 42}};
        fetchMergeRequestSummaryStatus(mergeRequestSummary);
        QCOMPARE(mergeRequestSummary.finishCount, 1);
        QCOMPARE(mergeRequestSummary.finished->state, ItemStatus::State::None);
        QVERIFY(mergeRequestSummary.client.requests().isEmpty());

        FakeStatusJob issueSummary;
        issueSummary.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};
        fetchIssueSummaryStatus(issueSummary);
        QCOMPARE(issueSummary.finishCount, 1);
        QCOMPARE(issueSummary.finished->state, ItemStatus::State::None);
        QVERIFY(issueSummary.client.requests().isEmpty());

        FakeStatusJob issue;
        issue.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};
        fetchIssueStatus(issue);
        QCOMPARE(issue.finishCount, 1);
        QCOMPARE(issue.finished->state, ItemStatus::State::None);
        QVERIFY(issue.client.requests().isEmpty());

        FakeStatusJob pipeline;
        pipeline.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                            {QStringLiteral("branchSource"), QStringLiteral("mergeRequest")}};
        fetchPipelineStatus(pipeline);
        QCOMPARE(pipeline.finishCount, 1);
        QCOMPARE(pipeline.finished->state, ItemStatus::State::None);
        QVERIFY(pipeline.client.requests().isEmpty());

        FakeStatusJob noBranch;
        noBranch.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")}};
        fetchPipelineStatus(noBranch);
        QCOMPARE(noBranch.finished->message, QStringLiteral("Set the branch in the item's settings."));
        QVERIFY(noBranch.client.requests().isEmpty());
    }

    // A missing token reaches the status as NoToken, once, for every type.
    void noTokenPassesThrough()
    {
        const auto check = [](void (*fetch)(mirante::StatusJob &), const QJsonObject &config) {
            FakeStatusJob job;
            job.configValues = config;
            job.client.failAll(ApiReply::Error::NoToken, QStringLiteral("Token not set on this device"));
            fetch(job);
            QTRY_COMPARE(job.finishCount, 1);
            QCoreApplication::processEvents();
            QCOMPARE(job.finishCount, 1);
            QCOMPARE(job.finished->state, ItemStatus::State::NoToken);
            QCOMPARE(job.finished->message, QStringLiteral("Token not set on this device"));
        };
        const QJsonObject single{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                 {QStringLiteral("iid"), 42}};
        check(&fetchMergeRequestStatus, single);
        check(&fetchIssueStatus, single);
        check(&fetchMergeRequestSummaryStatus, single);
        check(&fetchIssueSummaryStatus, single);
        check(&fetchPipelineStatus, QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                                {QStringLiteral("branch"), QStringLiteral("main")}});
        check(&fetchPipelineStatus, QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                                {QStringLiteral("branchSource"), QStringLiteral("mergeRequest")},
                                                {QStringLiteral("iid"), 42}});
    }

    // An unknown merge request or issue is NotFound, even when the other
    // requests answer.
    void notFoundPassesThrough()
    {
        FakeStatusJob mergeRequest;
        mergeRequest.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                                {QStringLiteral("iid"), 42}};
        QVERIFY(mergeRequest.client.setJsonFile(kUser, dataFile(QStringLiteral("user.json"))));
        QVERIFY(mergeRequest.client.setJsonFile(kApprovals, dataFile(QStringLiteral("approvals.json"))));
        fetchMergeRequestStatus(mergeRequest);
        WAIT_FOR_FINISH(mergeRequest);
        QCOMPARE(mergeRequest.finished->state, ItemStatus::State::NotFound);
        QCOMPARE(mergeRequest.finished->message, QStringLiteral("Not found (404)"));

        FakeStatusJob issue;
        issue.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                         {QStringLiteral("iid"), 7}};
        fetchIssueStatus(issue);
        WAIT_FOR_FINISH(issue);
        QCOMPARE(issue.finished->state, ItemStatus::State::NotFound);

        FakeStatusJob pipeline;
        pipeline.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                            {QStringLiteral("branchSource"), QStringLiteral("mergeRequest")},
                                            {QStringLiteral("iid"), 42}};
        fetchPipelineStatus(pipeline);
        WAIT_FOR_FINISH(pipeline);
        QCOMPARE(pipeline.finished->state, ItemStatus::State::NotFound);
        QCOMPARE(pipeline.client.requestKeys(), QStringList{kMergeRequest});
    }

    // The types that name one object report status; the lists do not yet.
    // fetchStatus() of a registered type runs the module's fetch.
    void typesReportStatus()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("gitlab"), &mirante::modules::gitlab::registerModule);
        const QStringList withStatus{QStringLiteral("gitlab.mr.summary"), QStringLiteral("gitlab.mr.page"),
                                     QStringLiteral("gitlab.pipeline"), QStringLiteral("gitlab.issue.summary"),
                                     QStringLiteral("gitlab.issue.page")};
        for (const mirante::ItemType *type : registry.types())
            QCOMPARE(type->hasStatus(), withStatus.contains(type->id()));

        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("project"), QStringLiteral("acme/backend")},
                                       {QStringLiteral("iid"), 7}};
        QVERIFY(job.client.setJsonFile(kIssue, dataFile(QStringLiteral("issue.json"))));
        registry.type(QStringLiteral("gitlab.issue.page")).fetchStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);

        FakeStatusJob list;
        registry.type(QStringLiteral("gitlab.mr.list")).fetchStatus(list);
        QCOMPARE(list.finishCount, 1);
        QCOMPARE(list.finished->state, ItemStatus::State::None);
    }
};

QTEST_MAIN(TestStatus)
#include "tst_status.moc"
