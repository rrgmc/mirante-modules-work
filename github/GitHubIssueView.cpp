#include "GitHubIssueView.h"

#include "GitHubIssueLayout.h"
#include "GitHubUrls.h"
#include "GitHubViewSupport.h"

#include <mirante/ItemContext.h>

namespace mirante::modules::github {

IssueModel::IssueModel(ItemContext &context)
    : SummaryModel(context)
{
}

Pill IssueModel::state() const
{
    return statePillOf(m_issue);
}

void IssueModel::read(const ItemStatus &status)
{
    m_issue = status.data.value(QStringLiteral("issue")).toObject();
    m_comments = status.data.value(QStringLiteral("comments")).toArray();
    // The status leaves the comments out both when there are none and when
    // they could not be read. The issue's own count tells the two apart.
    if (!m_comments.isEmpty())
        m_commentsState = Comments::Shown;
    else if (status.data.contains(QStringLiteral("comments")) || m_issue.value(QStringLiteral("comments")).toInt() == 0)
        m_commentsState = Comments::None;
    else
        m_commentsState = Comments::Unreadable;
    setPageAddress(webAddress(m_issue, issueUrl(webBase(), config(), defaultRepository())));
}

QWidget *createIssueView(ItemContext &context, QWidget *parent)
{
    auto *model = new IssueModel(context);
    model->follow(context.status());
    QWidget *view = makeIssueLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::github
