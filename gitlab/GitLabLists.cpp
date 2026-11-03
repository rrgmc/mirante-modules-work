#include "GitLabLists.h"

#include "GitLabClient.h"
#include "GitLabSettings.h"
#include "GitLabUrls.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QPair>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace mirante::modules::gitlab {

namespace {

// What a list's configuration points at. problem says what is missing, and is
// empty when the scope is complete.
struct ListScope
{
    enum class Where { Project, Group, All, Projects };

    Where where = Where::All;
    QString path;       // the project or the group
    QStringList paths;  // Projects: "group/project" each
    QString problem;
};

QString severalProjectsProblem()
{
    return QCoreApplication::translate("gitlab", "Set the projects in the item's settings.");
}

// An absent scope is a project, the first choice of the form.
ListScope readScope(const QJsonObject &config, const QJsonValue &defaultProject, ListSpan span = ListSpan::Scope)
{
    ListScope scope;
    if (span == ListSpan::Projects) {
        scope.where = ListScope::Where::Projects;
        scope.paths = configuredProjects(config);
        if (scope.paths.isEmpty())
            scope.problem = severalProjectsProblem();
        return scope;
    }
    const QString name = config.value(QStringLiteral("scope")).toString();
    if (name.isEmpty() || name == QLatin1String("project")) {
        scope.where = ListScope::Where::Project;
        scope.path = projectPath(config, defaultProject);
        if (scope.path.isEmpty())
            scope.problem = QCoreApplication::translate("gitlab", "Set the project in the item's settings.");
    } else if (name == QLatin1String("group")) {
        scope.where = ListScope::Where::Group;
        // projectPath() also tidies a group path, given as the default.
        scope.path = projectPath({}, config.value(QStringLiteral("group")));
        if (scope.path.isEmpty())
            scope.problem = QCoreApplication::translate("gitlab", "Set the group in the item's settings.");
    } else if (name != QLatin1String("all")) {
        scope.problem = QCoreApplication::translate("gitlab", "Set the scope in the item's settings.");
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
    case ListScope::Where::Projects:
        // fetchList() asks each project itself.
        break;
    }
}

ItemStatus listStatus(const QString &summary, const QJsonArray &rows);

// The rows of several projects, as they come. The first failure finishes the
// job; the last answer joins the rows.
struct Answers
{
    int waiting = 0;
    bool finished = false;
    QJsonArray rows;
};

// The newest rows by key, an ISO date in UTC, whose text sorts as the date does.
QJsonArray newestRows(const QJsonArray &rows, const QString &key, int limit)
{
    QList<QJsonObject> sorted;
    for (const QJsonValue &row : rows)
        sorted << row.toObject();
    std::stable_sort(sorted.begin(), sorted.end(), [&key](const QJsonObject &a, const QJsonObject &b) {
        return a.value(key).toString() > b.value(key).toString();
    });
    QJsonArray newest;
    for (int index = 0; index < sorted.size() && index < limit; ++index)
        newest << sorted.at(index);
    return newest;
}

// A status of one of several projects, with the project's path before its
// message.
ItemStatus ofProject(const QString &project, ItemStatus status)
{
    status.message = project + QStringLiteral(": ") + status.message;
    return status;
}

// Asks each project with ask, then finishes the job with the newest rows by
// sortKey. tag, when set, adds kRowProjectKey to each row.
using AskProject = std::function<void(const QString &project, GitLabClient::Done done)>;
void requestEachProject(StatusJob *target, const QStringList &projects, const QString &sortKey, int limit, bool tag,
                        QString (*summary)(int total, int limit), const AskProject &ask)
{
    auto answers = std::make_shared<Answers>();
    answers->waiting = int(projects.size());
    for (const QString &project : projects) {
        ask(project, [=](const ApiReply &reply) {
            if (answers->finished)
                return;
            if (!reply.ok()) {
                answers->finished = true;
                target->finish(ofProject(project, statusFromReply(reply)));
                return;
            }
            for (const QJsonValue &value : reply.json.array()) {
                QJsonObject row = value.toObject();
                if (tag)
                    row.insert(kRowProjectKey, project);
                answers->rows << row;
            }
            if (--answers->waiting > 0)
                return;
            answers->finished = true;
            const QJsonArray rows = newestRows(answers->rows, sortKey, limit);
            target->finish(listStatus(summary(int(rows.size()), limit), rows));
        });
    }
}

// A list that reaches its limit may have more.
QString listSummary(ListKind kind, int total, int limit)
{
    const bool mergeRequests = kind == ListKind::MergeRequests;
    if (total >= limit)
        return (mergeRequests ? QCoreApplication::translate("gitlab", "%1+ merge requests")
                              : QCoreApplication::translate("gitlab", "%1+ issues"))
            .arg(limit);
    if (total == 1)
        return mergeRequests ? QCoreApplication::translate("gitlab", "1 merge request")
                             : QCoreApplication::translate("gitlab", "1 issue");
    return (mergeRequests ? QCoreApplication::translate("gitlab", "%1 merge requests")
                          : QCoreApplication::translate("gitlab", "%1 issues"))
        .arg(total);
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

QString mergeRequestSummary(int total, int limit)
{
    return listSummary(ListKind::MergeRequests, total, limit);
}

void fetchList(StatusJob &job, ListKind kind, ListSpan span = ListSpan::Scope)
{
    const QJsonObject config = job.config();
    const ListScope scope = readScope(config, job.projectSetting(kProjectSettingKey), span);
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
    // Only merge requests have a type over several projects.
    const auto rows = [=](const QUrlQuery &query) {
        if (scope.where != ListScope::Where::Projects) {
            requestRows(client, kind, scope, query, limit, finishWithRows);
            return;
        }
        requestEachProject(target, scope.paths, QStringLiteral("updated_at"), limit, false, mergeRequestSummary,
                           [=](const QString &project, GitLabClient::Done done) {
            client.projectMergeRequests(project, query, limit, std::move(done));
        });
    };

    if (kind == ListKind::MergeRequests && preset == QLatin1String("reviewRequested")) {
        client.currentUser([=](const ApiReply &user) {
            if (!user.ok()) {
                target->finish(statusFromReply(user));
                return;
            }
            const qint64 userId = user.json.object().value(QStringLiteral("id")).toInteger();
            rows(listQuery(kind, preset, userId, filter));
        });
        return;
    }
    rows(listQuery(kind, preset, 0, filter));
}

QString pipelineSummary(int total, int limit)
{
    if (total >= limit)
        return QCoreApplication::translate("gitlab", "%1+ pipelines").arg(limit);
    return total == 1 ? QCoreApplication::translate("gitlab", "1 pipeline")
                      : QCoreApplication::translate("gitlab", "%1 pipelines").arg(total);
}

// A status without a request: the configuration is not complete.
ItemStatus incompleteList(const QString &message)
{
    ItemStatus status;
    status.message = message;
    return status;
}

} // namespace

QStringList configuredProjects(const QJsonObject &config)
{
    QStringList projects;
    QSet<QString> seen;
    for (const QJsonValue &value : config.value(QStringLiteral("projects")).toArray()) {
        // projectPath() tidies a path given as the default.
        const QString project = projectPath({}, value);
        if (project.isEmpty() || seen.contains(project))
            continue;
        seen.insert(project);
        projects << project;
    }
    return projects;
}

QStringList filterDefault(const QJsonObject &config, const QString &column)
{
    QStringList values;
    for (const QJsonValue &value : config.value(column + QStringLiteral("Filter")).toArray()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty())
            values << text;
    }
    return values;
}

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

