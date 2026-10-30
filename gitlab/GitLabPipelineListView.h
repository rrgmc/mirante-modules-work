// The native view of a list of GitLab pipelines. See decision 0071.
#pragma once

class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::gitlab {

QWidget *createPipelineListView(ItemContext &context, QWidget *parent);

} // namespace mirante::modules::gitlab
