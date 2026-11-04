#!/bin/sh
# mirante-bridge: a status line that passes its input to Mirante, then runs
# the user's own status line, if any, and prints what it prints.
#
#   mirante-statusline.sh [command [arguments...]]
#
# Set it as the status line command, with the old command after it. See
# ../README.md.

here=${0%/*}
if [ $# -eq 0 ]; then
    exec sh "$here/mirante-hook.sh" status
fi
input=$(cat)
printf '%s\n' "$input" | sh "$here/mirante-hook.sh" status
printf '%s\n' "$input" | "$@"
