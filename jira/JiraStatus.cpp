#include "JiraStatus.h"

#include "JiraClient.h"
#include "JiraSettings.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>

#include <functional>
#include <utility>

namespace mirante::modules::jira {

namespace {

QString nameOf(const QJsonObject &fields, const QString &key)
{
    return fields.value(key).toObject().value(QStringLiteral("name")).toString();
}

// One sprint from the value of the sprint field.
struct Sprint
{
    QString name;
    QString state;
};

// Older Jira Data Center writes a sprint as text, such as
// "com.atlassian.greenhopper.service.sprint.Sprint@1b[id=4,state=ACTIVE,name=Sprint 12,goal=]".
Sprint sprintFromText(const QString &text)
{
    static const QRegularExpression name(QStringLiteral("\\bname=(.*?)(?:,[A-Za-z]+=|\\]$)"));
    static const QRegularExpression state(QStringLiteral("\\bstate=([^,\\]]*)"));
    return {name.match(text).captured(1), state.match(text).captured(1)};
}

Sprint readSprint(const QJsonValue &value)
{
    if (value.isString())
        return sprintFromText(value.toString());
    const QJsonObject sprint = value.toObject();
    return {sprint.value(QStringLiteral("name")).toString(), sprint.value(QStringLiteral("state")).toString()};
}

// Fetches the issue named by the job's "key", and finishes the job with what
// read makes of the answer.
void fetchIssue(StatusJob &job, const QStringList &fields, const QStringList &expand,
                std::function<ItemStatus(const QJsonObject &)> read)
{
    const QString key = job.config().value(QStringLiteral("key")).toString().trimmed();
    if (key.isEmpty()) {
        ItemStatus status;
        status.state = ItemStatus::State::Error;
        status.message = QCoreApplication::translate("jira", "Set the issue key in the item's settings.");
        job.finish(status);
        return;
    }

    const JiraClient client(job.server(), deploymentOf(job.moduleSettings(kGeneralScreen)));
    StatusJob *owner = &job;
    client.issue(
        key, fields, job.owner(),
        [owner, read = std::move(read)](const ApiReply &reply) {
            owner->finish(reply.ok() ? read(reply.json.object()) : statusFromReply(reply));
        },
        expand);
}

} // namespace

QStringList statusFields()
{
    return {QStringLiteral("summary"),  QStringLiteral("status"),    QStringLiteral("assignee"),
            QStringLiteral("priority"), QStringLiteral("issuetype"), QStringLiteral("updated")};
}

QStringList summaryExpand()
{
    return {QStringLiteral("renderedFields"), QStringLiteral("names")};
}

ItemStatus issueStatus(const QJsonObject &issue)
{
    const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
    const QJsonObject status = fields.value(QStringLiteral("status")).toObject();
    const QString statusName = status.value(QStringLiteral("name")).toString();
    const QString statusId = status.value(QStringLiteral("id")).toString();
    const QString category =
        status.value(QStringLiteral("statusCategory")).toObject().value(QStringLiteral("key")).toString();
    const QString assignee =
        fields.value(QStringLiteral("assignee")).toObject().value(QStringLiteral("displayName")).toString();

    ItemStatus result;
    result.state = ItemStatus::State::Ok;
    result.data = QJsonObject{
        {QStringLiteral("key"), issue.value(QStringLiteral("key")).toString()},
        {QStringLiteral("summary"), fields.value(QStringLiteral("summary")).toString()},
        {QStringLiteral("status"), statusName},
        {QStringLiteral("statusId"), statusId},
        {QStringLiteral("statusCategory"), category},
        {QStringLiteral("assignee"), assignee},
        {QStringLiteral("priority"), nameOf(fields, QStringLiteral("priority"))},
        {QStringLiteral("issueType"), nameOf(fields, QStringLiteral("issuetype"))},
        {QStringLiteral("updated"), fields.value(QStringLiteral("updated")).toString()},
    };

    const QString who = assignee.isEmpty()
                            ? QCoreApplication::translate("jira", "unassigned")
                            : QCoreApplication::translate("jira", "assigned to %1").arg(assignee);
    const QString dot = QStringLiteral(" ") + QChar(0x00B7) + QStringLiteral(" ");
    result.summary = statusName.isEmpty() ? who : statusName + dot + who;

    // Reported on every fetch. The core records it as seen on the item's
    // first status, so it counts only once the status id changes.
    if (!statusId.isEmpty()) {
        AttentionReason changed;
        changed.key = QStringLiteral("status:") + statusId;
        changed.text = QCoreApplication::translate("jira", "Status changed to %1").arg(statusName);
        changed.clearsOnView = true;
        result.attention << changed;
    }

    result.closed = category == QLatin1String("done");
    return result;
}

QString sprintOf(const QJsonObject &issue)
{
    const QJsonObject names = issue.value(QStringLiteral("names")).toObject();
    const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
    for (auto name = names.constBegin(); name != names.constEnd(); ++name) {
        if (name.value().toString() != QLatin1String("Sprint"))
            continue;
        const QJsonArray sprints = fields.value(name.key()).toArray();
        QString last;
        for (const QJsonValue &value : sprints) {
            const Sprint sprint = readSprint(value);
            if (sprint.state.compare(QLatin1String("active"), Qt::CaseInsensitive) == 0)
                return sprint.name;
            last = sprint.name;
        }
        if (!last.isEmpty())
            return last;
    }
    return {};
}

void fetchIssueStatus(StatusJob &job)
{
    fetchIssue(job, statusFields(), {QStringLiteral("renderedFields")}, &issueStatus);
}

void fetchIssueSummaryStatus(StatusJob &job)
{
    fetchIssue(job, {}, summaryExpand(), [](const QJsonObject &issue) {
        ItemStatus status = issueStatus(issue);
        status.data.insert(QStringLiteral("issue"), issue);
        return status;
    });
}

} // namespace mirante::modules::jira
