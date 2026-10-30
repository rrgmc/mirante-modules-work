// The status of GitLab items: what the status cache shows in badges and
// summaries. See README.md#status.
#pragma once

#include <mirante/Status.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace mirante::modules::gitlab {

// Builders from the parsed answers, so the rules are tested without a job.
// user is the answer to GET user: "me" in the attention rules.

// A merge request with its approvals. Attention counts only while it is open.
ItemStatus mergeRequestStatus(const QJsonObject &mergeRequest, const QJsonObject &approvals,
                              const QJsonObject &user);
ItemStatus issueStatus(const QJsonObject &issue);
// The latest pipeline of branch and its jobs. An empty pipeline means the
// branch has none.
ItemStatus pipelineStatus(const QString &branch, const QJsonObject &pipeline, const QJsonArray &jobs);

// The fetches of ItemType::fetchStatus(). Each makes its requests through
// job.server() and ends with job.finish() once. A configuration without a
// project or a number finishes with State::None and a message, without a
// request.
void fetchMergeRequestStatus(StatusJob &job);
void fetchIssueStatus(StatusJob &job);
// The same for a summary, which also needs the failed jobs or the latest
// notes, in its data. A page does not, so it stays cheap.
void fetchMergeRequestSummaryStatus(StatusJob &job);
void fetchIssueSummaryStatus(StatusJob &job);
void fetchPipelineStatus(StatusJob &job);

} // namespace mirante::modules::gitlab
