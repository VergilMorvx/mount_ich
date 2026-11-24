/*
 * mount_ich.c — APFS volume mounter for iOS ramdisk environments.
 *
 * Mounts the volumes a ramdisk leaves unmounted: System, Data, Preboot,
 * xART, Hardware. Clean-room reimplementation of a stripped-free arm64
 * Mach-O tool of the same name; see docs/REVERSE_ENGINEERING.md for the
 * analysis this is derived from.
 *
 * Design rules:
 *   - never invoke a shell; argv arrays only
 *   - mount state comes from getmntinfo, not from parsing mount(8) output
 *   - every external command failure is visible to the caller
 *   - no banner, no sleeps, no vendor strings
 *
 * Copyright (c) 2026 vergilmorvx. All rights reserved.
 * Source-available restricted license — see LICENSE. NOT open source.
 * Do not distribute, publish, or incorporate this code. Credit is mandatory.
 */

#define _GNU_SOURCE

#include "mount_ich.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/mount.h>
#define HAVE_GETMNTINFO 1
#else
#define HAVE_GETMNTINFO 0
#endif

/* ------------------------------------------------------------------ */
/* configuration                                                       */
/* ------------------------------------------------------------------ */

#define MNT_SYSTEM    "/mnt1"
#define MNT_DATA      "/mnt2"
#define MNT_HARDWARE  "/mnt4"
#define MNT_PREBOOT   "/mnt6"
#define MNT_XART      "/mnt7"

static const char kApfsUtil[]  = "/System/Library/Filesystems/apfs.fs/apfs.util";
static const char kMountApfs[] = "/sbin/mount_apfs";
static const char kSepUtil[]   = "/usr/libexec/seputil";
static const char kNvram[]     = "/usr/sbin/nvram";
static const char kPathEnv[]   =
    "/usr/bin:/bin:/usr/sbin:/sbin:/System/Library/Filesystems/apfs.fs";

/* Both APFS container layouts: native (disk1sN) and checkra1n (disk0s1sN). */
static const char *const kDevices[] = {
    "/dev/disk1s1",   "/dev/disk1s2",   "/dev/disk1s3",   "/dev/disk1s4",
    "/dev/disk1s5",   "/dev/disk1s6",   "/dev/disk1s7",   "/dev/disk1s8",
    "/dev/disk0s1s1", "/dev/disk0s1s2", "/dev/disk0s1s3", "/dev/disk0s1s4",
    "/dev/disk0s1s5", "/dev/disk0s1s6", "/dev/disk0s1s7", "/dev/disk0s1s8",
};
#define NDEVICES (sizeof kDevices / sizeof kDevices[0])

static const char *const kVersionPlists[] = {
    "/mnt6/cryptex1/current/SystemVersion.plist",
    "/mnt1/System/Library/CoreServices/SystemVersion.plist",
};
#define NVERSIONPLISTS (sizeof kVersionPlists / sizeof kVersionPlists[0])

/* ------------------------------------------------------------------ */
/* filesystem helpers                                                  */
/* ------------------------------------------------------------------ */

static bool path_exists(const char *p)
{
    return p && access(p, F_OK) == 0;
}

static bool path_is_dir(const char *p)
{
    struct stat st;
    return p && stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ------------------------------------------------------------------ */
/* child process helpers                                               */
/* ------------------------------------------------------------------ */

/*
 * Run argv with stdout and stderr silenced. Returns the exit status, or -1 if
 * the process could not be started or was killed by a signal.
 */
static int run_quiet(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO)
                close(fd);
        }
        execv(argv[0], argv);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/*
 * Run argv and capture the first line of stdout with trailing whitespace
 * stripped. Returns true only if the child exited 0 and produced output, so a
 * missing or wedged tool is never mistaken for a successful query.
 */
static bool run_capture(char *const argv[], char *out, size_t outsz)
{
    if (!out || outsz < 2)
        return false;
    out[0] = '\0';

    int fds[2];
    if (pipe(fds) != 0)
        return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }

    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        if (fds[1] > STDOUT_FILENO)
            close(fds[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO)
                close(devnull);
        }
        execv(argv[0], argv);
        _exit(127);
    }

    close(fds[1]);

    size_t len = 0;
    while (len + 1 < outsz) {
        ssize_t n = read(fds[0], out + len, outsz - 1 - len);
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    out[len] = '\0';
    close(fds[0]);

    int status = 0;
    bool clean = waitpid(pid, &status, 0) >= 0 &&
                 WIFEXITED(status) && WEXITSTATUS(status) == 0;

    /* first line only */
    char *nl = strpbrk(out, "\r\n");
    if (nl)
        *nl = '\0';

    len = strlen(out);
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t'))
        out[--len] = '\0';

    return clean && len > 0;
}

