#!/usr/bin/env bash

set -u
cd "$(dirname "$0")" || exit 2
GCC="${1:-C:/msys64/mingw32/bin/gcc.exe}"
GCCDIR="$(dirname "$GCC")"
OBJDUMP="$GCCDIR/objdump.exe"
if command -v cygpath >/dev/null 2>&1; then
    export PATH="$(cygpath -u "$GCCDIR"):$PATH"
else
    export PATH="$GCCDIR:$PATH"
fi
mkdir -p .out
flags=(-m32 -O2 -Wall -Wextra -Werror -std=gnu99 -static-libgcc)
src=../../u9gl/src

"$GCC" "${flags[@]}" -DU9TM_TESTING ../../tools/u9telemetry.c "$src/telemetry/transport.c" "$src/common/perfclock.c" \
    -o .out/u9telemetry.exe || { echo "FAIL: synthetic publisher build"; exit 1; }
"$GCC" "${flags[@]}" metrics_adapter_test.c "$src/telemetry/metrics_snapshot.c" -o .out/metrics_adapter_test.exe \
    || { echo "FAIL: metrics adapter test build"; exit 1; }
./.out/metrics_adapter_test.exe || { echo "FAIL: metrics adapter test"; exit 1; }
"$OBJDUMP" -d .out/u9telemetry.exe | grep -Eq 'lock (add|xadd)' \
    || { echo "FAIL: expected locked 32-bit sequence operations in emitted code"; exit 1; }
echo "PASS: shared transport uses locked sequence operations; synthetic test tools built."
"$GCC" "${flags[@]}" -I../../u9gl/out/gen publisher_benchmark.c "$src/telemetry/publisher.c" \
    "$src/telemetry/metrics_snapshot.c" "$src/telemetry/transport.c" "$src/common/perfclock.c" \
    -o .out/publisher_benchmark.exe || { echo "FAIL: publisher benchmark build (run u9gl/build.sh for the version header)"; exit 1; }
