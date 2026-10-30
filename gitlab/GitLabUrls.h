// The addresses of GitLab pages, built from an item's configuration. Pure
// functions, so they are tested without a view.
#pragma once

#include <mirante/UrlMatch.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace mirante::modules::gitlab {

// The project path of the item: its "project" key, or defaultProject when that
// is empty. Slashes at the start and end are removed. Empty if neither is set.
QString projectPath(const QJsonObject &config, const QJsonValue &defaultProject);

// The "iid" key as a positive number, or 0. The form stores a number, but a
// file edited by hand may hold the number as text.
qint64 itemNumber(const QJsonObject &config);

// <baseUrl>/<project>/-/merge_requests/<iid>, or an empty URL if the project or
// the number is missing. defaultProject is the project's "project" setting.
QUrl mergeRequestUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject);

// <baseUrl>/<project>/-/issues/<iid>, or an empty URL if the project or the
// number is missing. defaultProject is the project's "project" setting.
QUrl issueUrl(const QUrl &baseUrl, const QJsonObject &config, const QJsonValue &defaultProject);

// A GitLab page that the user pasted: its server, its project and what it
// shows. See README.md#items-from-a-link.
struct PageAddress
{
    QUrl baseUrl;       // the server, with the path GitLab is installed at
    QString project;    // such as "group/subgroup/project"
    QString kind;       // "merge_requests", "issues" or "pipelines"
    qint64 number = 0;  // the merge request or issue number, or the pipeline id; 0 for a list
    QString ref;        // pipelines: the branch in the "ref" query, if any
};

// Reads a GitLab page address: <base>/<project>/-/<kind>[/<number>]. The base
// is the longest of baseUrls that url is below, or else url's server, so the
// pages of gitlab.com and of a self-hosted server that is not the profile's
// are read too. Null for any other address, such as a project's home page.
std::optional<PageAddress> readPageAddress(const QUrl &url, const QList<QUrl> &baseUrls);

// What the type with typeId makes of url, for ItemType::fromUrl(). Merge
// request and issue pages fill the summary and page types; their list pages
// fill the list types; a merge request page or a pipelines page fills the CI
// pipeline. Null if the type does not take the address.
std::optional<UrlMatch> matchUrl(const QString &typeId, const QUrl &url, const QList<QUrl> &baseUrls);

} // namespace mirante::modules::gitlab
