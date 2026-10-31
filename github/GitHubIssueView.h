// The native view of a GitHub issue. See README.md#summary-views.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

QWidget *createIssueView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::github
