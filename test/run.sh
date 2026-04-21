#!/bin/bash
# Run a DOS .exe under DOSBox headless, capturing stdout.
#
# Usage: ./test/run.sh <exe> [args...]
#
# Stdin may be redirected; it is forwarded to the DOS program as a text file
# fed via `<input.txt` on the DOS command line. This works for programs that
# read from stdin with fread/fgets; it does NOT work for programs that read
# raw keystrokes with conio getch(), because getch() reads from INT 16 BIOS
# keyboard buffer, not DOS stdin.

set -eu

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
EXE_REL="$1"; shift
EXE_ABS="$PROJ/$EXE_REL"

if [ ! -f "$EXE_ABS" ]; then
    echo "error: $EXE_ABS not found" >&2
    exit 1
fi

RUN="$PROJ/work/run"
mkdir -p "$RUN"
rm -f "$RUN"/*.exe "$RUN"/OUT.TXT "$RUN"/out.txt "$RUN"/IN.TXT "$RUN"/in.txt 2>/dev/null || true

cp "$EXE_ABS" "$RUN/PROG.EXE"

# If there are args that look like file paths in the project, copy them in.
DOS_ARGS=""
for a in "$@"; do
    if [ -f "$PROJ/$a" ]; then
        cp "$PROJ/$a" "$RUN/$(basename "$a")"
        DOS_ARGS="$DOS_ARGS $(basename "$a")"
    else
        DOS_ARGS="$DOS_ARGS $a"
    fi
done

# If stdin is not a tty, capture it into IN.TXT and add `<IN.TXT` to cmd.
STDIN_REDIR=""
if [ ! -t 0 ]; then
    cat > "$RUN/IN.TXT"
    STDIN_REDIR="<IN.TXT"
fi

SDL_VIDEODRIVER=dummy dosbox \
    -conf "$PROJ/build/dosbox.conf" \
    -c "mount d $RUN" \
    -c "d:" \
    -c "PROG.EXE$DOS_ARGS > OUT.TXT $STDIN_REDIR" \
    -c "exit" \
    >/dev/null 2>&1 || true

if [ -f "$RUN/OUT.TXT" ]; then
    cat "$RUN/OUT.TXT"
fi
