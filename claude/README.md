# claude module

Claude sessions, on claude.ai or on this device, as items. See [item types](../../../docs/item-types.md#claude).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `claude.session.web` | Claude session (web) | web | A Claude session on claude.ai, such as a Remote Control or cloud session, in a web view. |
| `claude.session.local` | Claude session (local) | native | A Claude Code session on one device, with a button that resumes it in a terminal in the project's folder. |

The module calls itself "Claude" and describes itself as: "Claude sessions, on claude.ai or on one of your devices." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Since M3 the types declare their configuration fields, so items can be created and edited. Since M4 `claude.session.web` shows its page in a web view, through `ItemContext::createWebView`. `ClaudeUrls.cpp` reads the address. If `url` is empty or is not an `http` or `https` address, the cell says "Set the address in the item's settings." instead. The view of `claude.session.local` arrives in a later milestone.

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

None in M2.
