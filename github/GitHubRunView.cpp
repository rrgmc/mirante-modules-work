// A stub until task T5 of the M11 plan implements the view.
#include "GitHubRunView.h"

#include <mirante/ItemContext.h>

#include <QCoreApplication>

namespace mirante::modules::github {

QWidget *createRunView(ItemContext &context, QWidget *parent)
{
    return context.createMessageView(QCoreApplication::translate("github", "This view is not ready yet."), parent);
}

} // namespace mirante::modules::github
