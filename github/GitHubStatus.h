// The status of GitHub items: what the status cache shows in badges and
// summaries. See README.md#status.
#pragma once

#include <mirante/Status.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace mirante::modules::github {

// Builders from the parsed answers, so the rules are tested without a job.
// user is the answer to GET user: "me" in the attention rules.

// The checks of a pull request's head commit, from its check runs and its
// combined commit status.
enum class ChecksState { None, Running, Passed, Failed };

// Failed when a check run's conclusion is failed (see isFailedConclusion()),
// or the combined status has statuses and its state is "failure" or "error";
// else Running when a check run is not "completed", or the combined status has
// statuses and its state is "pending"; else Passed when there is at least one
// check run or status; else None. checkRuns is the array of check runs, and
// combinedStatus the answer to the status of the commit, either possibly
// empty when its request failed.
ChecksState checksState(const QJsonArray &checkRuns, const QJsonObject &combinedStatus);

// A conclusion of a run, a job or a check run that counts as failed:
// "failure", "timed_out" or "startup_failure".
bool isFailedConclusion(const QString &conclusion);

// What the reviews of a pull request say, from each reviewer's latest review.
struct ReviewState
{
    int approvals = 0;              // reviewers whose latest review approves
    bool changesRequested = false;  // a reviewer's latest review requests changes

    // At least one approval and no changes requested.
    bool approved() const { return approvals > 0 && !changesRequested; }
};

// Each reviewer's latest review, from the answer to the reviews of a pull
// request, in the order of each reviewer's first review. Reviews count by
// submitted_at. A COMMENTED or PENDING review does not replace an earlier
// APPROVED or CHANGES_REQUESTED one, and DISMISSED removes it. A reviewer
// whose reviews are only COMMENTED keeps the latest of those. PENDING reviews
// alone count for nothing. Each entry is the review object of GitHub.
QJsonArray latestReviews(const QJsonArray &reviews);
// The approvals and changes requested of latestReviews(reviews).
ReviewState reviewState(const QJsonArray &reviews);

// A pull request with its reviews, its head commit's checks and the current
// user. Attention counts only while it is open. data holds pullRequest,
// reviews, user (its id and login), checkRuns and combinedStatus.
ItemStatus pullRequestStatus(const QJsonObject &pullRequest, const QJsonArray &reviews, const QJsonObject &user,
                             const QJsonArray &checkRuns, const QJsonObject &combinedStatus);
// An issue. data holds issue, and comments when comments is not empty:
// newest first, at most 20.
ItemStatus issueStatus(const QJsonObject &issue, const QJsonArray &comments = {});

// The runs of a branch, sorted into those of one commit and those before it.
// See README.md#the-run-group.
struct RunGroup
{
    QString headSha;        // the group's commit, or empty if there are no runs
    QJsonObject lead;       // the newest run of the group, or empty
    QJsonArray siblings;    // the rest of the group, newest first, without jobs
    QJsonArray older;       // the runs of other commits, newest first
};
// runs is the "workflow_runs" array of an answer, in any order. preferredSha is
// the linked pull request's head.sha, or empty; it is the group's commit when
// some run has it, else the newest run's head_sha is.
RunGroup runGroup(const QJsonArray &runs, const QString &preferredSha = {});

// The lead run of a group and its jobs, for branch. An empty run means the
// branch has no runs: Ok with "No runs on <branch>". siblings are the rest of
// its group, each as {"run": ..., "jobs": [...]}, without "jobs" if they could
// not be read. older are the runs before the group, without their jobs. A
// failed sibling needs attention too. pullRequest is the linked pull request,
// or empty. data holds branch, headSha, run, jobs, siblings, older, and
// pullRequest when it is not empty.
ItemStatus runStatus(const QString &branch, const QJsonObject &run, const QJsonArray &jobs,
                     const QJsonArray &siblings = {}, const QJsonArray &older = {},
                     const QJsonObject &pullRequest = {});

// The fetches of ItemType::fetchStatus(). Each makes its requests through
// job.server() and ends with job.finish() once. A configuration without a
// repository or a number finishes with State::None and a message, without a
// request.

// A pull request, for its summary and its page alike.
void fetchPullRequestStatus(StatusJob &job);
// An issue's page: the issue alone.
void fetchIssueStatus(StatusJob &job);
// An issue's summary: the issue and its latest comments.
void fetchIssueSummaryStatus(StatusJob &job);
// The runs of the configured branch's newest commit.
void fetchRunStatus(StatusJob &job);

} // namespace mirante::modules::github
