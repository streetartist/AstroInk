#!/usr/bin/env bash
# Regenerate the mquickjs ROM tables for AstroInk's JS stdlib.
#
# Run on the HOST (needs gcc). Produces two C-source headers consumed by the
# firmware build. -m32 makes the packed tables uint32_t (ESP32-S3 is 32-bit);
# the host tool itself can be native 64-bit. Re-run whenever ai_js_stdlib.c
# (the ai.* API surface) changes, then commit generated/.
#
# Usage:  components/ai_runtime_js/tools/gen_stdlib.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"       # components/ai_runtime_js
MQJS="$HERE/../../mquickjs"
GEN="$HERE/generated"
TOOL="$(mktemp -u)/ai_stdlib_gen"
mkdir -p "$(dirname "$TOOL")" "$GEN"

echo "compiling stdlib build tool..."
gcc -I "$MQJS" -o "$TOOL" "$HERE/ai_js_stdlib.c" "$MQJS/mquickjs_build.c"

echo "generating mquickjs_atom.h ..."
"$TOOL" -a -m32 > "$GEN/mquickjs_atom.h"

echo "generating ai_stdlib.h ..."
"$TOOL" -m32 > "$GEN/ai_stdlib.h"

echo "done -> $GEN/{mquickjs_atom.h,ai_stdlib.h}"
