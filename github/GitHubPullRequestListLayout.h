// The interface of the extension point "pullrequestlistlayout": the native view
// of a list of GitHub pull requests. GitHubPullRequestListLayoutDefault.cpp
// implements it, and so does any extension that an app names for
// "pullrequestlistlayout" in its manifest. See README.md#list-layouts.
#pragma once

#include "GitHubListModel.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QWidget;

namespace mirante {
class ApiClient;
}

namespace mirante::modules::github {

class GitHubClient;

// The rows come from the status. Their checks and reviews come later, from
// requests of their own, for the first 25 rows that the layout shows: see
// setVisibleRows(). Each answer says rowChanged().
class PullRequestListModel : public ListModel
{
public:
    // The rows of setVisibleRows() that get the further requests.
    static constexpr int AskedRows = 25;

    // Each reviewer's latest review, as in the summary.
    struct Reviews
    {
        // False until the answer comes, and when it fails.
        bool answered = false;
        int approvals = 0;
        bool changesRequested = false;
    };

    struct PullRequest : ListRow
    {
        QString repository;  // "owner/repo"
        qint64 number = 0;
        // Such as "widgets#42", and a key that sorts by the whole repository
        // and then by the number.
        QString ref;
        QString refSortKey;
        QString title;
        QString author;
        // Invalid when GitHub does not say.
        QDateTime updated;
        // "Passed", "Failed" or "Running". Empty text until the answer comes,
        // when it fails, and without checks.
        Pill checks;
        Reviews reviews;
    };

    // Called by the module only.
    explicit PullRequestListModel(ItemContext &context, ListSpan span = ListSpan::Scope);
    ~PullRequestListModel() override;

    // In the order of GitHub's answer.
    QList<PullRequest> pullRequests() const;
    PullRequest pullRequest(const QString &key) const;

    // Asks for the checks and reviews of the first AskedRows of keys, while the
    // view is visible. A row that stays keeps what is on the way, or its answer.
    // A row that goes drops them. The fields showChecks and showReviews turn
    // each request off.
    void setVisibleRows(const QStringList &keys) override;

protected:
    void read(const QJsonArray &rows, const QUrl &webBase) override;
    void viewVisibilityChanged(bool visible) override;

private:
    void askForNewKeys();
    void stopAsking();
    void ask(ApiClient &server, const QString &key);
    void askChecks(const GitHubClient &client, const QString &key, const QString &repository, const QString &sha);
    void answer(const QString &key);
    void forgetOthers(const QStringList &asked);

    QHash<QString, PullRequest> m_pullRequests;
    QStringList m_visible;
    // The requests of the rows asked for since the latest status, each row with
    // a receiver of its own, so they can be cancelled row by row. A row stays
    // here after its answers come.
    QHash<QString, QObject *> m_rounds;
    // The latest answers, by row.
    QHash<QString, Pill> m_checks;
    QHash<QString, Reviews> m_reviews;
    // rowChanged() is on its way, and a layout that sorts may tell of new rows.
    bool m_answering = false;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed() and rowChanged(), and tells the model which rows it shows.
QWidget *makePullRequestListLayout(PullRequestListModel &model, QWidget *parent);

} // namespace mirante::modules::github
