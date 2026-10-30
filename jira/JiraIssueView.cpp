#include "JiraIssueView.h"

#include <mirante/ItemContext.h>

#include <QString>

namespace mirante::modules::jira {

QWidget *createIssueView(ItemContext &context, QWidget *parent)
{
    Q_UNUSED(context)
    Q_UNUSED(parent)
    return nullptr;
}

QWidget *createIssuePreview(ItemContext &context, const QString &key, QWidget *parent)
{
    Q_UNUSED(context)
    Q_UNUSED(key)
    Q_UNUSED(parent)
    return nullptr;
}

} // namespace mirante::modules::jira
