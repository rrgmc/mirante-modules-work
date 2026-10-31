#include "GitHubStatus.h"

#include "GitHubClient.h"
#include "GitHubSettings.h"
#include "GitHubUrls.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QStringList>
#include <QUrlQuery>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace mirante::modules::github {

namespace {

const QString kSeparator = QStringLiteral(" · ");

AttentionReason reason(const QString &key, const QString &text, bool notify = false, bool clearsOnView = false)
{
    AttentionReason result;
    result.key = key;
    result.text = text;
    result.notify = notify;
    result.clearsOnView = clearsOnView;
    return result;
}

ItemStatus okStatus()
{
    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.updatedAt = QDateTime::currentDateTimeUtc();
    return status;
}

// A status without a request: the configuration is not complete.
ItemStatus incomplete(const QString &message)
{
    ItemStatus status;
    status.message = message;
    return status;
}

qint64 idOf(const QJsonValue &user)
{
    return user.toObject().value(QStringLiteral("id")).toInteger();
}

bool containsUser(const QJsonArray &users, qint64 id)
{
    for (const QJsonValue &user : users) {
        if (idOf(user) == id)
            return true;
    }
    return false;
}

// A date of GitHub's answers, such as "2026-10-27T17:45:56Z", in milliseconds.
// A date that cannot be read counts as the oldest.
qint64 timeOf(const QJsonObject &object, const QString &key)
{
    const QDateTime date = QDateTime::fromString(object.value(key).toString(), Qt::ISODateWithMs);
    return date.isValid() ? date.toMSecsSinceEpoch() : std::numeric_limits<qint64>::min();
}

// The reviewer of a review: the user's id, or the login without one.
QString reviewerOf(const QJsonObject &review)
{
    const QJsonObject user = review.value(QStringLiteral("user")).toObject();
    const qint64 id = user.value(QStringLiteral("id")).toInteger();
    return id != 0 ? QString::number(id) : user.value(QStringLiteral("login")).toString();
}

bool isMerged(const QJsonObject &pullRequest)
{
    return pullRequest.value(QStringLiteral("merged")).toBool()
        || !pullRequest.value(QStringLiteral("merged_at")).toString().isEmpty();
}

QString pullRequestState(const QJsonObject &pullRequest)
{
    if (isMerged(pullRequest))
        return QCoreApplication::translate("github", "Merged");
    if (pullRequest.value(QStringLiteral("state")).toString() == QLatin1String("closed"))
        return QCoreApplication::translate("github", "Closed");
    if (pullRequest.value(QStringLiteral("draft")).toBool())
        return QCoreApplication::translate("github", "Draft");
    return QCoreApplication::translate("github", "Open");
}

QString checksText(ChecksState state)
{
    switch (state) {
    case ChecksState::Failed:
        return QCoreApplication::translate("github", "checks failed");
    case ChecksState::Running:
        return QCoreApplication::translate("github", "checks running");
    case ChecksState::Passed:
        return QCoreApplication::translate("github", "checks passed");
    case ChecksState::None:
        break;
    }
    return {};
}

QString statusOf(const QJsonObject &run)
{
    return run.value(QStringLiteral("status")).toString();
}

QString conclusionOf(const QJsonObject &run)
{
    return run.value(QStringLiteral("conclusion")).toString();
}

bool runFailed(const QJsonObject &run)
{
    return isFailedConclusion(conclusionOf(run));
}

AttentionReason failedRun(const QJsonObject &run)
{
    const QString key = QStringLiteral("run-failed:%1:%2")
                            .arg(run.value(QStringLiteral("id")).toInteger())
                            .arg(std::max<qint64>(1, run.value(QStringLiteral("run_attempt")).toInteger()));
    return reason(key, QCoreApplication::translate("github", "Run failed"), true);
}

// The word of a run that has not completed, or of a completed run's conclusion.
QString runWord(const QJsonObject &run)
{
    const QString state = statusOf(run);
    if (state == QLatin1String("in_progress"))
        return QCoreApplication::translate("github", "Running");
    if (state == QLatin1String("queued"))
        return QCoreApplication::translate("github", "Queued");
    if (state != QLatin1String("completed"))
        return QCoreApplication::translate("github", "Waiting");
    const QString result = conclusionOf(run);
    if (result == QLatin1String("success"))
        return QCoreApplication::translate("github", "Passed");
    if (result == QLatin1String("timed_out"))
        return QCoreApplication::translate("github", "Timed out");
    if (isFailedConclusion(result))
        return QCoreApplication::translate("github", "Failed");
    if (result == QLatin1String("cancelled"))
        return QCoreApplication::translate("github", "Cancelled");
    if (result == QLatin1String("skipped"))
        return QCoreApplication::translate("github", "Skipped");
    if (result == QLatin1String("neutral"))
        return QCoreApplication::translate("github", "Neutral");
    if (result == QLatin1String("action_required"))
        return QCoreApplication::translate("github", "Action required");
    if (result == QLatin1String("stale"))
        return QCoreApplication::translate("github", "Stale");
    return result;
}

// A conclusion that lets a group pass.
bool passes(const QJsonObject &run)
{
    const QString result = conclusionOf(run);
    return statusOf(run) == QLatin1String("completed")
        && (result == QLatin1String("success") || result == QLatin1String("skipped")
            || result == QLatin1String("neutral"));
}

// The word of a group, runs newest first: "Failed" when one failed; else
// "Running" while one runs, or the word of the newest that has not completed;
// else "Passed" when all passed, or the word of the newest that did not.
QString groupWord(const QList<QJsonObject> &runs)
{
    if (std::any_of(runs.begin(), runs.end(), runFailed))
        return QCoreApplication::translate("github", "Failed");
    if (std::any_of(runs.begin(), runs.end(),
                    [](const QJsonObject &run) { return statusOf(run) == QLatin1String("in_progress"); }))
        return QCoreApplication::translate("github", "Running");
    for (const QJsonObject &run : runs) {
        if (statusOf(run) != QLatin1String("completed"))
            return runWord(run);
    }
    for (const QJsonObject &run : runs) {
        if (!passes(run))
            return runWord(run);
    }
    return QCoreApplication::translate("github", "Passed");
}

} // namespace

