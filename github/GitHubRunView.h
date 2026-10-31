// The native view of the Actions runs of a commit. See README.md#summary-views.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

QWidget *createRunView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::github
