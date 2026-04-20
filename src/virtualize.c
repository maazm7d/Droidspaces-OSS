/*
 * Droidspaces v5 - Resource Virtualization Layer
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "virtualize.h"
#include <time.h>

/*
 * Generate virtualized /proc/meminfo
 *
 * We take the host's meminfo as a template and override MemTotal/MemFree/SwapTotal/SwapFree
 * based on cgroup limits and usage.
 */
int ds_virtualize_meminfo(struct ds_config *cfg, char *buf, size_t size) {
    long long mem_limit = -1, mem_usage = -1;
    ds_cgroup_get_limits(cfg, &mem_limit, NULL, NULL, NULL);
    ds_cgroup_get_usage(cfg, &mem_usage, NULL, NULL);

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    char line[256];
    size_t offset = 0;
    while (fgets(line, sizeof(line), f) && offset < size) {
        long long val;
        if (mem_limit > 0) {
            if (sscanf(line, "MemTotal: %lld", &val) == 1) {
                offset += snprintf(buf + offset, size - offset, "MemTotal:       %11lld kB\n", mem_limit / 1024);
                continue;
            }
            if (sscanf(line, "MemFree: %lld", &val) == 1) {
                long long free = (mem_limit - mem_usage) / 1024;
                if (free < 0) free = 0;
                offset += snprintf(buf + offset, size - offset, "MemFree:        %11lld kB\n", free);
                continue;
            }
            if (sscanf(line, "MemAvailable: %lld", &val) == 1) {
                long long avail = (mem_limit - mem_usage) / 1024;
                if (avail < 0) avail = 0;
                offset += snprintf(buf + offset, size - offset, "MemAvailable:   %11lld kB\n", avail);
                continue;
            }
        }
        offset += snprintf(buf + offset, size - offset, "%s", line);
    }
    fclose(f);
    return 0;
}

/*
 * Generate virtualized /proc/cpuinfo
 *
 * Simple approach: limit the number of "processor" entries shown.
 */
int ds_virtualize_cpuinfo(struct ds_config *cfg, char *buf, size_t size) {
    int max_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN); // Fallback to host count
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        max_cpus = (int)(cfg->cpu_quota / cfg->cpu_period);
        if (max_cpus < 1) max_cpus = 1;
    }

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return -1;

    char line[256];
    size_t offset = 0;
    int current_cpu = -1;
    while (fgets(line, sizeof(line), f) && offset < size) {
        int cpu_id;
        if (sscanf(line, "processor : %d", &cpu_id) == 1) {
            current_cpu = cpu_id;
        }
        if (current_cpu >= max_cpus) break;
        offset += snprintf(buf + offset, size - offset, "%s", line);
    }
    fclose(f);
    return 0;
}

/*
 * Generate virtualized /proc/stat
 */
int ds_virtualize_stat(struct ds_config *cfg, char *buf, size_t size) {
    int max_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        max_cpus = (int)(cfg->cpu_quota / cfg->cpu_period);
        if (max_cpus < 1) max_cpus = 1;
    }

    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    char line[1024];
    size_t offset = 0;
    while (fgets(line, sizeof(line), f) && offset < size) {
        int cpu_id;
        if (sscanf(line, "cpu%d", &cpu_id) == 1) {
            if (cpu_id >= max_cpus) continue;
        }
        offset += snprintf(buf + offset, size - offset, "%s", line);
    }
    fclose(f);
    return 0;
}

/*
 * Generate virtualized /proc/uptime
 */
int ds_virtualize_uptime(struct ds_config *cfg, char *buf, size_t size) {
    (void)cfg;
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;
    double up, idle;
    if (fscanf(f, "%lf %lf", &up, &idle) != 2) {
        fclose(f);
        return -1;
    }
    fclose(f);

    /* For now, just use host uptime. Real container uptime would require
     * recording start time in monitor. */
    snprintf(buf, size, "%.2f %.2f\n", up, idle);
    return 0;
}

int ds_virtualize_init(struct ds_config *cfg) {
    /* Create tmpfs for virtual files */
    mkdir_p("run/droidspaces/vproc", 0755);
    if (domount("none", "run/droidspaces/vproc", "tmpfs", MS_NOSUID | MS_NODEV, "mode=755,size=1M") < 0) {
        return -1;
    }

    char vbuf[65536];

    if (ds_virtualize_meminfo(cfg, vbuf, sizeof(vbuf)) == 0)
        write_file("run/droidspaces/vproc/meminfo", vbuf);

    if (ds_virtualize_cpuinfo(cfg, vbuf, sizeof(vbuf)) == 0)
        write_file("run/droidspaces/vproc/cpuinfo", vbuf);

    if (ds_virtualize_stat(cfg, vbuf, sizeof(vbuf)) == 0)
        write_file("run/droidspaces/vproc/stat", vbuf);

    if (ds_virtualize_uptime(cfg, vbuf, sizeof(vbuf)) == 0)
        write_file("run/droidspaces/vproc/uptime", vbuf);

    /* Bind mount over real /proc entries */
    bind_mount("run/droidspaces/vproc/meminfo", "proc/meminfo");
    bind_mount("run/droidspaces/vproc/cpuinfo", "proc/cpuinfo");
    bind_mount("run/droidspaces/vproc/stat", "proc/stat");
    bind_mount("run/droidspaces/vproc/uptime", "proc/uptime");

    return 0;
}

void ds_virtualize_update(struct ds_config *cfg) {
    char vbuf[65536];
    char path[PATH_MAX];

    /* Monitor updates these files in the HOST view of the container's /run */
    /* We need to find the host path to the container's /run/droidspaces/vproc */
    /* But the monitor is already in the container's mount namespace if we are not careful?
     * No, the monitor is in the host mount namespace. */

    snprintf(path, sizeof(path), "/proc/%d/root/run/droidspaces/vproc/meminfo", cfg->container_pid);
    if (ds_virtualize_meminfo(cfg, vbuf, sizeof(vbuf)) == 0)
        write_file(path, vbuf);

    snprintf(path, sizeof(path), "/proc/%d/root/run/droidspaces/vproc/stat", cfg->container_pid);
    if (ds_virtualize_stat(cfg, vbuf, sizeof(vbuf)) == 0)
        write_file(path, vbuf);

    snprintf(path, sizeof(path), "/proc/%d/root/run/droidspaces/vproc/uptime", cfg->container_pid);
    if (ds_virtualize_uptime(cfg, vbuf, sizeof(vbuf)) == 0)
        write_file(path, vbuf);
}
