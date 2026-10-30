#include "GitLabStatus.h"

#include "GitLabClient.h"
#include "GitLabSettings.h"
#include "GitLabUrls.h"

#include <QCoreApplication>
#include <QDateTime>
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

namespace mirante::modules::gitlab {

namespace {

const QString kSeparator = QStringLiteral(" · ");

// How long after each other the pipelines of a group start. See decision 0072.
constexpr qint64 kGroupWindowMs = 10 * 60 * 1000;

QString tr(const char *text)
{
    return QCoreApplication::translate("gitlab", text);
}

// A pipeline or job status as words: "success" is "passed", and underscores
// become spaces, as in "waiting for resource".
QString statusWords(const QString &status)
{
    if (status == QLatin1String("success"))
        return tr("passed");
    QString words = status;
    words.replace(QLatin1Char('_'), QLatin1Char(' '));
    return words;
}

QString capitalised(QString text)
{
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}

QString stateText(const QString &state)
{
    if (state == QLatin1String("opened"))
        return tr("Open");
    if (state == QLatin1String("merged"))
        return tr("Merged");
    if (state == QLatin1String("closed"))
        return tr("Closed");
    if (state == QLatin1String("locked"))
        return tr("Locked");
    return capitalised(state);
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

// The ids in approvals' approved_by, whose entries are {"user": {...}}.
bool approvedBy(const QJsonObject &approvals, qint64 id)
{
    for (const QJsonValue &entry : approvals.value(QStringLiteral("approved_by")).toArray()) {
        if (idOf(entry.toObject().value(QStringLiteral("user"))) == id)
            return true;
    }
    return false;
}

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

QString mergeRequestApprovals(const QJsonObject &approvals)
{
    const int required = approvals.value(QStringLiteral("approvals_required")).toInt();
    const int given = int(approvals.value(QStringLiteral("approved_by")).toArray().size());
    if (required > 0)
        return tr("%1 of %2 approvals").arg(given).arg(required);
    if (given == 1)
        return tr("1 approval");
    if (given > 1)
        return tr("%1 approvals").arg(given);
    return {};
}

AttentionReason failedPipeline(const QJsonObject &pipeline)
{
    const QString id = QString::number(pipeline.value(QStringLiteral("id")).toInteger());
    return reason(QStringLiteral("pipeline-failed:") + id, tr("Pipeline failed"), true);
}

bool hasFailed(const QJsonObject &pipeline)
{
    return pipeline.value(QStringLiteral("status")).toString() == QLatin1String("failed");
}

QDateTime createdAt(const QJsonObject &pipeline)
{
    return QDateTime::fromString(pipeline.value(QStringLiteral("created_at")).toString(), Qt::ISODateWithMs);
}

int failedJobs(const QJsonArray &jobs)
{
    int count = 0;
    for (const QJsonValue &value : jobs) {
        const QJsonObject job = value.toObject();
        if (job.value(QStringLiteral("status")).toString() == QLatin1String("failed")
            && !job.value(QStringLiteral("allow_failure")).toBool())
            ++count;
    }
    return count;
}

} // namespace

ItemStatus mergeRequestStatus(const QJsonObject &mergeRequest, const QJsonObject &approvals,
                              const QJsonObject &user)
{
    ItemStatus status = okStatus();
    const QString state = mergeRequest.value(QStringLiteral("state")).toString();
    const bool open = state == QLatin1String("opened");
    const qint64 me = user.value(QStringLiteral("id")).toInteger();
    const bool author = me != 0 && idOf(mergeRequest.value(QStringLiteral("author"))) == me;
    const bool reviewer =
        me != 0 && containsUser(mergeRequest.value(QStringLiteral("reviewers")).toArray(), me);
    const QString pipeline =
        mergeRequest.value(QStringLiteral("head_pipeline")).toObject().value(QStringLiteral("status")).toString();
    const bool conflicts = mergeRequest.value(QStringLiteral("has_conflicts")).toBool();

    QStringList parts{stateText(state)};
    if (open && mergeRequest.value(QStringLiteral("draft")).toBool())
        parts << tr("draft");
    const QString approvalText = mergeRequestApprovals(approvals);
    if (!approvalText.isEmpty())
        parts << approvalText;
    if (!pipeline.isEmpty())
        parts << tr("pipeline %1").arg(statusWords(pipeline));
    if (open && conflicts)
        parts << tr("has conflicts");
    status.summary = parts.join(kSeparator);

    if (open) {
        if (pipeline == QLatin1String("failed"))
            status.attention << reason(QStringLiteral("pipeline-failed"), tr("Pipeline failed"), true);
        if (reviewer && !approvedBy(approvals, me))
            status.attention << reason(QStringLiteral("review"), tr("Review requested"));
        if (author && approvals.value(QStringLiteral("approved")).toBool())
            status.attention << reason(QStringLiteral("approved"), tr("Approved"), false, true);
        if (author && conflicts)
            status.attention << reason(QStringLiteral("conflicts"), tr("Has conflicts"));
    }

    status.closed = state == QLatin1String("merged") || state == QLatin1String("closed");
    status.data = QJsonObject{
        {QStringLiteral("mergeRequest"), mergeRequest},
        {QStringLiteral("approvals"), approvals},
        {QStringLiteral("user"),
         QJsonObject{{QStringLiteral("id"), user.value(QStringLiteral("id"))},
                     {QStringLiteral("username"), user.value(QStringLiteral("username"))}}},
    };
    return status;
}

ItemStatus issueStatus(const QJsonObject &issue)
{
    ItemStatus status = okStatus();
    const QString state = issue.value(QStringLiteral("state")).toString();
    QStringList parts{stateText(state)};
    QStringList assignees;
    for (const QJsonValue &assignee : issue.value(QStringLiteral("assignees")).toArray()) {
        const QString name = assignee.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            assignees << name;
    }
    if (!assignees.isEmpty())
        parts << tr("assigned to %1").arg(assignees.join(QStringLiteral(", ")));
    status.summary = parts.join(kSeparator);
    status.closed = state == QLatin1String("closed");
    status.data = QJsonObject{{QStringLiteral("issue"), issue}};
    return status;
}

ItemStatus pipelineStatus(const QString &branch, const QJsonObject &pipeline, const QJsonArray &jobs,
                          const QJsonArray &siblings, const QJsonArray &older)
{
    ItemStatus status = okStatus();
    status.closed = false;
    if (pipeline.isEmpty()) {
        status.summary = tr("No pipeline on %1").arg(branch);
        status.data = QJsonObject{{QStringLiteral("branch"), branch}};
        return status;
    }

    const QString state = pipeline.value(QStringLiteral("status")).toString();
    QStringList parts{capitalised(statusWords(state)), branch};
    const int failed = failedJobs(jobs);
    if (failed == 1)
        parts << tr("1 failed job");
    else if (failed > 1)
        parts << tr("%1 failed jobs").arg(failed);
    status.summary = parts.join(kSeparator);

    if (hasFailed(pipeline))
        status.attention << failedPipeline(pipeline);
    for (const QJsonValue &sibling : siblings) {
        const QJsonObject other = sibling.toObject().value(QStringLiteral("pipeline")).toObject();
        if (hasFailed(other))
            status.attention << failedPipeline(other);
    }
    status.data = QJsonObject{
        {QStringLiteral("branch"), branch},
        {QStringLiteral("pipeline"), pipeline},
        {QStringLiteral("jobs"), jobs},
    };
    if (!siblings.isEmpty())
        status.data.insert(QStringLiteral("siblings"), siblings);
    if (!older.isEmpty())
        status.data.insert(QStringLiteral("older"), older);
    return status;
}

PipelineGroup pipelineGroup(const QJsonObject &latest, const QJsonArray &recent)
{
    const qint64 latestId = latest.value(QStringLiteral("id")).toInteger();
    QList<QJsonObject> all;
    bool listed = false;
    for (const QJsonValue &value : recent) {
        const QJsonObject pipeline = value.toObject();
        const bool same = !latest.isEmpty() && pipeline.value(QStringLiteral("id")).toInteger() == latestId;
        all << (same ? latest : pipeline);
        listed = listed || same;
    }
    if (!latest.isEmpty() && !listed)
        all << latest;
    if (all.isEmpty())
        return {};

    // A date that cannot be read counts as the oldest.
    const auto order = [](const QJsonObject &pipeline) {
        const QDateTime date = createdAt(pipeline);
        return date.isValid() ? date.toMSecsSinceEpoch() : std::numeric_limits<qint64>::min();
    };
    std::stable_sort(all.begin(), all.end(), [&order](const QJsonObject &a, const QJsonObject &b) {
        const qint64 first = order(a);
        const qint64 second = order(b);
        if (first != second)
            return first > second;
        return a.value(QStringLiteral("id")).toInteger() > b.value(QStringLiteral("id")).toInteger();
    });

    PipelineGroup group;
    group.latest = all.first();
    const QDateTime newest = createdAt(group.latest);
    for (qsizetype i = 1; i < all.size(); ++i) {
        const QDateTime date = createdAt(all.at(i));
        const bool together = newest.isValid() && date.isValid() && date.msecsTo(newest) <= kGroupWindowMs;
        if (together)
            group.siblings.append(all.at(i));
        else
            group.older.append(all.at(i));
    }
    return group;
}

namespace {

// The latest notes a summary keeps, before system notes are left out.
constexpr int kSummaryNotes = 20;

// Finishes the job with a message if the configuration names no object.
bool namesAnObject(StatusJob &job, const QString &project, qint64 iid)
{
    if (!project.isEmpty() && iid != 0)
        return true;
    job.finish(incomplete(tr("Set the project and number in the item's settings.")));
    return false;
}

// Asks for the merge request, its approvals and the current user, and gives
// done the status from their answers, a failed one included.
void fetchMergeRequestAnswers(StatusJob &job, const QString &project, qint64 iid,
                              std::function<void(const ItemStatus &)> done)
{
    // The three requests run at once; done runs when all have answered.
    struct Replies
    {
        std::optional<ApiReply> mergeRequest;
        std::optional<ApiReply> approvals;
        std::optional<ApiReply> user;
    };
    auto replies = std::make_shared<Replies>();
    const auto finishWhenDone = [done = std::move(done), replies] {
        if (!replies->mergeRequest || !replies->approvals || !replies->user)
            return;
        for (const ApiReply *reply : {&*replies->mergeRequest, &*replies->user, &*replies->approvals}) {
            if (!reply->ok()) {
                done(statusFromReply(*reply));
                return;
            }
        }
        done(mergeRequestStatus(replies->mergeRequest->json.object(), replies->approvals->json.object(),
                                replies->user->json.object()));
    };

    const GitLabClient client(job.server(), job.owner());
    client.mergeRequest(project, iid, [replies, finishWhenDone](const ApiReply &reply) {
        replies->mergeRequest = reply;
        finishWhenDone();
    });
    client.approvals(project, iid, [replies, finishWhenDone](const ApiReply &reply) {
        replies->approvals = reply;
        finishWhenDone();
    });
    client.currentUser([replies, finishWhenDone](const ApiReply &reply) {
        replies->user = reply;
        finishWhenDone();
    });
}

QJsonArray withoutSystemNotes(const QJsonArray &notes)
{
    QJsonArray kept;
    for (const QJsonValue &note : notes) {
        if (!note.toObject().value(QStringLiteral("system")).toBool())
            kept.append(note);
    }
    return kept;
}

} // namespace

void fetchMergeRequestStatus(StatusJob &job)
{
    const QString project = projectPath(job.config(), job.projectSetting(kProjectSettingKey));
    const qint64 iid = itemNumber(job.config());
    if (!namesAnObject(job, project, iid))
        return;

    StatusJob *target = &job;
    fetchMergeRequestAnswers(job, project, iid, [target](const ItemStatus &status) { target->finish(status); });
}

void fetchIssueStatus(StatusJob &job)
{
    const QString project = projectPath(job.config(), job.projectSetting(kProjectSettingKey));
    const qint64 iid = itemNumber(job.config());
    if (!namesAnObject(job, project, iid))
        return;

    StatusJob *target = &job;
    GitLabClient(job.server(), job.owner()).issue(project, iid, [target](const ApiReply &reply) {
        target->finish(reply.ok() ? issueStatus(reply.json.object()) : statusFromReply(reply));
    });
}

void fetchMergeRequestSummaryStatus(StatusJob &job)
{
    const QString project = projectPath(job.config(), job.projectSetting(kProjectSettingKey));
    const qint64 iid = itemNumber(job.config());
    if (!namesAnObject(job, project, iid))
        return;

    StatusJob *target = &job;
    const GitLabClient client(job.server(), job.owner());
    fetchMergeRequestAnswers(job, project, iid, [target, client, project](const ItemStatus &status) {
        const QJsonObject pipeline = status.data.value(QStringLiteral("mergeRequest"))
                                         .toObject()
                                         .value(QStringLiteral("head_pipeline"))
                                         .toObject();
        if (pipeline.value(QStringLiteral("status")).toString() != QLatin1String("failed")) {
            target->finish(status);
            return;
        }
        // Failed jobs only add detail, so without them the status still stands.
        const qint64 id = pipeline.value(QStringLiteral("id")).toInteger();
        client.pipelineJobs(project, id, GetOptions().maxRows, [target, status](const ApiReply &jobs) {
            ItemStatus result = status;
            if (jobs.ok())
                result.data.insert(QStringLiteral("jobs"), jobs.json.array());
            target->finish(result);
        });
    });
}

void fetchIssueSummaryStatus(StatusJob &job)
{
    const QString project = projectPath(job.config(), job.projectSetting(kProjectSettingKey));
    const qint64 iid = itemNumber(job.config());
    if (!namesAnObject(job, project, iid))
        return;

    StatusJob *target = &job;
    const GitLabClient client(job.server(), job.owner());
    client.issue(project, iid, [target, client, project, iid](const ApiReply &reply) {
        if (!reply.ok()) {
            target->finish(statusFromReply(reply));
            return;
        }
        // Notes only add detail, so without them the status still stands.
        const ItemStatus status = issueStatus(reply.json.object());
        client.issueNotes(project, iid, kSummaryNotes, [target, status](const ApiReply &notes) {
            ItemStatus result = status;
            if (notes.ok())
                result.data.insert(QStringLiteral("notes"), withoutSystemNotes(notes.json.array()));
            target->finish(result);
        });
    });
}

namespace {

// The recent pipelines a status looks through for the group, and Previous and
// Next step through.
constexpr int kRecentPipelines = 20;

// What a pipeline status knows before its requests.
struct PipelineSource
{
    QString project;
    QString branch;
    qint64 iid = 0;             // the merge request's number, or 0 for a branch
    QJsonObject mergeRequest;   // that merge request, or the open one of the branch
};

ItemStatus withMergeRequest(ItemStatus status, const QJsonObject &mergeRequest)
{
    if (!mergeRequest.isEmpty())
        status.data.insert(QStringLiteral("mergeRequest"), mergeRequest);
    return status;
}

// The jobs of each pipeline in the group, at once. The latest's are needed, and
// a sibling's only add detail, so without them the status still stands.
void fetchGroupJobs(StatusJob *job, const PipelineSource &source, const PipelineGroup &group)
{
    if (group.latest.isEmpty()) {
        job->finish(withMergeRequest(pipelineStatus(source.branch, {}, {}), source.mergeRequest));
        return;
    }

    struct Answers
    {
        std::optional<ApiReply> latest;
        QList<QJsonObject> siblings;
        qsizetype waiting = 0;
    };
    auto answers = std::make_shared<Answers>();
    for (const QJsonValue &sibling : group.siblings)
        answers->siblings << QJsonObject{{QStringLiteral("pipeline"), sibling}};
    answers->waiting = answers->siblings.size() + 1;

    const auto finishWhenDone = [job, source, group, answers] {
        if (--answers->waiting > 0)
            return;
        if (!answers->latest->ok()) {
            job->finish(statusFromReply(*answers->latest));
            return;
        }
        QJsonArray siblings;
        for (const QJsonObject &sibling : std::as_const(answers->siblings))
            siblings.append(sibling);
        job->finish(withMergeRequest(pipelineStatus(source.branch, group.latest, answers->latest->json.array(),
                                                    siblings, group.older),
                                     source.mergeRequest));
    };

    const GitLabClient client(job->server(), job->owner());
    client.pipelineJobs(source.project, group.latest.value(QStringLiteral("id")).toInteger(), GetOptions().maxRows,
                        [answers, finishWhenDone](const ApiReply &reply) {
                            answers->latest = reply;
                            finishWhenDone();
                        });
    for (qsizetype i = 0; i < group.siblings.size(); ++i) {
        const qint64 id = group.siblings.at(i).toObject().value(QStringLiteral("id")).toInteger();
        client.pipelineJobs(source.project, id, GetOptions().maxRows, [answers, finishWhenDone, i](const ApiReply &reply) {
            if (reply.ok())
                answers->siblings[i].insert(QStringLiteral("jobs"), reply.json.array());
            finishWhenDone();
        });
    }
}

// The latest pipeline of the branch. Then, at once, the recent pipelines and, for
// a branch, its open merge request. These only add detail, so without them the
// status of the latest still stands.
void fetchPipelines(StatusJob *job, const PipelineSource &source)
{
    const GitLabClient client(job->server(), job->owner());
    client.latestPipeline(source.project, source.branch, [job, client, source](const ApiReply &reply) {
        // GitLab answers 404 for a branch without pipelines. A merge request
        // can still have some, on a ref of its own.
        if (!reply.ok() && reply.error != ApiReply::Error::NotFound) {
            job->finish(statusFromReply(reply));
            return;
        }
        const QJsonObject latest = reply.ok() ? reply.json.object() : QJsonObject();

        struct Answers
        {
            PipelineSource source;
            QJsonArray recent;
            int waiting = 0;
        };
        auto answers = std::make_shared<Answers>();
        answers->source = source;
        answers->waiting = source.iid != 0 ? 1 : 2;
        const auto continueWhenDone = [job, answers, latest] {
            if (--answers->waiting == 0)
                fetchGroupJobs(job, answers->source, pipelineGroup(latest, answers->recent));
        };
        const auto recentDone = [answers, continueWhenDone](const ApiReply &recent) {
            if (recent.ok())
                answers->recent = recent.json.array();
            continueWhenDone();
        };

        if (source.iid != 0) {
            client.mergeRequestPipelines(source.project, source.iid, kRecentPipelines, recentDone);
            return;
        }
        QUrlQuery ofBranch;
        ofBranch.addQueryItem(QStringLiteral("ref"), GitLabClient::encodePath(source.branch));
        client.pipelines(source.project, ofBranch, kRecentPipelines, recentDone);

        QUrlQuery openOfBranch;
        openOfBranch.addQueryItem(QStringLiteral("source_branch"), GitLabClient::encodePath(source.branch));
        openOfBranch.addQueryItem(QStringLiteral("state"), QStringLiteral("opened"));
        client.projectMergeRequests(source.project, openOfBranch, 1, [answers, continueWhenDone](const ApiReply &found) {
            if (found.ok())
                answers->source.mergeRequest = found.json.array().at(0).toObject();
            continueWhenDone();
        });
    });
}

} // namespace

void fetchPipelineStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    const QString project = projectPath(config, job.projectSetting(kProjectSettingKey));
    if (project.isEmpty()) {
        job.finish(incomplete(tr("Set the project in the item's settings.")));
        return;
    }

    const QString source = config.value(QStringLiteral("branchSource")).toString();
    if (source == QLatin1String("mainFolder")) {
        job.finish(incomplete(tr("Follows the main folder from M7")));
        return;
    }

    StatusJob *target = &job;
    if (source == QLatin1String("mergeRequest")) {
        const qint64 iid = itemNumber(config);
        if (iid == 0) {
            job.finish(incomplete(tr("Set the merge request number in the item's settings.")));
            return;
        }
        GitLabClient(job.server(), job.owner()).mergeRequest(project, iid, [target, project, iid](const ApiReply &reply) {
            if (!reply.ok()) {
                target->finish(statusFromReply(reply));
                return;
            }
            const QJsonObject mergeRequest = reply.json.object();
            const QString branch = mergeRequest.value(QStringLiteral("source_branch")).toString();
            fetchPipelines(target, {project, branch, iid, mergeRequest});
        });
        return;
    }

    fetchPipelines(target, {project, configuredBranch(config), 0, {}});
}

} // namespace mirante::modules::gitlab
