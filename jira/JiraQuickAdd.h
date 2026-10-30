#pragma once

#include <mirante/QuickAdd.h>

// Quick add for the jira module. See docs/modules.md#quick-add and the M7
// plan, part 6.

namespace mirante::modules::jira {

// Reads a Jira issue address or a bare key and proposes a task with its Jira
// Issue (summary) item, matched on the project setting "projectKey".
class JiraQuickAddHandler : public QuickAddHandler
{
public:
    void propose(QuickAddRequest &request) const override;
};

// Offers Jira Issue (summary) items for the keys in the texts that exist on
// the server.
class JiraRelatedItemFinder : public RelatedItemFinder
{
public:
    void find(RelatedItemRequest &request) const override;
};

} // namespace mirante::modules::jira
