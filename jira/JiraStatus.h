// The status of a Jira issue item, for the status cache. See
// README.md#status and decision 0061.
#pragma once

#include <mirante/Status.h>

#include <QJsonObject>
#include <QStringList>

namespace mirante::modules::jira {

// The fields the status asks for.
QStringList statusFields();

// The status of an issue from the JSON of GET issue/<key>.
ItemStatus issueStatus(const QJsonObject &issue);

// Fetches the issue named by the job's "key" and finishes the job with its
// status.
void fetchIssueStatus(StatusJob &job);
// The same for a summary, which asks for all fields, since it also shows the
// description, comments, sprint and links. A page does not, so it stays cheap.
void fetchIssueSummaryStatus(StatusJob &job);

} // namespace mirante::modules::jira
