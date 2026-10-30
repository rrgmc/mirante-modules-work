// The native view of a Jira list. See docs/plans/m6-native-item-types.md.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::jira {

QWidget *createListView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::jira
