// The status of the Jira List type, and the query behind it. See decision 0067.
#pragma once

#include "JiraSettings.h"

#include <mirante/Status.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>

#include <optional>

namespace mirante::modules::jira {

// The JQL of a preset. Nothing if a project preset has no project key, or if
// the "jql" preset has no query. The key is quoted, so a value cannot change
// the query.
std::optional<QString> listJql(const QString &preset, const QString &jql, const QString &projectKey);

// What a list shows, in words, for the line above its table, such as "My open
// issues", "Current sprint of OPS", or the query itself.
QString listDescription(const QString &preset, const QString &jql, const QString &projectKey);

// The JQL of the issues whose parent is this one: parent = "<key>", and on Jira
// Data Center also through the Epic Link field, which is how an epic holds its
// issues there. The key is quoted, as in listJql().
QString childrenJql(Deployment deployment, const QString &key);

// The search's page on the site, <baseUrl>/issues/?jql=<query>. Empty without
// a base URL.
QUrl listWebUrl(const QUrl &baseUrl, const QString &jql);

// The item's "limit", the rows a list fetches, and its "pageSize", the rows its
// view shows at a time. Items made before these fields lack them, so an absent
// value, or one below 1, gives the default: 100 and 50.
int listLimit(const QJsonObject &config);
int listPageSize(const QJsonObject &config);

// The fetch of ItemType::fetchStatus(). data holds rows, the issues as Jira
// gives them, and total, their number. With extra fields, a request to GET field
// first finds their ids, and data also holds names, the map from those ids to
// their names. If that request fails, the list has only its usual columns. See
// decision 0073.
void fetchListStatus(StatusJob &job);

} // namespace mirante::modules::jira
