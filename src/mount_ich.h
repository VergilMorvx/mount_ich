/*
 * mount_ich.h — APFS volume mounter for iOS ramdisk environments.
 *
 * Public interface. Everything else in mount_ich.c is internal.
 *
 * Copyright (c) 2026 vergilmorvx. All rights reserved.
 * Source-available restricted license — see LICENSE. NOT open source.
 */

#ifndef MOUNT_ICH_H
#define MOUNT_ICH_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mount strategy. Mirrors the original tool's CLI vocabulary so existing
 * invocations keep working.
 */
typedef enum {
    MI_MODE_AUTO = 0,   /* detect iOS version, pick a strategy */
    MI_MODE_SAFE,       /* firmware volumes only, never touch Data */
    MI_MODE_FULL,       /* role discovery + SEP load */
    MI_MODE_IOS12,      /* legacy mount_root / mount_data path */
    MI_MODE_IOS16,      /* fixed device map + SEP load */
    MI_MODE_UNIVERSAL   /* role-based discovery, widest path */
} mi_mode;

/*
 * Outcome. `ok` is the overall verdict; the per-volume flags exist so a caller
 * can report which stage failed instead of just "it didn't work".
 */
typedef struct {
    bool ok;
    bool system_mounted;
    bool data_mounted;
    bool preboot_mounted;
    bool hardware_mounted;
    char version[64];   /* detected (or forced) iOS version, "" if unknown */
} mi_result;

/*
 * Bring up the APFS volumes. Blocks until done. Not thread safe; call once at
 * boot. Requires root and a ramdisk-style device environment.
 *
 * forced_version may be NULL to auto-detect, or a string like "16.5" to skip
 * detection (useful on images where the plist is not readable).
 */
mi_result mi_mount(mi_mode mode, const char *forced_version);

/* Stable lowercase name for a mode, e.g. "universal". Never NULL. */
const char *mi_mode_name(mi_mode mode);

/* Parse a mode from a CLI-style string. Returns false if unrecognised. */
bool mi_mode_parse(const char *text, mi_mode *out);

#ifdef __cplusplus
}
#endif

#endif /* MOUNT_ICH_H */
