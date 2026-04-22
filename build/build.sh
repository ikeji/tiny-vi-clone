#!/bin/bash
# Build vi-clone using LSI C-86 via DOSBox.
#
# Usage: ./build/build.sh [source.c] [output.exe]
# Default: src/vi.c -> vi.exe (placed in project root)

set -eu

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
LSIC="${LSIC_DIR:-$HOME/tmp/lsic}"
SRC="${1:-src/vi.c}"
OUT="${2:-vi.exe}"

if [ ! -d "$LSIC" ]; then
    echo "error: LSI C-86 not found at $LSIC" >&2
    exit 1
fi

# work/ is the directory that gets mounted as D: in dosbox.
# Copy source(s) in, run compiler, copy output out.
WORK="$PROJ/work"
mkdir -p "$WORK"
rm -f "$WORK"/*.c "$WORK"/*.obj "$WORK"/*.exe "$WORK"/*.map 2>/dev/null || true

# Copy all sources from src/ so #include "other.c" and .h works.
cp "$PROJ"/src/*.c "$WORK"/ 2>/dev/null || true
cp "$PROJ"/src/*.h "$WORK"/ 2>/dev/null || true
cp "$PROJ/$SRC" "$WORK/$(basename "$SRC")"
# Use our modified _lcc so paths point at C: (the dosbox mount of LSIC)
cp "$PROJ/build/_lcc" "$WORK/_lcc"

SRC_DOS="$(basename "$SRC")"
OUT_DOS="$(basename "$OUT" .exe).exe"

# Optional extra compiler options (e.g. -DVI_QUIET_STATUS) via env var.
EXTRA_OPTS="${VI_DEFINES:-}"

cat > "$WORK/__build.bat" <<EOF
@echo off
set LSIC86=C:\\
set PATH=C:\\BIN
set TMP=D:\\
set TEMP=D:\\
set LSICOPTS=$EXTRA_OPTS
d:
lcc -o $OUT_DOS $SRC_DOS > __cc.log
if errorlevel 1 echo BUILD_FAILED > __status.txt
if not errorlevel 1 echo BUILD_OK > __status.txt
EOF

# Run DOSBox headless with the build script.
# -exit causes dosbox to exit after autoexec completes.
# SDL_VIDEODRIVER=dummy keeps it headless.
rm -f "$WORK"/__status.txt "$WORK"/__STATUS.TXT "$WORK"/__build.log
SDL_VIDEODRIVER=dummy dosbox \
    -conf "$PROJ/build/dosbox.conf" \
    -c "mount c $LSIC" \
    -c "mount d $WORK" \
    -c "d:" \
    -c "call __build.bat" \
    -c "exit" \
    >"$WORK/__build.log" 2>&1 || true

# DOSBox writes filenames in uppercase on the Linux side.
STATUS_FILE=""
for f in "$WORK/__STATUS.TXT" "$WORK/__status.txt"; do
    if [ -f "$f" ]; then STATUS_FILE="$f"; break; fi
done
if [ -z "$STATUS_FILE" ]; then
    echo "error: DOSBox did not complete build" >&2
    echo "--- log ---" >&2
    cat "$WORK/__build.log" >&2 || true
    exit 1
fi

STATUS="$(tr -d '\r\n' < "$STATUS_FILE")"
if [ "$STATUS" != "BUILD_OK" ]; then
    echo "error: build failed" >&2
    echo "--- dosbox log ---" >&2
    cat "$WORK/__build.log" >&2 || true
    echo "--- compiler log ---" >&2
    for f in "$WORK/__cc.log" "$WORK/__CC.LOG"; do
        [ -f "$f" ] && cat "$f" >&2
    done
    exit 1
fi

# Output is also uppercase.
OUT_UPPER="$(echo "$OUT_DOS" | tr '[:lower:]' '[:upper:]')"
OUT_FOUND=""
for f in "$WORK/$OUT_DOS" "$WORK/$OUT_UPPER"; do
    if [ -f "$f" ]; then OUT_FOUND="$f"; break; fi
done
if [ -z "$OUT_FOUND" ]; then
    echo "error: expected output $OUT_DOS not found" >&2
    exit 1
fi

cp "$OUT_FOUND" "$PROJ/$OUT"

# Post-link patch: LSI-C86's cdos.obj startup runs FNINIT/FNSTSW to detect
# an 8087. On an emulator that doesn't implement x87 opcodes (e.g. HIDOS VM
# using libx86emu), FNINIT hangs. Replace it with a JMP that skips straight
# to the "no 8087" branch. Harmless on real hardware and DOSBox.
if [ -f "$PROJ/build/patch_nofpu.py" ]; then
    python3 "$PROJ/build/patch_nofpu.py" "$PROJ/$OUT" "$PROJ/$OUT.tmp" 2>/dev/null \
        && mv "$PROJ/$OUT.tmp" "$PROJ/$OUT" \
        && echo "built: $PROJ/$OUT (FPU detection patched out)" \
        && exit 0
fi
echo "built: $PROJ/$OUT"