/* ------------------------------------------------------------------ */
/* mount state                                                         */
/* ------------------------------------------------------------------ */

static bool is_mounted(const char *mnt)
{
#if HAVE_GETMNTINFO
    struct statfs *buf = NULL;
    int count = getmntinfo(&buf, MNT_NOWAIT);
    for (int i = 0; i < count; i++)
        if (buf[i].f_mntonname && strcmp(buf[i].f_mntonname, mnt) == 0)
            return true;
    return false;
#else
    /* host fallback so the same source can be smoke tested off-device */
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp)
        return false;
    char line[512];
    char target[256];
    bool found = false;
    while (fgets(line, sizeof line, fp)) {
        if (sscanf(line, "%*s %255s", target) == 1 && strcmp(target, mnt) == 0) {
            found = true;
            break;
        }
    }
    fclose(fp);
    return found;
#endif
}

/* Ask apfs.util which role a device carries. Empty string when unknown. */
static void apfs_role(const char *dev, char *role, size_t rolesz)
{
    role[0] = '\0';
    if (!path_exists(dev) || !path_exists(kApfsUtil))
        return;

    char *argv[] = { (char *)kApfsUtil, "-p", (char *)dev, NULL };
    run_capture(argv, role, rolesz);
}

static int mount_dev(const char *dev, const char *mnt)
{
    if (!path_exists(dev))
        return -1;
    if (is_mounted(mnt))
        return 0;

    char *argv[] = { (char *)kMountApfs, (char *)dev, (char *)mnt, NULL };
    run_quiet(argv);

    return is_mounted(mnt) ? 0 : -1;
}

/* Mount the first device reporting `role`, if any. */
static int mount_role(const char *role, const char *mnt)
{
    char out[128];
    for (size_t i = 0; i < NDEVICES; i++) {
        apfs_role(kDevices[i], out, sizeof out);
        if (out[0] && strcmp(out, role) == 0)
            return mount_dev(kDevices[i], mnt);
    }
    return -1;
}

/* Sweep every device once and place the volumes not covered by mount_role. */
static void mount_aux_roles(void)
{
    static const struct {
        const char *role;
        const char *mnt;
    } aux[] = {
        { "Preboot",  MNT_PREBOOT  },
        { "xART",     MNT_XART     },
        { "Hardware", MNT_HARDWARE },
    };

    char out[128];
    for (size_t i = 0; i < NDEVICES; i++) {
        apfs_role(kDevices[i], out, sizeof out);
        if (!out[0])
            continue;
        for (size_t j = 0; j < sizeof aux / sizeof aux[0]; j++) {
            if (strcmp(out, aux[j].role) == 0 && !is_mounted(aux[j].mnt))
                mount_dev(kDevices[i], aux[j].mnt);
        }
    }
}

/* ------------------------------------------------------------------ */
/* SEP / SSV                                                           */
/* ------------------------------------------------------------------ */

static void nvram_set(const char *pair)
{
    if (!path_exists(kNvram))
        return;
    char *argv[] = { (char *)kNvram, (char *)pair, NULL };
    run_quiet(argv);
}

static void nvram_delete(const char *key)
{
    if (!path_exists(kNvram))
        return;
    char *argv[] = { (char *)kNvram, "-d", (char *)key, NULL };
    run_quiet(argv);
}

/*
 * Locate sep-firmware.img4. Prefer the active preboot volume named in
 * /mnt6/active, fall back to a filesystem search.
 */
static bool find_sep_firmware(char *out, size_t outsz)
{
    out[0] = '\0';

    if (path_exists("/mnt6/active")) {
        FILE *fp = fopen("/mnt6/active", "r");
        if (fp) {
            char active[256] = { 0 };
            if (fgets(active, sizeof active, fp)) {
                char *nl = strpbrk(active, "\r\n");
                if (nl)
                    *nl = '\0';
                if (active[0])
                    snprintf(out, outsz,
                             "/mnt6/%s/usr/standalone/firmware/sep-firmware.img4",
                             active);
                if (out[0] && !path_exists(out))
                    out[0] = '\0';
            }
            fclose(fp);
        }
    }

    if (out[0])
        return true;

    char *argv[] = { "/usr/bin/find", "/mnt6", "-iname",
                     "sep-firmware.img4", NULL };
    if (!run_capture(argv, out, outsz))
        out[0] = '\0';
    return out[0] != '\0';
}

