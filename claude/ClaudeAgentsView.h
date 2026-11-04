// The native view of a Claude agents list: the core's table, drawn from the
// item's status. See README.md.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::claude {

QWidget *createAgentsView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::claude
