# jira module

Jira issues and issue lists as items. See [item types](../../../docs/item-types.md#jira).

## Item types

| Id | Name | Rendering |
|---|---|---|
| `jira.issue.summary` | Jira Issue (summary) | native |
| `jira.issue.page` | Jira Issue (page) | web |
| `jira.list` | Jira List | native |

Since M3 the types declare their configuration fields, so items can be created and edited. Their views arrive in a later milestone.

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `key` | text, required | `jira.issue.summary`, `jira.issue.page` | The issue key, such as `PROJ-123`. |
| `preset` | choice, required | `jira.list` | `myOpen` (my open issues, the default), `currentSprint` (current sprint of the project's `jira.projectKey`), `recentlyUpdated` (recently updated in that project) or `jql` (the query in `jql`). |
| `jql` | text | `jira.list` | A JQL query, used when `preset` is `jql`. |

## Connection kinds

None in M2.

## Settings

None in M2.
