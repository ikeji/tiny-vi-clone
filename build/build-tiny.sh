#!/bin/bash
# Build with tinymain.obj startup (bypass cdos.obj).
# Usage: ./build/build-tiny.sh <source.c> <output.exe>
set -eu
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
LSIC="${LSIC_DIR:-$HOME/tmp/lsic}"
SRC="${1:?source required}"
OUT="${2:?output required}"

WORK="$PROJ/work"
mkdir -p "$WORK"
rm -f "$WORK"/*.c "$WORK"/*.obj "$WORK"/*.exe 2>/dev/null || true

cp "$PROJ"/src/*.c "$WORK"/ 2>/dev/null || true
cp "$PROJ"/src/*.h "$WORK"/ 2>/dev/null || true
cp "$PROJ/$SRC" "$WORK/$(basename "$SRC")"
# Use tiny config
cp "$PROJ/build/_lcc_tiny" "$WORK/_lcc"

SRC_DOS="$(basename "$SRC")"
OUT_DOS="$(basename "$OUT" .exe).exe"

cat > "$WORK/__build.bat" <<EOF
@echo off
set LSIC86=C:\\
set PATH=C:\\BIN
set TMP=D:\\
set TEMP=D:\\
set LSICOPTS=
d:
lcc -o $OUT_DOS $SRC_DOS > __cc.log
if errorlevel 1 echo BUILD_FAILED > __status.txt
if not errorlevel 1 echo BUILD_OK > __status.txt
EOF

rm -f "$WORK"/__status.txt "$WORK"/__STATUS.TXT
SDL_VIDEODRIVER=dummy dosbox \
    -conf "$PROJ/build/dosbox.conf" \
    -c "mount c $LSIC" \
    -c "mount d $WORK" \
    -c "d:" \
    -c "call __build.bat" \
    -c "exit" \
    >"$WORK/__build.log" 2>&1 || true

for f in "$WORK/__STATUS.TXT" "$WORK/__status.txt"; do
    [ -f "$f" ] || continue
    STATUS="$(tr -d '\r\n' < "$f")"
    if [ "$STATUS" = "BUILD_OK" ]; then
        OUT_UPPER="$(echo "$OUT_DOS" | tr '[:lower:]' '[:upper:]')"
        for x in "$WORK/$OUT_DOS" "$WORK/$OUT_UPPER"; do
            [ -f "$x" ] || continue
            cp "$x" "$PROJ/$OUT"
            echo "built: $PROJ/$OUT"
            exit 0
        done
    fi
done
echo "error: build failed" >&2
for f in "$WORK/__CC.LOG" "$WORK/__cc.log"; do [ -f "$f" ] && cat "$f" >&2; done
exit 1
