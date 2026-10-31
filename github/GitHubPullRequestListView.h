// The native view of a GitHub pull request list. See README.md#list-views.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

QWidget *createPullRequestListView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::github
