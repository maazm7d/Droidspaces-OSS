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

/* Helper to read cgroup v2 memory.stat */
static void get_cgroup_v2_mem_stat(struct ds_config *cfg, long long *anon, long long *file, long long *slab) {
    char path[PATH_MAX * 2];
    char buf[4096];
    snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/memory.stat", cfg->container_name);
    if (read_file(path, buf, sizeof(buf)) > 0) {
        char *p;
        if ((p = strstr(buf, "anon "))) sscanf(p + 5, "%lld", anon);
        if ((p = strstr(buf, "file "))) sscanf(p + 5, "%lld", file);
        if ((p = strstr(buf, "slab "))) sscanf(p + 5, "%lld", slab);
    }
}

/*
 * Generate virtualized /proc/meminfo
 */
int ds_virtualize_meminfo(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    long long mem_limit = -1, mem_usage = -1;
    ds_cgroup_get_limits(cfg, &mem_limit, NULL, NULL, NULL);
    ds_cgroup_get_usage(cfg, &mem_usage, NULL, NULL);

    /* Try to get memory.high as effective limit if memory.max is unlimited */
    if (mem_limit <= 0) {
        char path[PATH_MAX];
        char buf[64];
        snprintf(path, sizeof(path), "/sys/fs/cgroup/droidspaces/%s/memory.high", cfg->container_name);
        if (read_file(path, buf, sizeof(buf)) > 0) {
            if (strncmp(buf, "max", 3) != 0) mem_limit = atoll(buf);
        }
    }

    if (mem_usage < 0) mem_usage = 0;

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    /* Get host MemTotal first to calculate scaling ratio */
    long long host_total = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %lld", &host_total) == 1) break;
    }
    rewind(f);

    double ratio = 1.0;
    if (mem_limit > 0 && host_total > 0) {
        ratio = (double)mem_limit / (host_total * 1024.0);
    }

    /* Try to get accurate cgroup stats */
    long long cg_anon = -1, cg_file = -1, cg_slab = -1;
    get_cgroup_v2_mem_stat(cfg, &cg_anon, &cg_file, &cg_slab);

    size_t cap = 16384;
    char *buf = malloc(cap);
    if (!buf) { fclose(f); return -1; }

    size_t offset = 0;
    while (fgets(line, sizeof(line), f)) {
        if (offset + 1024 >= cap) {
            cap *= 2;
            char *newbuf = realloc(buf, cap);
            if (!newbuf) { free(buf); fclose(f); return -1; }
            buf = newbuf;
        }

        char key[64];
        long long val;
        if (sscanf(line, "%63[^:]: %lld", key, &val) == 2) {
            if (mem_limit > 0) {
                if (strcmp(key, "MemTotal") == 0) {
                    val = mem_limit / 1024;
                } else if (strcmp(key, "MemFree") == 0) {
                    val = (mem_limit - mem_usage) / 1024;
                    if (val < 0) val = 0;
                } else if (strcmp(key, "MemAvailable") == 0) {
                    /* Better approximation: Free + Cache + Buffers (scaled) */
                    long long free_kb = (mem_limit - mem_usage) / 1024;
                    if (free_kb < 0) free_kb = 0;
                    /* We don't have per-container Cache/Buffers easily for v1,
                     * so we use scaled values from host for components. */
                    val = free_kb; // Fallback, will be adjusted if we have more info
                } else if (strstr(key, "Swap")) {
                    val = 0;
                } else if (strcmp(key, "AnonPages") == 0 && cg_anon >= 0) {
                    val = cg_anon / 1024;
                } else if ((strcmp(key, "Cached") == 0 || strcmp(key, "Mapped") == 0) && cg_file >= 0) {
                    val = cg_file / 1024;
                } else if (strcmp(key, "Slab") == 0 && cg_slab >= 0) {
                    val = cg_slab / 1024;
                } else {
                    /* Scale other components */
                    val = (long long)(val * ratio);
                }

                /* Final MemAvailable adjustment if we just calculated components */
                if (strcmp(key, "MemAvailable") == 0) {
                    // For simplicity in this refined model, we treat MemAvailable same as MemFree
                    // unless we want to sum up scaled components.
                    // Let's use a slightly better one if we have cg_file.
                    if (cg_file >= 0) val += (cg_file / 1024);
                    else val += (long long)(val * 0.5); // heuristic if ratio based
                    if (val > mem_limit / 1024) val = mem_limit / 1024;
                }

                offset += snprintf(buf + offset, cap - offset, "%-16s %11lld kB\n", key, val);
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

/*
 * Generate virtualized /proc/loadavg
 */
int ds_virtualize_loadavg(struct ds_config *cfg, char **buf_out, size_t *size_out) {
    int host_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    int container_cpus = host_cpus;
    if (cfg->cpu_quota > 0 && cfg->cpu_period > 0) {
        container_cpus = (int)((cfg->cpu_quota + cfg->cpu_period - 1) / cfg->cpu_period);
        if (container_cpus < 1) container_cpus = 1;
    }

    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;

    double l1, l5, l15;
    int runnable, total, last_pid;
    if (fscanf(f, "%lf %lf %lf %d/%d %d", &l1, &l5, &l15, &runnable, &total, &last_pid) != 6) {
        fclose(f);
        return -1;
    }
    fclose(f);

    double ratio = (double)container_cpus / host_cpus;
    char *buf = malloc(256);
    if (!buf) return -1;

    *size_out = snprintf(buf, 256, "%.2f %.2f %.2f %d/%d %d\n",
                         l1 * ratio, l5 * ratio, l15 * ratio,
                         (int)(runnable * ratio) > 0 ? (int)(runnable * ratio) : (runnable > 0 ? 1 : 0),
                         (int)(total * ratio) > 0 ? (int)(total * ratio) : 1,
                         last_pid);
    *buf_out = buf;
    return 0;
}

int ds_virtualize_init(struct ds_config *cfg) {
    char vproc_path[PATH_MAX * 2] = "/run/droidspaces/vproc";

    if (mkdir_p(vproc_path, 0755) < 0) return -1;
    if (domount("none", vproc_path, "tmpfs", MS_NOSUID | MS_NODEV, "mode=755,size=1M") < 0) {
        return -1;
    }

    const char *files[] = {"meminfo", "cpuinfo", "stat", "uptime", "loadavg"};
    int (*funcs[])(struct ds_config *, char **, size_t *) = {
        ds_virtualize_meminfo, ds_virtualize_cpuinfo, ds_virtualize_stat, ds_virtualize_uptime, ds_virtualize_loadavg
    };

    for (int i = 0; i < 5; i++) {
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

    const char *files[] = {"meminfo", "stat", "uptime", "loadavg"};
    int (*funcs[])(struct ds_config *, char **, size_t *) = {
        ds_virtualize_meminfo, ds_virtualize_stat, ds_virtualize_uptime, ds_virtualize_loadavg
    };

    for (int i = 0; i < 4; i++) {
        if (funcs[i](cfg, &vbuf, &vsz) == 0) {
            snprintf(path, sizeof(path), "/proc/%d/root/run/droidspaces/vproc/%s", cfg->container_pid, files[i]);
            struct stat st;
            if (stat(path, &st) == 0) {
                write_file_atomic_at(path, vbuf);
            }
            free(vbuf);
        }
    }
}
