// The native view of a Jira issue. See docs/plans/m6-native-item-types.md.
#pragma once

class QString;
class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::jira {

QWidget *createIssueView(ItemContext &context, QWidget *parent);

// The same summary of any issue, for a list's preview. It reads the issue
// itself, through the context's server.
QWidget *createIssuePreview(ItemContext &context, const QString &key, QWidget *parent);

} // namespace mirante::modules::jira
