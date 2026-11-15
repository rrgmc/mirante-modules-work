#include "GitHubLists.h"

#include "GitHubClient.h"
#include "GitHubSettings.h"
#include "GitHubUrls.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrlQuery>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace mirante::modules::github {

namespace {

// What a pull request or issue list's configuration points at. problem says
// what is missing, and is empty when the scope is complete.
struct ListScope
{
    enum class Where { Repository, Owner, All, Repositories, Organizations };

    Where where = Where::All;
    QString name;      // Repository: "owner/repo"
    // Repositories: "owner/repo" each. Owner: the owners. Organizations: the
    // logins, once the fetch has read them.
    QStringList names;
    QString problem;
};

const QString kMyOrganizations = QStringLiteral("myOrganizations");

QString withoutSlashes(QString text)
{
    text = text.trimmed();
    while (text.startsWith(QLatin1Char('/')))
        text.remove(0, 1);
    while (text.endsWith(QLatin1Char('/')))
        text.chop(1);
    return text;
}

QString severalRepositoriesProblem()
{
    return QCoreApplication::translate("github", "Set the repositories in the item's settings.");
}

// An absent scope is a repository, the first choice of the form.
ListScope readScope(const QJsonObject &config, const QJsonValue &defaultRepository, ListSpan span)
{
    ListScope scope;
    if (span == ListSpan::Repositories) {
        scope.where = ListScope::Where::Repositories;
        scope.names = configuredRepositories(config);
        if (scope.names.isEmpty())
            scope.problem = severalRepositoriesProblem();
        return scope;
    }
    const QString name = config.value(QStringLiteral("scope")).toString();
    if (name.isEmpty() || name == QLatin1String("repository")) {
        scope.where = ListScope::Where::Repository;
        scope.name = repositoryOf(config, defaultRepository);
        if (scope.name.isEmpty())
            scope.problem = QCoreApplication::translate("github", "Set the repository in the item's settings.");
    } else if (name == QLatin1String("owner")) {
        scope.where = ListScope::Where::Owner;
        scope.names = configuredOwners(config);
        if (scope.names.isEmpty())
            scope.problem = QCoreApplication::translate("github", "Set the owner in the item's settings.");
    } else if (name == kMyOrganizations) {
        scope.where = ListScope::Where::Organizations;
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
    case ListScope::Where::Organizations:
        for (const QString &owner : scope.names)
            parts << QStringLiteral("user:") + owner;
        break;
    case ListScope::Where::All:
        break;
    case ListScope::Where::Repositories:
        for (const QString &repository : scope.names)
            parts << QStringLiteral("repo:") + repository;
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

// "owner acme", or "owners acme, labs" for several.
QString ownersDescription(const QStringList &owners)
{
    if (owners.size() == 1)
        return QCoreApplication::translate("github", "owner %1").arg(owners.first());
    return QCoreApplication::translate("github", "owners %1").arg(owners.join(QStringLiteral(", ")));
}

// An Error with the message of a GraphQL answer's first error, or fallback
// when it gives none.
ItemStatus graphqlError(const QJsonObject &answer, const QString &fallback)
{
    const QString message =
        answer.value(QStringLiteral("errors")).toArray().at(0).toObject().value(QStringLiteral("message")).toString();
    ItemStatus status;
    status.state = ItemStatus::State::Error;
    status.message = message.isEmpty() ? fallback : message;
    return status;
}

// Reads the organizations of the token's user, then calls next with their
// logins. A failed reply, an answer without the user, or a user without
// organizations finishes job instead.
void askOrganizations(const GitHubClient &client, StatusJob *job, std::function<void(const QStringList &)> next)
{
    client.graphql(viewerOrganizationsQuery(), QJsonObject(), [job, next](const ApiReply &reply) {
        if (!reply.ok()) {
            job->finish(statusFromReply(reply));
            return;
        }
        const QJsonObject answer = reply.json.object();
        if (!answer.value(QStringLiteral("data")).toObject().value(QStringLiteral("viewer")).isObject()) {
            job->finish(graphqlError(
                answer, QCoreApplication::translate("github", "GitHub did not give your organizations.")));
            return;
        }
        const QStringList logins = organizationLogins(answer);
        if (logins.isEmpty()) {
            job->finish(incompleteList(
                QCoreApplication::translate("github", "You belong to no organization on this server.")));
            return;
        }
        next(logins);
    });
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

void fetchSearchList(StatusJob &job, ListKind kind, ListSpan span)
{
    const QJsonObject config = job.config();
    const ListScope scope = readScope(config, job.projectSetting(kProjectSettingKey), span);
    if (!scope.problem.isEmpty()) {
        job.finish(incompleteList(scope.problem));
        return;
    }
    const int limit = listLimit(config);
    const GitHubClient client(job.server(), job.owner());
    const GitHubClient::Done done = finishWithRows(&job, QStringLiteral("items"), limit,
                                                   kind == ListKind::PullRequests ? pullRequestSummary : issueSummary);
    if (scope.where != ListScope::Where::Organizations) {
        client.searchIssues(queryOf(kind, scope, config), limit, done);
        return;
    }
    // The organizations first, so that the search covers each of them.
    StatusJob *target = &job;
    askOrganizations(client, target, [client, scope, kind, config, limit, done](const QStringList &logins) {
        ListScope found = scope;
        found.names = logins;
        client.searchIssues(queryOf(kind, found, config), limit, done);
    });
}

// A query of one parameter, its value encoded, so that "+" and "&" in a
// branch reach GitHub whole.
QUrlQuery queryWith(const QString &name, const QString &value)
{
    QUrlQuery query;
    query.addQueryItem(name, GitHubClient::encode(value));
    return query;
}

// The runs of several repositories, as they come. The first failure finishes
// the job; the last answer joins the rows.
struct RunAnswers
{
    int waiting = 0;
    bool finished = false;
    QJsonArray rows;
};

// created_at is an ISO date in UTC, so its text sorts as the date does.
QJsonArray newestRuns(const QJsonArray &rows, int limit)
{
    QList<QJsonObject> runs;
    for (const QJsonValue &row : rows)
        runs << row.toObject();
    std::stable_sort(runs.begin(), runs.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("created_at")).toString() > b.value(QStringLiteral("created_at")).toString();
    });
    QJsonArray newest;
    for (int index = 0; index < runs.size() && index < limit; ++index)
        newest << runs.at(index);
    return newest;
}

// A status of one of several repositories, with the repository's name before
// its message.
ItemStatus ofRepository(const QString &repository, ItemStatus status)
{
    status.message = repository + QStringLiteral(": ") + status.message;
    return status;
}

} // namespace

QStringList configuredRepositories(const QJsonObject &config)
{
    QStringList repositories;
    QSet<QString> seen;
    for (const QJsonValue &value : config.value(QStringLiteral("repositories")).toArray()) {
        const QString repository = withoutSlashes(value.toString());
        if (repository.isEmpty() || seen.contains(repository))
            continue;
        seen.insert(repository);
        repositories << repository;
    }
    return repositories;
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

QJsonObject withFilter(QJsonObject config, const QString &column, const QStringList &values)
{
    const QString key = column + QStringLiteral("Filter");
    if (values.isEmpty())
        config.remove(key);
    else
        config.insert(key, QJsonArray::fromStringList(values));
    return config;
}

QStringList listViewKeys()
{
    return {QStringLiteral("repositoryFilter"), QStringLiteral("statusFilter"), QStringLiteral("workflowFilter")};
}

int listLimit(const QJsonObject &config)
{
    return positiveNumber(config.value(QStringLiteral("limit")), kDefaultListLimit);
}

int listPageSize(const QJsonObject &config)
{
    return positiveNumber(config.value(QStringLiteral("pageSize")), kDefaultListPageSize);
}

QString scopeProblem(const QJsonObject &config, const QJsonValue &defaultRepository, ListSpan span)
{
    return readScope(config, defaultRepository, span).problem;
}

QString searchQuery(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository, ListSpan span,
                    const QStringList &organizations)
{
    ListScope scope = readScope(config, defaultRepository, span);
    if (!scope.problem.isEmpty())
        return {};
    if (scope.where == ListScope::Where::Organizations) {
        // Without the organizations, the query would search all of GitHub.
        if (organizations.isEmpty())
            return {};
        scope.names = organizations;
    }
    return queryOf(kind, scope, config);
}

QUrl listWebUrl(ListKind kind, const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository,
                ListSpan span)
{
    const ListScope scope = readScope(config, defaultRepository, span);
    if (!scope.problem.isEmpty() || webBase.isEmpty() || scope.where == ListScope::Where::Organizations)
        return {};

    const QString page = kind == ListKind::PullRequests ? QStringLiteral("pulls") : QStringLiteral("issues");
    QUrl url = webPageUrl(webBase, scope.where == ListScope::Where::Repository ? scope.name + QLatin1Char('/') + page
                                                                                : page);
    url.setQuery(QStringLiteral("q=") + GitHubClient::encode(queryOf(kind, scope, config)));
    return url;
}

QUrl runListWebUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository,
                   ListSpan span)
{
    if (span == ListSpan::Repositories)
        return {};
    const QString repository = repositoryOf(config, defaultRepository);
    if (repository.isEmpty() || webBase.isEmpty())
        return {};
    const QString workflow = configuredWorkflow(config);
    QString path = repository + QStringLiteral("/actions");
    if (!workflow.isEmpty())
        path += QStringLiteral("/workflows/") + workflow;
    return webPageUrl(webBase, path);
}

QString listDescription(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository,
                        ListSpan span)
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

