# claude module

Claude sessions on claude.ai, and the Claude agents on this device, as items. See [item types](../../../docs/item-types.md#claude).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `claude.session.web` | Claude session (web) | web | A Claude session on claude.ai, such as a Remote Control or cloud session, in a web view. |
| `claude.agents` | Claude agents | native | The Claude Code sessions on this device, with their title, status and repository. No session text. |

The module calls itself "Claude" and describes itself as: "Claude sessions on claude.ai, and the Claude agents on this device." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Its UUID is `00caf22e-8d0f-431f-8832-17a09c048ac2`. It is at version 0.1.0, by Rangel Reale. Help > Modules also shows these, and the module's homepage. See [decision 0089](../../../docs/decisions/0089-module-identity.md).

Since M3 the type declares its configuration fields, so items can be created and edited. Since M4 `claude.session.web` shows its page in a web view, through `ItemContext::createWebView`. `ClaudeUrls.cpp` reads the address. If `url` is empty or is not an `http` or `https` address, the cell says "Set the address in the item's settings." instead.

M7 added `claude.session.local`, a Claude Code session resumed in a terminal. [Decision 0084](../../../docs/decisions/0084-no-local-claude-sessions.md) removed it. Items of that type that are still in a workspace are kept unchanged, and their cell says "This item type is no longer available: claude.session.local."

## Claude agents

`claude.agents` lists the Claude Code sessions that run on this device: in a terminal, in the Claude desktop app, or in the background. It runs Claude Code's documented command `claude agents --json`, and shows only what that command prints: each session's title, status, folder and start time. It reads none of Claude Code's files and no session text. See [decision 0143](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0143-claude-agents-list.md).

- **The program.** The module looks for `claude` on the PATH, then in `~/.local/bin`, `~/.claude/local`, `/opt/homebrew/bin` and `/usr/local/bin`. If it is not found, the cell header says "Claude Code is not installed on this device."
- **Status.** The status cache runs the command, so the list refreshes like other lists, and the refresh button runs it at once. The summary reads like "3 agents · 1 waiting for input". A list raises no attention, and never counts as closed.
- **Columns.** Title, Status, Repository, Account and Started, sorted on Started, newest first. The status is a pill: amber for Working, red for Needs input and Failed, green for Done, grey for Idle and Stopped. A drop-down filters the rows by status.
- **Repository.** The module walks up from the session's folder to the folder that holds `.git`. For a worktree, it follows the `.git` file to the main repository. The column shows that repository's folder name and the branch, such as "mirante-core · fix-dialogs", or the session's folder outside Git. It starts no `git` process.
- **Accounts.** Claude Code keeps each account in a config folder, chosen by `CLAUDE_CONFIG_DIR`. The item's `accounts` lists those folders. The module runs the command once for each, and the Account column, shown only for more than one account, names the folder. An account that fails is named above the table, and the other accounts still show their rows.
- **Row menu.** "Show folder" opens the session's folder, and "Copy session ID" copies its id.
- **Not listed.** Cloud sessions on claude.ai and Remote Control sessions of another computer. Claude has no documented API for them. Use Claude session (web) for those.

### The bridge plugin

[`plugin/`](plugin/README.md) holds `mirante-bridge`, an optional Claude Code plugin. It adds what `claude agents --json` does not print. Its hooks and a status line wrapper write each session's latest event and status line input into an exchange folder: `MIRANTE_CLAUDE_DIR`, or `~/.mirante/claude/sessions`. The plugin's README says how to install it for each account. See [decision 0148](https://github.com/rrgmc/mirante-core/blob/master/docs/decisions/0148-claude-bridge-plugin.md).

- **Reading.** After the command, the fetch reads the folder and joins the files to the rows by session id, in `mergeBridge()`. It reads only files the plugin writes, and deletes those older than seven days.
- **Columns.** Tool, Model (with the effort, such as "Opus 5.5 · high"), Context (such as "42 %") and Cost (such as "$1.25"). Each shows only when a row has a value.
- **Status.** A session that waits for a permission reads "Needs permission" instead of "Needs input".
- **Rate limits.** The account's 5-hour and weekly use shows above the table, such as "Limits: 5 h 31 % · week 12 %", from the account's newest status file. A window that has reset is left out.
- **Order of events.** The hooks run in the background, so a file may hold an older event than the latest. The command's state decides: a tool shows only while the session works, and "Needs permission" only while it needs input.
- **Refresh.** The view watches the folder. A change refreshes the list, at most once every five seconds.

## Items from a link

`claude.session.web` implements `fromUrl()`, through `matchUrl()` in `ClaudeUrls.cpp`. It takes any `http` or `https` address on `claude.ai` or one of its subdomains, stores it in `url`, and names the item "Claude session". See [decision 0049](../../../docs/decisions/0049-items-from-a-pasted-link.md). The tests are in `tests/tst_from_url.cpp`.

## Configuration

The keys the type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `url` | URL, required | `claude.session.web` | The claude.ai address, such as a Remote Control or cloud session link. |
| `folder` | Folder | `claude.agents` | Lists only sessions in this project folder, through `--cwd`. Empty lists all. If the folder is not mapped on this device, the cell header says so. |
| `includeFinished` | Bool, default false | `claude.agents` | Also lists finished background sessions, through `--all`. |
| `accounts` | Text list | `claude.agents` | Claude Code config folders on this device, one per account. A leading `~` is the home folder. Empty uses the default account. |

## Settings

No settings screen and no device settings. The bridge plugin's folder can be moved with the environment variable `MIRANTE_CLAUDE_DIR`, set for both Claude Code and Mirante.
