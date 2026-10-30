// The native view of a GitLab merge request. See docs/plans/m6-native-item-types.md.
#pragma once

#include <QtGlobal>

class QString;
class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::gitlab {

QWidget *createMergeRequestView(ItemContext &context, QWidget *parent);

// The same summary of any merge request, for a list's preview. It reads the
// merge request itself, through the context's server.
QWidget *createMergeRequestPreview(ItemContext &context, const QString &project, qint64 iid, QWidget *parent);

} // namespace mirante::modules::gitlab
