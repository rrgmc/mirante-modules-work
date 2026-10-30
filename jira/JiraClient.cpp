#include "JiraClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <memory>
#include <utility>

namespace mirante::modules::jira {

namespace {

// A query value as QUrlQuery takes it. QUrlQuery keeps "%" and "+" as they
// are, and a server reads "+" as a space, so both are encoded here.
QString queryValue(QString value)
{
    value.replace(QLatin1Char('%'), QLatin1String("%25"));
    value.replace(QLatin1Char('+'), QLatin1String("%2B"));
    return value;
}

// One search across its pages. It holds what the next request needs, so the
// JiraClient that started it may be gone.
struct Search
{
    ApiClient *server = nullptr;
    Deployment deployment = Deployment::Cloud;
    QString path;
    QString jql;
    SearchOptions options;
    QObject *receiver = nullptr;
    JiraDone done;
    QJsonArray issues;
};

void nextPage(const std::shared_ptr<Search> &search, int startAt, const QString &pageToken)
{
    const int wanted = std::min(search->options.pageSize, search->options.maxRows - int(search->issues.size()));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("jql"), queryValue(search->jql));
    if (!search->options.fields.isEmpty())
        query.addQueryItem(QStringLiteral("fields"), search->options.fields.join(QLatin1Char(',')));
    query.addQueryItem(QStringLiteral("maxResults"), QString::number(wanted));
    if (search->deployment == Deployment::Cloud) {
        if (!pageToken.isEmpty())
            query.addQueryItem(QStringLiteral("nextPageToken"), queryValue(pageToken));
    } else {
        query.addQueryItem(QStringLiteral("startAt"), QString::number(startAt));
    }

    search->server->get(search->path, query, GetOptions(), search->receiver, [search, startAt](const ApiReply &reply) {
        if (!reply.ok()) {
            search->done(reply);
            return;
        }
        const QJsonObject page = reply.json.object();
        const QJsonArray issues = page.value(QStringLiteral("issues")).toArray();
        for (const QJsonValue &issue : issues) {
            if (search->issues.size() >= search->options.maxRows)
                break;
            search->issues.append(issue);
        }

        // Another page while this one had issues, the rows are not all
        // there, and Jira says there is more.
        bool more = !issues.isEmpty() && search->issues.size() < search->options.maxRows;
        QString token;
        const int next = startAt + int(issues.size());
        if (search->deployment == Deployment::Cloud) {
            token = page.value(QStringLiteral("nextPageToken")).toString();
            more = more && !token.isEmpty() && !page.value(QStringLiteral("isLast")).toBool();
        } else {
            const QJsonValue total = page.value(QStringLiteral("total"));
            more = more && (!total.isDouble() || next < total.toInt());
        }
        if (more) {
            nextPage(search, next, token);
            return;
        }

        ApiReply joined = reply;
        joined.json = QJsonDocument(QJsonObject{{QStringLiteral("issues"), search->issues}});
        joined.body = joined.json.toJson(QJsonDocument::Compact);
        search->done(joined);
    });
}

} // namespace

JiraClient::JiraClient(ApiClient &server, Deployment deployment)
    : m_server(server)
    , m_deployment(deployment)
{
}

QString JiraClient::apiRoot() const
{
    return m_deployment == Deployment::Cloud ? QStringLiteral("rest/api/3") : QStringLiteral("rest/api/2");
}

QString JiraClient::issuePath(const QString &key) const
{
    return apiRoot() + QStringLiteral("/issue/") + QString::fromLatin1(QUrl::toPercentEncoding(key));
}

void JiraClient::issue(const QString &key, const QStringList &fields, QObject *receiver, JiraDone done,
                       const QStringList &expand) const
{
    QUrlQuery query;
    if (!fields.isEmpty())
        query.addQueryItem(QStringLiteral("fields"), fields.join(QLatin1Char(',')));
    query.addQueryItem(QStringLiteral("expand"), expand.join(QLatin1Char(',')));
    m_server.get(issuePath(key), query, GetOptions(), receiver, std::move(done));
}

void JiraClient::search(const QString &jql, const SearchOptions &options, QObject *receiver, JiraDone done) const
{
    auto search = std::make_shared<Search>();
    search->server = &m_server;
    search->deployment = m_deployment;
    search->path = apiRoot()
                   + (m_deployment == Deployment::Cloud ? QStringLiteral("/search/jql") : QStringLiteral("/search"));
    search->jql = jql;
    search->options = options;
    search->options.maxRows = std::max(options.maxRows, 1);
    search->options.pageSize = std::max(options.pageSize, 1);
    search->receiver = receiver;
    search->done = std::move(done);
    nextPage(search, 0, QString());
}

void JiraClient::myself(QObject *receiver, JiraDone done) const
{
    m_server.get(apiRoot() + QStringLiteral("/myself"), QUrlQuery(), GetOptions(), receiver, std::move(done));
}

} // namespace mirante::modules::jira
