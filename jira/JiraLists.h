// The status of the Jira List type, and the query behind it. See decision 0067.
#pragma once

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

// The search's page on the site, <baseUrl>/issues/?jql=<query>. Empty without
// a base URL.
QUrl listWebUrl(const QUrl &baseUrl, const QString &jql);

// The item's "limit", the rows a list fetches, and its "pageSize", the rows its
// view shows at a time. Items made before these fields lack them, so an absent
// value, or one below 1, gives the default: 100 and 50.
int listLimit(const QJsonObject &config);
int listPageSize(const QJsonObject &config);

// The fetch of ItemType::fetchStatus(). data holds rows, the issues as Jira
// gives them, and total, their number.
void fetchListStatus(StatusJob &job);

} // namespace mirante::modules::jira
