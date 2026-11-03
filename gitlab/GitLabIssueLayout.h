// The interface of the extension point "issuelayout": the native view of a
// GitLab issue. GitLabIssueLayoutDefault.cpp implements it, and so does any
// extension that an app names for "issuelayout" in its manifest. See
// README.md#layouts.
#pragma once

#include "GitLabSummaryModel.h"

#include <QJsonArray>
#include <QJsonObject>

class QWidget;

namespace mirante::modules::gitlab {

class IssueModel : public SummaryModel
{
public:
    // What the comments are, apart from the comments themselves.
    enum class Comments {
        Shown,       // comments() has them
        None,        // the issue has none
        Unreadable,  // they could not be read
    };

    // Called by the module only.
    explicit IssueModel(ItemContext &context);

    // GitLab's issue object, as GET projects/<p>/issues/<iid> answers it.
    QJsonObject issue() const { return m_issue; }
    // "Open", "Closed" or "Locked".
    Pill state() const;
    // GitLab's note objects, newest first.
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

} // namespace mirante::modules::gitlab