ChecksState checksState(const QJsonArray &checkRuns, const QJsonObject &combinedStatus)
{
    bool failed = false;
    bool running = false;
    for (const QJsonValue &value : checkRuns) {
        const QJsonObject check = value.toObject();
        if (isFailedConclusion(conclusionOf(check)))
            failed = true;
        else if (statusOf(check) != QLatin1String("completed"))
            running = true;
    }
    const bool hasStatuses = !combinedStatus.value(QStringLiteral("statuses")).toArray().isEmpty();
    if (hasStatuses) {
        const QString state = combinedStatus.value(QStringLiteral("state")).toString();
        failed = failed || state == QLatin1String("failure") || state == QLatin1String("error");
        running = running || state == QLatin1String("pending");
    }
    if (failed)
        return ChecksState::Failed;
    if (running)
        return ChecksState::Running;
    if (!checkRuns.isEmpty() || hasStatuses)
        return ChecksState::Passed;
    return ChecksState::None;
}

bool isFailedConclusion(const QString &conclusion)
{
    return conclusion == QLatin1String("failure") || conclusion == QLatin1String("timed_out")
        || conclusion == QLatin1String("startup_failure");
}

QJsonArray latestReviews(const QJsonArray &reviews)
{
    QList<QJsonObject> submitted;
    for (const QJsonValue &value : reviews) {
        const QJsonObject review = value.toObject();
        if (review.value(QStringLiteral("state")).toString() != QLatin1String("PENDING"))
            submitted << review;
    }
    std::stable_sort(submitted.begin(), submitted.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return timeOf(a, QStringLiteral("submitted_at")) < timeOf(b, QStringLiteral("submitted_at"));
    });

    QStringList order;
    QHash<QString, QJsonObject> latest;
    for (const QJsonObject &review : std::as_const(submitted)) {
        const QString reviewer = reviewerOf(review);
        if (!order.contains(reviewer))
            order << reviewer;
        const QString state = review.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("DISMISSED")) {
            latest.remove(reviewer);
        } else if (state == QLatin1String("COMMENTED")) {
            const auto current = latest.constFind(reviewer);
            if (current == latest.constEnd()
                || current->value(QStringLiteral("state")).toString() == QLatin1String("COMMENTED"))
                latest.insert(reviewer, review);
        } else {
            latest.insert(reviewer, review);
        }
    }

    QJsonArray result;
    for (const QString &reviewer : std::as_const(order)) {
        const auto found = latest.constFind(reviewer);
        if (found != latest.constEnd())
            result.append(*found);
    }
    return result;
}

