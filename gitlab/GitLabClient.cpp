#include "GitLabClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace mirante::modules::gitlab {

namespace {

// GitLab gives at most 100 rows per page.
constexpr int kMaxPerPage = 100;

QString projectPathOf(const QString &project)
{
    return QStringLiteral("api/v4/projects/") + GitLabClient::encodePath(project);
}

QString mergeRequestPath(const QString &project, qint64 iid)
{
    return projectPathOf(project) + QStringLiteral("/merge_requests/") + QString::number(iid);
}

QString pipelinePath(const QString &project, qint64 pipelineId)
{
    return projectPathOf(project) + QStringLiteral("/pipelines/") + QString::number(pipelineId);
}

QString issuePath(const QString &project, qint64 iid)
{
    return projectPathOf(project) + QStringLiteral("/issues/") + QString::number(iid);
}

// The body of an action without parameters.
QJsonDocument emptyBody()
{
    return QJsonDocument(QJsonObject());
}

QJsonDocument noteBody(const QString &text)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("body"), text}});
}

} // namespace

GitLabClient::GitLabClient(ApiClient &server, QObject *receiver)
    : m_server(&server)
    , m_receiver(receiver)
{
}

QString GitLabClient::encodePath(const QString &path)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(path));
}

void GitLabClient::currentUser(Done done) const
{
    get(QStringLiteral("api/v4/user"), {}, std::move(done));
}

void GitLabClient::mergeRequest(const QString &project, qint64 iid, Done done) const
{
    get(mergeRequestPath(project, iid), {}, std::move(done));
}

void GitLabClient::approvals(const QString &project, qint64 iid, Done done) const
{
    get(mergeRequestPath(project, iid) + QStringLiteral("/approvals"), {}, std::move(done));
}

void GitLabClient::mergeRequests(const QUrlQuery &filter, int maxRows, Done done) const
{
    list(QStringLiteral("api/v4/merge_requests"), filter, maxRows, std::move(done));
}

void GitLabClient::projectMergeRequests(const QString &project, const QUrlQuery &filter, int maxRows,
                                        Done done) const
{
    list(projectPathOf(project) + QStringLiteral("/merge_requests"), filter, maxRows, std::move(done));
}

void GitLabClient::groupMergeRequests(const QString &group, const QUrlQuery &filter, int maxRows, Done done) const
{
    list(QStringLiteral("api/v4/groups/") + encodePath(group) + QStringLiteral("/merge_requests"), filter, maxRows,
         std::move(done));
}

void GitLabClient::latestPipeline(const QString &project, const QString &branch, Done done) const
{
    // Encoded here, so a branch with "+" or "&" in its name reaches GitLab whole.
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ref"), QString::fromLatin1(QUrl::toPercentEncoding(branch)));
    get(projectPathOf(project) + QStringLiteral("/pipelines/latest"), query, std::move(done));
}

void GitLabClient::pipelineJobs(const QString &project, qint64 pipelineId, int maxRows, Done done) const
{
    list(pipelinePath(project, pipelineId) + QStringLiteral("/jobs"), {}, maxRows, std::move(done));
}

void GitLabClient::issue(const QString &project, qint64 iid, Done done) const
{
    get(issuePath(project, iid), {}, std::move(done));
}

void GitLabClient::issues(const QUrlQuery &filter, int maxRows, Done done) const
{
    list(QStringLiteral("api/v4/issues"), filter, maxRows, std::move(done));
}

void GitLabClient::pipeline(const QString &project, qint64 pipelineId, Done done) const
{
    Q_UNUSED(project)
    Q_UNUSED(pipelineId)
    Q_UNUSED(done)
}

void GitLabClient::issueNotes(const QString &project, qint64 iid, int maxRows, Done done) const
{
    Q_UNUSED(project)
    Q_UNUSED(iid)
    Q_UNUSED(maxRows)
    Q_UNUSED(done)
}

void GitLabClient::projectIssues(const QString &project, const QUrlQuery &filter, int maxRows, Done done) const
{
    Q_UNUSED(project)
    Q_UNUSED(filter)
    Q_UNUSED(maxRows)
    Q_UNUSED(done)
}

void GitLabClient::groupIssues(const QString &group, const QUrlQuery &filter, int maxRows, Done done) const
{
    Q_UNUSED(group)
    Q_UNUSED(filter)
    Q_UNUSED(maxRows)
    Q_UNUSED(done)
}

void GitLabClient::setIssueState(const QString &project, qint64 iid, const QString &stateEvent, Done done) const
{
    Q_UNUSED(project)
    Q_UNUSED(iid)
    Q_UNUSED(stateEvent)
    Q_UNUSED(done)
}

void GitLabClient::retryPipeline(const QString &project, qint64 pipelineId, Done done) const
{
    post(pipelinePath(project, pipelineId) + QStringLiteral("/retry"), emptyBody(), std::move(done));
}

void GitLabClient::cancelPipeline(const QString &project, qint64 pipelineId, Done done) const
{
    post(pipelinePath(project, pipelineId) + QStringLiteral("/cancel"), emptyBody(), std::move(done));
}

void GitLabClient::retryJob(const QString &project, qint64 jobId, Done done) const
{
    post(projectPathOf(project) + QStringLiteral("/jobs/") + QString::number(jobId) + QStringLiteral("/retry"),
         emptyBody(), std::move(done));
}

void GitLabClient::approve(const QString &project, qint64 iid, Done done) const
{
    post(mergeRequestPath(project, iid) + QStringLiteral("/approve"), emptyBody(), std::move(done));
}

void GitLabClient::commentOnMergeRequest(const QString &project, qint64 iid, const QString &text, Done done) const
{
    post(mergeRequestPath(project, iid) + QStringLiteral("/notes"), noteBody(text), std::move(done));
}

void GitLabClient::commentOnIssue(const QString &project, qint64 iid, const QString &text, Done done) const
{
    post(issuePath(project, iid) + QStringLiteral("/notes"), noteBody(text), std::move(done));
}

void GitLabClient::get(const QString &path, const QUrlQuery &query, Done done) const
{
    m_server->get(path, query, GetOptions(), m_receiver, std::move(done));
}

void GitLabClient::list(const QString &path, QUrlQuery query, int maxRows, Done done) const
{
    GetOptions options;
    options.followLinks = true;
    options.maxRows = std::max(1, maxRows);
    if (!query.hasQueryItem(QStringLiteral("per_page")))
        query.addQueryItem(QStringLiteral("per_page"), QString::number(std::min(options.maxRows, kMaxPerPage)));
    m_server->get(path, query, options, m_receiver, std::move(done));
}

void GitLabClient::post(const QString &path, const QJsonDocument &body, Done done) const
{
    m_server->post(path, body, m_receiver, std::move(done));
}

void GitLabClient::put(const QString &path, const QJsonDocument &body, Done done) const
{
    m_server->put(path, body, m_receiver, std::move(done));
}

} // namespace mirante::modules::gitlab
