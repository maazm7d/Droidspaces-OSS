/*
 * Droidspaces v5 - Resource Virtualization Layer
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "virtualize.h"
#include <time.h>

/* Atomic write using rename within the same directory */
static int write_file_atomic_at(const char *path, const char *content) {
    char temp[PATH_MAX * 2];
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    if (write_file(temp, content) < 0) return -1;
    if (rename(temp, path) < 0) {
        unlink(temp);
        return -1;
    }
    return 0;
}

/*
 * Generate virtualized /proc/meminfo
 */
int ds_virtualize_meminfo(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    long long mem_limit = -1, mem_usage = -1;
    ds_cgroup_get_limits(cfg, &mem_limit, NULL, NULL, NULL);
    ds_cgroup_get_usage(cfg, &mem_usage, NULL, NULL);

    if (mem_usage < 0) mem_usage = 0;

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    size_t cap = 16384;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return -1; }

    char line[1024];
    size_t offset = 0;
    long long cached = 0, buffers = 0;

    while (fgets(line, sizeof(line), f)) {
        if (offset + 1024 >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) { free(buf); fclose(f); return -1; }
            buf = newbuf;
        }

        if (mem_limit > 0) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                offset += snprintf(buf + offset, cap - offset, "MemTotal:       %11lld kB\n", mem_limit / 1024);
                continue;
            }
            if (strncmp(line, "MemFree:", 8) == 0) {
                long long fval = (mem_limit - mem_usage) / 1024;
                if (fval < 0) fval = 0;
                offset += snprintf(buf + offset, cap - offset, "MemFree:        %11lld kB\n", fval);
                continue;
            }
            if (strncmp(line, "Cached:", 7) == 0) {
                (void)sscanf(line, "Cached: %lld", &cached);
            }
            if (strncmp(line, "Buffers:", 8) == 0) {
                (void)sscanf(line, "Buffers: %lld", &buffers);
            }
            if (strncmp(line, "MemAvailable:", 13) == 0) {
                long long fval = (mem_limit - mem_usage) / 1024;
                long long avail = fval + (long long)((cached + buffers) * 0.8);
                if (avail > mem_limit / 1024) avail = mem_limit / 1024;
                if (avail < 0) avail = 0;
                offset += snprintf(buf + offset, cap - offset, "MemAvailable:   %11lld kB\n", avail);
                continue;
            }
            /* Virtualize Swap as 0 for now as most containers don't have host swap access or shouldn't see it */
            if (strncmp(line, "SwapTotal:", 10) == 0) {
                offset += snprintf(buf + offset, cap - offset, "SwapTotal:             0 kB\n");
                continue;
            }
            if (strncmp(line, "SwapFree:", 9) == 0) {
                offset += snprintf(buf + offset, cap - offset, "SwapFree:              0 kB\n");
                continue;
            }
        }
        size_t len = strlen(line);
        if (offset + len < cap) {
            memcpy(buf + offset, line, len);
            offset += len;
        }
    }
    buf[offset] = '\0';
    fclose(f);
    *buf_out = buf;
    *size_out = offset;
    return 0;
}

/*
 * Generate virtualized /proc/cpuinfo
 */
int ds_virtualize_cpuinfo(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    int max_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        max_cpus = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
        if (max_cpus < 1) max_cpus = 1;
    }

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) return -1;

    size_t cap = 65536;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return -1; }

    char line[4096];
    size_t offset = 0;
    int current_cpu = -1;
    while (fgets(line, sizeof(line), f)) {
        int cpu_id;
        if (sscanf(line, "processor : %d", &cpu_id) == 1) {
            current_cpu = cpu_id;
        }
        if (current_cpu >= max_cpus) break;

        size_t len = strlen(line);
        if (offset + len + 1 >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) { free(buf); fclose(f); return -1; }
            buf = newbuf;
        }
        memcpy(buf + offset, line, len);
        offset += len;
    }
    buf[offset] = '\0';
    fclose(f);
    *buf_out = buf;
    *size_out = offset;
    return 0;
}

/*
 * Generate virtualized /proc/stat
 */