QString listDescription(ListKind kind, const QJsonObject &config, const QJsonValue &defaultProject, ListSpan span)
{
    const QString preset = config.value(QStringLiteral("preset")).toString();
    const bool mergeRequests = kind == ListKind::MergeRequests;
    QStringList parts;
    if (preset == QLatin1String("allOpen"))
        parts << QCoreApplication::translate("gitlab", "All open");
    else if (preset == QLatin1String("reviewRequested") && mergeRequests)
        parts << QCoreApplication::translate("gitlab", "Review requested from me");
    else if (preset == QLatin1String("createdByMe") || (mergeRequests && preset != QLatin1String("assignedToMe")))
        parts << QCoreApplication::translate("gitlab", "Created by me");
    else
        parts << QCoreApplication::translate("gitlab", "Assigned to me");

    const ListScope scope = readScope(config, defaultProject, span);
    if (scope.problem.isEmpty()) {
        switch (scope.where) {
        case ListScope::Where::Project:
            parts << scope.path;
            break;
        case ListScope::Where::Group:
            parts << QCoreApplication::translate("gitlab", "group %1").arg(scope.path);
            break;
        case ListScope::Where::All:
            parts << QCoreApplication::translate("gitlab", "all projects");
            break;
        case ListScope::Where::Projects:
            parts << scope.paths.join(QStringLiteral(", "));
            break;
        }
    }
    const QString filter = config.value(QStringLiteral("filter")).toString().trimmed();
    if (!filter.isEmpty())
        parts << filter;
    return parts.join(QStringLiteral(" · "));
}

