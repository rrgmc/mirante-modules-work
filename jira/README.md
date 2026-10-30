# jira module

Jira issues and issue lists as items. See [item types](../../../docs/item-types.md#jira).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `jira.issue.summary` | Jira Issue (summary) | native | A summary of one issue: its status, people, sprint, description and latest comments. |
| `jira.issue.page` | Jira Issue (page) | web | The web page of one issue, on the Jira site of the item's profile. |
| `jira.list` | Jira List | native | A table of issues from a preset, such as your open issues or the current sprint, or from a JQL query. |

The module calls itself "Jira" and describes itself as: "Issues and lists of issues from a Jira site. Each profile sets its site and token in the module's General Configuration." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Its UUID is `dfc0970e-7a8d-401e-b337-dffbfcabe1a7`. It is at version 0.1.0, by Rangel Reale. Help > Modules also shows these, and the module's homepage. See [decision 0087](../../../docs/decisions/0087-module-identity.md).

Since M3 the types declare their configuration fields, so items can be created and edited. Since M4 `jira.issue.page` shows the issue's page in a web view. Since M5 the issue types report [status](#status). Since M6 the summary and the list have native [views](#views), and the list reports status. Since M7 the module takes part in [quick add](#quick-add).

## Pages

`jira.issue.page` asks the core for a web view with `ItemContext::createWebView`. `JiraUrls.cpp` builds the address `<baseUrl>/browse/<key>` from the base URL of the [General Configuration](#settings-screens) of the item's profile, read with `ItemContext::moduleSettings()`, without doubling a slash at its end. When something is missing, the cell shows a message instead:

- without a base URL, or with one that is not an `http` or `https` address: "Set the Jira base URL in this profile's Modules > Jira > General Configuration, in the Profiles menu.";
- without an issue key: "Set the issue key in the item's settings."

## Items from a link

Every type implements `fromUrl()`, through `matchUrl()` in `JiraUrls.cpp`. See [decision 0049](../../../docs/decisions/0049-items-from-a-pasted-link.md).

- **The server.** The Jira base URL of the profile the new item would use, if the address is below it, else an `atlassian.net` site. `fromUrl()` gets that profile's id and reads the screen there. Any other server is read only from `/browse/<key>`, with Jira taken to be installed at the path before it.
- **An issue** fills `jira.issue.summary` and `jira.issue.page` with `key`, and names the item after the key. The key comes from `/browse/<key>`, from `/issues/<key>` as in Jira Cloud's project pages, or from the `selectedIssue` query of a board. It is read in upper case, such as `PROJ-123`.
- **A search**, an address with a `jql` query, fills `jira.list` with `preset` = `jql` and the query, named "Jira search".

Other addresses, such as a key without a number, are refused. The tests are in `tests/tst_from_url.cpp`.

## Quick add

`JiraQuickAdd.cpp` has a [quick add](../../../docs/modules.md#quick-add) handler and a related-item finder. Both use the server and the General Configuration of the profile the quick add dialog shows. Without a base URL there, both finish with nothing and send nothing. See [decision 0078](../../../docs/decisions/0078-quick-add-dialog.md).

**The handler** takes a bare key or an issue's address:

- **A key** is the whole text, trimmed, matching `^[A-Za-z][A-Za-z0-9_]+-\d+$`, such as `ops-7`. It is read in upper case.
- **An address** is read with `matchUrl()`, as `fromUrl()` reads it, against the profile's base URL. It must be below that URL: an address on another site belongs to another profile, so its key is not sent to this one.
- **Anything else**, such as a search address or a sentence holding a key, finishes with nothing at once.
- **Request.** `issue()` with the field `summary` and no `expand`, with the request's `owner()` as the receiver.
- **Proposal.** The title is the summary, or the key if it is empty. The one item is a `jira.issue.summary` named by the key, with `{"key": <key>}`, as `fromUrl()` makes it. The project match is `projectKey` = the key's prefix, such as `OPS`. The texts are the key.
- **A `404`**, or any other failure, finishes with nothing, so text that only looks like a key, such as `UTF-8`, is not offered.

**The finder** looks for keys in the texts, such as `ABC-123` in `feature/abc-123-login`:

- **Keys.** Any case, not starting or ending inside a word, read in upper case. A key seen twice counts once.
- **Project.** When the chosen project has a `projectKey`, only keys with that prefix count, without case. Otherwise all keys do.
- **Requests.** The first five keys that count, each asked for as the handler asks. A `200` offers a `jira.issue.summary` item as above, in the order of the keys. The finder finishes once, after the last answer.
- **No keys** finish at once, without a request.

The tests are in `tests/tst_quick_add.cpp`, on recorded responses of both deployments in `tests/data/issue-summary-*.json`.

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `key` | text, required | `jira.issue.summary`, `jira.issue.page` | The issue key, such as `PROJ-123`. |
| `showDetails`, `showDescription`, `showComments`, `showLinks` | boolean | `jira.issue.summary` | Each shows one section of the [view](#views). On by default, and when absent. |
| `preset` | choice, required | `jira.list` | `myOpen` (my open issues, the default), `currentSprint` (current sprint of the project's `jira.projectKey`), `recentlyUpdated` (recently updated in that project) or `jql` (the query in `jql`). |
| `jql` | text | `jira.list` | A JQL query, used when `preset` is `jql`. |
| `limit` | integer | `jira.list` | How many rows the list fetches. 100 by default, and when absent or below 1. |
| `pageSize` | integer | `jira.list` | How many rows the view shows at a time. 50 by default, and when absent or below 1. |

## Settings

None in M2.

## Project settings

Since M5 the project dialog has a Jira section with one field, registered with `addProjectSetting()`. See [data model](../../../docs/data-model.md#project).

| Key | Type | Label | Description |
|---|---|---|---|
| `projectKey` | text | Jira project key | The project's key in Jira, such as `PROJ`. The `currentSprint` and `recentlyUpdated` presets of `jira.list` use it. Stored in the project's `moduleSettings.jira.projectKey`. |

## Settings screens

The module calls itself "Jira", so each profile's Modules submenu shows Profiles > <profile> > Modules > Jira. It has one [settings screen](../../../docs/modules.md#settings-screens), in `JiraSettings.cpp`:

| Id | Name | Key | Type | Description |
|---|---|---|---|---|
| `general` | General Configuration | `baseUrl` | URL | The Jira site, such as `https://acme.atlassian.net` or `https://jira.example.org/jira`. No default. |
| | | `deployment` | choice | `auto` ("Automatic", the default), `cloud` ("Jira Cloud") or `dc` ("Jira Data Center"). See [server and authentication](#server-and-authentication). |
| | | `email` | text | The email of the Atlassian account. Used only for Jira Cloud. Not a secret, so it is synced with the other values. |
| | | `token` | secret | The token for that site: an API token on Jira Cloud, a personal access token on Data Center. Never stored with the values: the core keeps it in the keychain, under `<workspace id>/<profile id>/jira/token`. |
| | | `extraFields` | text list | Extra fields to show, by name, one per line, such as `Team`. None by default. See [extra fields](#extra-fields). |

Each profile has its own values, stored in its entry of `workspace.mirante` under `moduleSettings.jira.general`, and synced. See decisions [0054](../../../docs/decisions/0054-module-settings-replace-connections.md) and [0060](../../../docs/decisions/0060-jira-cloud-or-data-center-by-setting.md).

## Server and authentication

`JiraServerType`, in `JiraServerType.cpp`, tells the core where the server is. The core then gives the module an `ApiClient` for each profile. See [servers and tokens](../../../docs/modules.md#servers-and-tokens).

- **Deployment.** `deploymentOf()` in `JiraSettings.cpp` reads `deployment`. `cloud` and `dc` are followed as chosen. Anything else means Jira Cloud when the base URL's host ends in `.atlassian.net` or `.jira.com`, and Jira Data Center otherwise.
- **Authentication.** On Jira Cloud, `Authorization: Basic` with the email and the token, as `email:token` in Base64. On Data Center, `Authorization: Bearer <token>`. Neither the token nor the email is logged.
- **Missing email.** On Jira Cloud without an email, `problem()` returns "Set the email for Jira Cloud in the profile's Jira settings." The core then sends nothing and the item shows the message.

## Client

`JiraClient`, in `JiraClient.cpp`, is a thin layer over the `ApiClient`. It has one method per endpoint in [integrations](../../../docs/integrations.md#jira), with paths below the API root of the deployment: `rest/api/3` on Jira Cloud, `rest/api/2` on Data Center. It keeps no state, so code makes one for a request and drops it.

| Method | Request |
|---|---|
| `issue(key, fields, expand)` | `GET issue/<key>?fields=<fields>&expand=<expand>`. Without fields, all of them. `expand` is `renderedFields` unless given; an empty list sends no `expand`. |
| `search(jql, options)` | Cloud: `GET search/jql` with `jql`, `fields` and `maxResults`, then `nextPageToken` until `isLast`. Data Center: `GET search` with `startAt` until `total`. The pages are joined into `{"issues": [...]}`, up to `maxRows` issues, with Jira's `total` where it gives one, which Cloud's `search/jql` does not, and its `names` if `options.expand` asks for them. |
| `allFields()` | `GET field`: the id and name of each of the site's fields |
| `myself()` | `GET myself` |

The key is encoded as one path segment. A `+` or `%` in a query value is encoded, so the server reads the JQL as typed. The tests are in `tests/tst_client.cpp`.

## Status

All three types report status, in `JiraStatus.cpp` and `JiraLists.cpp`. See decisions [0061](../../../docs/decisions/0061-attention-from-status-of-any-rendering.md) and [0067](../../../docs/decisions/0067-lists-report-status.md).

**An issue:**

- **Request.** `jira.issue.page` makes one `issue()` call with the fields `summary`, `status`, `assignee`, `priority`, `issuetype` and `updated`. `jira.issue.summary` makes one with all fields and `expand=renderedFields,names`, then a search for its [children](#status).
- **Summary.** The status and the assignee, such as "In Review · assigned to Ana", or "Done · unassigned".
- **Data.** `key`, `summary`, `status`, `statusId`, `statusCategory`, `assignee`, `priority`, `issueType` and `updated`. For `jira.issue.summary` also `issue`, the whole answer, which the view draws from, and `children` and `childrenTotal`.
- **Children.** The issues whose parent is this one, from `search()` with the fields `summary` and `status`. The query is `parent = "<key>"` on Cloud, and `"Epic Link" = "<key>" OR parent = "<key>"` on Data Center, with the key quoted as in a list's query. `children` holds up to 50, without those in the issue's `subtasks`. `childrenTotal` counts all of them, also without those. Data Center gives a total, so one request is enough. Cloud gives none, so the rows are fetched and counted, up to 500. If only this search fails, as on a site that does not know `Epic Link`, the status stands and `children` is left out.
- **Sprint.** `sprintOf()` reads the field whose entry in `names` is "Sprint", because its id differs per site. It takes the open sprint, else the last one. Jira Cloud gives objects, and older Data Center gives text such as `...Sprint@1f[id=4,state=ACTIVE,name=Week 44,...]`.
- **Attention.** "Status changed to <name>", with the key `status:<status id>`, which clears when seen. It is reported on every fetch. The core records it as seen on the item's first status, so it counts only after the status changes.
- **Closed.** When the status category is `done`.
- **Errors.** A failed reply becomes its state with `statusFromReply()`. Without an issue key, the job ends with an error and sends nothing.

**A list:**

- **Query.** `listJql()` in `JiraLists.cpp`, from the preset:

  | Preset | JQL |
  |---|---|
  | `myOpen`, or none | `assignee = currentUser() AND resolution = Unresolved ORDER BY updated DESC` |
  | `currentSprint` | `project = "<key>" AND sprint in openSprints() ORDER BY rank` |
  | `recentlyUpdated` | `project = "<key>" AND updated >= -7d ORDER BY updated DESC` |
  | `jql` | the `jql` field |

  The project key is written as a JQL string, with its quotes and backslashes escaped, so a value cannot change the query.
- **Request.** One `search()` with the fields `summary`, `status`, `assignee` and `updated`, up to `limit` rows. With [extra fields](#extra-fields), first `allFields()`, and the search also asks for those fields and for `expand=names`.
- **Summary.** "12 issues", "1 issue", or "100+ issues" at the row limit, which is the item's `limit`.
- **Data.** `rows`, the issues as Jira gives them, and `total`, their number. With extra fields also `names`, from field id to name.
- **Attention and closed.** Neither: no reasons, and `closed` stays unset.
- **Missing values.** Without a project key, the two project presets finish with `State::None` and "Set the Jira project key in the project's settings." An empty `jql` finishes the same way with "Set the JQL query in the item's settings." Neither sends a request.

The tests are in `tests/tst_status.cpp` and `tests/tst_list_jql.cpp`, on responses from both deployments in `tests/data/`, with made-up names.

## Extra fields

The General Configuration's `extraFields` names fields that differ per site, such as "Team" and "Fix version". See [decision 0073](../../../docs/decisions/0073-jira-extra-fields.md). `JiraFields.cpp` holds the reading:

- **By name.** `fieldIdByName()` ignores case. A name that differs only in a final `s` or `/s` also matches, so "Fix version" finds Cloud's "Fix versions" and Data Center's "Fix Version/s". An exact name comes first, and of two fields with the same name, the lowest id in Jira's map.
- **The value.** `fieldText()`: text and numbers as they are; an object by its `name`, `value` or `displayName`; a list joined with commas. Anything else, such as Cloud's rich text document in a paragraph field, reads as empty. It is shown as plain text.
- **A name the site lacks** is left out quietly, and so is an empty value in the summary.

The two views find the ids differently:

- **A summary** asks for all fields and for `names`, so it has what it needs. The label is the name as the user wrote it.
- **A list** needs the ids before its search, since `fields` takes only ids. It asks `GET field` on each fetch, and only when there are extra fields. This works the same on Cloud and Data Center, whatever the query matches. The other way, reading the `names` of a first search, would not do: Jira gives names only for the fields asked for, and none without rows. The search still asks for `expand=names`, as the decision says, and `data.names` holds its answer, or the names from `GET field` if the server ignored it. If `GET field` fails, the list keeps its usual columns. The columns exist from the start and show once `data.names` has their field, so the sort and the filter do not start over. A new list of names takes effect in a new view.

## Views

`JiraIssueView.cpp` has `createIssueView()`, and `JiraListView.cpp` has `createListView()`. See decisions [0066](../../../docs/decisions/0066-fixed-native-layouts-with-sections.md), [0069](../../../docs/decisions/0069-open-page-in-the-placement.md), [0070](../../../docs/decisions/0070-read-mostly-native-views.md) and [0071](../../../docs/decisions/0071-lists-link-out-without-a-preview.md).

**The issue.** A `SummaryView` with the key and summary as its title, pills for the status and the type, and the actions. The sections, in order, are:

| Section | Key | Field | Shows |
|---|---|---|---|
| Details | `details` | `showDetails` | Priority, assignee, reporter, sprint, then the [extra fields](#extra-fields) |
| Description | `description` | `showDescription` | The HTML in `renderedFields`, else the raw text on Data Center |
| Comments | `comments` | `showComments` | The latest five, newest first, each with its author and date |
| Sub-tasks and links | `links` | `showLinks` | Each with its key, as a button to the external browser, then its relation, summary and status. Then the children, each with that button, its summary and a status pill, and a line such as "23 more" if there are others. |

Development information is left out in M6.

| Action | Id | What it does |
|---|---|---|
| Open page | `openPage` | `ItemContext::showPage()`, or the external browser if `Shift` is held. |
| Web | `web` | `<baseUrl>/browse/<key>` in the external browser. |

The views only read. Changing an issue happens on its page, as [decision 0070](../../../docs/decisions/0070-read-mostly-native-views.md) says.

**Where the data comes from.** The view draws from `ItemStatus::data`, and redraws only when the answer changed, so a poll does not lose a text selection. With no data it shows "Loading…" or the status's message. With old data and an error it keeps the data, and the cell header shows the error.

**Rendered HTML.** `RichText`, a `QTextBrowser`, holds the description and the comments.

- `loadResource()` loads nothing, and answers with a blank image, or an empty style sheet. It must not answer with nothing, because the document would then read a `file:` or network path in the HTML itself.
- `setOpenLinks(false)` and `setOpenExternalLinks(false)` are set. A click resolves the link against the base URL, and opens it in the external browser only for `http` and `https`.

Other text from the server goes in labels with `Qt::PlainText`. A `&` in an issue key on a button is escaped, so it does not mark a shortcut.

**The list.** An `ItemListView` with the columns Key, Summary, Status, Assignee, Updated, the [extra fields](#extra-fields) and Links, sorted on Updated. The extra fields hide first in a narrow cell, the last of them first, then Assignee, Updated, Status and Key. Summary and Links stay. The status's pill takes its role from the status category: `done` is Good, `indeterminate` is Waiting, otherwise Neutral. Keys sort by number, so `OPS-9` comes before `OPS-10`. Updated reads as `compactDate()` and sorts by the real date. A click on a row only selects it: there is no preview.

- **Web link.** `listWebUrl()`, `<baseUrl>/issues/?jql=<query>`, above the table. It is hidden when the query cannot be built.
- **Row links.** "Web" opens `<baseUrl>/browse/<key>` in the external browser. "Open" works as Open page above. "Add" calls `ItemContext::addItemToTask()` with the type `jira.issue.summary`, the key as its name and `{"key": <key>}`. Without a site only "Add" is there. The column sorts by the key.
- **Row menu.** The same, as "Web", "Open page" and "Add to task as item", then "Copy link", which copies `<baseUrl>/browse/<key>`.
- **Pages.** `setPageSize()` with the item's `pageSize`. The rows fetched are the item's `limit`.
- **Filter.** `addFilter()` on Status. The choice is not saved.

The tests are in `tests/tst_views.cpp`, with a fake context and status source. A handler stands in for the external browser, so nothing opens.
