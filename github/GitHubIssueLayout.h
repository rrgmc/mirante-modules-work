// The interface of the extension point "issuelayout": the native view of a
// GitHub issue. GitHubIssueLayoutDefault.cpp implements it, and so does any
// extension that an app names for "issuelayout" in its manifest. See
// README.md#layouts.
#pragma once

#include "GitHubSummaryModel.h"

#include <QJsonArray>
#include <QJsonObject>

class QWidget;

namespace mirante::modules::github {

class IssueModel : public SummaryModel
{
public:
    // What the comments are, apart from the comments themselves.
    enum class Comments {
        Shown,       // comments() has them
        None,        // the issue has none
        Unreadable,  // the issue has some, but they could not be read
    };

    // Called by the module only.
    explicit IssueModel(ItemContext &context);

    // GitHub's issue object, as GET repos/<r>/issues/<n> answers it.
    QJsonObject issue() const { return m_issue; }
    // "Open" or "Closed".
    Pill state() const;
    // GitHub's comment objects, newest first, at most 20.
    QJsonArray comments() const { return m_comments; }
    Comments commentsState() const { return m_commentsState; }

protected:
    void read(const ItemStatus &status) override;

private:
    QJsonObject m_issue;
    QJsonArray m_comments;
    Comments m_commentsState = Comments::None;
};

// Makes the view of model, a child of parent. It draws the model now, and again
// at each changed(). Its sections are "details", "labels" and "comments", which
// isSectionShown() turns on and off.
QWidget *makeIssueLayout(IssueModel &model, QWidget *parent);

} // namespace mirante::modules::github
