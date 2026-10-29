# gitlab module

GitLab merge requests, pipelines and issues as items. See [item types](../../../docs/item-types.md#gitlab).

## Item types

| Id | Name | Rendering |
|---|---|---|
| `gitlab.mr.summary` | GitLab MR (summary) | native |
| `gitlab.mr.page` | GitLab MR (page) | web |
| `gitlab.mr.list` | GitLab MR List | native |
| `gitlab.pipeline` | GitLab CI Pipeline | native |
| `gitlab.issue.summary` | GitLab Issue (summary) | native |
| `gitlab.issue.page` | GitLab Issue (page) | web |
| `gitlab.issue.list` | GitLab Issue List | native |

Since M3 the types declare their configuration fields, so items can be created and edited. Since M4 the two page types show their page in a web view. The other types still show a placeholder; their views arrive in a later milestone.

## Pages

The page types ask the core for a web view with `ItemContext::createWebView`. `GitLabUrls.cpp` builds the address from the connection's base URL, without doubling a slash at its end:

| Type | Address |
|---|---|
| `gitlab.mr.page` | `<baseUrl>/<project>/-/merge_requests/<iid>` |
| `gitlab.issue.page` | `<baseUrl>/<project>/-/issues/<iid>` |

An empty `project` uses the project's `gitlab.project` setting. When something is missing, the cell shows a message instead:

- without a `gitlab` connection: "No GitLab connection in this group.";
- without a project or a number: "Set the project and number in the item's settings."

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `project` | text | all | Project path, such as `group/project`. Empty uses the project's `gitlab.project` setting. |
| `iid` | integer | MR and issue summary and page; pipeline | The merge request or issue number in the project. Required, except for the pipeline, where it names the linked merge request. |
| `scope` | choice | lists | `project` (one project, the default), `group` (one group) or `all` (all projects visible to you). |
| `group` | text | lists | Group path, used when `scope` is `group`. |
| `preset` | choice | lists | MR List: `createdByMe` (the default), `reviewRequested`, `assignedToMe` or `allOpen`. Issue List: `assignedToMe` (the default), `createdByMe` or `allOpen`. |
| `filter` | text | lists | Optional custom filter parameters passed to the GitLab API, such as `labels=bug&target_branch=main`. |
| `branchSource` | choice | pipeline | `fixed` (the `branch` field, the default), `mainFolder` (the current branch of the main folder) or `mergeRequest` (the source branch of the merge request `iid`). |
| `branch` | text | pipeline | The branch, used when `branchSource` is `fixed`. |

The keys of each type, in the order the form shows them:

- `gitlab.mr.summary`, `gitlab.mr.page`, `gitlab.issue.summary`, `gitlab.issue.page`: `project`, `iid`.
- `gitlab.mr.list`, `gitlab.issue.list`: `scope`, `project`, `group`, `preset`, `filter`.
- `gitlab.pipeline`: `project`, `branchSource`, `branch`, `iid`.

## Connection kinds

Every type uses a `gitlab` connection. In M4 only the page types read it, for its base URL.

## Settings

The project setting `project`, read as `gitlab.project`: the default project path for items whose `project` is empty.
