#!/bin/sh
# build.sh — build mount_ich for a device or a host syntax check.
#
#   ./build.sh            # device build (arm64, -Os)
#   ./build.sh check      # host syntax check only
#   ./build.sh debug      # device build with -O0 -g
#
# Copyright (c) 2026 vergilmorvx. All rights reserved.
# Source-available restricted license — see LICENSE. NOT open source.

set -eu

CC="${CC:-clang}"
SRC="src/mount_ich.c src/main.c"
OUT="${OUT:-build/mount_ich}"
CFLAGS="${CFLAGS:--Wall -Wextra}"

mkdir -p build

case "${1:-release}" in
    check)
        # shellcheck disable=SC2086
        $CC -std=c99 $CFLAGS -Wpedantic -fsyntax-only -Isrc $SRC
        echo "syntax ok"
        ;;
    debug)
        TARGET="$OUT"
        # shellcheck disable=SC2086
        $CC -arch arm64 -O0 -g $CFLAGS -Isrc -o "$TARGET" $SRC
        echo "built $TARGET (debug)"
        ;;
    release|*)
        TARGET="$OUT"
        # shellcheck disable=SC2086
        $CC -arch arm64 -Os $CFLAGS -Isrc -o "$TARGET" $SRC
        echo "built $TARGET"
        ;;
esac
