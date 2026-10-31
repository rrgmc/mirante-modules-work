// The native view of a GitHub pull request. See README.md#summary-views.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

QWidget *createPullRequestView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::github
