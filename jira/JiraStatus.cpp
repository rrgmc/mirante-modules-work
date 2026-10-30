#include "JiraStatus.h"

#include "JiraClient.h"
#include "JiraSettings.h"

#include <QCoreApplication>

namespace mirante::modules::jira {

namespace {

QString nameOf(const QJsonObject &fields, const QString &key)
{
    return fields.value(key).toObject().value(QStringLiteral("name")).toString();
}

} // namespace

QStringList statusFields()
{
    return {QStringLiteral("summary"),  QStringLiteral("status"),    QStringLiteral("assignee"),
            QStringLiteral("priority"), QStringLiteral("issuetype"), QStringLiteral("updated")};
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

void fetchIssueStatus(StatusJob &job)
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
    client.issue(key, statusFields(), job.owner(), [owner](const ApiReply &reply) {
        owner->finish(reply.ok() ? issueStatus(reply.json.object()) : statusFromReply(reply));
    });
}

void fetchIssueSummaryStatus(StatusJob &job)
{
    fetchIssueStatus(job);
}

} // namespace mirante::modules::jira
