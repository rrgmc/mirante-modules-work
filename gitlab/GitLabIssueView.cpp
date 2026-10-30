#include "GitLabIssueView.h"

#include <mirante/ItemContext.h>

#include <QString>

namespace mirante::modules::gitlab {

QWidget *createIssueView(ItemContext &context, QWidget *parent)
{
    Q_UNUSED(context)
    Q_UNUSED(parent)
    return nullptr;
}

QWidget *createIssuePreview(ItemContext &context, const QString &project, qint64 iid, QWidget *parent)
{
    Q_UNUSED(context)
    Q_UNUSED(project)
    Q_UNUSED(iid)
    Q_UNUSED(parent)
    return nullptr;
}

} // namespace mirante::modules::gitlab
