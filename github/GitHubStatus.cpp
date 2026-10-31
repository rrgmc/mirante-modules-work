// Stubs until task T3 of the M11 plan implements README.md#status.
#include "GitHubStatus.h"

namespace mirante::modules::github {

ChecksState checksState(const QJsonArray &checkRuns, const QJsonObject &combinedStatus)
{
    Q_UNUSED(checkRuns)
    Q_UNUSED(combinedStatus)
    return ChecksState::None;
}

bool isFailedConclusion(const QString &conclusion)
{
    return conclusion == QLatin1String("failure") || conclusion == QLatin1String("timed_out")
        || conclusion == QLatin1String("startup_failure");
}

QJsonArray latestReviews(const QJsonArray &reviews)
{
    Q_UNUSED(reviews)
    return {};
}

ReviewState reviewState(const QJsonArray &reviews)
{
    Q_UNUSED(reviews)
    return {};
}

ItemStatus pullRequestStatus(const QJsonObject &pullRequest, const QJsonArray &reviews, const QJsonObject &user,
                             const QJsonArray &checkRuns, const QJsonObject &combinedStatus)
{
    Q_UNUSED(pullRequest)
    Q_UNUSED(reviews)
    Q_UNUSED(user)
    Q_UNUSED(checkRuns)
    Q_UNUSED(combinedStatus)
    return {};
}

ItemStatus issueStatus(const QJsonObject &issue, const QJsonArray &comments)
{
    Q_UNUSED(issue)
    Q_UNUSED(comments)
    return {};
}

RunGroup runGroup(const QJsonArray &runs, const QString &preferredSha)
{
    Q_UNUSED(runs)
    Q_UNUSED(preferredSha)
    return {};
}

ItemStatus runStatus(const QString &branch, const QJsonObject &run, const QJsonArray &jobs,
                     const QJsonArray &siblings, const QJsonArray &older, const QJsonObject &pullRequest)
{
    Q_UNUSED(branch)
    Q_UNUSED(run)
    Q_UNUSED(jobs)
    Q_UNUSED(siblings)
    Q_UNUSED(older)
    Q_UNUSED(pullRequest)
    return {};
}

void fetchPullRequestStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

void fetchIssueStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

void fetchIssueSummaryStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

void fetchRunStatus(StatusJob &job)
{
    job.finish(ItemStatus());
}

} // namespace mirante::modules::github
