// Stubs until task T7 of the M11 plan implements README.md#quick-add.
#include "GitHubQuickAdd.h"

#include <optional>
#include <vector>

namespace mirante::modules::github {

void GitHubQuickAddHandler::propose(QuickAddRequest &request) const
{
    request.finish(std::nullopt);
}

void GitHubRelatedItemFinder::find(RelatedItemRequest &request) const
{
    request.finish({});
}

} // namespace mirante::modules::github