    const ListScope scope = readScope(config, defaultRepository, span);
    if (scope.problem.isEmpty()) {
        switch (scope.where) {
        case ListScope::Where::Repository:
            parts << scope.name;
            break;
        case ListScope::Where::Owner:
            parts << ownersDescription(scope.names);
            break;
        case ListScope::Where::Organizations:
            parts << QCoreApplication::translate("github", "my organizations");
            break;
        case ListScope::Where::All:
            parts << QCoreApplication::translate("github", "all repositories");
            break;
        case ListScope::Where::Repositories:
            parts << scope.names.join(QStringLiteral(", "));
            break;
        }
    }
    const QString filter = config.value(QStringLiteral("filter")).toString().trimmed();
    if (!filter.isEmpty())
        parts << filter;
    return parts.join(QStringLiteral(" · "));
}

QString runListDescription(const QJsonObject &config, const QJsonValue &defaultRepository, ListSpan span)
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
    if (span == ListSpan::Repositories) {
        const QStringList repositories = configuredRepositories(config);
        if (!repositories.isEmpty())
            parts << repositories.join(QStringLiteral(", "));
        return parts.join(QStringLiteral(" · "));
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
    fetchSearchList(job, ListKind::PullRequests, ListSpan::Scope);
}

void fetchIssueListStatus(StatusJob &job)
{
    fetchSearchList(job, ListKind::Issues, ListSpan::Scope);
}

