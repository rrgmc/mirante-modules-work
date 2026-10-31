// The status of GitHub's list types, and the requests behind it. See decisions
// 0067, 0071 and 0102, and README.md#lists.
#pragma once

#include <mirante/Status.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>

namespace mirante::modules::github {

enum class ListKind { PullRequests, Issues };

// The rows a list fetches, and those its view shows at a time, unless the item's
// limit and pageSize fields say otherwise.
constexpr int kDefaultListLimit = 100;
constexpr int kDefaultListPageSize = 50;

// The item's limit and pageSize fields. An absent value, or one below 1, gives
// the default.
int listLimit(const QJsonObject &config);
int listPageSize(const QJsonObject &config);

// Why a pull request or issue list's scope is not complete, such as "Set the
// repository in the item's settings.", or empty when it is. An absent scope is
// "repository", the first choice of the form, and an empty repository uses
// defaultRepository, the project's setting.
QString scopeProblem(const QJsonObject &config, const QJsonValue &defaultRepository);

// The search query of a pull request or issue list, from its configuration:
// "is:pr" or "is:issue" and "is:open"; then "repo:<repository>",
// "user:<owner>" or nothing for the scope "all"; then the preset's
// "author:@me", "review-requested:@me" or "assignee:@me", nothing for
// "allOpen", except "involves:@me" for "allOpen" with the scope "all"; then
// the trimmed filter, as typed. Joined by spaces. A preset the type does not
// have counts as its first one. Empty when scopeProblem() is not.
QString searchQuery(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository);

// The list's page on the site: <webBase>/<repository>/pulls?q=<query> for the
// repository scope, <webBase>/pulls?q=<query> otherwise, and "issues" for
// issues. The query is searchQuery()'s, percent-encoded. Empty when the scope
// is not complete.
QUrl listWebUrl(ListKind kind, const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository);

// A run list's page on the site: <webBase>/<repository>/actions, or
// <webBase>/<repository>/actions/workflows/<file> with a workflow. Empty
// without a repository.
QUrl runListWebUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository);

// What a list's configuration shows, in words, for the line above its table,
// such as "Assigned to me · acme/backend" or "Of pull request #42 ·
// acme/backend".
QString listDescription(ListKind kind, const QJsonObject &config, const QJsonValue &defaultRepository);
QString runListDescription(const QJsonObject &config, const QJsonValue &defaultRepository);

// The fetches of ItemType::fetchStatus(). data holds rows and total. The rows
// of a pull request or issue list are the search's "items"; those of a run
// list are its "workflow_runs".
void fetchPullRequestListStatus(StatusJob &job);
void fetchIssueListStatus(StatusJob &job);
void fetchRunListStatus(StatusJob &job);

} // namespace mirante::modules::github
