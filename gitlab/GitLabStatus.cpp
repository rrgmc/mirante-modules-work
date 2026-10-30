#include "GitLabStatus.h"

#include "GitLabClient.h"
#include "GitLabSettings.h"
#include "GitLabUrls.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonValue>
#include <QStringList>

#include <memory>
#include <optional>

namespace mirante::modules::gitlab {

namespace {

const QString kSeparator = QStringLiteral(" · ");

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

ItemStatus pipelineStatus(const QString &branch, const QJsonObject &pipeline, const QJsonArray &jobs)
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

    if (state == QLatin1String("failed")) {
        const QString id = QString::number(pipeline.value(QStringLiteral("id")).toInteger());
        status.attention << reason(QStringLiteral("pipeline-failed:") + id, tr("Pipeline failed"), true);
    }
    status.data = QJsonObject{
        {QStringLiteral("branch"), branch},
        {QStringLiteral("pipeline"), pipeline},
        {QStringLiteral("jobs"), jobs},
    };
    return status;
}

void fetchMergeRequestStatus(StatusJob &job)
{
    const QString project = projectPath(job.config(), job.projectSetting(kProjectSettingKey));
    const qint64 iid = itemNumber(job.config());
    if (project.isEmpty() || iid == 0) {
        job.finish(incomplete(tr("Set the project and number in the item's settings.")));
        return;
    }

    // The three requests run at once; the job ends when all have answered.
    struct Replies
    {
        std::optional<ApiReply> mergeRequest;
        std::optional<ApiReply> approvals;
        std::optional<ApiReply> user;
    };
    auto replies = std::make_shared<Replies>();
    StatusJob *target = &job;
    const auto finishWhenDone = [target, replies] {
        if (!replies->mergeRequest || !replies->approvals || !replies->user)
            return;
        for (const ApiReply *reply : {&*replies->mergeRequest, &*replies->user, &*replies->approvals}) {
            if (!reply->ok()) {
                target->finish(statusFromReply(*reply));
                return;
            }
        }
        target->finish(mergeRequestStatus(replies->mergeRequest->json.object(), replies->approvals->json.object(),
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

void fetchIssueStatus(StatusJob &job)
{
    const QString project = projectPath(job.config(), job.projectSetting(kProjectSettingKey));
    const qint64 iid = itemNumber(job.config());
    if (project.isEmpty() || iid == 0) {
        job.finish(incomplete(tr("Set the project and number in the item's settings.")));
        return;
    }

    StatusJob *target = &job;
    GitLabClient(job.server(), job.owner()).issue(project, iid, [target](const ApiReply &reply) {
        target->finish(reply.ok() ? issueStatus(reply.json.object()) : statusFromReply(reply));
    });
}

void fetchMergeRequestSummaryStatus(StatusJob &job)
{
    fetchMergeRequestStatus(job);
}

void fetchIssueSummaryStatus(StatusJob &job)
{
    fetchIssueStatus(job);
}

namespace {

// The latest pipeline of branch, then its jobs.
void fetchLatestPipeline(StatusJob *job, const QString &project, const QString &branch)
{
    const GitLabClient client(job->server(), job->owner());
    client.latestPipeline(project, branch, [job, client, project, branch](const ApiReply &reply) {
        // GitLab answers 404 for a branch without pipelines.
        if (reply.error == ApiReply::Error::NotFound) {
            job->finish(pipelineStatus(branch, {}, {}));
            return;
        }
        if (!reply.ok()) {
            job->finish(statusFromReply(reply));
            return;
        }
        const QJsonObject pipeline = reply.json.object();
        const qint64 id = pipeline.value(QStringLiteral("id")).toInteger();
        client.pipelineJobs(project, id, GetOptions().maxRows, [job, branch, pipeline](const ApiReply &jobs) {
            job->finish(jobs.ok() ? pipelineStatus(branch, pipeline, jobs.json.array()) : statusFromReply(jobs));
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
        GitLabClient(job.server(), job.owner()).mergeRequest(project, iid, [target, project](const ApiReply &reply) {
            if (!reply.ok()) {
                target->finish(statusFromReply(reply));
                return;
            }
            const QString branch = reply.json.object().value(QStringLiteral("source_branch")).toString();
            fetchLatestPipeline(target, project, branch);
        });
        return;
    }

    const QString branch = config.value(QStringLiteral("branch")).toString().trimmed();
    if (branch.isEmpty()) {
        job.finish(incomplete(tr("Set the branch in the item's settings.")));
        return;
    }
    fetchLatestPipeline(target, project, branch);
}

} // namespace mirante::modules::gitlab
