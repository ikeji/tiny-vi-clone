#!/bin/bash
# Launch vi.exe interactively in DOSBox with a sample file.
# Intended for human use: the DOSBox window opens and you can test
# keybindings by hand. Quit with :q or :q! inside vi, then type `exit`
# in DOSBox.

set -eu
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
RUN="$PROJ/work/manual"
mkdir -p "$RUN"
cp "$PROJ/vi.exe" "$RUN/VI.EXE"
cp "$PROJ/test/fixtures/sample.txt" "$RUN/SAMPLE.TXT"

dosbox \
    -conf "$PROJ/build/dosbox.conf" \
    -c "mount d $RUN" \
    -c "d:" \
    -c "VI.EXE SAMPLE.TXT"
