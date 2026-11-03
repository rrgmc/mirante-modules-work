// The interface of the extension point "pullrequestlayout": the native view of
// a GitHub pull request. GitHubPullRequestLayoutDefault.cpp implements it, and
// so does any extension that an app names for "pullrequestlayout" in its
// manifest. See README.md#layouts.
#pragma once

#include "GitHubSummaryModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

class QWidget;

namespace mirante::modules::github {

class PullRequestModel : public SummaryModel
{
public:
    // A check run or a commit status of the head commit.
    struct Check
    {
        QString name;
        Pill state;
        // A check run that completed as success, skipped or neutral, or a
        // status whose state is success.
        bool passed = false;
        // Its page on the site, or empty.
        QUrl details;
        // Unique in the view: check-details-<id> or status-details-<id>.
        QString id;
    };

    // A reviewer's latest review, or a requested reviewer or team who has not
    // reviewed yet, whose state is Waiting "Waiting".
    struct Reviewer
    {
        QString name;   // a login, or a team's name
        Pill state;
        bool team = false;
    };

    // Called by the module only.
    explicit PullRequestModel(ItemContext &context);

    // GitHub's pull request object, as GET repos/<r>/pulls/<n> answers it.
    QJsonObject pullRequest() const { return m_pullRequest; }
    // "Open", "Merged" or "Closed".
    Pill state() const;
    // Open and a draft.
    bool isDraft() const;
    // Such as "Checks passed", "Checks failed", "Checks running" or "No checks".
    Pill checks() const { return m_checks; }
    // The check runs, then the commit statuses, in the order of the data.
    QList<Check> checkList() const { return m_checkList; }
    // The latest reviews, then the requested reviewers and teams without one.
    QList<Reviewer> reviewers() const { return m_reviewers; }

protected:
    void read(const ItemStatus &status) override;

private:
    QJsonObject m_pullRequest;
    Pill m_checks;
    QList<Check> m_checkList;
    QList<Reviewer> m_reviewers;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed(). Its sections are "checks", "reviews", "people" and
// "labels", which isSectionShown() turns on and off.
QWidget *makePullRequestLayout(PullRequestModel &model, QWidget *parent);

} // namespace mirante::modules::github