void fetchPullRequestMultiListStatus(StatusJob &job)
{
    fetchSearchList(job, ListKind::PullRequests, ListSpan::Repositories);
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

void fetchRunMultiListStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    const QStringList repositories = configuredRepositories(config);
    if (repositories.isEmpty()) {
        job.finish(incompleteList(severalRepositoriesProblem()));
        return;
    }
    // An absent source is the first choice of the form.
    const QString source = config.value(QStringLiteral("source")).toString();
    if (!source.isEmpty() && source != QLatin1String("all") && source != QLatin1String("mine")
        && source != QLatin1String("branch")) {
        job.finish(incompleteList(QCoreApplication::translate("github", "Set the source in the item's settings.")));
        return;
    }

    const int limit = listLimit(config);
    StatusJob *target = &job;
    const GitHubClient client(job.server(), job.owner());
    auto answers = std::make_shared<RunAnswers>();
    answers->waiting = int(repositories.size());
    const auto fail = [=](const ItemStatus &status) {
        answers->finished = true;
        target->finish(status);
    };
    // The runs of one repository, joined to the others' when the last comes.
    const auto runs = [=](const QString &repository, const QUrlQuery &query) {
        client.workflowRuns(repository, {}, query, limit, [=](const ApiReply &reply) {
            if (answers->finished)
                return;
            if (!reply.ok()) {
                fail(ofRepository(repository, statusFromReply(reply)));
                return;
            }
            for (const QJsonValue &row : reply.json.object().value(QStringLiteral("workflow_runs")).toArray())
                answers->rows << row;
            if (--answers->waiting > 0)
                return;
            answers->finished = true;
            const QJsonArray rows = newestRuns(answers->rows, limit);
            target->finish(listStatus(runSummary(int(rows.size()), limit), rows));
        });
    };

    if (source == QLatin1String("mine")) {
        client.currentUser([=](const ApiReply &user) {
            if (!user.ok()) {
                fail(statusFromReply(user));
                return;
            }
            const QUrlQuery query =
                queryWith(QStringLiteral("actor"), user.json.object().value(QStringLiteral("login")).toString());
            for (const QString &repository : repositories)
                runs(repository, query);
        });
    } else if (source == QLatin1String("branch")) {
        const QString branch = configuredBranch(config);
        for (const QString &repository : repositories) {
            if (!branch.isEmpty()) {
                runs(repository, queryWith(QStringLiteral("branch"), branch));
                continue;
            }
            // Each repository's own default branch.
            client.repository(repository, [=](const ApiReply &reply) {
                if (answers->finished)
                    return;
                if (!reply.ok()) {
                    fail(ofRepository(repository, statusFromReply(reply)));
                    return;
                }
                const QString defaultBranch = reply.json.object().value(QStringLiteral("default_branch")).toString();
                if (defaultBranch.isEmpty()) {
                    ItemStatus status;
                    status.state = ItemStatus::State::Error;
                    status.message =
                        QCoreApplication::translate("github", "The repository's default branch could not be read.");
                    fail(ofRepository(repository, status));
                    return;
                }
                runs(repository, queryWith(QStringLiteral("branch"), defaultBranch));
            });
        }
    } else {
        for (const QString &repository : repositories)
            runs(repository, {});
    }
}