ReviewState reviewState(const QJsonArray &reviews)
{
    ReviewState result;
    for (const QJsonValue &value : latestReviews(reviews)) {
        const QString state = value.toObject().value(QStringLiteral("state")).toString();
        if (state == QLatin1String("APPROVED"))
            ++result.approvals;
        else if (state == QLatin1String("CHANGES_REQUESTED"))
            result.changesRequested = true;
    }
    return result;
}

ItemStatus pullRequestStatus(const QJsonObject &pullRequest, const QJsonArray &reviews, const QJsonObject &user,
                             const QJsonArray &checkRuns, const QJsonObject &combinedStatus)
{
    ItemStatus status = okStatus();
    const bool closed = pullRequest.value(QStringLiteral("state")).toString() == QLatin1String("closed");
    const bool open = !closed && !isMerged(pullRequest);
    const qint64 me = user.value(QStringLiteral("id")).toInteger();
    const bool author = me != 0 && idOf(pullRequest.value(QStringLiteral("user"))) == me;
    const bool reviewer =
        me != 0 && containsUser(pullRequest.value(QStringLiteral("requested_reviewers")).toArray(), me);
    // null while GitHub computes it: only false means conflicts.
    const QJsonValue mergeable = pullRequest.value(QStringLiteral("mergeable"));
    const bool conflicts = mergeable.isBool() && !mergeable.toBool();
    const ReviewState review = reviewState(reviews);
    const ChecksState checks = checksState(checkRuns, combinedStatus);

    QStringList parts{pullRequestState(pullRequest)};
    if (review.changesRequested)
        parts << QCoreApplication::translate("github", "changes requested");
    else if (review.approvals > 0)
        parts << QCoreApplication::translate("github", "%n approval(s)", nullptr, review.approvals);
    if (checks != ChecksState::None)
        parts << checksText(checks);
    if (open && conflicts)
        parts << QCoreApplication::translate("github", "has conflicts");
    status.summary = parts.join(kSeparator);

    if (open) {
        if (checks == ChecksState::Failed)
            status.attention << reason(QStringLiteral("checks-failed"),
                                       QCoreApplication::translate("github", "Checks failed"), true);
        if (reviewer)
            status.attention << reason(QStringLiteral("review"),
                                       QCoreApplication::translate("github", "Review requested"));
        if (author && review.approved())
            status.attention << reason(QStringLiteral("approved"), QCoreApplication::translate("github", "Approved"),
                                       false, true);
        if (author && review.changesRequested)
            status.attention << reason(QStringLiteral("changes-requested"),
                                       QCoreApplication::translate("github", "Changes requested"));
        if (author && conflicts)
            status.attention << reason(QStringLiteral("conflicts"),
                                       QCoreApplication::translate("github", "Has conflicts"));
    }

    status.closed = closed;
    status.data = QJsonObject{
        {QStringLiteral("pullRequest"), pullRequest},
        {QStringLiteral("reviews"), reviews},
        {QStringLiteral("user"),
         QJsonObject{{QStringLiteral("id"), user.value(QStringLiteral("id"))},
                     {QStringLiteral("login"), user.value(QStringLiteral("login"))}}},
        {QStringLiteral("checkRuns"), checkRuns},
        {QStringLiteral("combinedStatus"), combinedStatus},
    };
    return status;
}

