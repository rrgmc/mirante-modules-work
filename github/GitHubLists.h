// The status of GitHub's list types, and the requests behind it. See decisions
// 0067, 0071 and 0102, and README.md#lists.
#pragma once

#include <mirante/Status.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace mirante::modules::github {

enum class ListKind { PullRequests, Issues };

// Where a list's repositories come from: the scope fields of a list type, or
// the repositories field of a type that lists several repositories, such as
// github.pr.multilist. See README.md#several-repositories.
enum class ListSpan { Scope, Repositories };

// The rows a list fetches, and those its view shows at a time, unless the item's
// limit and pageSize fields say otherwise.
constexpr int kDefaultListLimit = 100;
constexpr int kDefaultListPageSize = 50;

// The item's limit and pageSize fields. An absent value, or one below 1, gives
// the default.
int listLimit(const QJsonObject &config);
int listPageSize(const QJsonObject &config);

// The item's repositories field, "owner/repo" each, in their order. Each is
// trimmed, without slashes at its start and end. Empty and repeated ones are
// left out.
QStringList configuredRepositories(const QJsonObject &config);

// The values that a view filter of a list chooses at the start: the item's
// key <column>Filter, such as statusFilter, an array of texts, trimmed, without
// empty ones. Empty means "All". The view keeps the user's last choice there;
// the item's form does not show it.
QStringList filterDefault(const QJsonObject &config, const QString &column);
// config with the values of a view filter on a column, as filterDefault()
// reads them. Empty values remove the key.
QJsonObject withFilter(QJsonObject config, const QString &column, const QStringList &values);
// The keys of the view filters of the lists, which the view writes itself:
// repositoryFilter, statusFilter and workflowFilter. See decision 0081 in
// mirante-core.
QStringList listViewKeys();

// Why a pull request or issue list's scope is not complete, such as "Set the
// repository in the item's settings.", or empty when it is. An absent scope is
// "repository", the first choice of the form, and an empty repository uses
// defaultRepository, the project's setting. With ListSpan::Repositories the
// repositories field must name at least one.
QString scopeProblem(const QJsonObject &config, const QJsonValue &defaultRepository,
                     ListSpan span = ListSpan::Scope);

// The search query of a pull request or issue list, from its configuration:
// "is:pr" or "is:issue" and "is:open"; then "repo:<repository>",
// "user:<owner>" or nothing for the scope "all"; then the preset's
// "author:@me", "review-requested:@me" or "assignee:@me", nothing for
// "allOpen", except "involves:@me" for "allOpen" with the scope "all"; then
// the trimmed filter, as typed. Joined by spaces. A preset the type does not
// have counts as its first one. With ListSpan::Repositories, one
// "repo:<repository>" for each repository, which GitHub joins with OR. Empty
// when scopeProblem() is not.
QString searchQuery(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository,
                    ListSpan span = ListSpan::Scope);

// The list's page on the site: <webBase>/<repository>/pulls?q=<query> for the
// repository scope, <webBase>/pulls?q=<query> otherwise, and "issues" for
// issues. The query is searchQuery()'s, percent-encoded. Empty when the scope
// is not complete.
QUrl listWebUrl(ListKind kind, const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository,
                ListSpan span = ListSpan::Scope);

// A run list's page on the site: <webBase>/<repository>/actions, or
// <webBase>/<repository>/actions/workflows/<file> with a workflow. Empty
// without a repository, and with ListSpan::Repositories, which has no page.
QUrl runListWebUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository,
                   ListSpan span = ListSpan::Scope);

// What a list's configuration shows, in words, for the line above its table,
// such as "Assigned to me · acme/backend" or "Of pull request #42 ·
// acme/backend". Several repositories are joined by commas.
QString listDescription(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository,
                        ListSpan span = ListSpan::Scope);
QString runListDescription(const QJsonObject &config, const QJsonValue &defaultRepository,
                           ListSpan span = ListSpan::Scope);

// The fetches of ItemType::fetchStatus(). data holds rows and total. The rows
// of a pull request or issue list are the search's "items"; those of a run
// list are its "workflow_runs".
void fetchPullRequestListStatus(StatusJob &job);
void fetchIssueListStatus(StatusJob &job);
void fetchRunListStatus(StatusJob &job);

// The fetches of the types over several repositories. Pull requests come
// from one search. Runs come from one request per repository, each up to the
// limit; the rows are joined, sorted by created_at, newest first, and cut to
// the limit, since GitHub has no request for the runs of several
// repositories. The first repository that fails fails the list, with its
// name before the message. Their sources are "all", "mine" and "branch".
void fetchPullRequestMultiListStatus(StatusJob &job);
void fetchRunMultiListStatus(StatusJob &job);

} // namespace mirante::modules::github
