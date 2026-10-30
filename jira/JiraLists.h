// The status of the Jira List type, and the query behind it. See decision 0067.
#pragma once

#include <mirante/Status.h>

#include <QJsonValue>
#include <QString>
#include <QUrl>

#include <optional>

namespace mirante::modules::jira {

// The JQL of a preset. Nothing if a project preset has no project key. The
// key is quoted, so a value cannot change the query.
std::optional<QString> listJql(const QString &preset, const QString &jql, const QString &projectKey);

// The search's page on the site.
QUrl listWebUrl(const QUrl &baseUrl, const QString &jql);

// The fetch of ItemType::fetchStatus(). data holds rows and total.
void fetchListStatus(StatusJob &job);

} // namespace mirante::modules::jira
