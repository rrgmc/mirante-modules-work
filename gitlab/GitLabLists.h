// The status of GitLab's list types, and the requests behind it. See decisions
// 0067 and 0071.
#pragma once

#include <mirante/Status.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

namespace mirante::modules::gitlab {

enum class ListKind { MergeRequests, Issues };

// Where a list's projects come from: the scope fields of a list type, or the
// projects field of a type that lists several projects, such as
// gitlab.mr.multilist. See README.md#several-projects.
enum class ListSpan { Scope, Projects };

// The key that a list over several projects adds to each pipeline row: the
// path of the project it came from, since GitLab's row has only its id.
inline const QString kRowProjectKey = QStringLiteral("project_path");

// The rows a list fetches, and those its view shows at a time, unless the item's
// limit and pageSize fields say otherwise.
constexpr int kDefaultListLimit = 100;
constexpr int kDefaultListPageSize = 50;

// The item's limit and pageSize fields. Items from before these fields lack them.
// An absent value, or one below 1, gives the default.
int listLimit(const QJsonObject &config);
int listPageSize(const QJsonObject &config);

// The item's projects field, "group/project" each, in their order, tidied as
// projectPath() does. Empty and repeated ones are left out.
QStringList configuredProjects(const QJsonObject &config);

// The values that a view filter of a list chooses at the start: the item's
// TextList field <column>Filter, such as statusFilter, trimmed, without empty
// ones. Empty means "All".
QStringList filterDefault(const QJsonObject &config, const QString &column);

// The API's parameters for a preset. userId is the current user, for
// "reviewRequested". The custom filter's parameters come last and replace
// those of the same name.
QUrlQuery listQuery(ListKind kind, const QString &preset, qint64 userId, const QString &filter);

// The list's page on the site, or an empty address if its scope is not
// complete. A list over several projects has no page.
QUrl listWebUrl(ListKind kind, const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject,
                ListSpan span = ListSpan::Scope);

// What a list's configuration shows, in words, for the line above its table, such
// as "Assigned to me · acme/backend" or "Of merge request !42 · acme/backend".
// Several projects are joined by commas.
QString listDescription(ListKind kind, const QJsonObject &config, const QJsonValue &defaultProject,
                        ListSpan span = ListSpan::Scope);
QString pipelineListDescription(const QJsonObject &config, const QJsonValue &defaultProject,
                                ListSpan span = ListSpan::Scope);

// The fetches of ItemType::fetchStatus(). data holds rows and total.
void fetchMergeRequestListStatus(StatusJob &job);
void fetchIssueListStatus(StatusJob &job);
void fetchPipelineListStatus(StatusJob &job);

// The fetches of the types over several projects. GitLab has no request for
// several projects, so each makes one per project, each up to the limit; the
// rows are joined, sorted newest first, by updated_at for merge requests and
// created_at for pipelines, and cut to the limit. A pipeline row also gets
// kRowProjectKey. The first project that fails fails the list, with its path
// before the message. The pipeline sources are "all", "mine" and "branch".
void fetchMergeRequestMultiListStatus(StatusJob &job);
void fetchPipelineMultiListStatus(StatusJob &job);

} // namespace mirante::modules::gitlab
