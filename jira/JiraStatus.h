// The status of a Jira issue item, for the status cache. See
// README.md#status and decision 0061.
#pragma once

#include <mirante/Status.h>

#include <QJsonObject>
#include <QStringList>

namespace mirante::modules::jira {

// The most children a summary's data holds, and so the most it shows.
constexpr int kMaxChildren = 50;
// How many children it counts on Jira Cloud, whose search gives no total.
constexpr int kCountedChildren = 500;

// The fields the status asks for.
QStringList statusFields();

// The values of expand for a summary: the HTML of the description and the
// comments, and the names of the fields, which tell which one is the sprint.
QStringList summaryExpand();

// The status of an issue from the JSON of GET issue/<key>.
ItemStatus issueStatus(const QJsonObject &issue);

// The sprint of an issue asked for with summaryExpand(): its open sprint, else
// the last one. Empty if it has none. Jira Cloud gives objects and older Jira
// Data Center gives text, in a field whose id differs per site.
QString sprintOf(const QJsonObject &issue);

// Fetches the issue named by the job's "key" and finishes the job with its
// status.
void fetchIssueStatus(StatusJob &job);
// The same for a summary, which asks for all fields, since it also shows the
// description, comments, sprint and links. data also holds the answer, as
// "issue". A further search asks for the issues whose parent is this one. data
// holds them as "children", up to kMaxChildren and without those in the issue's
// sub-tasks, and their number as "childrenTotal". If only that search fails, as
// on a site without the Epic Link field, the status stands without them. A page
// does not ask for any of this, so it stays cheap.
void fetchIssueSummaryStatus(StatusJob &job);

} // namespace mirante::modules::jira
