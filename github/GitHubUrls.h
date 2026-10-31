// The addresses of GitHub pages, built from an item's configuration, and the
// reading of pasted ones. Pure functions, so they are tested without a view.
#pragma once

#include <mirante/UrlMatch.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace mirante::modules::github {

// The repository of the item, "owner/repo": its "repository" key, or
// defaultRepository when that is empty. Trimmed, without slashes at the start
// and end. Empty if neither is set.
QString repositoryOf(const QJsonObject &config, const QJsonValue &defaultRepository);

// The trimmed "branch" key. Empty means the repository's default branch,
// which the field's placeholder says.
QString configuredBranch(const QJsonObject &config);

// The trimmed "workflow" key, a workflow file such as "ci.yml". Empty means
// all workflows.
QString configuredWorkflow(const QJsonObject &config);

// The "number" key as a positive number, or 0. The form stores a number, but a
// file edited by hand may hold the number as text.
qint64 itemNumber(const QJsonObject &config);

// <webBase>/<repository>/pull/<number>, or an empty URL if the repository or
// the number is missing. defaultRepository is the project's "repository"
// setting. A path in webBase is kept, and no slash is doubled.
QUrl pullRequestUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository);

// <webBase>/<repository>/issues/<number>, as pullRequestUrl().
QUrl issueUrl(const QUrl &webBase, const QJsonObject &config, const QJsonValue &defaultRepository);

// <webBase>/<path>, keeping a path in webBase and without doubling a slash.
// path is used as given, so it may hold a repository and a page, such as
// "owner/repo/actions".
QUrl webPageUrl(const QUrl &webBase, const QString &path);

// A GitHub page that the user pasted: its server, its repository and what it
// shows. See README.md#items-from-a-link.
struct PageAddress
{
    QUrl webBase;           // the server, with the path GitHub is installed at
    QString repository;     // "owner/repo", keeping its case; empty on the web base itself
    // "pull", "pulls", "issues", "actions" or "tree". For "pull" and "issues"
    // with a number, the page of one; for "issues" without one, the list.
    QString kind;
    qint64 number = 0;      // the pull request or issue number, or the run id of actions/runs/<id>; else 0
    QString workflow;       // actions/workflows/<file>: the file
    QString branch;         // tree/<branch>: the branch, whole, slashes included
    // On the web base itself: "assigned" or "review-requested" after "pulls"
    // or "issues"; else empty.
    QString filter;
};

// Reads a GitHub page address: <web>/<owner>/<repo>/<kind>[/<rest>], or
// <web>/<pulls|issues>[/<filter>] on the web base itself. The web base is the
// longest of webBases that url is below, else https://github.com if url is on
// github.com. A query and a fragment are ignored. Null for any other address,
// such as a repository's home page, compare/... or pull/new/....
std::optional<PageAddress> readPageAddress(const QUrl &url, const QList<QUrl> &webBases);

// What the type with typeId makes of url, for ItemType::fromUrl(). webBases
// are the web addresses of the profile the new item would use; github.com is
// always read too. Null if the type does not take the address. See the table
// in README.md#items-from-a-link.
std::optional<UrlMatch> matchUrl(const QString &typeId, const QUrl &url, const QList<QUrl> &webBases);

} // namespace mirante::modules::github
