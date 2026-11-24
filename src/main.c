/*
 * main.c — CLI front end for mount_ich.
 *
 * Copyright (c) 2026 vergilmorvx. All rights reserved.
 * Source-available restricted license — see LICENSE. NOT open source.
 */

#include "mount_ich.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *out)
{
    fputs("usage: mount_ich [safe|full|auto|ios12|ios16|universal]\n"
          "\n"
          "  safe       mount firmware volumes only, never touch Data\n"
          "  full       role discovery plus SEP load\n"
          "  auto       detect the iOS version and pick a strategy (default)\n"
          "  ios12      legacy mount_root / mount_data path\n"
          "  ios16      fixed device map plus SEP load\n"
          "  universal  role-based discovery, widest path\n"
          "\n"
          "Run as root inside a ramdisk. Exit 0 on success, 1 on failure,\n"
          "2 on a bad argument.\n",
          out);
}

int main(int argc, char **argv)
{
    mi_mode mode = MI_MODE_UNIVERSAL;

    if (argc > 1) {
        const char *arg = argv[1];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
        }

        if (!mi_mode_parse(arg, &mode)) {
            fprintf(stderr, "mount_ich: unknown mode '%s'\n\n", arg);
            usage(stderr);
            return 2;
        }
    }

    mi_result r = mi_mount(mode, NULL);

    if (r.ok) {
        printf("mount done (mode=%s ios=%s system=%d data=%d preboot=%d hardware=%d)\n",
               mi_mode_name(mode),
               r.version[0] ? r.version : "unknown",
               (int)r.system_mounted,
               (int)r.data_mounted,
               (int)r.preboot_mounted,
               (int)r.hardware_mounted);
        return 0;
    }

    fprintf(stderr, "mount failed (mode=%s ios=%s system=%d data=%d preboot=%d hardware=%d)\n",
            mi_mode_name(mode),
            r.version[0] ? r.version : "unknown",
            (int)r.system_mounted,
            (int)r.data_mounted,
            (int)r.preboot_mounted,
            (int)r.hardware_mounted);
    return 1;
}