QStringList configuredOwners(const QJsonObject &config)
{
    QStringList owners;
    QSet<QString> seen;
    const QStringList parts = config.value(QStringLiteral("owner"))
                                  .toString()
                                  .split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        QString owner = withoutSlashes(part);
        while (owner.startsWith(QLatin1Char('@')))
            owner.remove(0, 1);
        owner = withoutSlashes(owner);
        if (owner.isEmpty() || seen.contains(owner.toLower()))
            continue;
        seen.insert(owner.toLower());
        owners << owner;
    }
    return owners;
}

bool coversMyOrganizations(const QJsonObject &config)
{
    return config.value(QStringLiteral("scope")).toString() == kMyOrganizations;
}

QString viewerOrganizationsQuery()
{
    return QStringLiteral("query { viewer { organizations(first: 100) { nodes { login } } } }");
}

QStringList organizationLogins(const QJsonObject &answer)
{
    QStringList logins;
    const QJsonArray nodes = answer.value(QStringLiteral("data"))
                                 .toObject()
                                 .value(QStringLiteral("viewer"))
                                 .toObject()
                                 .value(QStringLiteral("organizations"))
                                 .toObject()
                                 .value(QStringLiteral("nodes"))
                                 .toArray();
    for (const QJsonValue &node : nodes) {
        const QString login = node.toObject().value(QStringLiteral("login")).toString();
        if (!login.isEmpty())
            logins << login;
    }
    return logins;
}

