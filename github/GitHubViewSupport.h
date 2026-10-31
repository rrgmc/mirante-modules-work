// What the three GitHub summaries share: drawing from a status, the actions,
// dates and the small widgets. Private to the module. See README.md#summary-views.
#pragma once

#include "GitHubClient.h"
#include "GitHubStatus.h"

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

// The base of the three summaries. It draws from the item's status in the status
// cache. A subclass adds its actions in its constructor and implements draw().
// Requests use this view as their receiver, so an answer that comes after the
// view is gone is dropped.
class GitHubSummary : public widgets::SummaryView
{
public:
    // Draws the item's status now and at each change. Null, where the core
    // keeps no status, stays at "Loading…".
    void follow(ItemStatusSource *source);

protected:
    // Reads the status again.
    void refreshStatus();

    // Makes the request of an action, and passes done on to it.
    using Request = std::function<void(const GitHubClient &, GitHubClient::Done)>;

    GitHubSummary(ItemContext &context, QWidget *parent);

    // Only called with data. It replaces what was drawn before.
    virtual void draw(const ItemStatus &status) = 0;

    ItemContext &context() const { return *m_context; }
    QJsonObject config() const;
    // The item's repository, "owner/repo", or the project's setting.
    QString repository() const;
    // The item's server, for a request that is not an action, or null.
    ApiClient *server() const;
    // The profile's web address, for an address that the data does not give.
    QUrl webBase() const;
    // The project's default repository, for building an address.
    QJsonValue defaultRepository() const;

    // Shows or hides a section by its field, which is on when absent.
    void applyField(const QString &section, const QString &field);

    // Adds "Open page". Called after a subclass's other actions.
    void addOpenPage();
    // The address of "Open page" and "Web". Without a web address, both hide.
    void setPageAddress(const QUrl &url);

    // Marks the action busy while its request runs. On success the status is
    // read again, and on failure the action row shows why.
    void runAction(const QString &id, const Request &request);
    // Like runAction, for a button outside the action row. finished runs when
    // the answer comes, before anything else.
    void run(const Request &request, const std::function<void()> &finished);

private:
    void showStatus(const ItemStatus &status);
    void openPage();

    ItemContext *m_context;
    QUrl m_pageAddress;
    std::function<void()> m_refresh;
    bool m_drawn = false;
};

} // namespace mirante::modules::github