ItemStatus issueStatus(const QJsonObject &issue, const QJsonArray &comments)
{
    ItemStatus status = okStatus();
    const bool closed = issue.value(QStringLiteral("state")).toString() == QLatin1String("closed");
    QStringList parts{closed ? QCoreApplication::translate("github", "Closed")
                             : QCoreApplication::translate("github", "Open")};
    QStringList assignees;
    for (const QJsonValue &assignee : issue.value(QStringLiteral("assignees")).toArray()) {
        const QString login = assignee.toObject().value(QStringLiteral("login")).toString();
        if (!login.isEmpty())
            assignees << login;
    }
    if (!assignees.isEmpty())
        parts << QCoreApplication::translate("github", "assigned to %1").arg(assignees.join(QStringLiteral(", ")));
    status.summary = parts.join(kSeparator);
    status.closed = closed;
    status.data = QJsonObject{{QStringLiteral("issue"), issue}};
    if (!comments.isEmpty()) {
        QJsonArray kept;
        for (qsizetype i = 0; i < comments.size() && i < 20; ++i)
            kept.append(comments.at(i));
        status.data.insert(QStringLiteral("comments"), kept);
    }
    return status;
}

RunGroup runGroup(const QJsonArray &runs, const QString &preferredSha)
{
    QList<QJsonObject> all;
    for (const QJsonValue &value : runs)
        all << value.toObject();
    if (all.isEmpty())
        return {};
    // Newest first; runs started at once by one push go by id.
    std::stable_sort(all.begin(), all.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const qint64 first = timeOf(a, QStringLiteral("created_at"));
        const qint64 second = timeOf(b, QStringLiteral("created_at"));
        if (first != second)
            return first > second;
        return a.value(QStringLiteral("id")).toInteger() > b.value(QStringLiteral("id")).toInteger();
    });

    const auto shaOf = [](const QJsonObject &run) { return run.value(QStringLiteral("head_sha")).toString(); };
    RunGroup group;
    group.headSha = shaOf(all.first());
    if (!preferredSha.isEmpty()
        && std::any_of(all.begin(), all.end(), [&](const QJsonObject &run) { return shaOf(run) == preferredSha; }))
        group.headSha = preferredSha;
    for (const QJsonObject &run : std::as_const(all)) {
        if (shaOf(run) != group.headSha)
            group.older.append(run);
        else if (group.lead.isEmpty())
            group.lead = run;
        else
            group.siblings.append(run);
    }
    return group;
}

ItemStatus runStatus(const QString &branch, const QJsonObject &run, const QJsonArray &jobs,
                     const QJsonArray &siblings, const QJsonArray &older, const QJsonObject &pullRequest)
{
    ItemStatus status = okStatus();
    status.closed = false;
    status.data = QJsonObject{{QStringLiteral("branch"), branch}};
    if (!pullRequest.isEmpty())
        status.data.insert(QStringLiteral("pullRequest"), pullRequest);
    if (run.isEmpty()) {
        status.summary = QCoreApplication::translate("github", "No runs on %1").arg(branch);
        return status;
    }

    QList<QJsonObject> group{run};
    for (const QJsonValue &sibling : siblings)
        group << sibling.toObject().value(QStringLiteral("run")).toObject();

    QStringList parts{group.size() == 1 ? runWord(run) : groupWord(group), branch};
    if (group.size() == 1) {
        parts << run.value(QStringLiteral("name")).toString();
    } else {
        const int total = int(group.size());
        const int failed = int(std::count_if(group.begin(), group.end(), runFailed));
        if (failed > 0)
            parts << QCoreApplication::translate("github", "%1 of %n run(s) failed", nullptr, total).arg(failed);
        else
            parts << QCoreApplication::translate("github", "%n run(s)", nullptr, total);
    }
    status.summary = parts.join(kSeparator);

    for (const QJsonObject &member : std::as_const(group)) {
        if (runFailed(member))
            status.attention << failedRun(member);
    }

    status.data.insert(QStringLiteral("headSha"), run.value(QStringLiteral("head_sha")));
    status.data.insert(QStringLiteral("run"), run);
    status.data.insert(QStringLiteral("jobs"), jobs);
    status.data.insert(QStringLiteral("siblings"), siblings);
    status.data.insert(QStringLiteral("older"), older);
    return status;
}