QString ciHealthQuery(int owners)
{
    QString variables = QStringLiteral("$first: Int!, $isFork: Boolean, $isArchived: Boolean");
    QString fields;
    for (int index = 0; index < owners; ++index) {
        variables += QStringLiteral(", $o%1: String!").arg(index);
        fields += QStringLiteral(" o%1: repositoryOwner(login: $o%1) { ...Repositories }").arg(index);
    }
    // The newest commit of the default branch, its checks as in the pull
    // request list, and enough of the repository for a row.
    return QStringLiteral("query(") + variables + QStringLiteral(") {") + fields
        + QStringLiteral(" }"
                         " fragment Repositories on RepositoryOwner {"
                         " repositories(first: $first, orderBy: {field: PUSHED_AT, direction: DESC},"
                         " isFork: $isFork, isArchived: $isArchived) {"
                         " nodes {"
                         " nameWithOwner url pushedAt isArchived isFork"
                         " defaultBranchRef { name target { ... on Commit {"
                         " oid messageHeadline committedDate url"
                         " statusCheckRollup { contexts(first: 100) { nodes {"
                         " __typename"
                         " ... on CheckRun { status conclusion }"
                         " ... on StatusContext { state }"
                         " } } }"
                         " } } }"
                         " }"
                         " }"
                         " }");
}

QJsonObject ciHealthVariables(const QJsonObject &config, const QStringList &owners)
{
    // Absent means off, as the form's default.
    const auto onlyWhenOff = [&config](const QString &key) {
        return config.value(key).toBool(false) ? QJsonValue(QJsonValue::Null) : QJsonValue(false);
    };
    QJsonObject variables{{QStringLiteral("first"), std::min(listLimit(config), kMaxCiHealthRows)},
                          {QStringLiteral("isFork"), onlyWhenOff(QStringLiteral("includeForks"))},
                          {QStringLiteral("isArchived"), onlyWhenOff(QStringLiteral("includeArchived"))}};
    for (int index = 0; index < owners.size(); ++index)
        variables.insert(QStringLiteral("o%1").arg(index), owners.at(index));
    return variables;
}

QList<QStringList> ciHealthBatches(const QStringList &owners, int first)
{
    const int size = std::max(1, kCiHealthRowBudget / std::max(1, first));
    QList<QStringList> batches;
    for (int index = 0; index < owners.size(); index += size)
        batches << owners.mid(index, size);
    return batches;
}

QJsonArray newestRepositories(const QJsonArray &rows, int limit)
{
    QList<QJsonObject> repositories;
    for (const QJsonValue &row : rows)
        repositories << row.toObject();
    // pushedAt is an ISO date in UTC, so its text sorts as the date does, and
    // a missing one is the empty text, which sorts last.
    std::stable_sort(repositories.begin(), repositories.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("pushedAt")).toString() > b.value(QStringLiteral("pushedAt")).toString();
    });
    QJsonArray newest;
    for (int index = 0; index < repositories.size() && index < limit; ++index)
        newest << repositories.at(index);
    return newest;
}

ChecksState repositoryChecks(const QJsonObject &repository)
{
    const QJsonObject rollup = repository.value(QStringLiteral("defaultBranchRef"))
                                   .toObject()
                                   .value(QStringLiteral("target"))
                                   .toObject()
                                   .value(QStringLiteral("statusCheckRollup"))
                                   .toObject();
    const HeadChecks head = headChecksOfRollup(rollup);
    return checksState(head.checkRuns, head.combinedStatus);
}

QString ciHealthSummary(const QJsonArray &rows, int limit)
{
    const int total = int(rows.size());
    QString summary = total >= limit
        ? QCoreApplication::translate("github", "%n+ repository(s)", nullptr, limit)
        : QCoreApplication::translate("github", "%n repository(s)", nullptr, total);
    int failing = 0;
    for (const QJsonValue &row : rows) {
        if (repositoryChecks(row.toObject()) == ChecksState::Failed)
            ++failing;
    }
    if (failing > 0)
        summary += QStringLiteral(" · ") + QCoreApplication::translate("github", "%n failing", nullptr, failing);
    return summary;
}

QUrl ciHealthWebUrl(const QUrl &webBase, const QJsonObject &config)
{
    if (coversMyOrganizations(config))
        return {};
    const QStringList owners = configuredOwners(config);
    if (owners.size() != 1)
        return {};
    return webPageUrl(webBase, owners.first());
}

