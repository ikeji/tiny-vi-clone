#!/bin/bash
# Launch the host build inside luit so that SJIS bytes emitted by
# the editor are transparently translated to/from the terminal's
# encoding (UTF-8 typically). Works over ssh in a plain xterm/tmux.
set -eu
PROJ="$(cd "$(dirname "$0")" && pwd)"
if [ ! -x "$PROJ/vi-host" ]; then
    "$PROJ/build/build-host.sh"
fi
exec luit -encoding SJIS "$PROJ/vi-host" "$@"
