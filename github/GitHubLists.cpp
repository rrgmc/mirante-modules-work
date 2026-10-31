#include "GitHubLists.h"

#include "GitHubClient.h"
#include "GitHubSettings.h"
#include "GitHubUrls.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QStringList>
#include <QUrlQuery>

#include <utility>

namespace mirante::modules::github {

namespace {

// What a pull request or issue list's configuration points at. problem says
// what is missing, and is empty when the scope is complete.
struct ListScope
{
    enum class Where { Repository, Owner, All };

    Where where = Where::All;
    QString name;     // the repository, "owner/repo", or the owner
    QString problem;
};

QString withoutSlashes(QString text)
{
    text = text.trimmed();
    while (text.startsWith(QLatin1Char('/')))
        text.remove(0, 1);
    while (text.endsWith(QLatin1Char('/')))
        text.chop(1);
    return text;
}

// An absent scope is a repository, the first choice of the form.
ListScope readScope(const QJsonObject &config, const QJsonValue &defaultRepository)
{
    ListScope scope;
    const QString name = config.value(QStringLiteral("scope")).toString();
    if (name.isEmpty() || name == QLatin1String("repository")) {
        scope.where = ListScope::Where::Repository;
        scope.name = repositoryOf(config, defaultRepository);
        if (scope.name.isEmpty())
            scope.problem = QCoreApplication::translate("github", "Set the repository in the item's settings.");
    } else if (name == QLatin1String("owner")) {
        scope.where = ListScope::Where::Owner;
        scope.name = withoutSlashes(config.value(QStringLiteral("owner")).toString());
        if (scope.name.isEmpty())
            scope.problem = QCoreApplication::translate("github", "Set the owner in the item's settings.");
    } else if (name != QLatin1String("all")) {
        scope.problem = QCoreApplication::translate("github", "Set the scope in the item's settings.");
    }
    return scope;
}

// The preset, with one the type does not have replaced by the type's first.
QString effectivePreset(ListKind kind, const QJsonObject &config)
{
    const QString preset = config.value(QStringLiteral("preset")).toString();
    if (kind == ListKind::PullRequests) {
        if (preset == QLatin1String("reviewRequested") || preset == QLatin1String("assignedToMe")
            || preset == QLatin1String("allOpen"))
            return preset;
        return QStringLiteral("createdByMe");
    }
    if (preset == QLatin1String("createdByMe") || preset == QLatin1String("allOpen"))
        return preset;
    return QStringLiteral("assignedToMe");
}

// The query of a complete scope.
QString queryOf(ListKind kind, const ListScope &scope, const QJsonObject &config)
{
    QStringList parts;
    parts << (kind == ListKind::PullRequests ? QStringLiteral("is:pr") : QStringLiteral("is:issue"))
          << QStringLiteral("is:open");
    switch (scope.where) {
    case ListScope::Where::Repository:
        parts << QStringLiteral("repo:") + scope.name;
        break;
    case ListScope::Where::Owner:
        parts << QStringLiteral("user:") + scope.name;
        break;
    case ListScope::Where::All:
        break;
    }

    const QString preset = effectivePreset(kind, config);
    if (preset == QLatin1String("createdByMe"))
        parts << QStringLiteral("author:@me");
    else if (preset == QLatin1String("reviewRequested"))
        parts << QStringLiteral("review-requested:@me");
    else if (preset == QLatin1String("assignedToMe"))
        parts << QStringLiteral("assignee:@me");
    else if (scope.where == ListScope::Where::All)
        // All open pull requests of all of GitHub are not a useful list.
        parts << QStringLiteral("involves:@me");

    const QString filter = config.value(QStringLiteral("filter")).toString().trimmed();
    if (!filter.isEmpty())
        parts << filter;
    return parts.join(QLatin1Char(' '));
}

// A positive whole number, which a file edited by hand may hold as text, or
// fallback.
int positiveNumber(const QJsonValue &value, int fallback)
{
    const int number = value.isString() ? value.toString().trimmed().toInt() : value.toInt();
    return number >= 1 ? number : fallback;
}

// A list that reaches its limit may have more.
QString pullRequestSummary(int total, int limit)
{
    if (total >= limit)
        return QCoreApplication::translate("github", "%n+ pull request(s)", nullptr, limit);
    return QCoreApplication::translate("github", "%n pull request(s)", nullptr, total);
}

QString issueSummary(int total, int limit)
{
    if (total >= limit)
        return QCoreApplication::translate("github", "%n+ issue(s)", nullptr, limit);
    return QCoreApplication::translate("github", "%n issue(s)", nullptr, total);
}

QString runSummary(int total, int limit)
{
    if (total >= limit)
        return QCoreApplication::translate("github", "%n+ run(s)", nullptr, limit);
    return QCoreApplication::translate("github", "%n run(s)", nullptr, total);
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

// A status without a request: the configuration is not complete.
ItemStatus incompleteList(const QString &message)
{
    ItemStatus status;
    status.message = message;
    return status;
}

// The rows of an answer that wraps them under key, and the status they give.
GitHubClient::Done finishWithRows(StatusJob *job, const QString &key, int limit,
                                  QString (*summary)(int total, int limit))
{
    return [job, key, limit, summary](const ApiReply &reply) {
        if (!reply.ok()) {
            job->finish(statusFromReply(reply));
            return;
        }
        const QJsonArray rows = reply.json.object().value(key).toArray();
        job->finish(listStatus(summary(int(rows.size()), limit), rows));
    };
}

void fetchSearchList(StatusJob &job, ListKind kind)
{
    const QJsonObject config = job.config();
    const ListScope scope = readScope(config, job.projectSetting(kProjectSettingKey));
    if (!scope.problem.isEmpty()) {
        job.finish(incompleteList(scope.problem));
        return;
    }
    const int limit = listLimit(config);
    const GitHubClient client(job.server(), job.owner());
    client.searchIssues(queryOf(kind, scope, config), limit,
                        finishWithRows(&job, QStringLiteral("items"), limit,
                                       kind == ListKind::PullRequests ? pullRequestSummary : issueSummary));
}

// A query of one parameter, its value encoded, so that "+" and "&" in a
// branch reach GitHub whole.
QUrlQuery queryWith(const QString &name, const QString &value)
{
    QUrlQuery query;
    query.addQueryItem(name, GitHubClient::encode(value));
    return query;
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

QString scopeProblem(const QJsonObject &config, const QJsonValue &defaultRepository)
{
    return readScope(config, defaultRepository).problem;
}

QString searchQuery(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    const ListScope scope = readScope(config, defaultRepository);
    if (!scope.problem.isEmpty())
        return {};
    return queryOf(kind, scope, config);
}

QUrl listWebUrl(ListKind kind, const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    const ListScope scope = readScope(config, defaultRepository);
    if (!scope.problem.isEmpty() || webBase.isEmpty())
        return {};

    const QString page = kind == ListKind::PullRequests ? QStringLiteral("pulls") : QStringLiteral("issues");
    QUrl url = webPageUrl(webBase, scope.where == ListScope::Where::Repository ? scope.name + QLatin1Char('/') + page
                                                                                : page);
    url.setQuery(QStringLiteral("q=") + GitHubClient::encode(queryOf(kind, scope, config)));
    return url;
}

QUrl runListWebUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    const QString repository = repositoryOf(config, defaultRepository);
    if (repository.isEmpty() || webBase.isEmpty())
        return {};
    const QString workflow = configuredWorkflow(config);
    QString path = repository + QStringLiteral("/actions");
    if (!workflow.isEmpty())
        path += QStringLiteral("/workflows/") + workflow;
    return webPageUrl(webBase, path);
}

QString listDescription(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository)
{
    const QString preset = effectivePreset(kind, config);
    QStringList parts;
    if (preset == QLatin1String("allOpen"))
        parts << QCoreApplication::translate("github", "All open");
    else if (preset == QLatin1String("reviewRequested"))
        parts << QCoreApplication::translate("github", "Review requested from me");
    else if (preset == QLatin1String("createdByMe"))
        parts << QCoreApplication::translate("github", "Created by me");
    else
        parts << QCoreApplication::translate("github", "Assigned to me");

    const ListScope scope = readScope(config, defaultRepository);
    if (scope.problem.isEmpty()) {
        switch (scope.where) {
        case ListScope::Where::Repository:
            parts << scope.name;
            break;
        case ListScope::Where::Owner:
            parts << QCoreApplication::translate("github", "owner %1").arg(scope.name);
            break;
        case ListScope::Where::All:
            parts << QCoreApplication::translate("github", "all repositories");
            break;
        }
    }
    const QString filter = config.value(QStringLiteral("filter")).toString().trimmed();
    if (!filter.isEmpty())
        parts << filter;
    return parts.join(QStringLiteral(" · "));
}

QString runListDescription(const QJsonObject &config, const QJsonValue &defaultRepository)
{
    const QString source = config.value(QStringLiteral("source")).toString();
    QStringList parts;
    if (source == QLatin1String("mine")) {
        parts << QCoreApplication::translate("github", "Started by me");
    } else if (source == QLatin1String("branch")) {
        const QString branch = configuredBranch(config);
        parts << (branch.isEmpty() ? QCoreApplication::translate("github", "Of the default branch")
                                   : QCoreApplication::translate("github", "Of branch %1").arg(branch));
    } else if (source == QLatin1String("pullRequest")) {
        parts << QCoreApplication::translate("github", "Of pull request #%1").arg(itemNumber(config));
    } else {
        parts << QCoreApplication::translate("github", "All runs");
    }
    const QString workflow = configuredWorkflow(config);
    if (!workflow.isEmpty())
        parts << workflow;
    const QString repository = repositoryOf(config, defaultRepository);
    if (!repository.isEmpty())
        parts << repository;
    return parts.join(QStringLiteral(" · "));
}

void fetchPullRequestListStatus(StatusJob &job)
{
    fetchSearchList(job, ListKind::PullRequests);
}

void fetchIssueListStatus(StatusJob &job)
{
    fetchSearchList(job, ListKind::Issues);
}

void fetchRunListStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    const QString repository = repositoryOf(config, job.projectSetting(kProjectSettingKey));
    if (repository.isEmpty()) {
        job.finish(incompleteList(QCoreApplication::translate("github", "Set the repository in the item's settings.")));
        return;
    }

    const int limit = listLimit(config);
    const QString workflow = configuredWorkflow(config);
    StatusJob *target = &job;
    const GitHubClient client(job.server(), job.owner());
    const GitHubClient::Done done = finishWithRows(target, QStringLiteral("workflow_runs"), limit, runSummary);
    const auto runs = [=](const QUrlQuery &query) { client.workflowRuns(repository, workflow, query, limit, done); };

    // An absent source is the first choice of the form.
    const QString source = config.value(QStringLiteral("source")).toString();
    if (source.isEmpty() || source == QLatin1String("all")) {
        runs({});
    } else if (source == QLatin1String("mine")) {
        client.currentUser([=](const ApiReply &user) {
            if (!user.ok()) {
                target->finish(statusFromReply(user));
                return;
            }
            runs(queryWith(QStringLiteral("actor"), user.json.object().value(QStringLiteral("login")).toString()));
        });
    } else if (source == QLatin1String("branch")) {
        const QString branch = configuredBranch(config);
        if (!branch.isEmpty()) {
            runs(queryWith(QStringLiteral("branch"), branch));
            return;
        }
        client.repository(repository, [=](const ApiReply &reply) {
            if (!reply.ok()) {
                target->finish(statusFromReply(reply));
                return;
            }
            const QString defaultBranch = reply.json.object().value(QStringLiteral("default_branch")).toString();
            if (defaultBranch.isEmpty()) {
                ItemStatus status;
                status.state = ItemStatus::State::Error;
                status.message =
                    QCoreApplication::translate("github", "The repository's default branch could not be read.");
                target->finish(status);
                return;
            }
            runs(queryWith(QStringLiteral("branch"), defaultBranch));
        });
    } else if (source == QLatin1String("pullRequest")) {
        const qint64 number = itemNumber(config);
        if (number == 0) {
            job.finish(incompleteList(
                QCoreApplication::translate("github", "Set the pull request number in the item's settings.")));
            return;
        }
        client.pullRequest(repository, number, [=](const ApiReply &reply) {
            if (!reply.ok()) {
                target->finish(statusFromReply(reply));
                return;
            }
            const QString head = reply.json.object()
                                     .value(QStringLiteral("head"))
                                     .toObject()
                                     .value(QStringLiteral("ref"))
                                     .toString();
            QUrlQuery query = queryWith(QStringLiteral("branch"), head);
            query.addQueryItem(QStringLiteral("event"), QStringLiteral("pull_request"));
            runs(query);
        });
    } else {
        job.finish(incompleteList(QCoreApplication::translate("github", "Set the source in the item's settings.")));
    }
}

} // namespace mirante::modules::github
