#!/bin/sh
# mirante-bridge: writes what Mirante's Claude agents list shows into the
# exchange folder. See ../README.md and decision 0150 in mirante-core.
#
#   mirante-hook.sh hook     a hook event on stdin
#   mirante-hook.sh status   the status line input on stdin
#   mirante-hook.sh install  copies both scripts to ~/.mirante/claude/bin,
#                            a path that plugin updates do not change, for
#                            the status line setting
#
# The folder is $MIRANTE_CLAUDE_DIR, or ~/.mirante/claude/sessions. Each
# session has <session>.hook.json and <session>.status.json, replaced whole;
# the file's modified time is the event's time. A hook file keeps only the
# event's name, the tool's name and the notification's type: no prompt text
# and no tool input. The script always exits 0, so it never blocks or fails a
# session. It starts few processes, since starting one is slow on Windows.

mode=$1
if [ "$mode" = install ]; then
    here=${0%/*}
    bin=$HOME/.mirante/claude/bin
    [ -d "$bin" ] || mkdir -p "$bin" 2>/dev/null || exit 0
    for script in mirante-hook.sh mirante-statusline.sh; do
        cmp -s "$here/$script" "$bin/$script" || cp -f "$here/$script" "$bin/$script" 2>/dev/null
    done
    exit 0
fi
dir=${MIRANTE_CLAUDE_DIR:-$HOME/.mirante/claude/sessions}
[ -d "$dir" ] || mkdir -p "$dir" 2>/dev/null || exit 0
tmp="$dir/.tmp.$$"

# One awk reads the input and writes the temporary file, then prints the
# session id, or nothing when the input is not usable.
#
# A field's value is taken at the key's first place in the input. Claude Code
# puts the common fields before the tool input, and quotes inside a string
# are escaped, so the first unescaped key is the event's own. Enough for the
# plain names and ids read here.
session=$(awk -v mode="$mode" -v out="$tmp" '
    { text = text $0 "\n" }
    function field(name,    key, at, rest, end) {
        key = "\"" name "\""
        at = index(text, key)
        if (at == 0) return ""
        rest = substr(text, at + length(key))
        if (match(rest, /^[ \t\r\n]*:[ \t\r\n]*"/) == 0) return ""
        rest = substr(rest, RLENGTH + 1)
        end = index(rest, "\"")
        return end > 0 ? substr(rest, 1, end - 1) : ""
    }
    function clean(value, allowed,    result, i, c) {
        result = ""
        for (i = 1; i <= length(value); i++) {
            c = substr(value, i, 1)
            if (index(allowed, c) > 0) result = result c
        }
        return result
    }
    END {
        word = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
        session = field("session_id")
        # Session ids are UUIDs. Anything else is not used as a file name.
        if (session == "" || clean(session, word "-") != session) exit
        if (mode == "status") {
            # The status line input as Claude Code gave it. It holds no
            # prompt text.
            printf "%s", text > out
        } else if (mode == "hook") {
            event = field("hook_event_name")
            if (event == "" || clean(event, word) != event) exit
            if (event == "SessionEnd") {
                print session " end"
                exit
            }
            tool = clean(field("tool_name"), word "_.:-")
            notification = clean(field("notification_type"), word "_-")
            printf "{\"event\":\"%s\",\"tool\":\"%s\",\"notification\":\"%s\"}\n", event, tool, notification > out
        } else {
            exit
        }
        close(out)
        print session
    }')

case $session in
'') ;;
*' end') rm -f "$dir/${session% end}.hook.json" "$dir/${session% end}.status.json" ;;
*) mv -f "$tmp" "$dir/$session.$mode.json" 2>/dev/null ;;
esac
[ -f "$tmp" ] && rm -f "$tmp"
exit 0
