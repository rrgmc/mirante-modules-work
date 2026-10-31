// The native view of a list of GitHub Actions runs. See README.md#list-views.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

QWidget *createRunListView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::github
