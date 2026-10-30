#include "GitLabQuickAdd.h"

namespace mirante::modules::gitlab {

// Stubs until the M7 plan's task for this module: they recognise nothing.

void GitLabQuickAddHandler::propose(QuickAddRequest &request) const { request.finish(std::nullopt); }

void GitLabRelatedItemFinder::find(RelatedItemRequest &request) const { request.finish({}); }

} // namespace mirante::modules::gitlab
