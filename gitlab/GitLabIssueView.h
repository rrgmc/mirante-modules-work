// The native view of a GitLab issue. See docs/plans/m6-native-item-types.md.
#pragma once

#include <QtGlobal>

class QString;
class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::gitlab {

QWidget *createIssueView(ItemContext &context, QWidget *parent);

// The same summary of any issue, for a list's preview. It reads the issue
// itself, through the context's server.
QWidget *createIssuePreview(ItemContext &context, const QString &project, qint64 iid, QWidget *parent);

} // namespace mirante::modules::gitlab
