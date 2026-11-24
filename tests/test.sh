#!/bin/sh
# test.sh — what can actually be checked without a device.
#
#   1. the C compiles clean under -Wall -Wextra -Wpedantic
#   2. the shell script parses (sh -n)
#   3. both reject an unknown mode with exit code 2
#   4. the analysis tools reproduce the documented constant tables
#   5. the sample binary matches the documented hash
#
# Device behaviour is NOT covered here. See ../README.md "Caveats".
#
# Usage: ./tests/test.sh

set -eu

cd "$(dirname "$0")/.."

CC="${CC:-clang}"
FAIL=0

pass() { printf '  ok    %s\n' "$1"; }
fail() { printf '  FAIL  %s\n' "$1"; FAIL=$((FAIL + 1)); }

echo '== 1. C syntax check =='
if command -v "$CC" >/dev/null 2>&1; then
    if $CC -std=c99 -Wall -Wextra -Wpedantic -fsyntax-only -Isrc \
           src/mount_ich.c src/main.c 2>/tmp/mi_cc.log; then
        pass 'compiles clean'
    else
        fail 'compile errors:'
        sed 's/^/        /' /tmp/mi_cc.log
    fi
else
    printf '  skip  %s not found\n' "$CC"
fi

echo '== 2. shell script parses =='
if sh -n scripts/mount_ich.sh 2>/tmp/mi_sh.log; then
    pass 'syntax ok'
else
    fail 'syntax error:'
    sed 's/^/        /' /tmp/mi_sh.log
fi

echo '== 3. unknown mode rejected =='
set +e
sh scripts/mount_ich.sh bogus >/dev/null 2>&1
rc=$?
set -e
if [ "$rc" = 2 ]; then
    pass 'shell: exit 2'
else
    fail "shell: exit $rc, expected 2"
fi

echo '== 4. analysis tools =='
if command -v python3 >/dev/null 2>&1; then
    cd tools
    out=$(python3 constfix.py ../samples/mount_ich 2>/dev/null || true)
    cd ..

    n_dev=$(printf '%s\n' "$out" | grep -c '/dev/disk1s[1-8]' || true)
    n_chr=$(printf '%s\n' "$out" | grep -c '/dev/disk0s1s[1-8]' || true)
    n_plist=$(printf '%s\n' "$out" | grep -c 'SystemVersion.plist' || true)

    [ "$n_dev" = 8 ]   && pass 'device table: 8 native devices' \
                       || fail "device table: found $n_dev, expected 8"
    [ "$n_chr" = 8 ]   && pass 'device table: 8 checkra1n devices' \
                       || fail "device table: found $n_chr, expected 8"
    [ "$n_plist" = 2 ] && pass 'plist table: 2 entries' \
                       || fail "plist table: found $n_plist, expected 2"
else
    printf '  skip  python3 not found\n'
fi

echo '== 5. sample identity =='
want='83524dcf914a408a7cb8c740d4f37fd8f4f6a3be010db098f36568489b731613'
if command -v sha256sum >/dev/null 2>&1; then
    got=$(sha256sum samples/mount_ich | cut -d' ' -f1)
elif command -v shasum >/dev/null 2>&1; then
    got=$(shasum -a 256 samples/mount_ich | cut -d' ' -f1)
else
    got=''
fi

if [ -z "$got" ]; then
    printf '  skip  no sha256 tool found\n'
elif [ "$got" = "$want" ]; then
    pass 'sha256 matches the analysis'
else
    fail "sha256 $got != $want"
fi

echo
if [ "$FAIL" -eq 0 ]; then
    echo 'all checks passed'
    exit 0
fi

echo "$FAIL check(s) failed"
exit 1