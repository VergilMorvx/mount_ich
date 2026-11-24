#!/bin/sh
# mount_ich.sh — shell-only equivalent of mount_ich.
#
# For environments where you cannot drop a binary. Same strategies, same
# mountpoints, same success test. Run as root inside the ramdisk.
#
#   ./mount_ich.sh [safe|full|auto|ios12|ios16|universal]
#   VERBOSE=1 ./mount_ich.sh universal
#
# Copyright (c) 2026 vergilmorvx. All rights reserved.
# Source-available restricted license — see LICENSE. NOT open source.

set -u

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/System/Library/Filesystems/apfs.fs
export PATH

MODE="${1:-universal}"
APFS_UTIL=/System/Library/Filesystems/apfs.fs/apfs.util
MOUNT_APFS=/sbin/mount_apfs

log() { [ "${VERBOSE:-0}" = 1 ] && echo "[mount_ich] $*"; return 0; }

exists()     { [ -e "$1" ]; }
is_mounted() { mount | grep -q -e " on $1 " -e " on $1 (" ; }

role_of() {
    exists "$1" || return 1
    "$APFS_UTIL" -p "$1" 2>/dev/null
}

mount_dev() {
    dev="$1"; mnt="$2"
    exists "$dev" || return 1
    is_mounted "$mnt" && return 0
    "$MOUNT_APFS" "$dev" "$mnt" >/dev/null 2>&1
    if is_mounted "$mnt"; then
        log "mounted $dev -> $mnt"
        return 0
    fi
    return 1
}

# Both container layouts: disk1sN (native) and disk0s1sN (checkra1n).
mount_role() {
    want="$1"; mnt="$2"
    for n in 1 2 3 4 5 6 7 8; do
        for d in disk1 disk0s1; do
            dev="/dev/${d}s${n}"
            if [ "$(role_of "$dev")" = "$want" ]; then
                mount_dev "$dev" "$mnt"
                return $?
            fi
        done
    done
    return 1
}

mount_aux_roles() {
    for n in 1 2 3 4 5 6 7 8; do
        for d in disk1 disk0s1; do
            dev="/dev/${d}s${n}"
            case "$(role_of "$dev")" in
                Preboot)  mount_dev "$dev" /mnt6 ;;
                xART)     mount_dev "$dev" /mnt7 ;;
                Hardware) mount_dev "$dev" /mnt4 ;;
            esac
        done
    done
}

ios_version() {
    for p in /mnt6/cryptex1/current/SystemVersion.plist \
             /mnt1/System/Library/CoreServices/SystemVersion.plist; do
        [ -e "$p" ] || continue
        v=$(plutil -extract ProductVersion raw -o - "$p" 2>/dev/null) ||
            v=$(sed -n '/ProductVersion/{n;s/.*<string>\([^<]*\)<\/string>.*/\1/p;}' \
                    "$p" 2>/dev/null | head -1)
        [ -n "$v" ] && { echo "$v"; return 0; }
    done
    return 1
}

sep_load() {
    [ -e /usr/libexec/seputil ] && /usr/libexec/seputil --gigalocker-init >/dev/null 2>&1
    [ -e /usr/sbin/nvram ] && /usr/sbin/nvram oblit-inprogress=1 >/dev/null 2>&1

    sep=""
    if [ -e /mnt6/active ]; then
        active=$(head -1 /mnt6/active 2>/dev/null | tr -d '\r\n')
        cand="/mnt6/$active/usr/standalone/firmware/sep-firmware.img4"
        [ -n "$active" ] && [ -e "$cand" ] && sep="$cand"
    fi
    [ -n "$sep" ] || sep=$(find /mnt6 -iname 'sep-firmware.img4' 2>/dev/null | head -1)

    if [ -n "$sep" ] && [ -e /usr/libexec/seputil ]; then
        log "loading sep firmware $sep"
        /usr/libexec/seputil --load "$sep" >/dev/null 2>&1
    fi

    [ -e /usr/sbin/nvram ] && /usr/sbin/nvram -d oblit-inprogress >/dev/null 2>&1

    mount_role Data /mnt2 || mount_dev /dev/disk1s2 /mnt2
}

strategy_universal() {
    firmware_only="${1:-0}"

    mount_role System /mnt1 || mount_dev /dev/disk1s1 /mnt1
    mount_aux_roles
    is_mounted /mnt6 || mount_role Preboot  /mnt6
    is_mounted /mnt7 || mount_role xART     /mnt7
    is_mounted /mnt4 || mount_role Hardware /mnt4

    [ "$firmware_only" = 1 ] && return 0

    sep_load

    if [ ! -d /mnt2/containers/Data/System ] && [ ! -d /mnt2/root ]; then
        if [ -e /usr/bin/mount_filesystems ]; then
            /usr/bin/mount_filesystems >/dev/null 2>&1
        elif [ -e /usr/bin/mount_filesystems.original ]; then
            /usr/bin/mount_filesystems.original >/dev/null 2>&1
        fi
    fi
}

strategy_legacy() {
    for h in /usr/bin/mount_root /usr/bin/mount_data /usr/bin/mount_data2; do
        [ -e "$h" ] && "$h" >/dev/null 2>&1
    done
    mount_role Preboot /mnt6
    mount_role xART    /mnt7
    mount_role Data    /mnt2 || mount_dev /dev/disk1s2 /mnt2
}

strategy_fixed() {
    mount_dev /dev/disk1s1 /mnt1
    mount_dev /dev/disk1s6 /mnt6
    mount_dev /dev/disk1s3 /mnt7
    mount_dev /dev/disk1s5 /mnt4
    sep_load
}

case "$MODE" in
    -h|--help)
        echo "usage: mount_ich [safe|full|auto|ios12|ios16|universal]"
        exit 0
        ;;
    safe|full|auto|ios12|ios16|universal) ;;
    *)
        echo "mount_ich: unknown mode '$MODE'" >&2
        echo "usage: mount_ich [safe|full|auto|ios12|ios16|universal]" >&2
        exit 2
        ;;
esac

mkdir -p /mnt1 /mnt2 /mnt4 /mnt6 /mnt7 /tmp
mount -uw / >/dev/null 2>&1
mount -u -w / >/dev/null 2>&1

if [ ! -e /dev/disk1s1 ] && [ ! -e /dev/disk0s1s1 ] && \
   [ ! -e /dev/disk1s2 ] && [ ! -e /dev/disk0s1s2 ]; then
    echo "mount failed: no block devices visible" >&2
    exit 1
fi

VER=$(ios_version) || VER=""
log "detected ios ${VER:-unknown}"

case "$MODE" in
    safe)      strategy_universal 1 ;;
    full)      strategy_universal 0 ;;
    ios12)     strategy_legacy ;;
    ios16)     strategy_fixed ;;
    universal) strategy_universal 0 ;;
    auto)
        case "$VER" in
            10.*|11.*|12.*) strategy_legacy ;;
            *)              strategy_universal 0 ;;
        esac
        ;;
esac

if { [ -d /mnt2/root ] && [ -d /mnt2/mobile ]; } || \
   [ -d /mnt2/containers/Data/System ] || \
   { [ "$MODE" = safe ] && { [ -d /mnt1/System ] || [ -d /mnt1/Applications ]; }; }; then
    echo "mount done"
    exit 0
fi

echo "mount failed" >&2
exit 1
