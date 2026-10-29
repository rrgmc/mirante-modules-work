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

`jira.issue.page` asks the core for a web view with `ItemContext::createWebView`. `JiraUrls.cpp` builds the address `<baseUrl>/browse/<key>` from the base URL of the [General Configuration](#settings-screens) of the item's profile, read with `ItemContext::moduleSettings()`, without doubling a slash at its end. When something is missing, the cell shows a message instead:

- without a base URL, or with one that is not an `http` or `https` address: "Set the Jira base URL in Modules > Jira > General Configuration for this profile.";
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

## Settings screens

The module calls itself "Jira", so the Modules menu shows Modules > Jira. It has one [settings screen](../../../docs/modules.md#settings-screens), in `JiraSettings.cpp`:

| Id | Name | Key | Type | Description |
|---|---|---|---|---|
| `general` | General Configuration | `baseUrl` | URL | The Jira site, such as `https://acme.atlassian.net` or `https://jira.example.org/jira`. No default. |
| | | `token` | secret | The token for that site. Never stored with the values: the core keeps it in the keychain, under `<profile id>/jira/token`. |

Each profile has its own values, stored in its entry of `workspace.mirante` under `moduleSettings.jira.general`, and synced. Jira Cloud and Data Center are no longer told apart by a connection kind; M5 tells them apart when it signs requests. See [decision 0054](../../../docs/decisions/0054-module-settings-replace-connections.md).
