// The status of GitHub pull requests, issues and runs, on answers recorded
// from public repositories and trimmed by hand, in tests/data/. See the
// README's Status section for which files are written by hand. "Me" is the user
// in data/user.json, ana-reviewer, id 1001. The repository is acme/widgets.
//
// Texts with a count use %n. Tests install no translator, so they read the
// source forms, such as "1 approval(s)".
#include "../GitHubStatus.h"

#include "items/ItemTypeRegistry.h"

#include <mirante/testing/FakeStatusJob.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace mirante::modules::github {
void registerModule(mirante::ModuleRegistrar &registrar);
}

using mirante::ApiReply;
using mirante::ItemStatus;
using mirante::testing::FakeStatusJob;
using namespace mirante::modules::github;

namespace {

const QString kHeadSha = QStringLiteral("04c4ebf52e7033a83eb38e9741c8ace05bf8617b");
const QString kOlderSha = QStringLiteral("856786ce5925a1cc05dd91ea5712b59b33e36458");

const QString kPullRequest = QStringLiteral("GET repos/acme/widgets/pulls/42");
const QString kReviews = QStringLiteral("GET repos/acme/widgets/pulls/42/reviews?per_page=100");
const QString kUser = QStringLiteral("GET user");
const QString kCheckRuns = QStringLiteral("GET repos/acme/widgets/commits/") + kHeadSha
    + QStringLiteral("/check-runs?per_page=100");
const QString kCombinedStatus = QStringLiteral("GET repos/acme/widgets/commits/") + kHeadSha + QStringLiteral("/status");
const QString kIssue = QStringLiteral("GET repos/acme/widgets/issues/7");
const QString kCommentsPage1 = QStringLiteral("GET repos/acme/widgets/issues/7/comments?per_page=20&page=1");
const QString kCommentsPage2 = QStringLiteral("GET repos/acme/widgets/issues/7/comments?per_page=20&page=2");
const QString kRepository = QStringLiteral("GET repos/acme/widgets");
const QString kBranchRuns =
    QStringLiteral("GET repos/acme/widgets/actions/runs?branch=feature%2Fskip-refresh&per_page=20");
const QString kMainRuns = QStringLiteral("GET repos/acme/widgets/actions/runs?branch=main&per_page=20");
const QString kTrunkRuns = QStringLiteral("GET repos/acme/widgets/actions/runs?branch=trunk&per_page=20");

// The lead run of feature/skip-refresh and its two siblings, all created at
// once; the id orders them.
constexpr qint64 kLeadRun = 40553652299;
constexpr qint64 kLintRun = 40553652200;
constexpr qint64 kCodeQlRun = 40553652147;

QString jobsKey(qint64 runId)
{
    return QStringLiteral("GET repos/acme/widgets/actions/runs/%1/jobs?filter=latest&per_page=100").arg(runId);
}

QString dataFile(const QString &name)
{
    return QFINDTESTDATA(QStringLiteral("tests/data/") + name);
}

QJsonDocument readJson(const QString &name)
{
    QFile file(dataFile(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read %s", qPrintable(name));
    return QJsonDocument::fromJson(file.readAll());
}

QJsonObject object(const QString &name)
{
    return readJson(name).object();
}

QJsonArray array(const QString &name)
{
    return readJson(name).array();
}

QJsonArray checkRuns()
{
    return object(QStringLiteral("check_runs.json")).value(QStringLiteral("check_runs")).toArray();
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

ApiReply forbiddenReply()
{
    ApiReply reply;
    reply.error = ApiReply::Error::Forbidden;
    reply.httpStatus = 403;
    reply.message = QStringLiteral("Forbidden (403)");
    return reply;
}

// The ids of runs, or of {"run": ...} entries.
QList<qint64> idsOf(const QJsonArray &runs)
{
    QList<qint64> ids;
    for (const QJsonValue &value : runs) {
        const QJsonObject entry = value.toObject();
        const QJsonObject run =
            entry.contains(QStringLiteral("run")) ? entry.value(QStringLiteral("run")).toObject() : entry;
        ids << run.value(QStringLiteral("id")).toInteger();
    }
    return ids;
}

QList<qint64> reviewIds(const QJsonArray &reviews)
{
    QList<qint64> ids;
    for (const QJsonValue &review : reviews)
        ids << review.toObject().value(QStringLiteral("id")).toInteger();
    return ids;
}

QJsonObject review(qint64 id, qint64 user, const QString &state, const QString &submitted)
{
    QJsonObject result{{QStringLiteral("id"), id},
                       {QStringLiteral("user"), QJsonObject{{QStringLiteral("id"), user}}},
                       {QStringLiteral("state"), state}};
    if (!submitted.isEmpty())
        result.insert(QStringLiteral("submitted_at"), submitted);
    return result;
}

QJsonObject run(qint64 id, const QString &sha, const QString &created, const QString &status = QStringLiteral("completed"),
                const QString &conclusion = QStringLiteral("success"))
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("name"), QStringLiteral("CI")},
                       {QStringLiteral("head_sha"), sha},
                       {QStringLiteral("created_at"), created},
                       {QStringLiteral("status"), status},
                       {QStringLiteral("conclusion"), conclusion},
                       {QStringLiteral("run_attempt"), 1}};
}

QJsonObject check(const QString &status, const QString &conclusion)
{
    return QJsonObject{{QStringLiteral("status"), status}, {QStringLiteral("conclusion"), conclusion}};
}

QJsonObject combined(const QString &state, int statuses)
{
    QJsonArray list;
    for (int i = 0; i < statuses; ++i)
        list.append(QJsonObject{{QStringLiteral("state"), state}});
    return QJsonObject{{QStringLiteral("state"), state}, {QStringLiteral("statuses"), list}};
}

// A pull request job for acme/widgets#42, its repository taken from the
// project setting, with every answer.
void setUpPullRequest(FakeStatusJob &job, const QString &reviews = QStringLiteral("pull_request_reviews.json"))
{
    job.configValues = QJsonObject{{QStringLiteral("number"), 42}};
    job.projectSettings = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")}};
    QVERIFY(job.client.setJsonFile(kPullRequest, dataFile(QStringLiteral("pull_request.json"))));
    QVERIFY(job.client.setJsonFile(kReviews, dataFile(reviews)));
    QVERIFY(job.client.setJsonFile(kUser, dataFile(QStringLiteral("user.json"))));
    QVERIFY(job.client.setJsonFile(kCheckRuns, dataFile(QStringLiteral("check_runs.json"))));
    QVERIFY(job.client.setJsonFile(kCombinedStatus, dataFile(QStringLiteral("combined_status_empty.json"))));
}

// An issue job for acme/widgets#7.
void setUpIssue(FakeStatusJob &job)
{
    job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                   {QStringLiteral("number"), 7}};
    QVERIFY(job.client.setJsonFile(kIssue, dataFile(QStringLiteral("issue.json"))));
}

