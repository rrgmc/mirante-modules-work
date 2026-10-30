#include "GitLabLists.h"

#include "GitLabClient.h"
#include "GitLabSettings.h"
#include "GitLabUrls.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QPair>
#include <QStringList>

#include <utility>

namespace mirante::modules::gitlab {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("gitlab", text);
}

// What a list's configuration points at. problem says what is missing, and is
// empty when the scope is complete.
struct ListScope
{
    enum class Where { Project, Group, All };

    Where where = Where::All;
    QString path;     // the project or the group
    QString problem;
};

// An absent scope is a project, the first choice of the form.
ListScope readScope(const QJsonObject &config, const QJsonValue &defaultProject)
{
    ListScope scope;
    const QString name = config.value(QStringLiteral("scope")).toString();
    if (name.isEmpty() || name == QLatin1String("project")) {
        scope.where = ListScope::Where::Project;
        scope.path = projectPath(config, defaultProject);
        if (scope.path.isEmpty())
            scope.problem = tr("Set the project in the item's settings.");
    } else if (name == QLatin1String("group")) {
        scope.where = ListScope::Where::Group;
        // projectPath() also tidies a group path, given as the default.
        scope.path = projectPath({}, config.value(QStringLiteral("group")));
        if (scope.path.isEmpty())
            scope.problem = tr("Set the group in the item's settings.");
    } else if (name != QLatin1String("all")) {
        scope.problem = tr("Set the scope in the item's settings.");
    }
    return scope;
}

// A positive whole number, which a file edited by hand may hold as text, or
// fallback.
int positiveNumber(const QJsonValue &value, int fallback)
{
    const int number = value.isString() ? value.toString().trimmed().toInt() : value.toInt();
    return number >= 1 ? number : fallback;
}

void requestRows(const GitLabClient &client, ListKind kind, const ListScope &scope, const QUrlQuery &query, int limit,
                 GitLabClient::Done done)
{
    const bool mergeRequests = kind == ListKind::MergeRequests;
    switch (scope.where) {
    case ListScope::Where::Project:
        if (mergeRequests)
            client.projectMergeRequests(scope.path, query, limit, std::move(done));
        else
            client.projectIssues(scope.path, query, limit, std::move(done));
        break;
    case ListScope::Where::Group:
        if (mergeRequests)
            client.groupMergeRequests(scope.path, query, limit, std::move(done));
        else
            client.groupIssues(scope.path, query, limit, std::move(done));
        break;
    case ListScope::Where::All:
        if (mergeRequests)
            client.mergeRequests(query, limit, std::move(done));
        else
            client.issues(query, limit, std::move(done));
        break;
    }
}

// A list that reaches its limit may have more.
QString listSummary(ListKind kind, int total, int limit)
{
    const bool mergeRequests = kind == ListKind::MergeRequests;
    if (total >= limit)
        return (mergeRequests ? tr("%1+ merge requests") : tr("%1+ issues")).arg(limit);
    if (total == 1)
        return mergeRequests ? tr("1 merge request") : tr("1 issue");
    return (mergeRequests ? tr("%1 merge requests") : tr("%1 issues")).arg(total);
}

// No attention, and closed stays unset, so a list never counts towards a task
// being done. See decision 0067.
ItemStatus listStatus(const QString &summary, const QJsonArray &rows)
{
    ItemStatus status;
    status.state = ItemStatus::State::Ok;
    status.updatedAt = QDateTime::currentDateTimeUtc();
    status.summary = summary;
    status.data = QJsonObject{{QStringLiteral("rows"), rows}, {QStringLiteral("total"), int(rows.size())}};
    return status;
}

