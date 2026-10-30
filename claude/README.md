# claude module

Claude sessions, on claude.ai or on this device, as items. See [item types](../../../docs/item-types.md#claude).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `claude.session.web` | Claude session (web) | web | A Claude session on claude.ai, such as a Remote Control or cloud session, in a web view. |
| `claude.session.local` | Claude session (local) | native | A Claude Code session on one device, with a button that resumes it in a terminal in the project's folder. |

The module calls itself "Claude" and describes itself as: "Claude sessions, on claude.ai or on one of your devices." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Since M3 the types declare their configuration fields, so items can be created and edited. Since M4 `claude.session.web` shows its page in a web view, through `ItemContext::createWebView`. `ClaudeUrls.cpp` reads the address. If `url` is empty or is not an `http` or `https` address, the cell says "Set the address in the item's settings." instead. Since M7 `claude.session.local` has a native view; see [View](#view).

## View

`LocalSessionView.cpp` draws `claude.session.local` with the view kit's `SummaryView`. The title is the session id. Under it are the folder's name and local path, and the device. The folder's name is the last part of the path, since module views cannot read the project's folders.

**Resume in terminal** calls `ItemContext::launchTerminal()` in the folder's local path, with the `program` device setting (`claude` when empty) and the arguments `--resume` and the id. The core quotes them for the platform; see [decision 0082](../../../docs/decisions/0082-commands-as-argument-lists.md).

The button is disabled, with the reason on the action row's error line, in this order of priority:

| Case | Message |
|---|---|
| The id is empty. | Set the session id in the item's settings. |
| The id holds anything but letters, digits and dashes. | The session id may hold only letters, digits and dashes. |
| `device` is set and differs from `deviceName()`, ignoring case and surrounding spaces. | This session lives on <device>. |
| The folder has no local path. | Set the folder's path on this device. |

In the last case **Choose folder…** calls `mapFolder()`. The core does not make the view again after the picker, so the view reads the path again and redraws itself. A launch failure shows its message on the same line.

The tests are in `tests/tst_views.cpp`, with a fake context that records the launches. No terminal starts.

## Items from a link

`claude.session.web` implements `fromUrl()`, through `matchUrl()` in `ClaudeUrls.cpp`. It takes any `http` or `https` address on `claude.ai` or one of its subdomains, stores it in `url`, and names the item "Claude session". `claude.session.local` takes no link. See [decision 0049](../../../docs/decisions/0049-items-from-a-pasted-link.md). The tests are in `tests/tst_from_url.cpp`.

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `url` | URL, required | `claude.session.web` | The claude.ai address, such as a Remote Control or cloud session link. |
| `sessionId` | text, required | `claude.session.local` | The session id passed to `claude --resume`. |
| `folder` | folder | `claude.session.local` | The id of one of the project's folders. Empty uses the main folder. |
| `device` | text | `claude.session.local` | A note about which device the session lives on. |

## Settings

No settings screen. One device setting, kept on each device ([decision 0079](../../../docs/decisions/0079-module-device-settings.md)):

| Key | Default | Description |
|---|---|---|
| `program` | `claude` | The program that runs Claude Code. |
