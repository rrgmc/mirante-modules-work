// The native view of the GitHub CI health list. See README.md#ci-health.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::github {

QWidget *createCiHealthView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::github
