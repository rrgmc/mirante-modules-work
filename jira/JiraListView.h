// The native view of a Jira list. See README.md#views.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::jira {

QWidget *createListView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::jira