namespace {

// The comments a summary keeps, and the fewest it shows before it also reads
// the page before the last.
constexpr int kSummaryComments = 20;
constexpr int kFewComments = 5;
// The runs of a branch a run status looks through.
constexpr int kRecentRuns = 20;

QString repositoryOfJob(const StatusJob &job)
{
    return repositoryOf(job.config(), job.projectSetting(kProjectSettingKey));
}

// Finishes the job with a message if the configuration names no object.
bool namesAnObject(StatusJob &job, const QString &repository, qint64 number)
{
    if (!repository.isEmpty() && number != 0)
        return true;
    job.finish(
        incomplete(QCoreApplication::translate("github", "Set the repository and number in the item's settings.")));
    return false;
}

// The checks of the head commit, at once. Each only adds detail, so a failed
// one is left out.
void fetchChecks(StatusJob *job, const QString &repository, const QJsonObject &pullRequest,
                 std::function<void(const QJsonArray &, const QJsonObject &)> done)
{
    const QString sha =
        pullRequest.value(QStringLiteral("head")).toObject().value(QStringLiteral("sha")).toString();
    if (sha.isEmpty()) {
        done({}, {});
        return;
    }
    struct Answers
    {
        QJsonArray checkRuns;
        QJsonObject combinedStatus;
        int waiting = 2;
    };
    auto answers = std::make_shared<Answers>();
    const auto finishWhenDone = [answers, done = std::move(done)] {
        if (--answers->waiting == 0)
            done(answers->checkRuns, answers->combinedStatus);
    };
    const GitHubClient client(job->server(), job->owner());
    client.checkRuns(repository, sha, [answers, finishWhenDone](const ApiReply &reply) {
        if (reply.ok())
            answers->checkRuns = reply.json.object().value(QStringLiteral("check_runs")).toArray();
        finishWhenDone();
    });
    client.combinedStatus(repository, sha, [answers, finishWhenDone](const ApiReply &reply) {
        if (reply.ok())
            answers->combinedStatus = reply.json.object();
        finishWhenDone();
    });
}

} // namespace

void fetchPullRequestStatus(StatusJob &job)
{
    const QString repository = repositoryOfJob(job);
    const qint64 number = itemNumber(job.config());
    if (!namesAnObject(job, repository, number))
        return;

    // The three requests run at once; the checks follow when all have answered.
    struct Replies
    {
        std::optional<ApiReply> pullRequest;
        std::optional<ApiReply> reviews;
        std::optional<ApiReply> user;
    };
    auto replies = std::make_shared<Replies>();
    StatusJob *target = &job;
    const auto continueWhenDone = [target, replies, repository] {
        if (!replies->pullRequest || !replies->reviews || !replies->user)
            return;
        for (const ApiReply *reply : {&*replies->pullRequest, &*replies->reviews, &*replies->user}) {
            if (!reply->ok()) {
                target->finish(statusFromReply(*reply));
                return;
            }
        }
        const QJsonObject pullRequest = replies->pullRequest->json.object();
        const QJsonArray reviews = replies->reviews->json.array();
        const QJsonObject user = replies->user->json.object();
        fetchChecks(target, repository, pullRequest,
                    [target, pullRequest, reviews, user](const QJsonArray &checkRuns, const QJsonObject &combined) {
                        target->finish(pullRequestStatus(pullRequest, reviews, user, checkRuns, combined));
                    });
    };

    const GitHubClient client(job.server(), job.owner());
    client.pullRequest(repository, number, [replies, continueWhenDone](const ApiReply &reply) {
        replies->pullRequest = reply;
        continueWhenDone();
    });
    client.pullRequestReviews(repository, number, [replies, continueWhenDone](const ApiReply &reply) {
        replies->reviews = reply;
        continueWhenDone();
    });
    client.currentUser([replies, continueWhenDone](const ApiReply &reply) {
        replies->user = reply;
        continueWhenDone();
    });
}

