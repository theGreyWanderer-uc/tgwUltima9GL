#!/usr/bin/env bash

set -u
cd "$(dirname "$0")" || exit 2
GCC="${1:-C:/msys64/mingw32/bin/gcc.exe}"
GCCDIR="$(dirname "$GCC")"
if command -v cygpath >/dev/null 2>&1; then
    export PATH="$(cygpath -u "$GCCDIR"):$PATH"
else
    export PATH="$GCCDIR:$PATH"
fi
command -v node >/dev/null 2>&1 || { echo "node not found"; exit 2; }
mkdir -p .out

"$GCC" -m32 -O2 -Wall -Wextra -Werror -std=gnu99 -static-libgcc contract_test.c -o .out/contract_test.exe \
    || { echo "FAIL: telemetry C contract check did not compile"; exit 1; }
names="$(node export-fixtures.mjs .out)" || { echo "FAIL: could not export samples.json"; exit 1; }

checked=0
for name in $names; do
    ./.out/contract_test.exe ".out/$name.bin" ".out/$name.expected"
    status=$?
    if [ "$status" -ne 0 ]; then echo "FAIL: telemetry C fixture $name, exit $status"; exit 1; fi
    checked=$((checked+1))
done
[ "$checked" -gt 0 ] || { echo "FAIL: no accepted samples in samples.json"; exit 1; }
echo "PASS: i686 C sizes/offsets and all fields of $checked accepted synthetic samples."