static void sep_load_and_mount_data(void)
{
    if (path_exists(kSepUtil)) {
        char *argv[] = { (char *)kSepUtil, "--gigalocker-init", NULL };
        run_quiet(argv);
    }

    nvram_set("oblit-inprogress=1");

    char sep[512];
    if (find_sep_firmware(sep, sizeof sep) && path_exists(kSepUtil)) {
        char *argv[] = { (char *)kSepUtil, "--load", sep, NULL };
        run_quiet(argv);
    }

    nvram_delete("oblit-inprogress");

    if (mount_role("Data", MNT_DATA) != 0)
        mount_dev("/dev/disk1s2", MNT_DATA);
}

/* ------------------------------------------------------------------ */
/* version detection                                                   */
/* ------------------------------------------------------------------ */

/* Pull ProductVersion out of a SystemVersion.plist, structured parse first. */
static bool read_version_from(const char *plist, char *out, size_t outsz)
{
    char *argv[] = {
        "/usr/bin/plutil", "-extract", "ProductVersion", "raw",
        "-o", "-", (char *)plist, NULL
    };
    if (run_capture(argv, out, outsz))
        return true;

    /* fallback: pull the <string> after the ProductVersion key */
    char *grep_argv[] = { "/usr/bin/grep", "-A1", "ProductVersion",
                          (char *)plist, NULL };
    char raw[256];
    if (!run_capture(grep_argv, raw, sizeof raw))
        return false;

    const char *lt = strchr(raw, '>');
    const char *end = lt ? strchr(lt + 1, '<') : NULL;
    if (!lt || !end || end <= lt + 1)
        return false;

    size_t n = (size_t)(end - lt - 1);
    if (n >= outsz)
        return false;

    memcpy(out, lt + 1, n);
    out[n] = '\0';
    return true;
}

static void read_ios_version(char *out, size_t outsz)
{
    out[0] = '\0';
    for (size_t i = 0; i < NVERSIONPLISTS; i++) {
        if (!path_exists(kVersionPlists[i]))
            continue;
        if (read_version_from(kVersionPlists[i], out, outsz))
            return;
    }
}

/* ------------------------------------------------------------------ */
/* strategies                                                          */
/* ------------------------------------------------------------------ */

/* Wide role-based path. firmware_only is the original "safe" mode. */
static void strategy_universal(bool firmware_only)
{
    if (mount_role("System", MNT_SYSTEM) != 0)
        mount_dev("/dev/disk1s1", MNT_SYSTEM);

    mount_aux_roles();

    if (!is_mounted(MNT_PREBOOT))
        mount_role("Preboot", MNT_PREBOOT);
    if (!is_mounted(MNT_XART))
        mount_role("xART", MNT_XART);
    if (!is_mounted(MNT_HARDWARE))
        mount_role("Hardware", MNT_HARDWARE);

    if (firmware_only)
        return;

    sep_load_and_mount_data();

    if (path_is_dir("/mnt2/containers/Data/System") || path_is_dir("/mnt2/root"))
        return;

    static const char *const helpers[] = {
        "/usr/bin/mount_filesystems",
        "/usr/bin/mount_filesystems.original",
    };
    for (size_t i = 0; i < sizeof helpers / sizeof helpers[0]; i++) {
        if (path_exists(helpers[i])) {
            char *argv[] = { (char *)helpers[i], NULL };
            run_quiet(argv);
            break;
        }
    }
}

/* Legacy layout: delegate to Apple's own mount helpers. */
static void strategy_legacy(void)
{
    static const char *const helpers[] = {
        "/usr/bin/mount_root",
        "/usr/bin/mount_data",
        "/usr/bin/mount_data2",
    };

    for (size_t i = 0; i < sizeof helpers / sizeof helpers[0]; i++) {
        if (path_exists(helpers[i])) {
            char *argv[] = { (char *)helpers[i], NULL };
            run_quiet(argv);
        }
    }

    mount_role("Preboot", MNT_PREBOOT);
    mount_role("xART", MNT_XART);

    if (mount_role("Data", MNT_DATA) != 0 &&
        !path_is_dir("/mnt2/root") && !path_is_dir("/mnt2/mobile"))
        mount_dev("/dev/disk1s2", MNT_DATA);
}

/* Fixed device map used by the 16.x branch of the original tool. */
static void strategy_fixed_map(void)
{
    mount_dev("/dev/disk1s1", MNT_SYSTEM);
    mount_dev("/dev/disk1s6", MNT_PREBOOT);
    mount_dev("/dev/disk1s3", MNT_XART);
    mount_dev("/dev/disk1s5", MNT_HARDWARE);
    sep_load_and_mount_data();
}

