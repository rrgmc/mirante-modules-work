// The address of a Jira issue page, built from an item's configuration, and
// the reading of pasted Jira addresses. Pure functions, so they are tested
// without a view.
#pragma once

#include <mirante/UrlMatch.h>

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace mirante::modules::jira {

// <baseUrl>/browse/<key>, or an empty URL if the "key" key is empty.
QUrl issueUrl(const QUrl &baseUrl, const QJsonObject &config);

// A Jira page that the user pasted: its server, and the issue or the search
// it shows. See README.md#items-from-a-link.
struct PageAddress
{
    QUrl baseUrl;   // the server, with the path Jira is installed at
    QString key;    // the issue key, such as "PROJ-123"; empty for a search
    QString jql;    // the JQL query of a search; empty for an issue
};

// Reads a Jira address. The issue key comes from /browse/<key>, from
// /issues/<key> as in Jira Cloud's project pages, or from the selectedIssue
// query of a board. A search comes from a jql query. The base is the longest
// of baseUrls that url is below, else the server of an atlassian.net
// address, else the part before /browse/ of any other address. Null for
// anything else.
std::optional<PageAddress> readPageAddress(const QUrl &url, const QList<QUrl> &baseUrls);

// What the type with typeId makes of url, for ItemType::fromUrl(). An issue
// fills the issue summary and page types; a search fills Jira List. Null if
// the type does not take the address.
std::optional<UrlMatch> matchUrl(const QString &typeId, const QUrl &url, const QList<QUrl> &baseUrls);

} // namespace mirante::modules::jira
