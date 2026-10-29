# claude module

Claude sessions, on claude.ai or on this device, as items. See [item types](../../../docs/item-types.md#claude).

## Item types

| Id | Name | Rendering |
|---|---|---|
| `claude.session.web` | Claude session (web) | web |
| `claude.session.local` | Claude session (local) | native |

Since M3 the types declare their configuration fields, so items can be created and edited. Their views arrive in a later milestone.

## Configuration

The keys each type stores in the item's configuration. "Required" means the item dialog refuses an empty value.

| Key | Type | Used by | Description |
|---|---|---|---|
| `url` | URL, required | `claude.session.web` | The claude.ai address, such as a Remote Control or cloud session link. |
| `sessionId` | text, required | `claude.session.local` | The session id passed to `claude --resume`. |
| `folder` | folder | `claude.session.local` | The id of one of the project's folders. Empty uses the main folder. |
| `device` | text | `claude.session.local` | A note about which device the session lives on. |

## Connection kinds

None in M2.

## Settings

None in M2.
