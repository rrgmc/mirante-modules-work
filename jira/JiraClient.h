// The Jira REST endpoints the module uses, over the core's ApiClient. One
// method per endpoint in docs/integrations.md#jira. See README.md#client.
#pragma once

#include "JiraSettings.h"

#include <mirante/ApiClient.h>

#include <QString>
#include <QStringList>

#include <functional>

class QObject;

namespace mirante::modules::jira {

// The answer to a request: the reply of the ApiClient.
using JiraDone = std::function<void(const ApiReply &)>;

struct SearchOptions
{
    QStringList fields;     // the fields of each issue; empty for Jira's default
    QStringList expand;     // the values of expand, such as "names"; empty for none
    int maxRows = 100;      // stops after this many issues
    int pageSize = 50;      // issues asked for in each request
};

// A thin layer over an ApiClient that builds the paths and bodies of one
// deployment. It keeps no state, so it may be made for one request and
// dropped. The ApiClient must outlive the requests; each callback is dropped
// if its receiver is destroyed first.
class JiraClient
{
public:
    JiraClient(ApiClient &server, Deployment deployment);

    Deployment deployment() const { return m_deployment; }

    // "rest/api/3" on Jira Cloud, "rest/api/2" on Jira Data Center.
    QString apiRoot() const;

    // GET issue/<key>?expand=renderedFields, with only the given fields if
    // any, and otherwise all of them. expand replaces the values of expand.
    void issue(const QString &key, const QStringList &fields, QObject *receiver, JiraDone done,
               const QStringList &expand = {QStringLiteral("renderedFields")}) const;

    // The issues of a JQL query. Jira Cloud pages with nextPageToken on
    // search/jql, Jira Data Center with startAt on search. The pages are
    // joined: the reply's JSON is {"issues": [...]}, up to maxRows issues, with
    // Jira's "total" where it gives one, which Jira Cloud does not, and its
    // "names" if it answered to that expand. An error on any page answers with
    // that page's reply.
    void search(const QString &jql, const SearchOptions &options, QObject *receiver, JiraDone done) const;

    // GET field: all the site's fields, as a list of objects with id and name.
    void allFields(QObject *receiver, JiraDone done) const;

    // GET myself: the user the token belongs to.
    void myself(QObject *receiver, JiraDone done) const;

private:
    QString issuePath(const QString &key) const;

    ApiClient &m_server;
    Deployment m_deployment;
};

} // namespace mirante::modules::jira