// A run job for the fixed branch feature/skip-refresh, with its runs and the
// jobs of its group.
void setUpBranch(FakeStatusJob &job)
{
    job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                   {QStringLiteral("branchSource"), QStringLiteral("fixed")},
                                   {QStringLiteral("branch"), QStringLiteral(" feature/skip-refresh ")}};
    QVERIFY(job.client.setJsonFile(kBranchRuns, dataFile(QStringLiteral("workflow_runs.json"))));
    QVERIFY(job.client.setJsonFile(jobsKey(kLeadRun), dataFile(QStringLiteral("run_jobs.json"))));
    QVERIFY(job.client.setJsonFile(jobsKey(kLintRun), dataFile(QStringLiteral("run_jobs_lint.json"))));
    QVERIFY(job.client.setJsonFile(jobsKey(kCodeQlRun), dataFile(QStringLiteral("run_jobs_codeql.json"))));
}

// A run job for main, whose newest commit has a failed, re-run CI run, a
// timed-out Lint run and a passed Docs run.
void setUpFailedMain(FakeStatusJob &job)
{
    job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                   {QStringLiteral("branch"), QStringLiteral("main")}};
    QVERIFY(job.client.setJsonFile(kMainRuns, dataFile(QStringLiteral("workflow_runs_failed.json"))));
    QVERIFY(job.client.setJsonFile(jobsKey(9103), dataFile(QStringLiteral("run_jobs_failed.json"))));
    job.client.setJson(jobsKey(9102), QJsonDocument(QJsonObject{{QStringLiteral("total_count"), 0},
                                                                {QStringLiteral("jobs"), QJsonArray()}}));
    job.client.setJson(jobsKey(9101), QJsonDocument(QJsonObject{{QStringLiteral("total_count"), 0},
                                                                {QStringLiteral("jobs"), QJsonArray()}}));
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
    // ---- Checks ----

    void checksState_data()
    {
        QTest::addColumn<QJsonArray>("runs");
        QTest::addColumn<QJsonObject>("status");
        QTest::addColumn<int>("expected");

        const QString done = QStringLiteral("completed");
        QTest::newRow("nothing") << QJsonArray() << QJsonObject() << int(ChecksState::None);
        QTest::newRow("pending without statuses") << QJsonArray() << combined(QStringLiteral("pending"), 0)
                                                  << int(ChecksState::None);
        QTest::newRow("all passed") << QJsonArray{check(done, QStringLiteral("success")),
                                                  check(done, QStringLiteral("skipped"))}
                                    << QJsonObject() << int(ChecksState::Passed);
        QTest::newRow("statuses passed") << QJsonArray() << combined(QStringLiteral("success"), 2)
                                         << int(ChecksState::Passed);
        QTest::newRow("cancelled alone passes") << QJsonArray{check(done, QStringLiteral("cancelled")),
                                                              check(done, QStringLiteral("action_required"))}
                                                << QJsonObject() << int(ChecksState::Passed);
        QTest::newRow("one in progress") << QJsonArray{check(done, QStringLiteral("success")),
                                                       check(QStringLiteral("in_progress"), QString())}
                                         << QJsonObject() << int(ChecksState::Running);
        QTest::newRow("statuses pending") << QJsonArray{check(done, QStringLiteral("success"))}
                                          << combined(QStringLiteral("pending"), 1) << int(ChecksState::Running);
        QTest::newRow("failure") << QJsonArray{check(done, QStringLiteral("failure")),
                                               check(QStringLiteral("queued"), QString())}
                                 << QJsonObject() << int(ChecksState::Failed);
        QTest::newRow("timed out") << QJsonArray{check(done, QStringLiteral("timed_out"))} << QJsonObject()
                                   << int(ChecksState::Failed);
        QTest::newRow("startup failure") << QJsonArray{check(done, QStringLiteral("startup_failure"))}
                                         << QJsonObject() << int(ChecksState::Failed);
        QTest::newRow("statuses error") << QJsonArray{check(QStringLiteral("in_progress"), QString())}
                                        << combined(QStringLiteral("error"), 1) << int(ChecksState::Failed);
        QTest::newRow("statuses failure") << QJsonArray() << combined(QStringLiteral("failure"), 1)
                                          << int(ChecksState::Failed);
    }

    void checksState()
    {
        QFETCH(QJsonArray, runs);
        QFETCH(QJsonObject, status);
        QFETCH(int, expected);
        QCOMPARE(int(mirante::modules::github::checksState(runs, status)), expected);
    }

    // The recorded answers: eleven check runs that passed, and a combined
    // status from a repository that uses statuses, with one that errored.
    void checksOfRecordedAnswers()
    {
        QCOMPARE(mirante::modules::github::checksState(checkRuns(), object(QStringLiteral("combined_status_empty.json"))),
                 ChecksState::Passed);
        QCOMPARE(mirante::modules::github::checksState({}, object(QStringLiteral("combined_status.json"))),
                 ChecksState::Failed);
    }

    // ---- Reviews ----

    // Recorded: a bot and carla comment, dario approves.
    void reviewsOfARecordedAnswer()
    {
        const QJsonArray reviews = array(QStringLiteral("pull_request_reviews.json"));
        QCOMPARE(reviewIds(latestReviews(reviews)), (QList<qint64>{700001, 700002, 700003}));
        const ReviewState state = reviewState(reviews);
        QCOMPARE(state.approvals, 1);
        QVERIFY(!state.changesRequested);
        QVERIFY(state.approved());
    }

    // Written by hand: carla approves, then requests changes; dario approves,
    // then comments, which keeps the approval; erin's review was dismissed;
    // frank's is pending. The answer is out of order.
    void reviewsWithChangesRequested()
    {
        const QJsonArray reviews = array(QStringLiteral("pull_request_reviews_changes.json"));
        QCOMPARE(reviewIds(latestReviews(reviews)), (QList<qint64>{710004, 710002}));
        const ReviewState state = reviewState(reviews);
        QCOMPARE(state.approvals, 1);
        QVERIFY(state.changesRequested);
        QVERIFY(!state.approved());
    }

    // DISMISSED removes an earlier approval; a later approval counts again.
    // Only comments keep the latest comment.
    void dismissedAndCommentedReviews()
    {
        const QString approved = QStringLiteral("APPROVED");
        const QJsonArray dismissed{review(1, 10, approved, QStringLiteral("2026-10-01T10:00:00Z")),
                                   review(2, 10, QStringLiteral("DISMISSED"), QStringLiteral("2026-10-01T11:00:00Z"))};
        QVERIFY(latestReviews(dismissed).isEmpty());
        QCOMPARE(reviewState(dismissed).approvals, 0);

        QJsonArray again = dismissed;
        again.append(review(3, 10, approved, QStringLiteral("2026-10-01T12:00:00Z")));
        QCOMPARE(reviewIds(latestReviews(again)), QList<qint64>{3});

        const QJsonArray comments{review(4, 20, QStringLiteral("COMMENTED"), QStringLiteral("2026-10-01T10:00:00Z")),
                                  review(5, 20, QStringLiteral("COMMENTED"), QStringLiteral("2026-10-01T11:00:00Z")),
                                  review(6, 30, QStringLiteral("PENDING"), QString())};
        QCOMPARE(reviewIds(latestReviews(comments)), QList<qint64>{5});
        QCOMPARE(reviewState(comments).approvals, 0);
        QVERIFY(!reviewState(comments).approved());
    }

    // ---- Pull requests ----

    // Open, me among the requested reviewers, checks passed, one approval.
    void pullRequestWithReviewRequested()
    {
        FakeStatusJob job;
        setUpPullRequest(job);
        fetchPullRequestStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(status.message.isEmpty());
        QCOMPARE(status.summary, QStringLiteral("Open · 1 approval(s) · checks passed"));
        QCOMPARE(keysOf(status), QStringList{QStringLiteral("review")});
        QCOMPARE(status.attention.first().text, QStringLiteral("Review requested"));
        QVERIFY(!status.attention.first().notify);
        QVERIFY(!status.attention.first().clearsOnView);
        QVERIFY(status.closed.has_value());
        QVERIFY(!*status.closed);
        QVERIFY(status.updatedAt.isValid());

        QCOMPARE(status.data.value(QStringLiteral("pullRequest")).toObject().value(QStringLiteral("number")).toInt(), 42);
        QCOMPARE(status.data.value(QStringLiteral("reviews")).toArray().size(), 3);
        QCOMPARE(status.data.value(QStringLiteral("user")).toObject(),
                 (QJsonObject{{QStringLiteral("id"), 1001}, {QStringLiteral("login"), QStringLiteral("ana-reviewer")}}));
        QCOMPARE(status.data.value(QStringLiteral("checkRuns")).toArray().size(), 11);
        QCOMPARE(status.data.value(QStringLiteral("combinedStatus")).toObject().value(QStringLiteral("state")).toString(),
                 QStringLiteral("pending"));

        // The first three at once, then the checks of the head commit.
        QCOMPARE(job.client.requestKeys(), (QStringList{kPullRequest, kReviews, kUser, kCheckRuns, kCombinedStatus}));
        const auto &requests = job.client.requests();
        QVERIFY(requests.at(1).options.followLinks);
        QVERIFY(requests.at(3).options.followLinks);
        QCOMPARE(requests.at(3).options.arrayKey, QStringLiteral("check_runs"));
    }

    // Failed checks need attention from everyone, with a notification.
    void checksFailed()
    {
        FakeStatusJob job;
        setUpPullRequest(job);
        QVERIFY(job.client.setJsonFile(kCombinedStatus, dataFile(QStringLiteral("combined_status.json"))));
        fetchPullRequestStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.summary, QStringLiteral("Open · 1 approval(s) · checks failed"));
        QCOMPARE(keysOf(status), (QStringList{QStringLiteral("checks-failed"), QStringLiteral("review")}));
        const mirante::AttentionReason *failed = findReason(status, QStringLiteral("checks-failed"));
        QCOMPARE(failed->text, QStringLiteral("Checks failed"));
        QVERIFY(failed->notify);
        QVERIFY(!failed->clearsOnView);

        const ItemStatus bystander = pullRequestStatus(
            object(QStringLiteral("pull_request.json")), {}, QJsonObject{{QStringLiteral("id"), 9999}}, {},
            object(QStringLiteral("combined_status.json")));
        QCOMPARE(keysOf(bystander), QStringList{QStringLiteral("checks-failed")});
        QCOMPARE(bystander.summary, QStringLiteral("Open · checks failed"));
    }

    // Running checks read so, and need nothing.
    void checksRunning()
    {
        QJsonArray runs = checkRuns();
        QJsonObject first = runs.at(0).toObject();
        first.insert(QStringLiteral("status"), QStringLiteral("in_progress"));
        first.insert(QStringLiteral("conclusion"), QJsonValue::Null);
        runs.replace(0, first);
        const ItemStatus status = pullRequestStatus(object(QStringLiteral("pull_request.json")), {},
                                                    QJsonObject{{QStringLiteral("id"), 9999}}, runs, {});
        QCOMPARE(status.summary, QStringLiteral("Open · checks running"));
        QVERIFY(status.attention.isEmpty());
    }

    // My own pull request, approved: an event that clears on view.
    void myPullRequestApproved()
    {
        const QJsonObject me{{QStringLiteral("id"), 2002}, {QStringLiteral("login"), QStringLiteral("bruno-author")}};
        const ItemStatus status = pullRequestStatus(object(QStringLiteral("pull_request.json")),
                                                    array(QStringLiteral("pull_request_reviews.json")), me,
                                                    checkRuns(), {});
        QCOMPARE(keysOf(status), QStringList{QStringLiteral("approved")});
        QCOMPARE(status.attention.first().text, QStringLiteral("Approved"));
        QVERIFY(status.attention.first().clearsOnView);
        QVERIFY(!status.attention.first().notify);
    }

    // My own pull request with changes requested: not approved, though one
    // reviewer approves.
    void myPullRequestWithChangesRequested()
    {
        FakeStatusJob job;
        setUpPullRequest(job, QStringLiteral("pull_request_reviews_changes.json"));
        job.client.setJson(kUser, QJsonDocument(QJsonObject{{QStringLiteral("id"), 2002},
                                                            {QStringLiteral("login"), QStringLiteral("bruno-author")}}));
        fetchPullRequestStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.summary, QStringLiteral("Open · changes requested · checks passed"));
        QCOMPARE(keysOf(status), QStringList{QStringLiteral("changes-requested")});
        QCOMPARE(status.attention.first().text, QStringLiteral("Changes requested"));
        QVERIFY(!status.attention.first().clearsOnView);
        // GitHub's answer is stored whole, not the latest reviews.
        QCOMPARE(status.data.value(QStringLiteral("reviews")).toArray().size(), 6);
    }

    // Conflicts are the author's, and only false means conflicts: null is
    // GitHub still computing.
    void conflicts()
    {
        QJsonObject pullRequest = object(QStringLiteral("pull_request.json"));
        pullRequest.insert(QStringLiteral("mergeable"), false);
        pullRequest.insert(QStringLiteral("draft"), true);
        const QJsonObject author{{QStringLiteral("id"), 2002}};
        const ItemStatus mine = pullRequestStatus(pullRequest, {}, author, {}, {});
        QCOMPARE(mine.summary, QStringLiteral("Draft · has conflicts"));
        QCOMPARE(keysOf(mine), QStringList{QStringLiteral("conflicts")});
        QCOMPARE(mine.attention.first().text, QStringLiteral("Has conflicts"));

        const ItemStatus theirs = pullRequestStatus(pullRequest, {}, QJsonObject{{QStringLiteral("id"), 3003}}, {}, {});
        QCOMPARE(keysOf(theirs), QStringList{QStringLiteral("review")});

        pullRequest.insert(QStringLiteral("mergeable"), QJsonValue::Null);
        const ItemStatus computing = pullRequestStatus(pullRequest, {}, author, {}, {});
        QCOMPARE(computing.summary, QStringLiteral("Draft"));
        QVERIFY(computing.attention.isEmpty());
    }

    // Merged or closed: closed, and no attention even with failed checks and
    // conflicts.
    void closedPullRequests()
    {
        QJsonObject merged = object(QStringLiteral("pull_request.json"));
        merged.insert(QStringLiteral("state"), QStringLiteral("closed"));
        merged.insert(QStringLiteral("merged"), true);
        merged.insert(QStringLiteral("merged_at"), QStringLiteral("2026-10-29T10:00:00Z"));
        merged.insert(QStringLiteral("mergeable"), false);
        const QJsonObject me = object(QStringLiteral("user.json"));
        const QJsonObject failed = object(QStringLiteral("combined_status.json"));
        const ItemStatus mergedStatus =
            pullRequestStatus(merged, array(QStringLiteral("pull_request_reviews.json")), me, {}, failed);
        QCOMPARE(mergedStatus.summary, QStringLiteral("Merged · 1 approval(s) · checks failed"));
        QVERIFY(mergedStatus.attention.isEmpty());
        QVERIFY(*mergedStatus.closed);

        // A pull request list answer may hold merged_at without merged.
        merged.remove(QStringLiteral("merged"));
        QCOMPARE(pullRequestStatus(merged, {}, me, {}, {}).summary, QStringLiteral("Merged"));

        QJsonObject closed = object(QStringLiteral("pull_request.json"));
        closed.insert(QStringLiteral("state"), QStringLiteral("closed"));
        const ItemStatus closedStatus = pullRequestStatus(closed, {}, me, {}, failed);
        QCOMPARE(closedStatus.summary, QStringLiteral("Closed · checks failed"));
        QVERIFY(closedStatus.attention.isEmpty());
        QVERIFY(*closedStatus.closed);
    }

    // Several approvals.
    void approvalsCount()
    {
        const QJsonArray reviews{review(1, 10, QStringLiteral("APPROVED"), QStringLiteral("2026-10-01T10:00:00Z")),
                                 review(2, 20, QStringLiteral("APPROVED"), QStringLiteral("2026-10-01T11:00:00Z"))};
        QCOMPARE(pullRequestStatus(object(QStringLiteral("pull_request.json")), reviews, {}, {}, {}).summary,
                 QStringLiteral("Open · 2 approval(s)"));
    }

    // A failed check request leaves that part out.
    void pullRequestWhenChecksFail()
    {
        FakeStatusJob runsFailed;
        setUpPullRequest(runsFailed);
        runsFailed.client.setReply(kCheckRuns, forbiddenReply());
        QVERIFY(runsFailed.client.setJsonFile(kCombinedStatus, dataFile(QStringLiteral("combined_status.json"))));
        fetchPullRequestStatus(runsFailed);
        WAIT_FOR_FINISH(runsFailed);
        QCOMPARE(runsFailed.finished->state, ItemStatus::State::Ok);
        QCOMPARE(runsFailed.finished->summary, QStringLiteral("Open · 1 approval(s) · checks failed"));
        QVERIFY(runsFailed.finished->data.value(QStringLiteral("checkRuns")).toArray().isEmpty());

        FakeStatusJob both;
        setUpPullRequest(both);
        both.client.setReply(kCheckRuns, forbiddenReply());
        both.client.setReply(kCombinedStatus, forbiddenReply());
        fetchPullRequestStatus(both);
        WAIT_FOR_FINISH(both);
        QCOMPARE(both.finished->state, ItemStatus::State::Ok);
        QVERIFY(both.finished->message.isEmpty());
        QCOMPARE(both.finished->summary, QStringLiteral("Open · 1 approval(s)"));
        QVERIFY(both.finished->data.value(QStringLiteral("combinedStatus")).toObject().isEmpty());
    }

    // The pull request's own reply wins; a failed review or user reply is an
    // error too; no checks are asked for.
    void pullRequestErrors()
    {
        FakeStatusJob notFound;
        setUpPullRequest(notFound);
        notFound.client.setReply(kReviews, forbiddenReply());
        ApiReply missing;
        missing.error = ApiReply::Error::NotFound;
        missing.httpStatus = 404;
        missing.message = QStringLiteral("Not found (404)");
        notFound.client.setReply(kPullRequest, missing);
        fetchPullRequestStatus(notFound);
        WAIT_FOR_FINISH(notFound);
        QCOMPARE(notFound.finished->state, ItemStatus::State::NotFound);
        QCOMPARE(notFound.finished->message, QStringLiteral("Not found (404)"));
        QCOMPARE(notFound.client.requestKeys(), (QStringList{kPullRequest, kReviews, kUser}));

        FakeStatusJob reviews;
        setUpPullRequest(reviews);
        reviews.client.setReply(kReviews, forbiddenReply());
        fetchPullRequestStatus(reviews);
        WAIT_FOR_FINISH(reviews);
        QCOMPARE(reviews.finished->state, ItemStatus::State::Error);
        QCOMPARE(reviews.finished->message, QStringLiteral("Forbidden (403)"));
        QVERIFY(reviews.finished->data.isEmpty());

        FakeStatusJob user;
        setUpPullRequest(user);
        user.client.setReply(kUser, forbiddenReply());
        fetchPullRequestStatus(user);
        WAIT_FOR_FINISH(user);
        QCOMPARE(user.finished->state, ItemStatus::State::Error);
        QCOMPARE(user.client.requestKeys(), (QStringList{kPullRequest, kReviews, kUser}));
    }

    // ---- Issues ----

    void issue()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("/acme/widgets/")},
                                       {QStringLiteral("number"), QStringLiteral("7")}};
        QVERIFY(job.client.setJsonFile(kIssue, dataFile(QStringLiteral("issue.json"))));
        fetchIssueStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Open · assigned to carla-dev"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!*status.closed);
        QCOMPARE(status.data.value(QStringLiteral("issue")).toObject().value(QStringLiteral("number")).toInt(), 7);
        QVERIFY(!status.data.contains(QStringLiteral("comments")));
        QCOMPARE(job.client.requestKeys(), QStringList{kIssue});

        QJsonObject closed = object(QStringLiteral("issue.json"));
        closed.insert(QStringLiteral("state"), QStringLiteral("closed"));
        closed.insert(QStringLiteral("state_reason"), QStringLiteral("not_planned"));
        closed.insert(QStringLiteral("assignees"),
                      QJsonArray{QJsonObject{{QStringLiteral("login"), QStringLiteral("ana-reviewer")}},
                                 QJsonObject{{QStringLiteral("login"), QStringLiteral("carla-dev")}}});
        const ItemStatus closedStatus = issueStatus(closed);
        QCOMPARE(closedStatus.summary, QStringLiteral("Closed · assigned to ana-reviewer, carla-dev"));
        QVERIFY(*closedStatus.closed);

        closed.insert(QStringLiteral("assignees"), QJsonArray());
        QCOMPARE(issueStatus(closed).summary, QStringLiteral("Closed"));
    }

    // 21 comments: the last page holds one, fewer than five, so the page
    // before it is read too. The newest 20 are kept, newest first.
    void issueSummaryReadsTheLastPages()
    {
        FakeStatusJob job;
        setUpIssue(job);
        QVERIFY(job.client.setJsonFile(kCommentsPage2, dataFile(QStringLiteral("issue_comments_page2.json"))));
        QVERIFY(job.client.setJsonFile(kCommentsPage1, dataFile(QStringLiteral("issue_comments_page1.json"))));
        fetchIssueSummaryStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Open · assigned to carla-dev"));
        const QJsonArray comments = status.data.value(QStringLiteral("comments")).toArray();
        QCOMPARE(comments.size(), 20);
        QCOMPARE(comments.first().toObject().value(QStringLiteral("body")).toString(), QStringLiteral("Comment 21."));
        QCOMPARE(comments.at(1).toObject().value(QStringLiteral("body")).toString(), QStringLiteral("Comment 20."));
        QCOMPARE(comments.last().toObject().value(QStringLiteral("body")).toString(), QStringLiteral("Comment 2."));
        QCOMPARE(job.client.requestKeys(), (QStringList{kIssue, kCommentsPage2, kCommentsPage1}));
        QVERIFY(!job.client.requests().at(1).options.followLinks);
    }

    // A last page with five or more, or the first page, is read alone.
    void issueSummaryReadsOnePage()
    {
        FakeStatusJob many;
        setUpIssue(many);
        QJsonObject issue = object(QStringLiteral("issue.json"));
        issue.insert(QStringLiteral("comments"), 25);
        many.client.setJson(kIssue, QJsonDocument(issue));
        QJsonArray five = array(QStringLiteral("issue_comments_page1.json"));
        while (five.size() > 5)
            five.removeFirst();
        many.client.setJson(kCommentsPage2, QJsonDocument(five));
        fetchIssueSummaryStatus(many);
        WAIT_FOR_FINISH(many);
        QCOMPARE(many.finished->data.value(QStringLiteral("comments")).toArray().size(), 5);
        QCOMPARE(many.finished->data.value(QStringLiteral("comments"))
                     .toArray()
                     .first()
                     .toObject()
                     .value(QStringLiteral("body"))
                     .toString(),
                 QStringLiteral("Comment 20."));
        QCOMPARE(many.client.requestKeys(), (QStringList{kIssue, kCommentsPage2}));

        FakeStatusJob few;
        setUpIssue(few);
        issue.insert(QStringLiteral("comments"), 3);
        few.client.setJson(kIssue, QJsonDocument(issue));
        QJsonArray three = five;
        while (three.size() > 3)
            three.removeFirst();
        few.client.setJson(kCommentsPage1, QJsonDocument(three));
        fetchIssueSummaryStatus(few);
        WAIT_FOR_FINISH(few);
        QCOMPARE(few.finished->data.value(QStringLiteral("comments")).toArray().size(), 3);
        QCOMPARE(few.client.requestKeys(), (QStringList{kIssue, kCommentsPage1}));
    }

    // Without comments none are asked for, and data has no comments.
    void issueSummaryWithoutComments()
    {
        FakeStatusJob job;
        setUpIssue(job);
        QJsonObject issue = object(QStringLiteral("issue.json"));
        issue.insert(QStringLiteral("comments"), 0);
        job.client.setJson(kIssue, QJsonDocument(issue));
        fetchIssueSummaryStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QVERIFY(!job.finished->data.contains(QStringLiteral("comments")));
        QCOMPARE(job.client.requestKeys(), QStringList{kIssue});
    }

    // A failed comments request keeps the status; a failed page before the
    // last keeps the last page's comments.
    void issueSummaryWhenCommentsFail()
    {
        FakeStatusJob last;
        setUpIssue(last);
        last.client.setReply(kCommentsPage2, forbiddenReply());
        fetchIssueSummaryStatus(last);
        WAIT_FOR_FINISH(last);
        QCOMPARE(last.finished->state, ItemStatus::State::Ok);
        QVERIFY(last.finished->message.isEmpty());
        QVERIFY(last.finished->data.contains(QStringLiteral("issue")));
        QVERIFY(!last.finished->data.contains(QStringLiteral("comments")));
        QCOMPARE(last.client.requestKeys(), (QStringList{kIssue, kCommentsPage2}));

        FakeStatusJob before;
        setUpIssue(before);
        QVERIFY(before.client.setJsonFile(kCommentsPage2, dataFile(QStringLiteral("issue_comments_page2.json"))));
        before.client.setReply(kCommentsPage1, forbiddenReply());
        fetchIssueSummaryStatus(before);
        WAIT_FOR_FINISH(before);
        QCOMPARE(before.finished->state, ItemStatus::State::Ok);
        QCOMPARE(before.finished->data.value(QStringLiteral("comments")).toArray().size(), 1);
    }

    // An unknown issue is NotFound, and no comments are asked for.
    void issueSummaryNotFound()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                       {QStringLiteral("number"), 7}};
        fetchIssueSummaryStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::NotFound);
        QCOMPARE(job.client.requestKeys(), QStringList{kIssue});
    }

    // More than 20 comments given are cut to 20.
    void issueStatusKeepsTwentyComments()
    {
        QJsonArray comments;
        for (int i = 0; i < 25; ++i)
            comments.append(QJsonObject{{QStringLiteral("id"), i}});
        const ItemStatus status = issueStatus(object(QStringLiteral("issue.json")), comments);
        const QJsonArray kept = status.data.value(QStringLiteral("comments")).toArray();
        QCOMPARE(kept.size(), 20);
        QCOMPARE(kept.first().toObject().value(QStringLiteral("id")).toInt(), 0);
    }

    // The registered types: pages and summaries of pull requests ask alike;
    // an issue page asks for no comments.
    void pagesAndSummaries()
    {
        mirante::ItemTypeRegistry registry;
        registry.registerModule(QStringLiteral("github"), &mirante::modules::github::registerModule);

        for (const QString &typeId : {QStringLiteral("github.pr.summary"), QStringLiteral("github.pr.page")}) {
            FakeStatusJob job;
            setUpPullRequest(job);
            registry.type(typeId).fetchStatus(job);
            WAIT_FOR_FINISH(job);
            QCOMPARE(job.finished->state, ItemStatus::State::Ok);
            QCOMPARE(job.client.requestKeys(), (QStringList{kPullRequest, kReviews, kUser, kCheckRuns, kCombinedStatus}));
        }

        const auto runIssue = [&registry](const QString &typeId, FakeStatusJob &job) {
            setUpIssue(job);
            QVERIFY(job.client.setJsonFile(kCommentsPage2, dataFile(QStringLiteral("issue_comments_page2.json"))));
            QVERIFY(job.client.setJsonFile(kCommentsPage1, dataFile(QStringLiteral("issue_comments_page1.json"))));
            registry.type(typeId).fetchStatus(job);
            WAIT_FOR_FINISH(job);
        };
        FakeStatusJob issuePage;
        runIssue(QStringLiteral("github.issue.page"), issuePage);
        QCOMPARE(issuePage.client.requestKeys(), QStringList{kIssue});
        QVERIFY(!issuePage.finished->data.contains(QStringLiteral("comments")));

        FakeStatusJob issueSummary;
        runIssue(QStringLiteral("github.issue.summary"), issueSummary);
        QCOMPARE(issueSummary.client.requestKeys(), (QStringList{kIssue, kCommentsPage2, kCommentsPage1}));
        QVERIFY(issueSummary.finished->data.contains(QStringLiteral("comments")));

        FakeStatusJob runs;
        setUpBranch(runs);
        registry.type(QStringLiteral("github.run")).fetchStatus(runs);
        WAIT_FOR_FINISH(runs);
        QCOMPARE(runs.finished->state, ItemStatus::State::Ok);
    }

    // ---- The run group ----

    // Newest first by created_at, then by id; the newest run's commit.
    void runGroupOfARecordedAnswer()
    {
        const QJsonArray runs = object(QStringLiteral("workflow_runs.json")).value(QStringLiteral("workflow_runs")).toArray();
        const RunGroup group = runGroup(runs);
        QCOMPARE(group.headSha, kHeadSha);
        QCOMPARE(group.lead.value(QStringLiteral("id")).toInteger(), kLeadRun);
        QCOMPARE(idsOf(group.siblings), (QList<qint64>{kLintRun, kCodeQlRun}));
        QCOMPARE(idsOf(group.older),
                 (QList<qint64>{40552055136, 40552026434, 40552025101, 40552025055, 40552024851}));
    }

    // A preferred commit leads when some run has it; else the newest does.
    void runGroupPrefersTheSha()
    {
        const QJsonArray runs = object(QStringLiteral("workflow_runs.json")).value(QStringLiteral("workflow_runs")).toArray();
        const RunGroup older = runGroup(runs, kOlderSha);
        QCOMPARE(older.headSha, kOlderSha);
        QCOMPARE(older.lead.value(QStringLiteral("id")).toInteger(), 40552055136);
        QCOMPARE(idsOf(older.siblings), (QList<qint64>{40552026434, 40552025101, 40552025055, 40552024851}));
        QCOMPARE(idsOf(older.older), (QList<qint64>{kLeadRun, kLintRun, kCodeQlRun}));

        const RunGroup unknown = runGroup(runs, QStringLiteral("ffff"));
        QCOMPARE(unknown.headSha, kHeadSha);

        const RunGroup none = runGroup({});
        QVERIFY(none.headSha.isEmpty());
        QVERIFY(none.lead.isEmpty());
        QVERIFY(none.siblings.isEmpty());
        QVERIFY(none.older.isEmpty());

        // A date that cannot be read counts as the oldest.
        const RunGroup undated = runGroup(
            QJsonArray{run(1, QStringLiteral("a"), QString()), run(2, QStringLiteral("b"), QStringLiteral("2026-10-01T10:00:00Z"))});
        QCOMPARE(undated.headSha, QStringLiteral("b"));
    }

    // ---- Runs ----

    // A fixed branch: the runs, then the jobs of the three runs of its newest
    // commit at once.
    void runsOfAFixedBranch()
    {
        FakeStatusJob job;
        setUpBranch(job);
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Passed · feature/skip-refresh · 3 run(s)"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(status.closed.has_value());
        QVERIFY(!*status.closed);

        const QJsonObject &data = status.data;
        QCOMPARE(data.value(QStringLiteral("branch")).toString(), QStringLiteral("feature/skip-refresh"));
        QCOMPARE(data.value(QStringLiteral("headSha")).toString(), kHeadSha);
        QCOMPARE(data.value(QStringLiteral("run")).toObject().value(QStringLiteral("id")).toInteger(), kLeadRun);
        QCOMPARE(data.value(QStringLiteral("jobs")).toArray().size(), 6);
        const QJsonArray siblings = data.value(QStringLiteral("siblings")).toArray();
        QCOMPARE(idsOf(siblings), (QList<qint64>{kLintRun, kCodeQlRun}));
        QCOMPARE(siblings.at(0).toObject().value(QStringLiteral("jobs")).toArray().size(), 2);
        QCOMPARE(siblings.at(1).toObject().value(QStringLiteral("jobs")).toArray().size(), 2);
        QCOMPARE(data.value(QStringLiteral("older")).toArray().size(), 5);
        QVERIFY(!data.contains(QStringLiteral("pullRequest")));

        QCOMPARE(job.client.requestKeys(),
                 (QStringList{kBranchRuns, jobsKey(kLeadRun), jobsKey(kLintRun), jobsKey(kCodeQlRun)}));
        const auto &requests = job.client.requests();
        QCOMPARE(requests.at(0).options.arrayKey, QStringLiteral("workflow_runs"));
        QCOMPARE(requests.at(0).options.maxRows, 20);
        QCOMPARE(requests.at(1).options.arrayKey, QStringLiteral("jobs"));
    }

    // A failed group: each failed run of the commit needs attention, keyed by
    // its attempt, so a re-run that fails again counts again. The older failed
    // run does not.
    void failedRunGroup()
    {
        FakeStatusJob job;
        setUpFailedMain(job);
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("Failed · main · 2 of 3 run(s) failed"));
        QCOMPARE(keysOf(status), (QStringList{QStringLiteral("run-failed:9103:2"), QStringLiteral("run-failed:9102:1")}));
        for (const mirante::AttentionReason &reason : status.attention) {
            QCOMPARE(reason.text, QStringLiteral("Run failed"));
            QVERIFY(reason.notify);
            QVERIFY(!reason.clearsOnView);
        }
        QCOMPARE(status.data.value(QStringLiteral("run")).toObject().value(QStringLiteral("id")).toInt(), 9103);
        QCOMPARE(status.data.value(QStringLiteral("jobs")).toArray().size(), 2);
        QCOMPARE(idsOf(status.data.value(QStringLiteral("siblings")).toArray()), (QList<qint64>{9102, 9101}));
        QCOMPARE(idsOf(status.data.value(QStringLiteral("older")).toArray()), QList<qint64>{9090});
    }

    // A sibling's jobs that cannot be read are left out, and it still counts.
    void runWhenASiblingsJobsFail()
    {
        FakeStatusJob job;
        setUpFailedMain(job);
        job.client.setReply(jobsKey(9102), forbiddenReply());
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QVERIFY(findReason(status, QStringLiteral("run-failed:9102:1")));
        const QJsonArray siblings = status.data.value(QStringLiteral("siblings")).toArray();
        QVERIFY(!siblings.at(0).toObject().contains(QStringLiteral("jobs")));
        QVERIFY(siblings.at(1).toObject().contains(QStringLiteral("jobs")));
    }

    // The lead's jobs that cannot be read are an error.
    void runWhenTheLeadsJobsFail()
    {
        FakeStatusJob job;
        setUpFailedMain(job);
        job.client.setReply(jobsKey(9103), forbiddenReply());
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Error);
        QCOMPARE(job.finished->message, QStringLiteral("Forbidden (403)"));
        QVERIFY(job.finished->data.isEmpty());
    }

    // So are the runs.
    void runWhenTheRunsFail()
    {
        FakeStatusJob job;
        setUpFailedMain(job);
        job.client.setReply(kMainRuns, forbiddenReply());
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->state, ItemStatus::State::Error);
        QCOMPARE(job.client.requestKeys(), QStringList{kMainRuns});
    }

    // The words of a group and of a single run, which names its workflow.
    void runSummaries()
    {
        const QString sha = QStringLiteral("a");
        const QString date = QStringLiteral("2026-10-01T10:00:00Z");
        const auto sibling = [](const QJsonObject &other) { return QJsonObject{{QStringLiteral("run"), other}}; };

        QCOMPARE(runStatus(QStringLiteral("main"), run(1, sha, date), {}).summary, QStringLiteral("Passed · main · CI"));
        QCOMPARE(runStatus(QStringLiteral("main"), run(1, sha, date, QStringLiteral("in_progress"), QString()), {})
                     .summary,
                 QStringLiteral("Running · main · CI"));
        QCOMPARE(runStatus(QStringLiteral("main"), run(1, sha, date, QStringLiteral("queued"), QString()), {}).summary,
                 QStringLiteral("Queued · main · CI"));
        QCOMPARE(runStatus(QStringLiteral("main"), run(1, sha, date, QStringLiteral("waiting"), QString()), {}).summary,
                 QStringLiteral("Waiting · main · CI"));
        QCOMPARE(runStatus(QStringLiteral("main"), run(1, sha, date, QStringLiteral("completed"),
                                                       QStringLiteral("cancelled")),
                           {})
                     .summary,
                 QStringLiteral("Cancelled · main · CI"));
        const ItemStatus timedOut = runStatus(
            QStringLiteral("main"), run(1, sha, date, QStringLiteral("completed"), QStringLiteral("timed_out")), {});
        QCOMPARE(timedOut.summary, QStringLiteral("Timed out · main · CI"));
        QCOMPARE(keysOf(timedOut), QStringList{QStringLiteral("run-failed:1:1")});

        // A group runs while one of its runs does, and passes when all pass.
        const QJsonArray running{sibling(run(2, sha, date, QStringLiteral("in_progress"), QString())),
                                 sibling(run(3, sha, date))};
        QCOMPARE(runStatus(QStringLiteral("main"), run(1, sha, date), {}, running).summary,
                 QStringLiteral("Running · main · 3 run(s)"));
        const QJsonArray skipped{sibling(run(2, sha, date, QStringLiteral("completed"), QStringLiteral("skipped")))};
        QCOMPARE(runStatus(QStringLiteral("main"), run(1, sha, date), {}, skipped).summary,
                 QStringLiteral("Passed · main · 2 run(s)"));
        const QJsonArray cancelled{sibling(run(2, sha, date, QStringLiteral("completed"), QStringLiteral("cancelled")))};
        QCOMPARE(runStatus(QStringLiteral("main"), run(1, sha, date), {}, cancelled).summary,
                 QStringLiteral("Cancelled · main · 2 run(s)"));
        // A failed sibling makes the group fail, though the lead passed.
        const QJsonArray failed{sibling(run(2, sha, date, QStringLiteral("completed"), QStringLiteral("failure")))};
        const ItemStatus failedGroup = runStatus(QStringLiteral("main"), run(1, sha, date), {}, failed);
        QCOMPARE(failedGroup.summary, QStringLiteral("Failed · main · 1 of 2 run(s) failed"));
        QCOMPARE(keysOf(failedGroup), QStringList{QStringLiteral("run-failed:2:1")});
    }

    // A branch without runs is Ok.
    void branchWithoutRuns()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                       {QStringLiteral("branch"), QStringLiteral("main")}};
        job.client.setJson(kMainRuns, QJsonDocument(QJsonObject{{QStringLiteral("total_count"), 0},
                                                                {QStringLiteral("workflow_runs"), QJsonArray()}}));
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.summary, QStringLiteral("No runs on main"));
        QVERIFY(status.attention.isEmpty());
        QVERIFY(!*status.closed);
        QCOMPARE(status.data, (QJsonObject{{QStringLiteral("branch"), QStringLiteral("main")}}));
        QCOMPARE(job.client.requestKeys(), QStringList{kMainRuns});
    }

    // An empty branch is the repository's default branch.
    void emptyBranchIsTheDefault()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                       {QStringLiteral("branch"), QStringLiteral("  ")}};
        QVERIFY(job.client.setJsonFile(kRepository, dataFile(QStringLiteral("repository.json"))));
        job.client.setJson(kTrunkRuns, QJsonDocument(QJsonObject{{QStringLiteral("workflow_runs"), QJsonArray()}}));
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.finished->summary, QStringLiteral("No runs on trunk"));
        QCOMPARE(job.client.requestKeys(), (QStringList{kRepository, kTrunkRuns}));

        FakeStatusJob failed;
        failed.configValues = job.configValues;
        failed.client.setReply(kRepository, forbiddenReply());
        fetchRunStatus(failed);
        WAIT_FOR_FINISH(failed);
        QCOMPARE(failed.finished->state, ItemStatus::State::Error);
        QCOMPARE(failed.client.requestKeys(), QStringList{kRepository});
    }

    // A workflow narrows the runs to its file.
    void runsOfAWorkflow()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                       {QStringLiteral("branch"), QStringLiteral("main")},
                                       {QStringLiteral("workflow"), QStringLiteral(" ci.yml ")}};
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);
        QCOMPARE(job.client.requestKeys(),
                 QStringList{QStringLiteral("GET repos/acme/widgets/actions/workflows/ci.yml/runs?branch=main&per_page=20")});
    }

    // A linked pull request: its head branch, its head commit's runs leading
    // even when newer runs of the branch exist, and the pull request in data.
    void runsOfAPullRequest()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("branchSource"), QStringLiteral("pullRequest")},
                                       {QStringLiteral("number"), 42}};
        job.projectSettings = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")}};
        QJsonObject pullRequest = object(QStringLiteral("pull_request.json"));
        QJsonObject head = pullRequest.value(QStringLiteral("head")).toObject();
        head.insert(QStringLiteral("sha"), kOlderSha);
        pullRequest.insert(QStringLiteral("head"), head);
        job.client.setJson(kPullRequest, QJsonDocument(pullRequest));
        QVERIFY(job.client.setJsonFile(kBranchRuns, dataFile(QStringLiteral("workflow_runs.json"))));
        QVERIFY(job.client.setJsonFile(jobsKey(40552055136), dataFile(QStringLiteral("run_jobs_lint.json"))));
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);

        const ItemStatus &status = *job.finished;
        QCOMPARE(status.state, ItemStatus::State::Ok);
        QCOMPARE(status.data.value(QStringLiteral("headSha")).toString(), kOlderSha);
        QCOMPARE(status.data.value(QStringLiteral("run")).toObject().value(QStringLiteral("id")).toInteger(),
                 40552055136);
        QCOMPARE(status.data.value(QStringLiteral("siblings")).toArray().size(), 4);
        QCOMPARE(idsOf(status.data.value(QStringLiteral("older")).toArray()),
                 (QList<qint64>{kLeadRun, kLintRun, kCodeQlRun}));
        QCOMPARE(status.data.value(QStringLiteral("pullRequest")).toObject().value(QStringLiteral("number")).toInt(), 42);
        // The jobs of the four siblings could not be read: the fake answers 404.
        QCOMPARE(status.summary, QStringLiteral("Passed · feature/skip-refresh · 5 run(s)"));
        QCOMPARE(job.client.requestKeys().mid(0, 3),
                 (QStringList{kPullRequest, kBranchRuns, jobsKey(40552055136)}));
        QCOMPARE(job.client.requestKeys().size(), 7);
    }

    // A pull request whose head has no runs yet: the newest commit leads, and
    // the pull request is still in data. A pull request that cannot be read
    // is an error, with nothing more asked.
    void pullRequestSourceEdgeCases()
    {
        FakeStatusJob newest;
        newest.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                          {QStringLiteral("branchSource"), QStringLiteral("pullRequest")},
                                          {QStringLiteral("number"), 42}};
        QJsonObject pullRequest = object(QStringLiteral("pull_request.json"));
        QJsonObject head = pullRequest.value(QStringLiteral("head")).toObject();
        head.insert(QStringLiteral("sha"), QStringLiteral("ffff"));
        pullRequest.insert(QStringLiteral("head"), head);
        newest.client.setJson(kPullRequest, QJsonDocument(pullRequest));
        newest.client.setJson(kBranchRuns, QJsonDocument(QJsonObject{{QStringLiteral("workflow_runs"), QJsonArray()}}));
        fetchRunStatus(newest);
        WAIT_FOR_FINISH(newest);
        QCOMPARE(newest.finished->summary, QStringLiteral("No runs on feature/skip-refresh"));
        QVERIFY(newest.finished->data.contains(QStringLiteral("pullRequest")));

        FakeStatusJob missing;
        missing.configValues = newest.configValues;
        fetchRunStatus(missing);
        WAIT_FOR_FINISH(missing);
        QCOMPARE(missing.finished->state, ItemStatus::State::NotFound);
        QCOMPARE(missing.client.requestKeys(), QStringList{kPullRequest});
    }

    // ---- The main folder ----

    void mainFolderNotMapped()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                       {QStringLiteral("branchSource"), QStringLiteral("mainFolder")}};
        fetchRunStatus(job);
        QCOMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::None);
        QCOMPARE(job.finished->message, QStringLiteral("Set the main folder's path on this device."));
        QVERIFY(job.headsRead.isEmpty());
        QVERIFY(job.client.requests().isEmpty());
    }

    void mainFolderNotOnABranch()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                       {QStringLiteral("branchSource"), QStringLiteral("mainFolder")}};
        job.folderPaths.insert(QString(), QStringLiteral("C:/work/widgets"));
        fetchRunStatus(job);
        QCOMPARE(job.finishCount, 1);
        QCOMPARE(job.finished->state, ItemStatus::State::None);
        QCOMPARE(job.finished->message, QStringLiteral("The main folder is not on a branch."));
        QCOMPARE(job.headsRead, QStringList{QStringLiteral("C:/work/widgets")});
        QVERIFY(job.client.requests().isEmpty());
    }

    // The branch read from the head is then followed as a fixed one.
    void mainFolderBranchFromItsHead()
    {
        FakeStatusJob job;
        job.configValues = QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                       {QStringLiteral("branchSource"), QStringLiteral("mainFolder")},
                                       {QStringLiteral("branch"), QStringLiteral("ignored")}};
        job.folderPaths.insert(QString(), QStringLiteral("C:/work/widgets"));
        job.heads.insert(QStringLiteral("C:/work/widgets"), QStringLiteral("main"));
        QVERIFY(job.client.setJsonFile(kMainRuns, dataFile(QStringLiteral("workflow_runs_failed.json"))));
        QVERIFY(job.client.setJsonFile(jobsKey(9103), dataFile(QStringLiteral("run_jobs_failed.json"))));
        fetchRunStatus(job);
        WAIT_FOR_FINISH(job);

        QCOMPARE(job.headsRead, QStringList{QStringLiteral("C:/work/widgets")});
        QCOMPARE(job.finished->state, ItemStatus::State::Ok);
        QCOMPARE(job.finished->summary, QStringLiteral("Failed · main · 2 of 3 run(s) failed"));
        QCOMPARE(job.client.requestKeys().first(), kMainRuns);
    }

    // ---- Configuration and errors ----

    // Without a repository or a number nothing is asked.
    void incompleteConfiguration()
    {
        const QString message = QStringLiteral("Set the repository and number in the item's settings.");
        const auto check = [](void (*fetch)(mirante::StatusJob &), const QJsonObject &config, const QString &text) {
            FakeStatusJob job;
            job.configValues = config;
            fetch(job);
            QCOMPARE(job.finishCount, 1);
            QCOMPARE(job.finished->state, ItemStatus::State::None);
            QCOMPARE(job.finished->message, text);
            QVERIFY(job.client.requests().isEmpty());
        };
        const QJsonObject noRepository{{QStringLiteral("number"), 42}};
        const QJsonObject noNumber{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                   {QStringLiteral("number"), 0}};
        for (const QJsonObject &config : {noRepository, noNumber}) {
            check(&fetchPullRequestStatus, config, message);
            check(&fetchIssueStatus, config, message);
            check(&fetchIssueSummaryStatus, config, message);
        }
        check(&fetchRunStatus, QJsonObject{{QStringLiteral("branch"), QStringLiteral("main")}},
              QStringLiteral("Set the repository in the item's settings."));
        check(&fetchRunStatus,
              QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                          {QStringLiteral("branchSource"), QStringLiteral("pullRequest")}},
              QStringLiteral("Set the pull request number in the item's settings."));
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
        const QJsonObject single{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                 {QStringLiteral("number"), 42}};
        check(&fetchPullRequestStatus, single);
        check(&fetchIssueStatus, single);
        check(&fetchIssueSummaryStatus, single);
        check(&fetchRunStatus, QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                           {QStringLiteral("branch"), QStringLiteral("main")}});
        check(&fetchRunStatus, QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")}});
        check(&fetchRunStatus, QJsonObject{{QStringLiteral("repository"), QStringLiteral("acme/widgets")},
                                           {QStringLiteral("branchSource"), QStringLiteral("pullRequest")},
                                           {QStringLiteral("number"), 42}});
    }
};

QTEST_MAIN(TestStatus)
#include "tst_status.moc"
