# jira module

Jira issues and issue lists as items. See [item types](../../../docs/item-types.md#jira).

## Item types

| Id | Name | Rendering |
|---|---|---|
| `jira.issue.summary` | Jira Issue (summary) | native |
| `jira.issue.page` | Jira Issue (page) | web |
| `jira.list` | Jira List | native |

Since M3 the types declare their configuration fields, so items can be created and edited. Since M4 `jira.issue.page` shows the issue's page in a web view. The other types still show a placeholder; their views arrive in a later milestone.

## Pages

`jira.issue.page` asks the core for a web view with `ItemContext::createWebView`. `JiraUrls.cpp` builds the address `<baseUrl>/browse/<key>` from the connection's base URL, without doubling a slash at its end. When something is missing, the cell shows a message instead:

- without a `jira.cloud` or `jira.dc` connection: "No Jira connection in this profile.";
- without an issue key: "Set the issue key in the item's settings."

## Items from a link

Every type implements `fromUrl()`, through `matchUrl()` in `JiraUrls.cpp`. See [decision 0049](../../../docs/decisions/0049-items-from-a-pasted-link.md).

- **The server.** The longest base URL of the group's `jira.cloud` and `jira.dc` connections that the address is below, else an `atlassian.net` site. Any other server is read only from `/browse/<key>`, with Jira taken to be installed at the path before it.
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

## Connection kinds

Every type uses a `jira.cloud` connection of the item's [profile](../../../docs/data-model.md#profile), or else a `jira.dc` one. In M4 only the page type reads it, for its base URL.

## Settings

None in M2.