void fetchList(StatusJob &job, ListKind kind)
{
    const QJsonObject config = job.config();
    const ListScope scope = readScope(config, job.projectSetting(kProjectSettingKey));
    if (!scope.problem.isEmpty()) {
        ItemStatus status;
        status.message = scope.problem;
        job.finish(status);
        return;
    }

    const QString preset = config.value(QStringLiteral("preset")).toString();
    const QString filter = config.value(QStringLiteral("filter")).toString();
    const int limit = listLimit(config);
    StatusJob *target = &job;
    const GitLabClient client(job.server(), job.owner());
    const GitLabClient::Done finishWithRows = [target, kind, limit](const ApiReply &reply) {
        if (!reply.ok()) {
            target->finish(statusFromReply(reply));
            return;
        }
        const QJsonArray rows = reply.json.array();
        target->finish(listStatus(listSummary(kind, int(rows.size()), limit), rows));
    };

    if (kind == ListKind::MergeRequests && preset == QLatin1String("reviewRequested")) {
        client.currentUser([=](const ApiReply &user) {
            if (!user.ok()) {
                target->finish(statusFromReply(user));
                return;
            }
            const qint64 userId = user.json.object().value(QStringLiteral("id")).toInteger();
            requestRows(client, kind, scope, listQuery(kind, preset, userId, filter), limit, finishWithRows);
        });
        return;
    }
    requestRows(client, kind, scope, listQuery(kind, preset, 0, filter), limit, finishWithRows);
}

QString pipelineSummary(int total, int limit)
{
    if (total >= limit)
        return tr("%1+ pipelines").arg(limit);
    return total == 1 ? tr("1 pipeline") : tr("%1 pipelines").arg(total);
}

// A status without a request: the configuration is not complete.
ItemStatus incompleteList(const QString &message)
{
    ItemStatus status;
    status.message = message;
    return status;
}

} // namespace

int listLimit(const QJsonObject &config)
{
    return positiveNumber(config.value(QStringLiteral("limit")), kDefaultListLimit);
}

int listPageSize(const QJsonObject &config)
{
    return positiveNumber(config.value(QStringLiteral("pageSize")), kDefaultListPageSize);
}

QUrlQuery listQuery(ListKind kind, const QString &preset, qint64 userId, const QString &filter)
{
    const bool mergeRequests = kind == ListKind::MergeRequests;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("state"), QStringLiteral("opened"));
    if (mergeRequests && preset == QLatin1String("reviewRequested")) {
        query.addQueryItem(QStringLiteral("scope"), QStringLiteral("all"));
        query.addQueryItem(QStringLiteral("reviewer_id"), QString::number(userId));
    } else if (preset == QLatin1String("allOpen")) {
        query.addQueryItem(QStringLiteral("scope"), QStringLiteral("all"));
    } else if (preset == QLatin1String("createdByMe")) {
        query.addQueryItem(QStringLiteral("scope"), QStringLiteral("created_by_me"));
    } else if (preset == QLatin1String("assignedToMe")) {
        query.addQueryItem(QStringLiteral("scope"), QStringLiteral("assigned_to_me"));
    } else {
        // Any other preset is the first choice of the type's form.
        query.addQueryItem(QStringLiteral("scope"),
                           mergeRequests ? QStringLiteral("created_by_me") : QStringLiteral("assigned_to_me"));
    }
    query.addQueryItem(QStringLiteral("order_by"), QStringLiteral("updated_at"));

    // Removed first, so that a name the filter repeats, as in labels[]=a&labels[]=b,
    // keeps all its values.
    const QList<QPair<QString, QString>> custom = QUrlQuery(filter.trimmed()).queryItems(QUrl::FullyEncoded);
    for (const QPair<QString, QString> &item : custom)
        query.removeAllQueryItems(item.first);
    for (const QPair<QString, QString> &item : custom) {
        if (!item.first.isEmpty())
            query.addQueryItem(item.first, item.second);
    }
    return query;
}

QString listDescription(ListKind kind, const QJsonObject &config, const QJsonValue &defaultProject)
{
    const QString preset = config.value(QStringLiteral("preset")).toString();
    const bool mergeRequests = kind == ListKind::MergeRequests;
    QStringList parts;
    if (preset == QLatin1String("allOpen"))
        parts << tr("All open");
    else if (preset == QLatin1String("reviewRequested") && mergeRequests)
        parts << tr("Review requested from me");
    else if (preset == QLatin1String("createdByMe") || (mergeRequests && preset != QLatin1String("assignedToMe")))
        parts << tr("Created by me");
    else
        parts << tr("Assigned to me");

    const ListScope scope = readScope(config, defaultProject);
    if (scope.problem.isEmpty()) {
        switch (scope.where) {
        case ListScope::Where::Project:
            parts << scope.path;
            break;
        case ListScope::Where::Group:
            parts << tr("group %1").arg(scope.path);
            break;
        case ListScope::Where::All:
            parts << tr("all projects");
            break;
        }
    }
    const QString filter = config.value(QStringLiteral("filter")).toString().trimmed();
    if (!filter.isEmpty())
        parts << filter;
    return parts.join(QStringLiteral(" · "));
}