/* ------------------------------------------------------------------ */
/* driver                                                              */
/* ------------------------------------------------------------------ */

static void prepare_mountpoints(void)
{
    static const char *const dirs[] = {
        MNT_SYSTEM, MNT_DATA, MNT_HARDWARE, MNT_PREBOOT, MNT_XART, "/tmp",
    };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
        mkdir(dirs[i], 0755);
}

static void make_root_writable(void)
{
    char *modern[] = { "/sbin/mount", "-uw", "/", NULL };
    char *legacy[] = { "/sbin/mount", "-u", "-w", "/", NULL };
    run_quiet(modern);
    run_quiet(legacy);
}

static bool any_block_device_present(void)
{
    static const char *const probe[] = {
        "/dev/disk1s1", "/dev/disk0s1s1", "/dev/disk1s2", "/dev/disk0s1s2",
    };
    for (size_t i = 0; i < sizeof probe / sizeof probe[0]; i++)
        if (path_exists(probe[i]))
            return true;
    return false;
}

const char *mi_mode_name(mi_mode mode)
{
    switch (mode) {
    case MI_MODE_AUTO:      return "auto";
    case MI_MODE_SAFE:      return "safe";
    case MI_MODE_FULL:      return "full";
    case MI_MODE_IOS12:     return "ios12";
    case MI_MODE_IOS16:     return "ios16";
    case MI_MODE_UNIVERSAL: return "universal";
    }
    return "unknown";
}

bool mi_mode_parse(const char *text, mi_mode *out)
{
    static const struct {
        const char *text;
        mi_mode mode;
    } table[] = {
        { "auto",      MI_MODE_AUTO      },
        { "safe",      MI_MODE_SAFE      },
        { "full",      MI_MODE_FULL      },
        { "ios12",     MI_MODE_IOS12     },
        { "ios16",     MI_MODE_IOS16     },
        { "universal", MI_MODE_UNIVERSAL },
    };

    if (!text || !out)
        return false;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) {
        if (strcmp(text, table[i].text) == 0) {
            *out = table[i].mode;
            return true;
        }
    }
    return false;
}

mi_result mi_mount(mi_mode mode, const char *forced_version)
{
    mi_result r;
    memset(&r, 0, sizeof r);

    setenv("PATH", kPathEnv, 1);

    prepare_mountpoints();
    make_root_writable();

    if (!any_block_device_present())
        return r;

    /* Firmware volumes first, unless the mode mounts them itself. */
    if (mode != MI_MODE_IOS12) {
        if (mount_role("System", MNT_SYSTEM) != 0)
            mount_dev("/dev/disk1s1", MNT_SYSTEM);
        mount_aux_roles();
        if (!is_mounted(MNT_PREBOOT))
            mount_role("Preboot", MNT_PREBOOT);
    }

    if (forced_version)
        snprintf(r.version, sizeof r.version, "%s", forced_version);
    else
        read_ios_version(r.version, sizeof r.version);

    bool safe_mode = false;

    switch (mode) {
    case MI_MODE_SAFE:
        safe_mode = true;
        strategy_universal(true);
        break;

    case MI_MODE_FULL:
    case MI_MODE_UNIVERSAL:
        strategy_universal(false);
        break;

    case MI_MODE_IOS12:
        strategy_legacy();
        break;

    case MI_MODE_IOS16:
        strategy_fixed_map();
        break;

    case MI_MODE_AUTO:
    default: {
        const char *v = r.version;
        bool legacy = v[0] == '1' && (v[1] == '0' || v[1] == '1' || v[1] == '2')
                      && v[2] == '.';
        if (legacy)
            strategy_legacy();
        else
            strategy_universal(false);
        break;
    }
    }

    r.system_mounted   = is_mounted(MNT_SYSTEM);
    r.data_mounted     = is_mounted(MNT_DATA);
    r.preboot_mounted  = is_mounted(MNT_PREBOOT);
    r.hardware_mounted = is_mounted(MNT_HARDWARE);

    /*
     * Success means a layout a caller can actually work with: both halves of
     * the legacy Data volume, or the modern per-container Data directory.
     * "safe" only ever claims the firmware volumes, so judge it on those.
     */
    r.ok = (path_is_dir("/mnt2/root") && path_is_dir("/mnt2/mobile"))
        || path_is_dir("/mnt2/containers/Data/System")
        || (safe_mode && (path_is_dir("/mnt1/System") ||
                          path_is_dir("/mnt1/Applications")));
    return r;
}
