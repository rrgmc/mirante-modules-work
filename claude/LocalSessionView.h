#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::claude {

// The view of a Claude session (local): its id, folder and device, and a
// button that resumes it in a terminal. See docs/item-types.md#claude-session-local.
QWidget *createLocalSessionView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::claude
