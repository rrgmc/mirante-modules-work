#pragma once

#include <mirante/QuickAdd.h>

// Quick add for the github module. See docs/modules.md#quick-add and the M11
// plan, part 7.

namespace mirante::modules::github {

// Reads a pull request's address below the web base of the dialog's profile,
// or on github.com, and proposes a task with its PR summary and Actions run
// items, matched on the project setting "repository".
class GitHubQuickAddHandler : public QuickAddHandler
{
public:
    void propose(QuickAddRequest &request) const override;
};

// Offers PR summary items for the open pull requests whose title or head
// branch holds a Jira-style key from the texts: in the project's repository,
// else in the repositories of the profile's General Configuration.
class GitHubRelatedItemFinder : public RelatedItemFinder
{
public:
    void find(RelatedItemRequest &request) const override;
};

} // namespace mirante::modules::github
