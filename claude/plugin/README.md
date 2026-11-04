# mirante-bridge, a Claude Code plugin

The plugin reports each Claude Code session's activity, model and usage to Mirante's Claude agents list (`claude.agents`). Without it, the list shows only what `claude agents --json` prints. See [the module's README](../README.md#the-bridge-plugin) and decision 0148 in mirante-core.

The plugin has two parts:

- **Hooks**, which the plugin installs. They give the current tool and whether the session waits for a permission.
- **A status line wrapper**, which you set yourself, because a plugin cannot set the status line. It gives the model, the effort, the context use, the cost and the rate limits.

You can use either part alone. The list then shows only that part's columns.

## What it writes

Both parts write into the exchange folder: `MIRANTE_CLAUDE_DIR`, or `~/.mirante/claude/sessions` when that is not set. Each session has up to two files, named by its session id:

- `<session>.hook.json`: the latest hook event's name, its tool's name and its notification's type, such as `{"event":"PreToolUse","tool":"Bash","notification":""}`. Prompt text and tool input are never written.
- `<session>.status.json`: the latest status line input, as Claude Code gives it. It holds the session's folder, model, cost, context use and rate limits, and no prompt text.

The file's modified time is the time of the event. A session's files are deleted when the session ends. Mirante deletes files older than seven days.

## Install

Claude Code keeps each account in a config folder. Do these steps once for each account you list in Mirante.

1. Add this folder as a plugin marketplace, then install the plugin. In Claude Code:

   ```text
   /plugin marketplace add <path to mirante-modules-work>/claude/plugin
   /plugin install mirante-bridge@mirante
   ```

2. Start a new session. Its first hook copies the two scripts to `~/.mirante/claude/bin`, whose path stays the same when the plugin updates.

3. Point the status line at the wrapper, in the account's `settings.json` (`~/.claude/settings.json` for the default account). Put your current status line command, if any, after the wrapper. The wrapper runs it and prints what it prints:

   ```json
   "statusLine": {
     "type": "command",
     "command": "sh ~/.mirante/claude/bin/mirante-statusline.sh <your current command>"
   }
   ```

   With no current command, the wrapper prints nothing, and the status line stays empty.

Restart the session for the status line to take effect.

## Notes

- **Windows.** Claude Code runs hooks and the status line in Git Bash, so `sh` and `~` work. The scripts start few processes, because starting one is slow there.
- **Background.** The hooks run in the background, so they never slow a session. Two quick events can therefore finish out of order. Mirante shows a tool only while `claude agents --json` says the session works, and a permission only while it says the session needs input, so a late file never shows a wrong state.
- **Rate limits** show only for Pro, Max and Ultra subscriptions, and only after a session's first answer.
- **The Claude desktop app** may not run plugin hooks or the status line. Its sessions then show only the usual columns.
- **Another folder.** To use another exchange folder, set `MIRANTE_CLAUDE_DIR` in the environment of both Claude Code and Mirante. On Windows, write it with forward slashes, such as `C:/Users/me/mirante-claude`, so both read it the same way.
