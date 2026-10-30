# jira module

Jira issues and issue lists as items. See [item types](../../../docs/item-types.md#jira).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `jira.issue.summary` | Jira Issue (summary) | native | A summary of one issue: its status, people, sprint, description and latest comments. |
| `jira.issue.page` | Jira Issue (page) | web | The web page of one issue, on the Jira site of the item's profile. |
| `jira.list` | Jira List | native | A table of issues from a preset, such as your open issues or the current sprint, or from a JQL query. |

The module calls itself "Jira" and describes itself as: "Issues and lists of issues from a Jira site. Each profile sets its site and token in the module's General Configuration." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Since M3 the types declare their configuration fields, so items can be created and edited. Since M4 `jira.issue.page` shows the issue's page in a web view. Since M5 the issue types report [status](#status). Since M6 the list reports status. The summary and the list still show a placeholder.

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

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `key` | text, required | `jira.issue.summary`, `jira.issue.page` | The issue key, such as `PROJ-123`. |
| `preset` | choice, required | `jira.list` | `myOpen` (my open issues, the default), `currentSprint` (current sprint of the project's `jira.projectKey`), `recentlyUpdated` (recently updated in that project) or `jql` (the query in `jql`). |
| `jql` | text | `jira.list` | A JQL query, used when `preset` is `jql`. |

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
| `issue(key, fields, expand)` | `GET issue/<key>?fields=<fields>&expand=<expand>`. Without fields, all of them. `expand` is `renderedFields` unless given. |
| `search(jql, options)` | Cloud: `GET search/jql` with `jql`, `fields` and `maxResults`, then `nextPageToken` until `isLast`. Data Center: `GET search` with `startAt` until `total`. The pages are joined into `{"issues": [...]}`, up to `maxRows` issues (100 by default). |
| `transitions(key)` | `GET issue/<key>/transitions` |
| `transition(key, id)` | `POST issue/<key>/transitions` with `{"transition": {"id": <id>}}` |
| `comment(key, text)` | `POST issue/<key>/comment`. Cloud gets the text as an Atlassian Document Format (ADF) body: a paragraph for each block between blank lines, and a hard break between lines. Data Center gets it as plain text. |
| `myself()` | `GET myself` |

The key is encoded as one path segment. A `+` or `%` in a query value is encoded, so the server reads the JQL as typed. The tests are in `tests/tst_client.cpp`.

## Status

All three types report status, in `JiraStatus.cpp` and `JiraLists.cpp`. See decisions [0061](../../../docs/decisions/0061-attention-from-status-of-any-rendering.md) and [0067](../../../docs/decisions/0067-lists-report-status.md).

**An issue:**

- **Request.** `jira.issue.page` makes one `issue()` call with the fields `summary`, `status`, `assignee`, `priority`, `issuetype` and `updated`. `jira.issue.summary` makes one with all fields and `expand=renderedFields,names`.
- **Summary.** The status and the assignee, such as "In Review · assigned to Ana", or "Done · unassigned".
- **Data.** `key`, `summary`, `status`, `statusId`, `statusCategory`, `assignee`, `priority`, `issueType` and `updated`. For `jira.issue.summary` also `issue`, the whole answer.
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
- **Request.** One `search()` with the fields `summary`, `status`, `assignee` and `updated`, up to 100 rows.
- **Summary.** "12 issues", "1 issue", or "100+ issues" at the row limit.
- **Data.** `rows`, the issues as Jira gives them, and `total`, their number.
- **Attention and closed.** Neither: no reasons, and `closed` stays unset.
- **Missing values.** Without a project key, the two project presets finish with `State::None` and "Set the Jira project key in the project's settings." An empty `jql` finishes the same way with "Set the JQL query in the item's settings." Neither sends a request.

The tests are in `tests/tst_status.cpp` and `tests/tst_list_jql.cpp`, on responses from both deployments in `tests/data/`, with made-up names.