void fetchIssueStatus(StatusJob &job)
{
    const QString repository = repositoryOfJob(job);
    const qint64 number = itemNumber(job.config());
    if (!namesAnObject(job, repository, number))
        return;

    StatusJob *target = &job;
    GitHubClient(job.server(), job.owner()).issue(repository, number, [target](const ApiReply &reply) {
        target->finish(reply.ok() ? issueStatus(reply.json.object()) : statusFromReply(reply));
    });
}

void fetchIssueSummaryStatus(StatusJob &job)
{
    const QString repository = repositoryOfJob(job);
    const qint64 number = itemNumber(job.config());
    if (!namesAnObject(job, repository, number))
        return;

    StatusJob *target = &job;
    const GitHubClient client(job.server(), job.owner());
    client.issue(repository, number, [target, client, repository, number](const ApiReply &reply) {
        if (!reply.ok()) {
            target->finish(statusFromReply(reply));
            return;
        }
        const QJsonObject issue = reply.json.object();
        const qint64 count = issue.value(QStringLiteral("comments")).toInteger();
        if (count <= 0) {
            target->finish(issueStatus(issue));
            return;
        }

        // The comments come oldest first, without a sort, so the newest are on
        // the last page. Comments only add detail, so without them the status
        // still stands.
        const int last = int((count + kSummaryComments - 1) / kSummaryComments);
        const auto finishWith = [target, issue](const QJsonArray &oldestFirst) {
            QJsonArray newestFirst;
            for (qsizetype i = oldestFirst.size() - 1; i >= 0; --i)
                newestFirst.append(oldestFirst.at(i));
            target->finish(issueStatus(issue, newestFirst));
        };
        client.issueComments(
            repository, number, kSummaryComments, last,
            [client, repository, number, last, finishWith](const ApiReply &lastPage) {
                const QJsonArray newest = lastPage.ok() ? lastPage.json.array() : QJsonArray();
                if (!lastPage.ok() || newest.size() >= kFewComments || last <= 1) {
                    finishWith(newest);
                    return;
                }
                client.issueComments(repository, number, kSummaryComments, last - 1,
                                     [newest, finishWith](const ApiReply &before) {
                                         QJsonArray all = before.ok() ? before.json.array() : QJsonArray();
                                         for (const QJsonValue &comment : newest)
                                             all.append(comment);
                                         finishWith(all);
                                     });
            });
    });
}

