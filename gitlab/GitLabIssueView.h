// The native view of a GitLab issue. See docs/plans/m6-native-item-types.md.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::gitlab {

QWidget *createIssueView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::gitlab
