#include "GitLabIssueView.h"

#include "GitLabIssueLayout.h"
#include "GitLabUrls.h"
#include "GitLabViewSupport.h"

#include <mirante/ItemContext.h>

namespace mirante::modules::gitlab {

IssueModel::IssueModel(ItemContext &context)
    : SummaryModel(context)
{
}

Pill IssueModel::state() const
{
    return statePillOf(m_issue.value(QStringLiteral("state")).toString());
}

void IssueModel::read(const ItemStatus &status)
{
    m_issue = status.data.value(QStringLiteral("issue")).toObject();
    // An empty list means no comments. A missing one means the request failed.
    const QJsonValue notes = status.data.value(QStringLiteral("notes"));
    m_comments = notes.toArray();
    if (!notes.isArray())
        m_commentsState = Comments::Unreadable;
    else if (m_comments.isEmpty())
        m_commentsState = Comments::None;
    else
        m_commentsState = Comments::Shown;
    setPageAddress(webAddress(m_issue, issueUrl(baseUrl(), config(), defaultProject())));
}

QWidget *createIssueView(ItemContext &context, QWidget *parent)
{
    auto *model = new IssueModel(context);
    model->follow(context.status());
    QWidget *view = makeIssueLayout(*model, parent);
    model->attach(view);
    return view;
}

} // namespace mirante::modules::gitlab
