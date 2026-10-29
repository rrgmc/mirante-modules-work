// The addresses of GitLab pages, built from an item's configuration. Pure
// functions, so they are tested without a view.
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>

namespace mirante::modules::gitlab {

// The project path of the item: its "project" key, or defaultProject when that
// is empty. Slashes at the start and end are removed. Empty if neither is set.
QString projectPath(const QJsonObject &config, const QJsonValue &defaultProject);

// <baseUrl>/<project>/-/merge_requests/<iid>, or an empty URL if the project or
// the number is missing. defaultProject is the project's "project" setting.
QUrl mergeRequestUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject);

// <baseUrl>/<project>/-/issues/<iid>, or an empty URL if the project or the
// number is missing. defaultProject is the project's "project" setting.
QUrl issueUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject);

} // namespace mirante::modules::gitlab