QString pipelineListDescription(const QJsonObject &config, const QJsonValue &defaultProject)
{
    const QString source = config.value(QStringLiteral("source")).toString();
    QStringList parts;
    if (source == QLatin1String("mine")) {
        parts << tr("Started by me");
    } else if (source == QLatin1String("branch")) {
        const QString branch = config.value(QStringLiteral("branch")).toString().trimmed();
        parts << tr("Of branch %1").arg(branch.isEmpty() ? kDefaultBranch : branch);
    } else if (source == QLatin1String("mergeRequest")) {
        parts << tr("Of merge request !%1").arg(itemNumber(config));
    } else {
        parts << tr("All pipelines");
    }
    const QString project = projectPath(config, defaultProject);
    if (!project.isEmpty())
        parts << project;
    return parts.join(QStringLiteral(" · "));
}

QUrl listWebUrl(ListKind kind, const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject)
{
    const ListScope scope = readScope(config, defaultProject);
    if (!scope.problem.isEmpty() || baseUrl.isEmpty())
        return {};

    QString path = baseUrl.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    const QString page = kind == ListKind::MergeRequests ? QStringLiteral("merge_requests") : QStringLiteral("issues");
    switch (scope.where) {
    case ListScope::Where::Project:
        path += QLatin1Char('/') + scope.path + QStringLiteral("/-/") + page;
        break;
    case ListScope::Where::Group:
        path += QStringLiteral("/groups/") + scope.path + QStringLiteral("/-/") + page;
        break;
    case ListScope::Where::All:
        path += QStringLiteral("/dashboard/") + page;
        break;
    }

    QUrl url = baseUrl;
    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

void fetchMergeRequestListStatus(StatusJob &job)
{
    fetchList(job, ListKind::MergeRequests);
}

void fetchIssueListStatus(StatusJob &job)
{
    fetchList(job, ListKind::Issues);
}

void fetchPipelineListStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    const QString project = projectPath(config, job.projectSetting(kProjectSettingKey));
    if (project.isEmpty()) {
        job.finish(incompleteList(tr("Set the project in the item's settings.")));
        return;
    }

    const int limit = listLimit(config);
    StatusJob *target = &job;
    const GitLabClient client(job.server(), job.owner());
    const GitLabClient::Done finishWithRows = [target, limit](const ApiReply &reply) {
        if (!reply.ok()) {
            target->finish(statusFromReply(reply));
            return;
        }
        const QJsonArray rows = reply.json.array();
        target->finish(listStatus(pipelineSummary(int(rows.size()), limit), rows));
    };

    // An absent source is the first choice of the form.
    const QString source = config.value(QStringLiteral("source")).toString();
    if (source == QLatin1String("mergeRequest")) {
        const qint64 iid = itemNumber(config);
        if (iid == 0) {
            job.finish(incompleteList(tr("Set the merge request number in the item's settings.")));
            return;
        }
        client.mergeRequestPipelines(project, iid, limit, finishWithRows);
    } else if (source == QLatin1String("mine")) {
        client.currentUser([=](const ApiReply &user) {
            if (!user.ok()) {
                target->finish(statusFromReply(user));
                return;
            }
            // Encoded here, as a branch is, so the name reaches GitLab whole.
            const QString username = user.json.object().value(QStringLiteral("username")).toString();
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("username"), GitLabClient::encodePath(username));
            client.pipelines(project, query, limit, finishWithRows);
        });
    } else if (source == QLatin1String("branch")) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("ref"), GitLabClient::encodePath(configuredBranch(config)));
        client.pipelines(project, query, limit, finishWithRows);
    } else if (source.isEmpty() || source == QLatin1String("all")) {
        client.pipelines(project, {}, limit, finishWithRows);
    } else {
        job.finish(incompleteList(tr("Set the source in the item's settings.")));
    }
}

} // namespace mirante::modules::gitlab
