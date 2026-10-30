// The status of GitLab's two list types, and the requests behind it. See
// decision 0067.
#pragma once

#include <mirante/Status.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

namespace mirante::modules::gitlab {

enum class ListKind { MergeRequests, Issues };

// The API's parameters for a preset. userId is the current user, for
// "reviewRequested". The custom filter's parameters come last and replace
// those of the same name.
QUrlQuery listQuery(ListKind kind, const QString &preset, qint64 userId, const QString &filter);

// The list's page on the site, or an empty address if its scope is not
// complete.
QUrl listWebUrl(ListKind kind, const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject);

// The fetches of ItemType::fetchStatus(). data holds rows and total.
void fetchMergeRequestListStatus(StatusJob &job);
void fetchIssueListStatus(StatusJob &job);

} // namespace mirante::modules::gitlab
