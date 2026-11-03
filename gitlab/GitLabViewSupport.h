// What the GitLab summaries share: dates, pills and the small widgets of the
// default layouts. Private to the module: a layout of an app uses the models. See
// docs/plans/m6-native-item-types.md and docs/plans/m6-feedback.md.
#pragma once

#include "GitLabClient.h"
#include "GitLabSummaryModel.h"

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

namespace mirante::modules::gitlab {

// Short and local. Text that is not a date comes back unchanged.
QString localDateTime(const QString &text);
QString localDate(const QString &text);
// Such as "42 s", "5 min 2 s" or "1 h 3 min". Empty if unknown, as for a job
// that has not run.
QString durationText(const QJsonValue &seconds);

// A pipeline or job status as a pill's text and role.
QString statusText(const QString &status);
widgets::StatusRole statusRole(const QString &status);

// A pipeline that "Retry pipeline" applies to: failed or canceled.
bool canRetryPipeline(const QString &status);

// Only pages on the web open from a view. An address can come from a server.
bool isWebAddress(const QUrl &url);
// The address in web_url, if it is a web address, else fallback.
QUrl webAddress(const QJsonObject &object, const QUrl &fallback);

// What a failed action shows. A 403 names the scope the token lacks.
QString actionError(const ApiReply &reply);

// The names of users, joined, or empty.
QString userNames(const QJsonArray &users);

// A label that wraps and never reads its text as markup.
QLabel *plainLine(const QString &text);
// The same on one line, for a row that wraps by itself.
QLabel *plainWord(const QString &text);
// The same states as Pill values, for a model, and a pill widget of one.
Pill statusPillOf(const QString &status);
Pill statePillOf(const QString &state);
Pill jobPillOf(const QJsonObject &job);
widgets::StatusPill *pillWidget(const Pill &pill);

// A merge request's or an issue's state as a pill.
widgets::StatusPill *statePill(const QString &state);
// A job's status as a pill. A failed job that may fail says so.
widgets::StatusPill *jobPill(const QJsonObject &job);
// A button that reads as a link, for a row of a section.
QPushButton *linkButton(const QString &text, const QString &name);
// The same, which opens url in the external browser. Null if it is not a web
// address, so the row goes without it.
QPushButton *webButton(const QString &text, const QString &name, const QUrl &url);
// A widget with a column of its own, for a section's content.
QWidget *newColumn();
// The labels as a wrapping row of pills, or a line that says there are none.
QWidget *labelChips(const QJsonArray &labels);

// For the default layouts. The object name of a default layout's view, such
// as "gitlab-issue-default-layout", by which the tests know it.
QString defaultLayoutName(const QString &view);
// Shows the model's busy actions and its error in the view's action row.
void followActions(widgets::SummaryView *view, SummaryModel &model);
// Adds "Open page", after the view's other actions.
void addOpenPage(widgets::SummaryView *view, SummaryModel &model);
// The address of "Open page" and "Web". Without a web address, both hide.
void showPageAddress(widgets::SummaryView *view, const SummaryModel &model);

} // namespace mirante::modules::gitlab
