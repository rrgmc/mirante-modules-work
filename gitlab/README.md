# gitlab module

GitLab merge requests, pipelines and issues as items. See [item types](../../../docs/item-types.md#gitlab).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `gitlab.mr.summary` | GitLab MR (summary) | native | A summary of one merge request: its state, branches, people, pipeline and approvals. |
| `gitlab.mr.page` | GitLab MR (page) | web | The web page of one merge request, on the GitLab server of the item's profile. |
| `gitlab.mr.list` | GitLab MR List | native | A table of merge requests in a project, a group or everywhere, such as those that wait for your review. |
| `gitlab.pipeline` | GitLab CI Pipeline | native | The latest pipeline of a branch, with its stages and jobs. The branch is a fixed one, the main folder's current branch or a merge request's branch. |
| `gitlab.issue.summary` | GitLab Issue (summary) | native | A summary of one issue: its state, assignees, labels, milestone and latest comments. |
| `gitlab.issue.page` | GitLab Issue (page) | web | The web page of one issue, on the GitLab server of the item's profile. |
| `gitlab.issue.list` | GitLab Issue List | native | A table of issues in a project, a group or everywhere, such as those assigned to you. |

The module calls itself "GitLab" and describes itself as: "Merge requests, pipelines and issues from a GitLab server. Each profile sets its server and token in the module's General Configuration." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Since M3 the types declare their configuration fields, so items can be created and edited. Since M4 the two page types show their page in a web view. The other types still show a placeholder; their views arrive in a later milestone.

## Pages

The page types ask the core for a web view with `ItemContext::createWebView`. `GitLabUrls.cpp` builds the address from the base URL of the [General Configuration](#settings-screens) of the item's profile, read with `ItemContext::moduleSettings()`, without doubling a slash at its end:

| Type | Address |
|---|---|
| `gitlab.mr.page` | `<baseUrl>/<project>/-/merge_requests/<iid>` |
| `gitlab.issue.page` | `<baseUrl>/<project>/-/issues/<iid>` |

An empty `project` uses the project's `gitlab.project` setting. When something is missing, the cell shows a message instead:

- with a base URL that is not an `http` or `https` address: "Set the GitLab base URL in this profile's Modules > GitLab > General Configuration, in the Profiles menu.";
- without a project or a number: "Set the project and number in the item's settings."

## Items from a link

Every type implements `fromUrl()`, through `matchUrl()` in `GitLabUrls.cpp`. It reads `<base>/<project>/-/<kind>[/<number>]`, where `<base>` is the GitLab base URL of the profile the new item would use, if the address is below it. `fromUrl()` gets that profile's id and reads the screen there. Otherwise the server is taken without a path, so `gitlab.com` and other servers are still read by GitLab's `/-/`. The project needs a group, such as `group/project`. A tab after the number, such as `/diffs`, a query and a fragment are ignored. See [decision 0049](../../../docs/decisions/0049-items-from-a-pasted-link.md).

| Page | Types | Configuration | Name |
|---|---|---|---|
| `merge_requests/<iid>` | `gitlab.mr.summary`, `gitlab.mr.page` | `project`, `iid` | `group/project!42` |
| `merge_requests/<iid>` | `gitlab.pipeline` | `project`, `branchSource` = `mergeRequest`, `iid` | `group/project!42 CI` |
| `issues/<iid>` | `gitlab.issue.summary`, `gitlab.issue.page` | `project`, `iid` | `group/project#7` |
| `merge_requests` | `gitlab.mr.list` | `scope` = `project`, `project`, `preset` = `allOpen` | `group/project MRs` |
| `issues` | `gitlab.issue.list` | `scope` = `project`, `project`, `preset` = `allOpen` | `group/project issues` |
| `pipelines[/<id>][?ref=<branch>]` | `gitlab.pipeline` | `project`, `branchSource` = `fixed`, `branch` from `ref` | `group/project <branch> CI` |

The pipeline type follows a branch, so a pipeline's id is not kept. Other pages, such as a project's home page, `merge_requests/new` or `tree/main`, are refused. The tests are in `tests/tst_from_url.cpp`.

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `project` | text | all | Project path, such as `group/project`. Empty uses the project's `gitlab.project` setting. The form offers the paths of [General Configuration](#settings-screens) in the item's profile. |
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

## Settings

The project setting `project`, read as `gitlab.project`: the default project path for items whose `project` is empty.

## Settings screens

The module calls itself "GitLab", so each profile's Modules submenu shows Profiles > <profile> > Modules > GitLab. It has one [settings screen](../../../docs/modules.md#settings-screens), in `GitLabSettings.cpp`:

| Id | Name | Key | Type | Description |
|---|---|---|---|---|
| `general` | General Configuration | `baseUrl` | URL | The GitLab server, such as `https://gitlab.example.org/gitlab`. `https://gitlab.com` by default, and when empty. |
| | | `token` | secret | The token for that server. Never stored with the values: the core keeps it in the keychain, under `<profile id>/gitlab/token`. |
| | | `projects` | text list | Project paths the user works on, such as `group/project`, one per line. |

Each profile has its own values, stored in its entry of `workspace.mirante` under `moduleSettings.gitlab.general`, and synced. They hold no token. The screen is opened for one profile at a time. See decisions [0050](../../../docs/decisions/0050-profiles-hold-logins-connections-and-settings.md) and [0054](../../../docs/decisions/0054-module-settings-replace-connections.md), which replaced connections with the base URL.

The `project` field of every type offers the paths of the profile the item uses, in an editable drop-down list. The types do this in `fieldsForProfile(profileId)`, which the item dialog calls with the profile the edited item would use; `fields()` offers none. Each path is trimmed, slashes at its start and end are removed, and empty or repeated paths are left out. Any other path can still be typed. The list is read when the item dialog builds its form, and again when another profile is chosen in it, so a change shows the next time the dialog opens.
