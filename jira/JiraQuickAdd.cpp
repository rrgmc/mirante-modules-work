#include "JiraQuickAdd.h"

namespace mirante::modules::jira {

// Stubs until the M7 plan's task for this module: they recognise nothing.

void JiraQuickAddHandler::propose(QuickAddRequest &request) const { request.finish(std::nullopt); }

void JiraRelatedItemFinder::find(RelatedItemRequest &request) const { request.finish({}); }

} // namespace mirante::modules::jira
