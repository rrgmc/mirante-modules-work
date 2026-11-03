// What the three GitHub summaries share: the model that a layout draws. A
// layout is the code of an extension point, so this header is an interface that
// apps build against. A change to it is noted in CHANGELOG.md. See
// README.md#layouts.
#pragma once

#include <mirante/ApiClient.h>
#include <mirante/Status.h>
#include <mirante/widgets/StatusPill.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

class QEvent;
class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

class GitHubClient;

// The text and colour of a state, such as Good "Passed" or Bad "Failed", as the
// module words it. A layout draws it as it likes, such as with
// widgets::StatusPill.
struct Pill
{
    QString text;
    widgets::StatusRole role = widgets::StatusRole::Neutral;
};

// The model of a summary: the item's status, read into what a layout shows, and
// the actions on it. A subclass adds what its view shows. The module makes the
// model, then the view with the point's make function, which draws the model now
// and again at each changed(). The model lives as long as the view.
class SummaryModel : public QObject
{
    Q_OBJECT

public:
    ~SummaryModel() override;

    ItemContext &context() const { return *m_context; }
    // The item's configuration.
    QJsonObject config() const;

    // False until the first data comes, and when a status without data replaces
    // it. The view then shows message() alone, such as "Loading…".
    bool hasData() const { return m_hasData; }
    QString message() const { return m_message; }

    // Whether the item's settings show a section: the boolean field
    // show<Section>, on when absent. isSectionShown("labels") reads showLabels.
    bool isSectionShown(const QString &section) const;

    // The item's page on the site, or empty when there is none.
    QUrl pageAddress() const { return m_pageAddress; }
    // Shows the page in Mirante, or in the external browser while Shift is held.
    void openPage();

    // Whether the request of an action runs, by the action's id.
    bool isBusy(const QString &action) const { return m_busy.contains(action); }
    // Why the last action failed, or empty.
    QString error() const { return m_error; }
    void clearError();
    // Asks for the item's status again.
    void refresh();

    // Called by the module only. follow() reads the item's status now and at
    // each change; a null source stays at "Loading…". attach() gives the model
    // to the view, which then owns it.
    void follow(ItemStatusSource *source);
    void attach(QWidget *view);

signals:
    // New data, another message, or another part of the data shown.
    void changed();
    void busyChanged(const QString &action);
    void errorChanged();

protected:
    explicit SummaryModel(ItemContext &context);

    // Only called with data. It replaces what was read before.
    virtual void read(const ItemStatus &status) = 0;
    // When the view shows or hides.
    virtual void viewVisibilityChanged(bool visible) { Q_UNUSED(visible) }
    bool isViewVisible() const { return m_viewVisible; }

    bool eventFilter(QObject *watched, QEvent *event) override;

    // The item's repository, "owner/repo", or the project's setting.
    QString repository() const;
    // The item's server, for a request that is not an action, or null.
    ApiClient *server() const;
    // The profile's web address, for an address that the data does not give.
    QUrl webBase() const;
    // The project's default repository, for building an address.
    QJsonValue defaultRepository() const;
    // Kept only if it is a web address.
    void setPageAddress(const QUrl &url);

    // Makes the request of an action, and passes done on to it.
    using Request = std::function<void(const GitHubClient &, std::function<void(const ApiReply &)>)>;
    // Clears the error and marks the action busy while its request runs. On
    // success the status is read again; on failure error() says why.
    void runAction(const QString &id, const Request &request);

private:
    void showStatus(const ItemStatus &status);
    void setBusy(const QString &action, bool busy);
    void setError(const QString &text);

    ItemContext *m_context;
    std::function<void()> m_refresh;
    bool m_hasData = false;
    QString m_message;
    QUrl m_pageAddress;
    QSet<QString> m_busy;
    QString m_error;
    QWidget *m_view = nullptr;
    bool m_viewVisible = false;
};

} // namespace mirante::modules::github
