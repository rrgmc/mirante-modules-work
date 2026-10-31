#include "GitHubClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace mirante::modules::github {

namespace {

// GitHub gives at most 100 rows per page.
constexpr int kMaxPerPage = 100;

QString pullRequestPath(const QString &repository, qint64 number)
{
    return GitHubClient::repositoryPath(repository) + QStringLiteral("/pulls/") + QString::number(number);
}

QString issuePath(const QString &repository, qint64 number)
{
    return GitHubClient::repositoryPath(repository) + QStringLiteral("/issues/") + QString::number(number);
}

QString commitPath(const QString &repository, const QString &sha)
{
    return GitHubClient::repositoryPath(repository) + QStringLiteral("/commits/") + GitHubClient::encode(sha);
}

QString runPath(const QString &repository, qint64 runId)
{
    return GitHubClient::repositoryPath(repository) + QStringLiteral("/actions/runs/") + QString::number(runId);
}

} // namespace

GitHubClient::GitHubClient(ApiClient &server, QObject *receiver)
    : m_server(&server)
    , m_receiver(receiver)
{
}

QString GitHubClient::encode(const QString &text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

QString GitHubClient::repositoryPath(const QString &repository)
{
    const qsizetype slash = repository.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return QStringLiteral("repos/") + encode(repository);
    return QStringLiteral("repos/") + encode(repository.left(slash)) + QLatin1Char('/')
        + encode(repository.mid(slash + 1));
}

QString GitHubClient::repositoryOfApiUrl(const QString &url)
{
    const QStringList parts = QUrl(url).path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const qsizetype repos = parts.indexOf(QStringLiteral("repos"));
    if (repos < 0 || repos + 2 >= parts.size())
        return {};
    return parts.at(repos + 1) + QLatin1Char('/') + parts.at(repos + 2);
}

void GitHubClient::currentUser(Done done) const
{
    get(QStringLiteral("user"), {}, std::move(done));
}

void GitHubClient::repository(const QString &repository, Done done) const
{
    get(repositoryPath(repository), {}, std::move(done));
}

void GitHubClient::pullRequest(const QString &repository, qint64 number, Done done) const
{
    get(pullRequestPath(repository, number), {}, std::move(done));
}

void GitHubClient::pullRequestReviews(const QString &repository, qint64 number, Done done) const
{
    list(pullRequestPath(repository, number) + QStringLiteral("/reviews"), {}, kMaxPerPage, {}, std::move(done));
}

void GitHubClient::repositoryPullRequests(const QString &repository, const QUrlQuery &query, int maxRows,
                                          Done done) const
{
    list(repositoryPath(repository) + QStringLiteral("/pulls"), query, maxRows, {}, std::move(done));
}

void GitHubClient::checkRuns(const QString &repository, const QString &sha, Done done) const
{
    list(commitPath(repository, sha) + QStringLiteral("/check-runs"), {}, kMaxPerPage, QStringLiteral("check_runs"),
         std::move(done));
}

void GitHubClient::combinedStatus(const QString &repository, const QString &sha, Done done) const
{
    get(commitPath(repository, sha) + QStringLiteral("/status"), {}, std::move(done));
}

void GitHubClient::issue(const QString &repository, qint64 number, Done done) const
{
    get(issuePath(repository, number), {}, std::move(done));
}

void GitHubClient::issueComments(const QString &repository, qint64 number, int perPage, int page, Done done) const
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("per_page"), QString::number(std::clamp(perPage, 1, kMaxPerPage)));
    query.addQueryItem(QStringLiteral("page"), QString::number(std::max(1, page)));
    get(issuePath(repository, number) + QStringLiteral("/comments"), query, std::move(done));
}

void GitHubClient::searchIssues(const QString &query, int maxRows, Done done) const
{
    // Encoded here, so that "+", "&" and "#" in a filter reach GitHub whole.
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("q"), encode(query));
    parameters.addQueryItem(QStringLiteral("sort"), QStringLiteral("updated"));
    parameters.addQueryItem(QStringLiteral("order"), QStringLiteral("desc"));
    list(QStringLiteral("search/issues"), parameters, maxRows, QStringLiteral("items"), std::move(done));
}

void GitHubClient::workflowRuns(const QString &repository, const QString &workflow, const QUrlQuery &query,
                                int maxRows, Done done) const
{
    QString path = repositoryPath(repository) + QStringLiteral("/actions/");
    if (!workflow.isEmpty())
        path += QStringLiteral("workflows/") + encode(workflow) + QLatin1Char('/');
    list(path + QStringLiteral("runs"), query, maxRows, QStringLiteral("workflow_runs"), std::move(done));
}

void GitHubClient::runJobs(const QString &repository, qint64 runId, Done done) const
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("filter"), QStringLiteral("latest"));
    list(runPath(repository, runId) + QStringLiteral("/jobs"), query, kMaxPerPage, QStringLiteral("jobs"),
         std::move(done));
}

void GitHubClient::rerunRun(const QString &repository, qint64 runId, Done done) const
{
    post(runPath(repository, runId) + QStringLiteral("/rerun"), std::move(done));
}

void GitHubClient::rerunFailedJobs(const QString &repository, qint64 runId, Done done) const
{
    post(runPath(repository, runId) + QStringLiteral("/rerun-failed-jobs"), std::move(done));
}

void GitHubClient::cancelRun(const QString &repository, qint64 runId, Done done) const
{
    post(runPath(repository, runId) + QStringLiteral("/cancel"), std::move(done));
}

void GitHubClient::rerunJob(const QString &repository, qint64 jobId, Done done) const
{
    post(repositoryPath(repository) + QStringLiteral("/actions/jobs/") + QString::number(jobId)
             + QStringLiteral("/rerun"),
         std::move(done));
}

void GitHubClient::get(const QString &path, const QUrlQuery &query, Done done) const
{
    m_server->get(path, query, GetOptions(), m_receiver, std::move(done));
}

void GitHubClient::list(const QString &path, QUrlQuery query, int maxRows, const QString &arrayKey, Done done) const
{
    GetOptions options;
    options.followLinks = true;
    options.maxRows = std::max(1, maxRows);
    options.arrayKey = arrayKey;
    if (!query.hasQueryItem(QStringLiteral("per_page")))
        query.addQueryItem(QStringLiteral("per_page"), QString::number(std::min(options.maxRows, kMaxPerPage)));
    m_server->get(path, query, options, m_receiver, std::move(done));
}

void GitHubClient::post(const QString &path, Done done) const
{
    // GitHub's actions take no parameters, and an empty object is a valid body.
    m_server->post(path, QJsonDocument(QJsonObject()), m_receiver, std::move(done));
}

} // namespace mirante::modules::github
