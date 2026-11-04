# gitlab module

GitLab merge requests, pipelines and issues as items. See [item types](../../../docs/item-types.md#gitlab).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `gitlab.mr.summary` | GitLab MR (summary) | native | A summary of one merge request: its state, branches, people, pipeline and approvals. |
| `gitlab.mr.page` | GitLab MR (page) | web | The web page of one merge request, on the GitLab server of the item's profile. |
| `gitlab.mr.list` | GitLab MR List | native | A table of merge requests in a project, a group or everywhere, such as those that wait for your review. |
| `gitlab.mr.multilist` | GitLab MR List (several projects) | native | A table of the merge requests of the projects you name, such as those that wait for your review, with a filter on the project. |
| `gitlab.pipeline` | GitLab CI Pipeline | native | The latest pipeline of a branch and those started with it, with their stages and jobs. The branch is a fixed one, the main folder's current branch or a merge request's branch. |
| `gitlab.pipeline.list` | GitLab CI Pipeline List | native | A table of a project's pipelines: all of them, those you started, or those of a branch or a merge request. |
| `gitlab.pipeline.multilist` | GitLab CI Pipeline List (several projects) | native | A table of the pipelines of the projects you name, newest first: all of them, those you started, or those of a branch, with a filter on the project. |
| `gitlab.issue.summary` | GitLab Issue (summary) | native | A summary of one issue: its state, assignees, labels, milestone and latest comments. |
| `gitlab.issue.page` | GitLab Issue (page) | web | The web page of one issue, on the GitLab server of the item's profile. |
| `gitlab.issue.list` | GitLab Issue List | native | A table of issues in a project, a group or everywhere, such as those assigned to you. |

The module calls itself "GitLab" and describes itself as: "Merge requests, pipelines and issues from a GitLab server. Each profile sets its server and token in the module's General Configuration." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Its UUID is `24254446-1d00-47a0-b8de-b7c5f286588a`. It is at version 0.1.0, by Rangel Reale. Help > Modules also shows these, and the module's homepage. See [decision 0089](../../../docs/decisions/0089-module-identity.md).

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
| `pipelines` | `GET api/v4/projects/<project>/pipelines`, with a query such as `ref=<branch>` or `username=<me>` |
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
| `gitlab.pipeline` | for `mergeRequest`, the merge request and then its `source_branch`; for `fixed`, `branch`; for `mainFolder`, the main folder's branch; then the latest pipeline of the branch; then, at once, the latest 20 pipelines (`mergeRequestPipelines` or `pipelines?ref=<branch>`) and, for `fixed` and `mainFolder`, the open merge request of the branch (`projectMergeRequests`); then the jobs of each pipeline in the [group](#the-group), at once | "Pipeline failed" (`pipeline-failed:<pipeline id>`, notify) for each pipeline of the group whose `status` is `failed` | never |

- **Only open merge requests need attention.** A merged or closed merge request reports no reasons, even with a failed pipeline.
- **Summaries.** A merge request reads like "Open · 2 of 3 approvals · pipeline failed": the state, "draft" for an open draft, the approvals given of those required (or just the count when none are required), the head pipeline's status, and "has conflicts" while open. An issue reads like "Open · assigned to Ana". A pipeline reads like "Failed · main · 1 failed job"; failed jobs that are allowed to fail are not counted. `success` reads as "passed".
- **A branch without pipelines.** GitLab answers `404` for the latest pipeline of such a branch. The status is then `Ok` with "No pipeline on <branch>", unless the list has pipelines, as for a merge request whose pipelines run on a ref of its own.
- **The main folder's branch.** A pipeline with `branchSource` `mainFolder` reads the main folder's path with `job.folderPath("")` and its branch with `job.gitHead(path)`, at each fetch, and then works as a `fixed` branch. The status cache watches the head, so a switch of branch fetches again. Without a path it reports `State::None` with "Set the main folder's path on this device.", and without a branch, such as on a detached head, "The main folder is not on a branch.", both without a request. The job gives an empty path both for a project without a main folder and for one not mapped on this device, so the first message covers both.
- **An incomplete configuration,** without a project, a number or a branch, reports `State::None` with a message, such as "Set the project and number in the item's settings.", without a request.
- **Errors.** Any other failed reply becomes its state with `statusFromReply()`, such as `NoToken` or `NotFound`. A merge request's own reply wins over those of the user and the approvals. A failed request for a summary's jobs or notes is not an error: the status stands, and `data` leaves them out. The same holds for a pipeline's list, a sibling's jobs and the open merge request of a branch. A failed request for the latest pipeline's jobs is an error. An unknown issue asks for no notes.
- **Data.** `data` holds what M6's summaries need: `mergeRequest`, `approvals` and `user` (`id` and `username`) for a merge request; `issue` for an issue; `branch`, `pipeline` and `jobs` for a pipeline, which also adds `siblings` and `older`, as [the group](#the-group) says, and `mergeRequest`, when it knows one. The objects are GitLab's answers, unchanged. A merge request summary adds `jobs` when its head pipeline failed. An issue summary adds `notes`, newest first, without the notes that have `system` set. The 20 are counted before system notes are left out, so `notes` can hold fewer. Page types add neither.

### The group

`pipelineGroup()` sorts the answers of decision [0072](../../../docs/decisions/0072-pipelines-started-together.md) by `created_at`, newest first. The newest leads, and it is the latest of the status: its summary, `pipeline` and `jobs`. An entry of a list lacks fields such as `duration`, so the answer to `pipelines/latest` takes the place of its own entry.

- **`siblings`** are the others created at most 10 minutes before the newest, each as `{"pipeline": ..., "jobs": [...]}`, without `jobs` if that request failed.
- **`older`** are the rest, as pipelines without jobs. A date that cannot be read counts as older. They need no attention.

The tests are in `tests/tst_status.cpp`, on responses in `tests/data/` recorded from a GitLab server, trimmed by hand, with names and emails replaced. `issue_notes.json`, `pipelines.json`, `pipeline_jobs_sibling.json` and `pipeline_jobs_running.json` were written by hand in the shape of GitLab's API.

## Summary views

Each summary has two parts: a **model**, which the module keeps, and a **layout**, which draws the model and which an app can replace. See [layouts](#layouts). `GitLabIssueView.cpp`, `GitLabMergeRequestView.cpp` and `GitLabPipelineView.cpp` hold the models; `GitLab*LayoutDefault.cpp` hold the default layouts, which this section describes. The layouts, fields and rules are in [item types](../../../docs/item-types.md#gitlab). `SummaryModel`, the models' base in `GitLabSummaryModel.h`, holds what they share:

- **Drawing.** `follow()` reads the item's status now and at each change, and says `changed()`. With no data, `hasData()` is false and `message()` says "Loading…" or the status's message. With old data it keeps that data through an error, because the cell header shows the problem. A status without data, with `State::None` and a message, such as "The main folder is not on a branch.", replaces the old data, since the item has nothing to show.
- **Actions.** `runAction()` clears the last error, marks the action busy and makes the request. After a success it refreshes the status. A failure sets `error()` to `reply.message`, and a `403` to "The token cannot do this. It needs the api scope." A default layout shows these in its action row.
- **Open page.** `openPage()` calls `ItemContext::showPage()`, or opens the external browser if `Shift` is held. `pageAddress()` is the same address as the Web link, and both hide without a web address.
- **Dialogs.** The questions before cancelling and triggering run an event loop of their own, and the view can be made again during it. So each is on the heap, and nothing runs afterwards if it or the view is gone.

The action ids, which are also the buttons' object names, are `retry-pipeline`, `cancel-pipeline`, `trigger-pipeline`, `merge-request` and `open-page`. A job's buttons are `log-<job id>`, `retry-job-<job id>` and `play-job-<job id>`. They are not in the action row, so the default layout disables them while `isBusy()` says their request runs. The merge request summary's link to its pipeline is `pipeline-web`.

The pipeline model, `PipelineModel`, keeps more state than the other two:

- **What it shows.** `shownIndex()` is the pipeline the user chose with `showPipeline()`, or the latest. It stays through a new status while that pipeline is still there. The latest is never held on to, so the view follows a newer one.
- **The head.** In the default layout, the state row is one widget that stays, so Previous and Next (`previous-pipeline`, `next-pipeline`) keep the keyboard's focus. Only the line of state and the pills (`pipeline-pill-<id>`) inside it change.
- **Jobs.** `jobs()` are in the order they were created, and `stages()` in the order of their first job. The default layout's section, `jobs`, holds one grid, so its columns line up across stages. A pipeline without `jobs` in the status, either older or a sibling whose request failed, asks for them with `pipelineJobs()` each time it shows. It shows what it last read meanwhile. Only the answer to the latest read counts.
- **The refresh.** The model's `QTimer`, named `pipeline-refresh`, asks the status source for a refresh every 15 seconds, whatever the layout. It runs while the view is visible, while the shown pipeline is unfinished, which is every status but `success`, `failed`, `canceled`, `skipped` and `manual`. A manual pipeline waits for a person, so it does not ask.

Text from a server is plain text in every label. The tests of the models are in `tests/tst_summary_models.cpp`, and those of the default layouts in `tests/tst_summary_views.cpp`, with a fake context and a fake status source from `tests/SummaryTestSupport.h`, on the responses in `tests/data/`. The layouts' tests skip when an app replaces a layout; the models' tests run in every build.

## Layouts

An app can replace the whole layout of each summary when it builds Mirante. **The contract** and how an app names an extension, in its own repository or in a repository of extensions, are in [replacing the layout of a view](../README.md#replacing-the-layout-of-a-view).

| Point | Interface | Model | Default |
|---|---|---|---|
| `issuelayout` | `makeIssueLayout()` in `GitLabIssueLayout.h` | `IssueModel` | `GitLabIssueLayoutDefault.cpp` |
| `mergerequestlayout` | `makeMergeRequestLayout()` in `GitLabMergeRequestLayout.h` | `MergeRequestModel` | `GitLabMergeRequestLayoutDefault.cpp` |
| `pipelinelayout` | `makePipelineLayout()` in `GitLabPipelineLayout.h` | `PipelineModel` | `GitLabPipelineLayoutDefault.cpp` |

An extension defines its make function in the namespace `mirante::modules::gitlab`.

**What every model gives** (`SummaryModel`, in `GitLabSummaryModel.h`):

- `hasData()` and `message()`: without data, show the message alone, such as "Loading…".
- `config()` and `isSectionShown(section)`: the item's settings, and its `show<Section>` fields, on when absent.
- `pageAddress()` and `openPage()`.
- `isBusy(action)`, `error()` and `clearError()`, with the signals `busyChanged(action)` and `errorChanged()`.
- `refresh()`, to read the status again.
- `Pill` values, a text and a `widgets::StatusRole`, for every state, so a layout words and colours states as the module does.

**What each model adds:**

- `IssueModel`: `issue()`, GitLab's issue object; `state()`; `comments()`, GitLab's notes, newest first; and `commentsState()`: `Shown`, `None` or `Unreadable`.
- `MergeRequestModel`: `mergeRequest()`, GitLab's object; `state()` and `isDraft()`; `pipeline()`, the head pipeline, with `pipelineState()` and `pipelineAddress()`; `failedJobs()`, each with its state, duration and log; `approvals()`: those given, those required and the approvers' names; and `canRetryPipeline()` with the action `retryPipeline()`.
- `PipelineModel`: `pipelines()`, newest first, each with its title, state, creation, duration, and whether it is of the group; `branch()`; `shownIndex()`, `shownPipeline()` and `showPipeline(index)`; `jobs()`, `stages()` and `jobsState()`: `Loading`, `Shown` or `Unreadable`; `canRetry()`, `canCancel()`, `canTrigger()` and `mergeRequestAddress()`; and the actions `retry()`, `cancel()`, `trigger()`, `retryJob(id)` and `playJob(id)`. `cancel()` and `trigger()` do not ask first: a layout that wants a confirmation asks before it calls them, as the default does.

The point headers and `GitLabSummaryModel.h` are an interface that apps build against; a change to them is listed in [`CHANGELOG.md`](../CHANGELOG.md).

## Lists

`gitlab.mr.list`, `gitlab.issue.list` and `gitlab.pipeline.list` report status too, from `GitLabLists.cpp`. See decisions [0067](../../../docs/decisions/0067-lists-report-status.md) and [0071](../../../docs/decisions/0071-lists-link-out-without-a-preview.md).

- **Sizes.** `listLimit()` and `listPageSize()` read the fields `limit` (100 by default) and `pageSize` (50 by default). Items from before these fields lack them, and a value below 1 uses the default, so neither needs a migration.
- **Requests.** The client method of the scope (`mergeRequests`, `projectMergeRequests` or `groupMergeRequests`, or the issue equivalents), up to `limit` rows. Only a merge request list with the `reviewRequested` preset asks for the current user first. If that request fails, the list is not asked for.
- **Status.** The state is `Ok`, and the summary reads "12 merge requests", "12 issues" or "12 pipelines", with singular forms, and "100+ merge requests" when the rows reach `limit`. A list reports no attention and leaves `closed` unset.
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

`fetchPipelineListStatus()` serves `gitlab.pipeline.list`, whose `source` picks the request. An absent `source` is `all`.

| Source | Request |
|---|---|
| `all` | `pipelines()` |
| `mine` | `currentUser()`, then `pipelines()` with `username=<me>`. If the first fails, the list is not asked for. |
| `branch` | `pipelines()` with `ref=<branch>`. An empty `branch` is `master`. |
| `mergeRequest` | `mergeRequestPipelines()` with `iid` |

Without a project, without a number for `mergeRequest`, or with an unknown `source`, it reports `State::None` with a message, without a request. `pipelinesUrl()`, in `GitLabUrls.cpp`, builds the list's page, `<baseUrl>/<project>/-/pipelines`.

### Several projects

`gitlab.mr.multilist` and `gitlab.pipeline.multilist` list the projects of their `projects` field, one `group/project` per line. The field's **Add from List…** button offers the project paths of the profile's settings, as `fieldsForProfile()` puts them in its `suggestions` (decision 0139 in mirante-core). They are types of their own, so that their forms leave out what fits one project only: the scope, the group and a merge request number. `configuredProjects()` reads the field, tidied as `projectPath()` does, without empty and repeated values. Without any, the list reports `State::None` with "Set the projects in the item's settings.", without a request. The functions above take `ListSpan::Projects` for these types.

GitLab has no request for the merge requests or pipelines of several projects, so each list asks once per project, each up to `limit`, with the query of one project. The rows are joined, sorted newest first, by `updated_at` for merge requests and `created_at` for pipelines, and cut to `limit`. `reviewRequested` and `mine` read the user once. The pipeline sources are `all`, `mine` and `branch`. A pipeline row also gets `project_path` (`kRowProjectKey`), the path of its project, since GitLab's row has only `project_id`. The first project that fails fails the list, with its path before the message, such as "acme/web: Forbidden (403)". The lists have no Web link, as GitLab has no page for several projects, and the description names the projects, joined by commas.

### Kept filters

Each filter above a table keeps the user's last choice. When the user changes a filter, the view writes the chosen values to the item's key `<column>Filter`, an array of texts: `statusFilter` for the pipeline lists, and `projectFilter` for the lists over several projects. `keepFilter()` writes the key, and an empty choice, "All", removes it. `filterDefault()` reads it at the next start, trimmed, without empty values. A value that no row has is chosen once a row has it. The keys are the list types' `viewKeys()`, so writing them keeps the view, as [decision 0081](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0081-views-that-keep-their-own-keys.md) in the core says. They live in the item's configuration, so they sync with it. The item's form does not show them.

The tests are in `tests/tst_list_query.cpp`, on `merge_request_list.json`, `issue_list.json` and `pipeline_list.json`, written by hand in the shape of GitLab's API.

## List views

Each list has two parts: a **model**, which the module keeps, and a **layout**, which draws the model and which an app can replace. See [list layouts](#list-layouts). `GitLabMergeRequestListView.cpp`, `GitLabIssueListView.cpp` and `GitLabPipelineListView.cpp` hold the models; `GitLab*ListLayoutDefault.cpp` hold the default layouts, which draw an `ItemListView` of the [view kit](../../../docs/plans/m6-native-item-types.md#part-2-the-view-kit) through `DefaultListLayout` in `GitLabListSupport.h`. The columns and rules are in [item types](../../../docs/item-types.md#gitlab). `ListModel`, the models' base in `GitLabListModel.h`, and the default layouts hold what they share:

- **Drawing.** `follow()` reads the item's status now and at each change, as a summary's model does, and says `changed()`. An empty list shows the table and a line of its own, `emptyText()`, because `setMessage()` shows instead of the table.
- **Rows.** A row's key is its project and number, such as `acme/backend!42`, because a list can span projects. `rowProject()` reads the project from `references.full`, else from `web_url` with the base URL of the item's profile. `project_id` is a number, so it is not used, and neither is the item's configuration. A row that names neither still draws, without requests or "Add". A pipeline's key is its id, and its project is its `project_path` in a list over several projects, else the item's. See [decision 0071](../../../docs/decisions/0071-lists-link-out-without-a-preview.md): there is no preview, and a click on a row only selects it.
- **Pages and dates.** `pageSize()` reads the `pageSize` field, and the default layout calls `setPageSize()` with it. `dateCell()` draws a date with `compactDate()` and sorts by the date itself. A missing date is a dash, and sorts as the oldest.
- **Links.** The row actions are the model's: `openWeb()`, `openPage()`, `addToTask()` and `copyLink()`, by the row's key. In the default layout, `linksCell()` makes the last column, whose ids are `web`, `open` and `add`, in that order. A link goes when the row cannot do it: without an address "Web" and "Open", without a project or a ref "Add". The cell's `sortKey` is the row's, because a cell with links has no text, and without it the order of equal rows would depend on the sort before.
- **The row menu** is a `QMenu` that belongs to the view and uses `popup()`, so nothing waits on it if the view is made again. It offers the same as the links, and "Copy link".

The merge request model, `MergeRequestListModel`, also reads each row's head pipeline and approvals, which fill the default layout's Pipeline and Approvals columns, as decisions [0067](../../../docs/decisions/0067-lists-report-status.md) and [0071](../../../docs/decisions/0071-lists-link-out-without-a-preview.md) describe:

- **Requests.** The layout tells the model which rows it shows with `setVisibleRows()`, after each `changed()` and whenever they change; the default layout passes `visibleKeys()`. While the view is visible, after each new status and each time the view shows, the model asks with `GitLabClient::mergeRequest()` for the pipeline and `approvals()` for the approvals, for the first 25 of them (`AskedRows`). A column with its field off asks for nothing. A column hidden only because the cell is narrow still asks.
- **A new page, sort or filter.** The model asks only for rows that have not been asked for since the latest status, and a row that stays keeps what is on the way. An answer can move its row when the table is sorted on that column, and such a move does not ask again. Otherwise rows with an answer and rows without one could keep trading places, each asking the server again.
- **Receivers.** Each row's requests use a child `QObject` of the model as their receiver. A row leaving the first 25 deletes its own. A new status, or hiding the view, deletes them all. The answers on the way are dropped. `ServerClient` still sends requests that were already queued.
- **Answers.** The latest answers are kept by row, and new rows draw with them, so the cells do not flicker at each poll. Each answer says `rowChanged(key)`, and the default layout then sets only the cells that changed. A failed request puts the dash back, and so does a row leaving the first 25.

The pipeline model makes no further requests, so Author and Started show what the list's answer holds. GitLab leaves `user` and `started_at` out of its list answers, so Author usually shows a dash, and Started falls back to `created_at`. "Add" reads `refs/merge-requests/<iid>/head`, `/merge` and `/train` as that merge request, and any other ref as a branch.

A list over several projects shows a Project column after the Title or Id column, with a filter on it before the others. A list of one project hides that column and has no such filter. Each filter keeps the user's last choice, as [kept filters](#kept-filters) says.

The table's cells draw text from a server as plain text. The tests of the models are in `tests/tst_list_models.cpp`, and those of the default layouts in `tests/tst_list_views.cpp`, with a fake context and a fake status source from `tests/ListTestSupport.h`, on `merge_request_list.json`, `issue_list.json` and `pipeline_list.json`. The layouts' tests skip when an app replaces a list layout; the models' tests run in every build.

## List layouts

An app can replace the whole layout of each list when it builds Mirante, as for the [summaries](#layouts). **The contract** and how an app names an extension are in [replacing the layout of a view](../README.md#replacing-the-layout-of-a-view).

| Point | Interface | Model | Default |
|---|---|---|---|
| `issuelistlayout` | `makeIssueListLayout()` in `GitLabIssueListLayout.h` | `IssueListModel` | `GitLabIssueListLayoutDefault.cpp` |
| `mergerequestlistlayout` | `makeMergeRequestListLayout()` in `GitLabMergeRequestListLayout.h` | `MergeRequestListModel` | `GitLabMergeRequestListLayoutDefault.cpp` |
| `pipelinelistlayout` | `makePipelineListLayout()` in `GitLabPipelineListLayout.h` | `PipelineListModel` | `GitLabPipelineListLayoutDefault.cpp` |

**What every list model gives** (`ListModel`, in `GitLabListModel.h`): `hasData()` and `message()`; `description()`, `pageAddress()`, `emptyText()` and `pageSize()`; `config()` and `isColumnShown(column)`, which reads the `show<Column>` fields; `spansProjects()`, whether the list is over several projects; `filterDefault(column)`, the values a filter on a column starts with, from the item's `<column>Filter` key, and `keepFilter(column, values)`, which keeps the user's choice there; `keys()` and `row(key)`, with GitLab's row object, its page and whether it can be added; the row actions `openWeb()`, `openPage()`, `addToTask()` and `copyLink()`; and `setVisibleRows(keys)`, which a layout calls after each `changed()` and whenever the rows it shows change.

**What each model adds:**

- `IssueListModel`: `issues()` and `issue(key)`, each with its project, iid, ref, title, assignees, labels and update time.
- `MergeRequestListModel`: `mergeRequests()` and `mergeRequest(key)`, each with its project, iid, ref, title, author, update time, `pipeline` (a `Pill`, empty until the answer comes) and `approvals`: whether answered, those given and those required. Only the first 25 shown rows, `AskedRows`, get them, and each answer says `rowChanged(key)`.
- `PipelineListModel`: `pipelines()` and `pipeline(key)`, each with its id, project, state (a `Pill`), ref, author and start time.

A layout that does not use `ItemListView` sorts, filters and pages the rows itself. The headers and `GitLabListModel.h` are an interface that apps build against; a change to them is listed in [`CHANGELOG.md`](../CHANGELOG.md).

## Pages

The page types ask the core for a web view with `ItemContext::createWebView`. `GitLabUrls.cpp` builds the address from the base URL of the [General Configuration](#settings-screens) of the item's profile, read with `ItemContext::moduleSettings()`, without doubling a slash at its end:

| Type | Address |
|---|---|
| `gitlab.mr.page` | `<baseUrl>/<project>/-/merge_requests/<iid>` |
| `gitlab.issue.page` | `<baseUrl>/<project>/-/issues/<iid>` |

An empty `project` uses the project's `gitlab.project` setting. When something is missing, the cell shows a message instead:

- with a base URL that is not an `http` or `https` address: "Set the GitLab base URL in Profiles > Manage Profiles…, under this profile's GitLab.";
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
| `pipelines`, without `ref` or an id | `gitlab.pipeline.list` | `project`, `source` = `all` | `group/project pipelines` |

The pipeline type follows a branch, so a pipeline's id is not kept. The item dialog lists types by name, so for the page of all pipelines it offers the pipeline first and the list after it. Other pages, such as a project's home page, `merge_requests/new` or `tree/main`, are refused. The tests are in `tests/tst_from_url.cpp`.

## Quick add

`GitLabQuickAdd.cpp` holds the module's [quick add](../../../docs/modules.md#quick-add) handler and finder. The tests are in `tests/tst_quick_add.cpp`, on `merge_request.json` and on `merge_requests_open.json` and `merge_requests_search_abc_*.json`, which were written by hand in the shape of GitLab's API.

**`GitLabQuickAddHandler`** takes a merge request's address, read by `readPageAddress()` as [items from a link](#items-from-a-link) does, but only below the base URL of the General Configuration screen in the dialog's profile. Any other text, an address on another server, or no server finishes with nothing at once, without a request. Otherwise it asks for the merge request with `mergeRequest()`, and a failed reply finishes with nothing. It proposes:

- **Title:** the merge request's `title`.
- **Items:** `gitlab.mr.summary` named `!42`, with `project` and `iid`; `gitlab.pipeline` named `!42 CI`, with `project`, `branchSource` = `mergeRequest` and `iid`.
- **Project:** the project whose setting `project` is the address's project path.
- **Texts:** the title and the `source_branch`.

**`GitLabRelatedItemFinder`** reads Jira-style keys in the texts, `[A-Za-z][A-Za-z0-9_]+-\d+`, in upper case, at most five distinct ones in the order they appear. Without keys it finishes at once, without a request.

- **With the project setting `project`:** one request for the project's open merge requests, `projectMergeRequests()` with `state=opened&order_by=updated_at&sort=desc`, up to 100. A merge request matches a key when its `title` or `source_branch` holds it, without case.
- **Without it:** for each key, `GET api/v4/merge_requests?scope=all&state=opened&search=<key>&in=title`, up to 100, which searches titles only. GitLab's search also matches the key's words apart, so the title is checked for the key again.

Each match is a `gitlab.mr.summary` with `project`, from `references.full`, and `iid`. It is named `!42` in the project of the setting, else `group/project!42`. A failed request finds nothing for its keys. The finder finishes once, after every reply, with each merge request once, in the order of the keys.

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `project` | text | all | Project path, such as `group/project`. Empty uses the project's `gitlab.project` setting. The form offers the paths of [General Configuration](#settings-screens) in the item's profile. |
| `iid` | integer | MR and issue summary and page; pipeline; pipeline list | The merge request or issue number in the project. Required, except for the pipeline and the pipeline list, where it names the linked merge request. |
| `scope` | choice | MR and issue lists | `project` (one project, the default), `group` (one group) or `all` (all projects visible to you). |
| `group` | text | MR and issue lists | Group path, used when `scope` is `group`. |
| `preset` | choice | MR and issue lists | MR List: `createdByMe` (the default), `reviewRequested`, `assignedToMe` or `allOpen`. Issue List: `assignedToMe` (the default), `createdByMe` or `allOpen`. |
| `filter` | text | MR and issue lists | Optional custom filter parameters passed to the GitLab API, such as `labels=bug&target_branch=main`. |
| `branchSource` | choice | pipeline | `fixed` (the `branch` field, the default), `mainFolder` (the current branch of the main folder) or `mergeRequest` (the source branch of the merge request `iid`). |
| `branch` | text | pipeline; pipeline list | The branch, used when `branchSource` or `source` says so. `master` by default, also as its placeholder, and an empty value means `master`. |
| `source` | choice | pipeline list | `all` (the default), `mine` (started by the current user), `branch` (of the `branch` field) or `mergeRequest` (of the merge request `iid`). |
| `limit` | integer | lists | How many rows it fetches. 100 by default, also when absent or below 1. |
| `pageSize` | integer | lists | How many rows the view shows at a time. 50 by default, also when absent or below 1. |

The keys of each type, in the order the form shows them:

- `gitlab.mr.summary`, `gitlab.mr.page`, `gitlab.issue.summary`, `gitlab.issue.page`: `project`, `iid`.
- `gitlab.mr.list`, `gitlab.issue.list`: `scope`, `project`, `group`, `preset`, `filter`, `limit`, `pageSize`.
- `gitlab.pipeline`: `project`, `branchSource`, `branch`, `iid`.
- `gitlab.pipeline.list`: `project`, `source`, `branch`, `iid`, `limit`, `pageSize`.
- `gitlab.mr.multilist`: `projects`, `preset`, `filter`, `limit`, `pageSize`, `showPipeline`, `showApprovals`.
- `gitlab.pipeline.multilist`: `projects`, `source`, `branch`, `limit`, `pageSize`.

## Settings

The project setting `project`, "GitLab project path", a text field registered with `addProjectSetting()`. It is kept in the project's `moduleSettings.gitlab.project`, and read as `gitlab.project`: the default project path for items whose `project` is empty. The page views read it with `ItemContext::projectSetting()`, and status fetches with `StatusJob::projectSetting()`.

## Settings screens

The module calls itself "GitLab", so the Profiles > Manage Profiles… window shows "GitLab" under each profile, with the profile's GitLab accounts under it. Selecting an account shows its settings screen. It has one [settings screen](../../../docs/modules.md#settings-screens), in `GitLabSettings.cpp`:

| Id | Name | Key | Type | Description |
|---|---|---|---|---|
| `general` | General Configuration | `baseUrl` | URL | The GitLab server, such as `https://gitlab.example.org/gitlab`. `https://gitlab.com` by default, and when empty. |
| | | `token` | secret | The token for that server. Never stored with the values: the core keeps it in the keychain, under `<workspace id>/<profile id>/gitlab/token`. |
| | | `projects` | text list | Project paths the user works on, such as `group/project`, one per line. |

The project paths list has an **Add from Server…** button. It asks `api/v4/projects` for the projects the token's user is a member of, without archived ones, sorted by path, in pages of 100 up to 1,000, with the base URL and token in the dialog, saved or not. The list shows each project's `path_with_namespace`, marked "Private" or "Internal" where that applies. See [decision 0138](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0138-add-list-values-from-the-server.md) in the core.

Each profile has its own values, stored in its entry of `workspace.mirante` under `moduleSettings.gitlab.general`, and synced. They hold no token. The screen is opened for one profile at a time. See decisions [0050](../../../docs/decisions/0050-profiles-hold-logins-connections-and-settings.md) and [0054](../../../docs/decisions/0054-module-settings-replace-connections.md), which replaced connections with the base URL.

A profile can also hold other accounts of the module, added with **Add Account** on the module's page in Profiles > Manage Profiles…. Each has its own server, token and project paths, under `accounts.gitlab` of the profile, with its token under `<workspace id>/<profile id>/gitlab/<account id>/token`. An item chooses its account in the item dialog, and a pasted link is read in the account whose server is on its host. See [decision 0146](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0146-several-accounts-per-module.md) in the core.

The `project` field of every type offers the paths of the profile the item uses, in an editable drop-down list. The types do this in `fieldsForProfile(profileId)`, which the item dialog calls with the profile the edited item would use; `fields()` offers none. Each path is trimmed, slashes at its start and end are removed, and empty or repeated paths are left out. Any other path can still be typed. The list is read when the item dialog builds its form, and again when another profile is chosen in it, so a change shows the next time the dialog opens.
