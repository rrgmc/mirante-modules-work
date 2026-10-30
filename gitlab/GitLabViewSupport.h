// What the three GitLab summaries share: drawing from a status, the actions,
// dates and the small widgets. Private to the module. See
// docs/plans/m6-native-item-types.md and docs/plans/m6-feedback.md.
#pragma once

#include "GitLabClient.h"

#include <mirante/Status.h>
#include <mirante/widgets/StatusPill.h>
#include <mirante/widgets/SummaryView.h>

#include <QCoreApplication>
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

// The base of the three summaries. It draws from a status: the item's own from
// the status cache, or, for a preview, one it asks the server for through the
// same fetch. A subclass adds its actions in its constructor and implements
// draw(). Requests use this view as their receiver, so an answer that comes
// after the view is gone is dropped.
class GitLabSummary : public widgets::SummaryView
{
    Q_DECLARE_TR_FUNCTIONS(gitlab)

public:
    // Draws the item's status now and at each change. Null, where the core
    // keeps no status, stays at "Loading…".
    void follow(ItemStatusSource *source);
    // Draws what fetch reads through the context's server, and reads again
    // after an action. See decision 0068.
    void readItself(void (*fetch)(StatusJob &));

protected:
    // Reads the status again.
    void refreshStatus();

    // Makes the request of an action, and passes done on to it.
    using Request = std::function<void(const GitLabClient &, GitLabClient::Done)>;

    // ownConfig replaces the item's configuration in a preview. It holds no
    // section fields, so a preview's sections are all on.
    GitLabSummary(ItemContext &context, const QJsonObject &ownConfig, QWidget *parent);

    // Only called with data. It replaces what was drawn before.
    virtual void draw(const ItemStatus &status) = 0;

    QJsonObject config() const;
    QString project() const;
    // The item's server, for a request that is not an action, or null.
    ApiClient *server() const;
    // The server's base URL, for an address that the data does not give.
    QUrl baseUrl() const;
    // The project's default project path, for building an address.
    QJsonValue defaultProject() const;

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
    QJsonObject m_ownConfig;
    QUrl m_pageAddress;
    std::function<void()> m_refresh;
    bool m_drawn = false;
    // Only the answer to the latest read of a preview counts.
    int m_reads = 0;
};

} // namespace mirante::modules::gitlab