namespace {

// What a run status knows before it asks for the runs.
struct RunSource
{
    QString repository;
    QString workflow;
    QString branch;
    QJsonObject pullRequest;    // the linked pull request, or empty
};

// The jobs of each run in the group, at once. The lead's are needed, and a
// sibling's only add detail, so without them the status still stands.
void fetchGroupJobs(StatusJob *job, const RunSource &source, const RunGroup &group)
{
    if (group.lead.isEmpty()) {
        job->finish(runStatus(source.branch, {}, {}, {}, {}, source.pullRequest));
        return;
    }

    struct Answers
    {
        std::optional<ApiReply> lead;
        QList<QJsonObject> siblings;
        qsizetype waiting = 0;
    };
    auto answers = std::make_shared<Answers>();
    for (const QJsonValue &sibling : group.siblings)
        answers->siblings << QJsonObject{{QStringLiteral("run"), sibling}};
    answers->waiting = answers->siblings.size() + 1;

    const auto finishWhenDone = [job, source, group, answers] {
        if (--answers->waiting > 0)
            return;
        if (!answers->lead->ok()) {
            job->finish(statusFromReply(*answers->lead));
            return;
        }
        QJsonArray siblings;
        for (const QJsonObject &sibling : std::as_const(answers->siblings))
            siblings.append(sibling);
        const QJsonArray jobs = answers->lead->json.object().value(QStringLiteral("jobs")).toArray();
        job->finish(runStatus(source.branch, group.lead, jobs, siblings, group.older, source.pullRequest));
    };

    const GitHubClient client(job->server(), job->owner());
    client.runJobs(source.repository, group.lead.value(QStringLiteral("id")).toInteger(),
                   [answers, finishWhenDone](const ApiReply &reply) {
                       answers->lead = reply;
                       finishWhenDone();
                   });
    for (qsizetype i = 0; i < group.siblings.size(); ++i) {
        const qint64 id = group.siblings.at(i).toObject().value(QStringLiteral("id")).toInteger();
        client.runJobs(source.repository, id, [answers, finishWhenDone, i](const ApiReply &reply) {
            if (reply.ok())
                answers->siblings[i].insert(QStringLiteral("jobs"),
                                            reply.json.object().value(QStringLiteral("jobs")).toArray());
            finishWhenDone();
        });
    }
}

// The newest runs of the branch, then the jobs of the group.
void fetchRuns(StatusJob *job, const RunSource &source)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("branch"), GitHubClient::encode(source.branch));
    GitHubClient(job->server(), job->owner())
        .workflowRuns(source.repository, source.workflow, query, kRecentRuns, [job, source](const ApiReply &reply) {
            if (!reply.ok()) {
                job->finish(statusFromReply(reply));
                return;
            }
            const QString preferredSha =
                source.pullRequest.value(QStringLiteral("head")).toObject().value(QStringLiteral("sha")).toString();
            const QJsonArray runs = reply.json.object().value(QStringLiteral("workflow_runs")).toArray();
            fetchGroupJobs(job, source, runGroup(runs, preferredSha));
        });
}

} // namespace

void fetchRunStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    RunSource source;
    source.repository = repositoryOfJob(job);
    source.workflow = configuredWorkflow(config);
    if (source.repository.isEmpty()) {
        job.finish(incomplete(QCoreApplication::translate("github", "Set the repository in the item's settings.")));
        return;
    }

    StatusJob *target = &job;
    const QString branchSource = config.value(QStringLiteral("branchSource")).toString();
    if (branchSource == QLatin1String("mainFolder")) {
        // The branch checked out in the main folder on this device, read
        // again at each fetch, then as a fixed branch.
        const QString path = job.folderPath(QString());
        if (path.isEmpty()) {
            job.finish(incomplete(QCoreApplication::translate("github", "Set the main folder's path on this device.")));
            return;
        }
        source.branch = job.gitHead(path);
        if (source.branch.isEmpty()) {
            job.finish(incomplete(QCoreApplication::translate("github", "The main folder is not on a branch.")));
            return;
        }
        fetchRuns(target, source);
        return;
    }

    if (branchSource == QLatin1String("pullRequest")) {
        const qint64 number = itemNumber(config);
        if (number == 0) {
            job.finish(incomplete(
                QCoreApplication::translate("github", "Set the pull request number in the item's settings.")));
            return;
        }
        GitHubClient(job.server(), job.owner())
            .pullRequest(source.repository, number, [target, source](const ApiReply &reply) {
                if (!reply.ok()) {
                    target->finish(statusFromReply(reply));
                    return;
                }
                RunSource withPullRequest = source;
                withPullRequest.pullRequest = reply.json.object();
                withPullRequest.branch = withPullRequest.pullRequest.value(QStringLiteral("head"))
                                             .toObject()
                                             .value(QStringLiteral("ref"))
                                             .toString();
                fetchRuns(target, withPullRequest);
            });
        return;
    }

    source.branch = configuredBranch(config);
    if (!source.branch.isEmpty()) {
        fetchRuns(target, source);
        return;
    }
    // An empty branch is the repository's default branch.
    GitHubClient(job.server(), job.owner()).repository(source.repository, [target, source](const ApiReply &reply) {
        if (!reply.ok()) {
            target->finish(statusFromReply(reply));
            return;
        }
        RunSource withBranch = source;
        withBranch.branch = reply.json.object().value(QStringLiteral("default_branch")).toString();
        fetchRuns(target, withBranch);
    });
}

} // namespace mirante::modules::github
