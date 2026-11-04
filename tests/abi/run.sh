#!/usr/bin/env bash

set -u
cd "$(dirname "$0")" || exit 2
here="."
GCC="${1:-C:/msys64/mingw32/bin/gcc.exe}"
GCCDIR="$(dirname "$GCC")"
OBJDUMP="$GCCDIR/objdump.exe"
if command -v cygpath >/dev/null 2>&1; then
    export PATH="$(cygpath -u "$GCCDIR"):$PATH"
else
    export PATH="$GCCDIR:$PATH"
fi
out="$here/.out"
mkdir -p "$out"
fails=0

say()  { printf '%s\n' "$*"; }
ok()   { printf '  %-46s ok\n'   "$1"; }
bad()  { printf '  %-46s FAIL\n' "$1"; fails=$((fails+1)); }

command -v "$GCC" >/dev/null 2>&1 || [ -x "$GCC" ] || { say "gcc not found: $GCC"; exit 2; }

say "toolchain: $("$GCC" -dumpmachine) $("$GCC" -dumpversion)"
say ""

say "static: RET imm16 per arity"
cat > "$out/arity.c" <<'EOF'
#define TC __attribute__((thiscall))
struct R { void *v; unsigned a,b,c,d; };
void TC f0(struct R *t)                                             { t->a=1; }
void TC f1(struct R *t, unsigned x)                                 { t->a=x; }
void TC f2(struct R *t, unsigned x, unsigned y)                     { t->a=x; t->b=y; }
void TC f3(struct R *t, unsigned x, unsigned y, unsigned z)         { t->a=x; t->b=y; t->c=z; }
void TC f4(struct R *t, unsigned x, unsigned y, unsigned z, float w){ t->a=x; t->b=y; t->c=z; t->d=(unsigned)w; }
EOF

if ! "$GCC" -m32 -O2 -c "$out/arity.c" -o "$out/arity.o" 2>"$out/arity.log"; then
    say "  compile failed:"; sed 's/^/    /' "$out/arity.log"; exit 2
fi

check_ret() {
    local sym="$1" want="$2"
    local got
    got="$("$OBJDUMP" -d "$out/arity.o" \
           | awk -v s="_$sym" 'index($0,"<"s">:"){f=1;next} f&&/ret/{print;exit}' \
           | sed 's/.*ret *//; s/[[:space:]]*$//')"
    [ -z "$got" ] && got="(none)"
    if [ "$got" = "$want" ]; then ok "$sym emits 'ret $want'"
    else bad "$sym emits 'ret $got', expected 'ret $want'"; fi
}
check_ret f0 "(none)"
check_ret f1 "\$0x4"
check_ret f2 "\$0x8"
check_ret f3 "\$0xc"
check_ret f4 "\$0x10"

say ""

say "runtime: dispatch through a vtable"
if ! "$GCC" -m32 -O2 "$here/thiscall_test.c" -o "$out/thiscall_test.exe" 2>"$out/build.log"; then
    say "  compile failed:"; sed 's/^/    /' "$out/build.log"; exit 2
fi
if "$out/thiscall_test.exe" | sed 's/^/  /'; then :; else fails=$((fails+1)); fi

say ""
if [ "$fails" -ne 0 ]; then
    say "RESULT: $fails failure(s). Do not build the backend with this toolchain."
    exit 1
fi
say "RESULT: pass. Toolchain is safe for the renderer backend."
