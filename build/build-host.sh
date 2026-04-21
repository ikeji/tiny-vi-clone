#!/bin/bash
# Build vi-clone for the Linux host (gcc), for trying it over ssh.
# Output: ./vi-host
set -eu
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ"
CC="${CC:-cc}"
# -w silences any char-class warnings on SJIS bytes in tuttable.h comments.
$CC -O2 -DHOST_BUILD -w src/vi.c -o vi-host
echo "built: $PROJ/vi-host"
