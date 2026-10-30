#pragma once

#include <mirante/QuickAdd.h>

// Quick add for the gitlab module. See docs/modules.md#quick-add and the M7
// plan, part 5.

namespace mirante::modules::gitlab {

// Reads a merge request address and proposes a task with its MR summary and
// CI Pipeline items, matched on the project setting "project".
class GitLabQuickAddHandler : public QuickAddHandler
{
public:
    void propose(QuickAddRequest &request) const override;
};

// Offers MR summary items for the merge requests whose title or source branch
// holds a Jira-style key from the texts.
class GitLabRelatedItemFinder : public RelatedItemFinder
{
public:
    void find(RelatedItemRequest &request) const override;
};

} // namespace mirante::modules::gitlab
