#include "JiraStatus.h"

#include "JiraClient.h"
#include "JiraLists.h"
#include "JiraSettings.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
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

// Fetches the issue named by the job's "key", and passes the answer on to read,
// which finishes the job. A failed reply finishes it here.
void fetchIssue(StatusJob &job, const QStringList &fields, const QStringList &expand,
                std::function<void(StatusJob &, const QJsonObject &)> read)
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
            if (reply.ok())
                read(*owner, reply.json.object());
            else
                owner->finish(statusFromReply(reply));
        },
        expand);
}

// What finishing the search for an issue's children needs.
struct ChildrenSearch
{
    JiraClient client;
    SearchOptions options;
    ItemStatus status;
    QSet<QString> listed;
    int subtaskCount = 0;
};

// Searches with jql, adds the children to the status, and finishes the job. A
// site that does not know Epic Link refuses the whole query with a 400, so
// fallback, if any, is asked next. Any other failure keeps the status as it is.
void searchChildren(StatusJob *owner, const ChildrenSearch &search, const QString &jql, const QString &fallback)
{
    search.client.search(jql, search.options, owner->owner(), [owner, search, fallback](const ApiReply &reply) {
        if (reply.error == ApiReply::Error::Http && !fallback.isEmpty()) {
            searchChildren(owner, search, fallback, {});
            return;
        }
        ItemStatus result = search.status;
        if (reply.ok()) {
            const QJsonObject answer = reply.json.object();
            const QJsonArray rows = answer.value(QStringLiteral("issues")).toArray();
            QJsonArray children;
            for (const QJsonValue &row : rows) {
                const QString key = row.toObject().value(QStringLiteral("key")).toString();
                if (!search.listed.contains(key) && children.size() < kMaxChildren)
                    children.append(row);
            }
            const int total = answer.value(QStringLiteral("total")).toInt(int(rows.size()));
            result.data.insert(QStringLiteral("children"), children);
            result.data.insert(QStringLiteral("childrenTotal"),
                               std::max(total - search.subtaskCount, int(children.size())));
        }
        owner->finish(result);
    });
}

// Adds the issue's children to status, and finishes the job.
void fetchChildren(StatusJob &job, const QJsonObject &issue, const ItemStatus &status)
{
    const QJsonArray subtasks =
        issue.value(QStringLiteral("fields")).toObject().value(QStringLiteral("subtasks")).toArray();
    QSet<QString> listed;
    for (const QJsonValue &subtask : subtasks)
        listed.insert(subtask.toObject().value(QStringLiteral("key")).toString());

    const JiraClient client(job.server(), deploymentOf(job.moduleSettings(kGeneralScreen)));
    SearchOptions options;
    options.fields = QStringList{QStringLiteral("summary"), QStringLiteral("status")};
    options.pageSize = 100;
    // Jira Data Center gives the total, so the rows that will not show are not
    // needed. Jira Cloud gives none, so they are the only way to count.
    options.maxRows = client.deployment() == Deployment::Cloud ? kCountedChildren
                                                               : kMaxChildren + int(subtasks.size());

    const QString key = issue.value(QStringLiteral("key")).toString();
    const QString jql = childrenJql(client.deployment(), key);
    const QString parentOnly = childrenJql(Deployment::Cloud, key);
    searchChildren(&job, {client, options, status, listed, int(subtasks.size())}, jql,
                   jql == parentOnly ? QString() : parentOnly);
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
    fetchIssue(job, statusFields(), {QStringLiteral("renderedFields")},
               [](StatusJob &owner, const QJsonObject &issue) { owner.finish(issueStatus(issue)); });
}

void fetchIssueSummaryStatus(StatusJob &job)
{
    fetchIssue(job, {}, summaryExpand(), [](StatusJob &owner, const QJsonObject &issue) {
        ItemStatus status = issueStatus(issue);
        status.data.insert(QStringLiteral("issue"), issue);
        fetchChildren(owner, issue, status);
    });
}

} // namespace mirante::modules::jira
