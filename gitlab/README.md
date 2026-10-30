# gitlab module

GitLab merge requests, pipelines and issues as items. See [item types](../../../docs/item-types.md#gitlab).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `gitlab.mr.summary` | GitLab MR (summary) | native | A summary of one merge request: its state, branches, people, pipeline and approvals. |
| `gitlab.mr.page` | GitLab MR (page) | web | The web page of one merge request, on the GitLab server of the item's profile. |
| `gitlab.mr.list` | GitLab MR List | native | A table of merge requests in a project, a group or everywhere, such as those that wait for your review. |
| `gitlab.pipeline` | GitLab CI Pipeline | native | The latest pipeline of a branch and those started with it, with their stages and jobs. The branch is a fixed one, the main folder's current branch or a merge request's branch. |
| `gitlab.issue.summary` | GitLab Issue (summary) | native | A summary of one issue: its state, assignees, labels, milestone and latest comments. |
| `gitlab.issue.page` | GitLab Issue (page) | web | The web page of one issue, on the GitLab server of the item's profile. |
| `gitlab.issue.list` | GitLab Issue List | native | A table of issues in a project, a group or everywhere, such as those assigned to you. |

The module calls itself "GitLab" and describes itself as: "Merge requests, pipelines and issues from a GitLab server. Each profile sets its server and token in the module's General Configuration." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Since M3 the types declare their configuration fields, so items can be created and edited. Since M4 the two page types show their page in a web view. Since M5 the module describes its [server](#server), and the merge request, issue and pipeline types report a [status](#status). Since M6 the merge request, pipeline and issue summaries have [native views](#summary-views), and the lists have [tables with links on each row](#list-views).

## Server

`GitLabServerType`, in `GitLabSettings.h`, tells the core where each profile's GitLab server is. The module registers it with `setServerType()`:

- the screen is `general` and the token field is `token`;
- the base URL is `baseUrlOf()` of the screen's values, so `https://gitlab.com` when none is set;
- `authorize()` puts the token in the `PRIVATE-TOKEN` header;
- `problem()` is always empty: a base URL and a token are all GitLab needs.

The module never reads the token itself. It makes every request through the `ApiClient` of the item's profile, which adds the header. See [servers and tokens](../../../docs/modules.md#servers-and-tokens).

## The client

`GitLabClient`, in `GitLabClient.cpp`, is a thin, copyable wrapper over an `ApiClient` and a receiver. It has one method per endpoint in [integrations](../../../docs/integrations.md#gitlab). Each takes the parsed arguments, such as a project path and a number, and passes the `ApiReply` on unchanged.

- Paths are relative to the base URL and start with `api/v4/`, so a server installed at a path works.
- Project and group paths are percent-encoded into the path: `acme/backend` becomes `acme%2Fbackend`.
- A branch is percent-encoded into its query, so a branch with `+` or `&` in its name reaches GitLab whole.
- Lists (the three merge request lists, the three issue lists, the two pipeline lists, a pipeline's jobs and an issue's notes) set `followLinks` with a row limit, and ask for `per_page` of at most 100, and of at most that limit, unless the filter names its own.
- An issue's notes are asked for with `sort=desc&order_by=created_at`, so the latest come first. System notes are included.
- The CI actions send an empty JSON body, `{}`. `createPipeline` names its branch in the path's query, because `ApiClient::post()` takes no query of its own.

| Method | Request |
|---|---|
| `currentUser` | `GET api/v4/user` |
| `mergeRequest` | `GET api/v4/projects/<project>/merge_requests/<iid>` |
| `approvals` | `GET api/v4/projects/<project>/merge_requests/<iid>/approvals` |
| `mergeRequests` | `GET api/v4/merge_requests` |
| `projectMergeRequests` | `GET api/v4/projects/<project>/merge_requests` |
| `groupMergeRequests` | `GET api/v4/groups/<group>/merge_requests` |
| `latestPipeline` | `GET api/v4/projects/<project>/pipelines/latest?ref=<branch>` |
| `pipelines` | `GET api/v4/projects/<project>/pipelines`, with a query such as `ref=<branch>` |
| `mergeRequestPipelines` | `GET api/v4/projects/<project>/merge_requests/<iid>/pipelines` |
| `pipelineJobs` | `GET api/v4/projects/<project>/pipelines/<id>/jobs` |
| `pipeline` | `GET api/v4/projects/<project>/pipelines/<id>` |
| `issue` | `GET api/v4/projects/<project>/issues/<iid>` |
| `issueNotes` | `GET api/v4/projects/<project>/issues/<iid>/notes?sort=desc&order_by=created_at` |
| `issues` | `GET api/v4/issues` |
| `projectIssues` | `GET api/v4/projects/<project>/issues` |
| `groupIssues` | `GET api/v4/groups/<group>/issues` |
| `retryPipeline`, `cancelPipeline` | `POST api/v4/projects/<project>/pipelines/<id>/retry`, `.../cancel` |
| `retryJob`, `playJob` | `POST api/v4/projects/<project>/jobs/<id>/retry`, `.../play` |
| `createPipeline` | `POST api/v4/projects/<project>/pipeline?ref=<branch>` |

The list and action methods are tested in `tests/tst_client.cpp`.

## Status

Every type reports status: `hasStatus()` is true and `fetchStatus()` runs a function of `GitLabStatus.cpp`, or of `GitLabLists.cpp` for the [lists](#lists). The rules for the types that name one object are in pure functions, `mergeRequestStatus()`, `issueStatus()` and `pipelineStatus()`, so they are tested without a job. "Me" is the user of `GET api/v4/user`, asked once per status job; the client's ETag memory makes it cheap.

| Types | Requests | Attention | Closed |
|---|---|---|---|
| `gitlab.mr.summary`, `gitlab.mr.page` | the merge request, its approvals and the current user, at once; for the summary, when `head_pipeline.status` is `failed`, then that pipeline's jobs | "Pipeline failed" (`pipeline-failed`, notify) when `head_pipeline.status` is `failed`; "Review requested" (`review`) when I am a reviewer and have not approved; "Approved" (`approved`, clears on view) when I am the author and `approved` is true; "Has conflicts" (`conflicts`) when I am the author and `has_conflicts` is true | `state` is `merged` or `closed` |
| `gitlab.issue.summary`, `gitlab.issue.page` | the issue; for the summary, then its latest 20 notes | none | `state` is `closed` |
| `gitlab.pipeline` | for `mergeRequest`, the merge request and then its `source_branch`; for `fixed`, `branch`; then the latest pipeline of the branch; then, at once, the latest 20 pipelines (`mergeRequestPipelines` or `pipelines?ref=<branch>`) and, for `fixed`, the open merge request of the branch (`projectMergeRequests`); then the jobs of each pipeline in the [group](#the-group), at once | "Pipeline failed" (`pipeline-failed:<pipeline id>`, notify) for each pipeline of the group whose `status` is `failed` | never |

- **Only open merge requests need attention.** A merged or closed merge request reports no reasons, even with a failed pipeline.
- **Summaries.** A merge request reads like "Open · 2 of 3 approvals · pipeline failed": the state, "draft" for an open draft, the approvals given of those required (or just the count when none are required), the head pipeline's status, and "has conflicts" while open. An issue reads like "Open · assigned to Ana". A pipeline reads like "Failed · main · 1 failed job"; failed jobs that are allowed to fail are not counted. `success` reads as "passed".
- **A branch without pipelines.** GitLab answers `404` for the latest pipeline of such a branch. The status is then `Ok` with "No pipeline on <branch>", unless the list has pipelines, as for a merge request whose pipelines run on a ref of its own.
- **The main folder's branch.** A pipeline with `branchSource` `mainFolder` reports `State::None` with "Follows the main folder from M7", without a request.
- **An incomplete configuration,** without a project, a number or a branch, reports `State::None` with a message, such as "Set the project and number in the item's settings.", without a request.
- **Errors.** Any other failed reply becomes its state with `statusFromReply()`, such as `NoToken` or `NotFound`. A merge request's own reply wins over those of the user and the approvals. A failed request for a summary's jobs or notes is not an error: the status stands, and `data` leaves them out. The same holds for a pipeline's list, a sibling's jobs and the open merge request of a branch. A failed request for the latest pipeline's jobs is an error. An unknown issue asks for no notes.
- **Data.** `data` holds what M6's summaries need: `mergeRequest`, `approvals` and `user` (`id` and `username`) for a merge request; `issue` for an issue; `branch`, `pipeline` and `jobs` for a pipeline, which also adds `siblings` and `older`, as [the group](#the-group) says, and `mergeRequest`, when it knows one. The objects are GitLab's answers, unchanged. A merge request summary adds `jobs` when its head pipeline failed. An issue summary adds `notes`, newest first, without the notes that have `system` set. The 20 are counted before system notes are left out, so `notes` can hold fewer. Page types add neither.

### The group

`pipelineGroup()` sorts the answers of decision [0072](../../../docs/decisions/0072-pipelines-started-together.md) by `created_at`, newest first. The newest leads, and it is the latest of the status: its summary, `pipeline` and `jobs`. An entry of a list lacks fields such as `duration`, so the answer to `pipelines/latest` takes the place of its own entry.

- **`siblings`** are the others created at most 10 minutes before the newest, each as `{"pipeline": ..., "jobs": [...]}`, without `jobs` if that request failed.
- **`older`** are the rest, as pipelines without jobs. A date that cannot be read counts as older. They need no attention.

The tests are in `tests/tst_status.cpp`, on responses in `tests/data/` recorded from a GitLab server, trimmed by hand, with names and emails replaced. `issue_notes.json`, `pipelines.json`, `pipeline_jobs_sibling.json` and `pipeline_jobs_running.json` were written by hand in the shape of GitLab's API.

## Summary views

`GitLabMergeRequestView.cpp`, `GitLabPipelineView.cpp` and `GitLabIssueView.cpp` each hold one class, a subclass of `GitLabSummary` in `GitLabViewSupport.h`, which is private to the module. The layouts, fields and rules are in [item types](../../../docs/item-types.md#gitlab). `GitLabSummary` holds what they share:

- **Drawing.** `follow()` draws the item's status now and at each change. With no data it shows "Loading…" or the status's message. With old data it keeps drawing that data, because the cell header shows the problem.
- **Actions.** `runAction()` marks the button busy and makes the request. After a success it refreshes the status. A failure shows `reply.message` in the action row. A `403` shows "The token cannot do this. It needs the api scope."
- **Open page.** It calls `ItemContext::showPage()`, or opens the external browser if `Shift` is held. It has the same address as the Web link, and both hide without a web address.
- **Dialogs.** The questions before cancelling and triggering run an event loop of their own, and the view can be made again during it. So each is on the heap, and nothing runs afterwards if it or the view is gone.

The action ids, which are also the buttons' object names, are `retry-pipeline`, `cancel-pipeline`, `trigger-pipeline`, `merge-request` and `open-page`. A job's buttons are `log-<job id>`, `retry-job-<job id>` and `play-job-<job id>`. They are not in the action row, so a job's Retry and Play clear the last error themselves. The merge request summary's link to its pipeline is `pipeline-web`.

`GitLabPipelineView.cpp` keeps more state than the other two:

- **What it shows.** `m_selectedId` is the pipeline the user chose, or 0 for the latest. It stays through a new status while that pipeline is still there. The latest is never held on to, so the view follows a newer one.
- **The head.** The state row is one widget that stays, so Previous and Next (`previous-pipeline`, `next-pipeline`) keep the keyboard's focus. Only the line of state and the pills (`pipeline-pill-<id>`) inside it change.
- **Jobs.** One section, `jobs`, holds one grid, so its columns line up across stages. A pipeline without `jobs` in the status, either older or a sibling whose request failed, asks for them with `pipelineJobs()` each time it shows. It shows what it last read meanwhile. Only the answer to the latest read counts.
- **The refresh.** A `QTimer` named `pipeline-refresh` asks the status source for a refresh every 15 seconds. It runs between the view's show and hide events, while the shown pipeline is unfinished, which is every status but `success`, `failed`, `canceled`, `skipped` and `manual`. A manual pipeline waits for a person, so it does not ask.

Text from a server is plain text in every label. The tests are in `tests/tst_summary_views.cpp`, with a fake context and a fake status source, on the responses in `tests/data/`.

## Lists

`gitlab.mr.list` and `gitlab.issue.list` report status too, from `GitLabLists.cpp`. See decisions [0067](../../../docs/decisions/0067-lists-report-status.md) and [0071](../../../docs/decisions/0071-lists-link-out-without-a-preview.md).

- **Sizes.** `listLimit()` and `listPageSize()` read the fields `limit` (100 by default) and `pageSize` (50 by default). Items from before these fields lack them, and a value below 1 uses the default, so neither needs a migration.
- **Requests.** The client method of the scope (`mergeRequests`, `projectMergeRequests` or `groupMergeRequests`, or the issue equivalents), up to `limit` rows. Only a merge request list with the `reviewRequested` preset asks for the current user first. If that request fails, the list is not asked for.
- **Status.** The state is `Ok`, and the summary reads "12 merge requests" or "12 issues", with singular forms, and "100+ merge requests" when the rows reach `limit`. A list reports no attention and leaves `closed` unset.
- **Data.** `rows` holds GitLab's answers, unchanged, and `total` their number.
- **An incomplete scope,** such as `group` without a group path, reports `State::None` with a message, such as "Set the group in the item's settings.", without a request. An empty `project` uses the project's `gitlab.project` setting.
- **Errors.** A failed reply becomes its state with `statusFromReply()`.

`listQuery()` builds the query from the preset. Each adds `order_by=updated_at`. A preset the type does not have, such as `reviewRequested` for issues, counts as the type's first one.

| Preset | Query |
|---|---|
| `createdByMe` | `state=opened&scope=created_by_me` |
| `assignedToMe` | `state=opened&scope=assigned_to_me` |
| `reviewRequested` | `state=opened&scope=all&reviewer_id=<me>` |
| `allOpen` | `state=opened&scope=all` |

The parameters of `filter` come last and replace those of the same name. A name the filter repeats, as in `labels[]=a&labels[]=b`, keeps all its values.

`listWebUrl()` builds the list's page on the site, without doubling a slash at the end of the base URL. It is empty when the scope is incomplete or there is no base URL.

| Scope | Address |
|---|---|
| `project` | `<baseUrl>/<project>/-/merge_requests` or `/issues` |
| `group` | `<baseUrl>/groups/<group>/-/merge_requests` or `/issues` |
| `all` | `<baseUrl>/dashboard/merge_requests` or `/issues` |

The tests are in `tests/tst_list_query.cpp`, on `merge_request_list.json` and `issue_list.json`, written by hand in the shape of GitLab's API.

## List views

`GitLabMergeRequestListView.cpp` and `GitLabIssueListView.cpp` each hold one class, a subclass of `GitLabList` in `GitLabListSupport.h`, which is private to the module. It is an `ItemListView` of the [view kit](../../../docs/plans/m6-native-item-types.md#part-2-the-view-kit). `NumberedList` reads a row's project and number. The columns and rules are in [item types](../../../docs/item-types.md#gitlab). `GitLabList` holds what they share:

- **Drawing.** `follow()` draws the item's status now and at each change, as a summary does. An empty list shows the table and a line of its own, because `setMessage()` shows instead of the table.
- **Rows.** A row's key is its project and number, such as `acme/backend!42`, because a list can span projects. `rowProject()` reads the project from `references.full`, else from `web_url` with the base URL of the item's profile. `project_id` is a number, so it is not used, and neither is the item's configuration. A row that names neither still draws, without requests or "Add". See [decision 0071](../../../docs/decisions/0071-lists-link-out-without-a-preview.md): there is no preview, and a click on a row only selects it.
- **Pages and dates.** The constructor calls `setPageSize()` with the `pageSize` field. `dateCell()` draws a date with `compactDate()` and sorts by the date itself. A missing date is a dash, and sorts as the oldest.
- **Links.** `linksCell()` makes the last column, whose ids are `web`, `open` and `add`, in that order. A link goes when the row cannot do it: without an address "Web" and "Open", without a project "Add". The cell's `sortKey` is the row's, because a cell with links has no text, and without it the order of equal rows would depend on the sort before.
- **The row menu** is a `QMenu` that belongs to the view and uses `popup()`, so nothing waits on it if the view is made again. It offers the same as the links, and "Copy link".

`GitLabMergeRequestListView.cpp` also fills the Pipeline and Approvals columns, as decisions [0067](../../../docs/decisions/0067-lists-report-status.md) and [0071](../../../docs/decisions/0071-lists-link-out-without-a-preview.md) describe:

- **Requests.** `askForCells()` runs while the view is visible, after each new status and each time the view shows. It asks with `GitLabClient::mergeRequest()` for the Pipeline column and `approvals()` for the Approvals column, for the first 25 of `visibleKeys()`. A column with its field off asks for nothing. A column hidden only because the cell is narrow still asks.
- **A new page, sort or filter.** `visibleKeysChanged()` runs `askForNewKeys()`. It asks only for rows that have not been asked for since the latest status, and a row that stays keeps what is on the way. An answer can move its row when the table is sorted on that column, and such a move does not ask again. Otherwise rows with an answer and rows without one could keep trading places, each asking the server again.
- **Receivers.** Each row's requests use a child `QObject` of the view as their receiver. A row leaving the first 25 deletes its own. A new status, or hiding the view, deletes them all. The answers on the way are dropped. `ServerClient` still sends requests that were already queued.
- **Answers.** The latest answers are kept by row, and new rows draw with them, so the cells do not flicker at each poll. A failed request puts the dash back, and so does a row leaving the first 25.

The table's cells draw text from a server as plain text. The tests are in `tests/tst_list_views.cpp`, with a fake context and a fake status source, on `merge_request_list.json` and `issue_list.json`.

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
| `pipelines[/<id>][?ref=<branch>]` | `gitlab.pipeline` | `project`, `branchSource` = `fixed`, `branch` from `ref`, or `master` without one | `group/project <branch> CI` |

The pipeline type follows a branch, so a pipeline's id is not kept. Other pages, such as a project's home page, `merge_requests/new` or `tree/main`, are refused. The tests are in `tests/tst_from_url.cpp`.

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `project` | text | all | Project path, such as `group/project`. Empty uses the project's `gitlab.project` setting. The form offers the paths of [General Configuration](#settings-screens) in the item's profile. |
| `iid` | integer | MR and issue summary and page; pipeline | The merge request or issue number in the project. Required, except for the pipeline, where it names the linked merge request. |
| `scope` | choice | MR and issue lists | `project` (one project, the default), `group` (one group) or `all` (all projects visible to you). |
| `group` | text | MR and issue lists | Group path, used when `scope` is `group`. |
| `preset` | choice | MR and issue lists | MR List: `createdByMe` (the default), `reviewRequested`, `assignedToMe` or `allOpen`. Issue List: `assignedToMe` (the default), `createdByMe` or `allOpen`. |
| `filter` | text | MR and issue lists | Optional custom filter parameters passed to the GitLab API, such as `labels=bug&target_branch=main`. |
| `branchSource` | choice | pipeline | `fixed` (the `branch` field, the default), `mainFolder` (the current branch of the main folder) or `mergeRequest` (the source branch of the merge request `iid`). |
| `branch` | text | pipeline | The branch, used when `branchSource` is `fixed`. `master` by default, also as its placeholder. |
| `limit` | integer | lists | How many rows it fetches. 100 by default, also when absent or below 1. |
| `pageSize` | integer | lists | How many rows the view shows at a time. 50 by default, also when absent or below 1. |

The keys of each type, in the order the form shows them:

- `gitlab.mr.summary`, `gitlab.mr.page`, `gitlab.issue.summary`, `gitlab.issue.page`: `project`, `iid`.
- `gitlab.mr.list`, `gitlab.issue.list`: `scope`, `project`, `group`, `preset`, `filter`, `limit`, `pageSize`.
- `gitlab.pipeline`: `project`, `branchSource`, `branch`, `iid`.

## Settings

The project setting `project`, "GitLab project path", a text field registered with `addProjectSetting()`. It is kept in the project's `moduleSettings.gitlab.project`, and read as `gitlab.project`: the default project path for items whose `project` is empty. The page views read it with `ItemContext::projectSetting()`, and status fetches with `StatusJob::projectSetting()`.

## Settings screens

The module calls itself "GitLab", so each profile's Modules submenu shows Profiles > <profile> > Modules > GitLab. It has one [settings screen](../../../docs/modules.md#settings-screens), in `GitLabSettings.cpp`:

| Id | Name | Key | Type | Description |
|---|---|---|---|---|
| `general` | General Configuration | `baseUrl` | URL | The GitLab server, such as `https://gitlab.example.org/gitlab`. `https://gitlab.com` by default, and when empty. |
| | | `token` | secret | The token for that server. Never stored with the values: the core keeps it in the keychain, under `<workspace id>/<profile id>/gitlab/token`. |
| | | `projects` | text list | Project paths the user works on, such as `group/project`, one per line. |

Each profile has its own values, stored in its entry of `workspace.mirante` under `moduleSettings.gitlab.general`, and synced. They hold no token. The screen is opened for one profile at a time. See decisions [0050](../../../docs/decisions/0050-profiles-hold-logins-connections-and-settings.md) and [0054](../../../docs/decisions/0054-module-settings-replace-connections.md), which replaced connections with the base URL.

The `project` field of every type offers the paths of the profile the item uses, in an editable drop-down list. The types do this in `fieldsForProfile(profileId)`, which the item dialog calls with the profile the edited item would use; `fields()` offers none. Each path is trimmed, slashes at its start and end are removed, and empty or repeated paths are left out. Any other path can still be typed. The list is read when the item dialog builds its form, and again when another profile is chosen in it, so a change shows the next time the dialog opens.
