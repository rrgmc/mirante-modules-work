# claude module

Claude sessions on claude.ai, as items. See [item types](../../../docs/item-types.md#claude).

## Item types

| Id | Name | Rendering | Description |
|---|---|---|---|
| `claude.session.web` | Claude session (web) | web | A Claude session on claude.ai, such as a Remote Control or cloud session, in a web view. |

The module calls itself "Claude" and describes itself as: "Claude sessions on claude.ai." Help > Modules shows the names and descriptions. See [decision 0056](../../../docs/decisions/0056-modules-window.md).

Its UUID is `00caf22e-8d0f-431f-8832-17a09c048ac2`. It is at version 0.1.0, by Rangel Reale. Help > Modules also shows these, and the module's homepage. See [decision 0087](../../../docs/decisions/0087-module-identity.md).

Since M3 the type declares its configuration fields, so items can be created and edited. Since M4 `claude.session.web` shows its page in a web view, through `ItemContext::createWebView`. `ClaudeUrls.cpp` reads the address. If `url` is empty or is not an `http` or `https` address, the cell says "Set the address in the item's settings." instead.

M7 added `claude.session.local`, a Claude Code session resumed in a terminal. [Decision 0084](../../../docs/decisions/0084-no-local-claude-sessions.md) removed it. Items of that type that are still in a workspace are kept unchanged, and their cell says "This item type is no longer available: claude.session.local."

## Items from a link

`claude.session.web` implements `fromUrl()`, through `matchUrl()` in `ClaudeUrls.cpp`. It takes any `http` or `https` address on `claude.ai` or one of its subdomains, stores it in `url`, and names the item "Claude session". See [decision 0049](../../../docs/decisions/0049-items-from-a-pasted-link.md). The tests are in `tests/tst_from_url.cpp`.

## Configuration

The keys the type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `url` | URL, required | `claude.session.web` | The claude.ai address, such as a Remote Control or cloud session link. |

## Settings

No settings screen and no device settings.