QString pipelineListDescription(const QJsonObject &config, const QJsonValue &defaultProject, ListSpan span)
{
    const QString source = config.value(QStringLiteral("source")).toString();
    QStringList parts;
    if (source == QLatin1String("mine")) {
        parts << QCoreApplication::translate("gitlab", "Started by me");
    } else if (source == QLatin1String("branch")) {
        const QString branch = config.value(QStringLiteral("branch")).toString().trimmed();
        parts << QCoreApplication::translate("gitlab", "Of branch %1").arg(branch.isEmpty() ? kDefaultBranch : branch);
    } else if (source == QLatin1String("mergeRequest")) {
        parts << QCoreApplication::translate("gitlab", "Of merge request !%1").arg(itemNumber(config));
    } else {
        parts << QCoreApplication::translate("gitlab", "All pipelines");
    }
    if (span == ListSpan::Projects) {
        const QStringList projects = configuredProjects(config);
        if (!projects.isEmpty())
            parts << projects.join(QStringLiteral(", "));
        return parts.join(QStringLiteral(" · "));
    }
    const QString project = projectPath(config, defaultProject);
    if (!project.isEmpty())
        parts << project;
    return parts.join(QStringLiteral(" · "));
}

QUrl listWebUrl(ListKind kind, const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject,
                ListSpan span)
{
    const ListScope scope = readScope(config, defaultProject, span);
    if (!scope.problem.isEmpty() || baseUrl.isEmpty() || scope.where == ListScope::Where::Projects)
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
    case ListScope::Where::Projects:
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

void fetchMergeRequestMultiListStatus(StatusJob &job)
{
    fetchList(job, ListKind::MergeRequests, ListSpan::Projects);
}

void fetchPipelineListStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    const QString project = projectPath(config, job.projectSetting(kProjectSettingKey));
    if (project.isEmpty()) {
        job.finish(incompleteList(QCoreApplication::translate("gitlab", "Set the project in the item's settings.")));
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
            job.finish(incompleteList(
                QCoreApplication::translate("gitlab", "Set the merge request number in the item's settings.")));
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
        job.finish(incompleteList(QCoreApplication::translate("gitlab", "Set the source in the item's settings.")));
    }
}

void fetchPipelineMultiListStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    const QStringList projects = configuredProjects(config);
    if (projects.isEmpty()) {
        job.finish(incompleteList(severalProjectsProblem()));
        return;
    }
    // An absent source is the first choice of the form.
    const QString source = config.value(QStringLiteral("source")).toString();
    const int limit = listLimit(config);
    StatusJob *target = &job;
    const GitLabClient client(job.server(), job.owner());
    const auto pipelines = [=](const QUrlQuery &query) {
        requestEachProject(target, projects, QStringLiteral("created_at"), limit, true, pipelineSummary,
                           [=](const QString &project, GitLabClient::Done done) {
            client.pipelines(project, query, limit, std::move(done));
        });
    };

    if (source == QLatin1String("mine")) {
        client.currentUser([=](const ApiReply &user) {
            if (!user.ok()) {
                target->finish(statusFromReply(user));
                return;
            }
            // Encoded here, as a branch is, so the name reaches GitLab whole.
            const QString username = user.json.object().value(QStringLiteral("username")).toString();
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("username"), GitLabClient::encodePath(username));
            pipelines(query);
        });
    } else if (source == QLatin1String("branch")) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("ref"), GitLabClient::encodePath(configuredBranch(config)));
        pipelines(query);
    } else if (source.isEmpty() || source == QLatin1String("all")) {
        pipelines({});
    } else {
        job.finish(incompleteList(QCoreApplication::translate("gitlab", "Set the source in the item's settings.")));
    }
}

} // namespace mirante::modules::gitlab