QString ciHealthDescription(const QJsonObject &config)
{
    QStringList parts;
    if (coversMyOrganizations(config)) {
        parts << QCoreApplication::translate("github", "my organizations");
    } else {
        const QStringList owners = configuredOwners(config);
        if (!owners.isEmpty())
            parts << ownersDescription(owners);
    }
    if (!config.value(QStringLiteral("includeForks")).toBool(false))
        parts << QCoreApplication::translate("github", "without forks");
    if (!config.value(QStringLiteral("includeArchived")).toBool(false))
        parts << QCoreApplication::translate("github", "without archived");
    return parts.join(QStringLiteral(" · "));
}

namespace {

// The answers of the requests of the CI health list, as they come. The first
// failure finishes the job; the last answer joins the rows.
struct CiHealthAnswers
{
    int waiting = 0;
    bool finished = false;
    QJsonArray rows;
};

// An owner GitHub did not give: the message of the error at its alias, else
// one of its own. GitHub answers an unknown login with a null owner.
ItemStatus missingOwner(const QJsonObject &answer, const QString &alias, const QString &owner)
{
    ItemStatus status;
    status.state = ItemStatus::State::Error;
    for (const QJsonValue &value : answer.value(QStringLiteral("errors")).toArray()) {
        const QJsonObject error = value.toObject();
        if (error.value(QStringLiteral("path")).toArray().at(0).toString() == alias) {
            status.message = error.value(QStringLiteral("message")).toString();
            if (!status.message.isEmpty())
                return status;
        }
    }
    status.message = QCoreApplication::translate("github", "GitHub has no owner %1.").arg(owner);
    return status;
}

// The repositories of owners, in one request per batch, all at once.
void askCiHealth(const GitHubClient &client, StatusJob *job, const QJsonObject &config, const QStringList &owners,
                 int limit)
{
    const QList<QStringList> batches = ciHealthBatches(owners, limit);
    auto answers = std::make_shared<CiHealthAnswers>();
    answers->waiting = int(batches.size());
    for (const QStringList &batch : batches) {
        client.graphql(ciHealthQuery(int(batch.size())), ciHealthVariables(config, batch),
                       [job, answers, batch, limit](const ApiReply &reply) {
            if (answers->finished)
                return;
            const auto fail = [&answers, job](const ItemStatus &status) {
                answers->finished = true;
                job->finish(status);
            };
            if (!reply.ok()) {
                fail(statusFromReply(reply));
                return;
            }
            const QJsonObject answer = reply.json.object();
            const QJsonObject data = answer.value(QStringLiteral("data")).toObject();
            for (int index = 0; index < batch.size(); ++index) {
                const QString alias = QStringLiteral("o%1").arg(index);
                const QJsonValue found = data.value(alias);
                if (!found.isObject()) {
                    fail(missingOwner(answer, alias, batch.at(index)));
                    return;
                }
                const QJsonArray nodes = found.toObject()
                                             .value(QStringLiteral("repositories"))
                                             .toObject()
                                             .value(QStringLiteral("nodes"))
                                             .toArray();
                for (const QJsonValue &node : nodes)
                    answers->rows.append(node);
            }
            if (--answers->waiting > 0)
                return;
            answers->finished = true;
            const QJsonArray newest = newestRepositories(answers->rows, limit);
            job->finish(listStatus(ciHealthSummary(newest, limit), newest));
        });
    }
}

} // namespace

void fetchCiHealthStatus(StatusJob &job)
{
    const QJsonObject config = job.config();
    const int limit = std::min(listLimit(config), kMaxCiHealthRows);
    StatusJob *target = &job;
    const GitHubClient client(job.server(), job.owner());
    if (coversMyOrganizations(config)) {
        askOrganizations(client, target, [client, target, config, limit](const QStringList &logins) {
            askCiHealth(client, target, config, logins, limit);
        });
        return;
    }
    const QStringList owners = configuredOwners(config);
    if (owners.isEmpty()) {
        job.finish(incompleteList(QCoreApplication::translate("github", "Set the owner in the item's settings.")));
        return;
    }
    askCiHealth(client, target, config, owners, limit);
}

} // namespace mirante::modules::github
