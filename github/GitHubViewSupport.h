// What the GitHub summaries share: dates, pills and the small widgets of the
// default layouts. Private to the module: a layout of an app uses the models. See README.md#summary-views.
#pragma once

#include "GitHubClient.h"
#include "GitHubStatus.h"
#include "GitHubSummaryModel.h"

#include <mirante/Status.h>
#include <mirante/widgets/StatusPill.h>
#include <mirante/widgets/SummaryView.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>

#include <functional>

class QLabel;
class QPushButton;
class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

// Short and local. Text that is not a date comes back unchanged.
QString localDateTime(const QString &text);
QString localDate(const QString &text);
// Such as "42 s", "5 min 2 s" or "1 h 3 min". Empty if unknown.
QString durationText(const QJsonValue &seconds);
// The time from start to end, two dates of GitHub, as durationText(). Empty
// if either is missing or not a date, as for a job that has not finished.
QString durationBetween(const QString &start, const QString &end);

// The text and role of a run's, a job's or a check run's pill, from its status
// and, once "completed", its conclusion. See README.md#status-pills.
QString statusText(const QString &status, const QString &conclusion);
widgets::StatusRole statusRole(const QString &status, const QString &conclusion);
// The same as a pill, from the object's "status" and "conclusion".
widgets::StatusPill *runPill(const QJsonObject &object);
// A commit status of the combined status, from its "state": "success" Good
// "Passed", "failure" Bad "Failed", "error" Bad "Error", "pending" Waiting
// "Pending".
widgets::StatusPill *commitStatusPill(const QJsonObject &status);

// "Checks passed", "Checks failed", "Checks running" or "No checks", and its
// role.
QString checksText(ChecksState state);
widgets::StatusRole checksRole(ChecksState state);
widgets::StatusPill *checksPill(ChecksState state);

// A review's state as a pill: "APPROVED" Good "Approved", "CHANGES_REQUESTED"
// Bad "Changes requested", "COMMENTED" Neutral "Commented", and an empty state,
// for a requested reviewer who has not reviewed yet, Waiting "Waiting".
widgets::StatusPill *reviewPill(const QString &state);

// A run that "Cancel run" applies to: queued, in_progress, waiting, requested
// or pending. The run view also refreshes such a run.
bool canCancelRun(const QString &status);

// Only pages on the web open from a view. An address can come from a server.
bool isWebAddress(const QUrl &url);
// The address in html_url, if it is a web address, else fallback.
QUrl webAddress(const QJsonObject &object, const QUrl &fallback);

// What a failed action shows. A 403 says the token needs write access to
// Actions.
QString actionError(const ApiReply &reply);

// The logins of users, joined, or empty.
QString userNames(const QJsonArray &users);

// A label that wraps and never reads its text as markup.
QLabel *plainLine(const QString &text);
// The same on one line, for a row that wraps by itself.
QLabel *plainWord(const QString &text);
// The same states as Pill values, for a model, and a pill widget of one.
Pill runPillOf(const QJsonObject &object);
Pill commitStatusPillOf(const QJsonObject &status);
Pill checksPillOf(ChecksState state);
Pill reviewPillOf(const QString &state);
Pill statePillOf(const QJsonObject &object);
widgets::StatusPill *pillWidget(const Pill &pill);

// A pull request's or an issue's state as a pill: "Open", "Merged" (when
// merged or merged_at is set) or "Closed". A draft's own pill is the view's.
widgets::StatusPill *statePill(const QJsonObject &object);
// A button that reads as a link, for a row of a section.
QPushButton *linkButton(const QString &text, const QString &name);
// The same, which opens url in the external browser. Null if it is not a web
// address, so the row goes without it.
QPushButton *webButton(const QString &text, const QString &name, const QUrl &url);
// A widget with a column of its own, for a section's content.
QWidget *newColumn();
// The labels, GitHub's objects with a "name", as a wrapping row of pills, or a
// line that says there are none.
QWidget *labelChips(const QJsonArray &labels);

// For the default layouts. The object name of a default layout's view, such
// as "github-issue-default-layout", by which the tests know it.
QString defaultLayoutName(const QString &view);
// Shows the model's busy actions and its error in the view's action row.
void followActions(widgets::SummaryView *view, SummaryModel &model);
// Adds "Open page", after the view's other actions.
void addOpenPage(widgets::SummaryView *view, SummaryModel &model);
// The address of "Open page" and "Web". Without a web address, both hide.
void showPageAddress(widgets::SummaryView *view, const SummaryModel &model);

} // namespace mirante::modules::github