int ds_virtualize_stat(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    int max_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        max_cpus = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
        if (max_cpus < 1) max_cpus = 1;
    }

    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    size_t cap = 65536;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return -1; }

    char line[2048];
    size_t offset = 0;
    unsigned long long sum_user = 0, sum_nice = 0, sum_system = 0, sum_idle = 0,
                       sum_iowait = 0, sum_irq = 0, sum_softirq = 0, sum_steal = 0,
                       sum_guest = 0, sum_guest_nice = 0;

    /* First pass: calculate sums for aggregate cpu line */
    while (fgets(line, sizeof(line), f)) {
        int cpu_id;
        if (sscanf(line, "cpu%d", &cpu_id) == 1) {
            if (cpu_id < max_cpus) {
                unsigned long long u = 0, n = 0, s = 0, i = 0, io = 0, ir = 0, si = 0, st = 0, gu = 0, gn = 0;
                sscanf(line, "cpu%*d %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &u, &n, &s, &i, &io, &ir, &si, &st, &gu, &gn);
                sum_user += u; sum_nice += n; sum_system += s; sum_idle += i;
                sum_iowait += io; sum_irq += ir; sum_softirq += si; sum_steal += st;
                sum_guest += gu; sum_guest_nice += gn;
            }
        }
    }
    rewind(f);

    /* Second pass: output virtualized content */
    int aggregate_written = 0;
    while (fgets(line, sizeof(line), f)) {
        if (offset + 1024 >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) { free(buf); fclose(f); return -1; }
            buf = newbuf;
        }

        if (strncmp(line, "cpu ", 4) == 0) {
            if (!aggregate_written) {
                offset += snprintf(buf + offset, cap - offset, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",
                                   sum_user, sum_nice, sum_system, sum_idle, sum_iowait, sum_irq, sum_softirq, sum_steal, sum_guest, sum_guest_nice);
                aggregate_written = 1;
            }
            continue;
        }

        int cpu_id;
        if (sscanf(line, "cpu%d", &cpu_id) == 1) {
            if (cpu_id >= max_cpus) continue;
        }

        size_t len = strlen(line);
        memcpy(buf + offset, line, len);
        offset += len;
    }
    buf[offset] = '\0';
    fclose(f);
    *buf_out = buf;
    *size_out = offset;
    return 0;
}

/*
 * Generate virtualized /proc/uptime
 */
int ds_virtualize_uptime(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double up = (double)(now.tv_sec - cfg->start_time.tv_sec) +
                (double)(now.tv_nsec - cfg->start_time.tv_nsec) / 1e9;
    if (up < 0) up = 0;

    char *buf = malloc(128);
    if (!buf) return -1;
    *size_out = snprintf(buf, 128, "%.2f 0.00\n", up);
    *buf_out = buf;
    return 0;
}

int ds_virtualize_init(struct ds_config *cfg) {
    /* Use absolute path for vproc in container rootfs */
    char vproc_path[PATH_MAX * 2] = "/run/droidspaces/vproc";

    if (mkdir_p(vproc_path, 0755) < 0) return -1;
    if (domount("none", vproc_path, "tmpfs", MS_NOSUID | MS_NODEV, "mode=755,size=1M") < 0) {
        return -1;
    }

    const char *files[] = {"meminfo", "cpuinfo", "stat", "uptime"};
    int (*funcs[])(struct ds_config *, char **, size_t *) = {
        ds_virtualize_meminfo, ds_virtualize_cpuinfo, ds_virtualize_stat, ds_virtualize_uptime
    };

    for (int i = 0; i < 4; i++) {
        char *vbuf = NULL;
        size_t vsz = 0;
        if (funcs[i](cfg, &vbuf, &vsz) == 0) {
            char path[PATH_MAX * 4];
            snprintf(path, sizeof(path), "%s/%s", vproc_path, files[i]);
            if (write_file(path, vbuf) < 0) {
                free(vbuf);
                return -1;
            }
            free(vbuf);

            char target[PATH_MAX];
            snprintf(target, sizeof(target), "/proc/%s", files[i]);
            if (bind_mount(path, target) < 0) {
                ds_warn("Failed to bind mount virtual %s over %s", path, target);
                return -1;
            }
        } else {
            ds_warn("Failed to generate virtual content for %s", files[i]);
            return -1;
        }
    }

    return 0;
}

void ds_virtualize_update(struct ds_config *cfg) {
    char *vbuf = NULL;
    size_t vsz = 0;
    char path[PATH_MAX];

    const char *files[] = {"meminfo", "stat", "uptime"};
    int (*funcs[])(struct ds_config *, char **, size_t *) = {
        ds_virtualize_meminfo, ds_virtualize_stat, ds_virtualize_uptime
    };

    for (int i = 0; i < 3; i++) {
        if (funcs[i](cfg, &vbuf, &vsz) == 0) {
            snprintf(path, sizeof(path), "/proc/%d/root/run/droidspaces/vproc/%s", cfg->container_pid, files[i]);
            /* Check if still valid container and path exists to avoid PID recycling race */
            struct stat st;
            if (stat(path, &st) == 0) {
                write_file_atomic_at(path, vbuf);
            }
            free(vbuf);
        }
    }
}
