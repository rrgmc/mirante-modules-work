// The native view of a GitLab merge request list. See docs/plans/m6-native-item-types.md.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::gitlab {

QWidget *createMergeRequestListView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::gitlab
