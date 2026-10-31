// The native view of a GitHub issue list. See README.md#list-views.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

QWidget *createIssueListView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::github
