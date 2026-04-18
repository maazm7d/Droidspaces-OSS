    if (hosts[i].version == 2) {
      if (cfg->memory_limit > 0) {
        snprintf(file_path, sizeof(file_path), "%s/memory.max", cg_path);
        snprintf(val, sizeof(val), "%lld", cfg->memory_limit);
        if (write_file(file_path, val) < 0) {
          ds_warn("[CGROUP] Failed to set memory limit: %s", strerror(errno));
          errors++;
        }
      }
      if (cfg->cpu_quota > 0) {
        long long period = (cfg->cpu_period > 0) ? cfg->cpu_period : 100000;
        snprintf(file_path, sizeof(file_path), "%s/cpu.max", cg_path);
        snprintf(val, sizeof(val), "%lld %lld", cfg->cpu_quota, period);
        if (write_file(file_path, val) < 0) {
          ds_warn("[CGROUP] Failed to set CPU limit: %s", strerror(errno));
          errors++;
        }
      }
      if (cfg->pids_limit > 0) {
        snprintf(file_path, sizeof(file_path), "%s/pids.max", cg_path);
        snprintf(val, sizeof(val), "%lld", cfg->pids_limit);
        if (write_file(file_path, val) < 0) {
          ds_warn("[CGROUP] Failed to set PIDs limit: %s", strerror(errno));
          errors++;
        }
      }
    } else {
      /* Cgroup V1 */
      if (cfg->memory_limit > 0 &&
          ds_cgroup_match_controller(hosts[i].controllers, "memory")) {
        snprintf(file_path, sizeof(file_path), "%s/memory.limit_in_bytes",
                 cg_path);
        snprintf(val, sizeof(val), "%lld", cfg->memory_limit);
        if (write_file(file_path, val) < 0) {
          ds_warn("[CGROUP] Failed to set memory limit (V1): %s",
                  strerror(errno));
          errors++;
        }
      }
      if (cfg->cpu_quota > 0 &&
          (ds_cgroup_match_controller(hosts[i].controllers, "cpu") ||
           ds_cgroup_match_controller(hosts[i].controllers, "cpuacct"))) {
        long long period = (cfg->cpu_period > 0) ? cfg->cpu_period : 100000;
        snprintf(file_path, sizeof(file_path), "%s/cpu.cfs_period_us", cg_path);
        snprintf(val, sizeof(val), "%lld", period);
        if (write_file(file_path, val) < 0)
          errors++;

        snprintf(file_path, sizeof(file_path), "%s/cpu.cfs_quota_us", cg_path);
        snprintf(val, sizeof(val), "%lld", cfg->cpu_quota);
        if (write_file(file_path, val) < 0) {
          ds_warn("[CGROUP] Failed to set CPU limit (V1): %s", strerror(errno));
          errors++;
        }
      }
      if (cfg->pids_limit > 0 &&
          ds_cgroup_match_controller(hosts[i].controllers, "pids")) {
        snprintf(file_path, sizeof(file_path), "%s/pids.max", cg_path);
        snprintf(val, sizeof(val), "%lld", cfg->pids_limit);
        if (write_file(file_path, val) < 0) {
          ds_warn("[CGROUP] Failed to set PIDs limit (V1): %s", strerror(errno));
          errors++;
        }
      }
    }
  }
  return (errors > 0) ? -1 : 0;
}

int ds_cgroup_get_usage(struct ds_config *cfg, long long *mem_usage, long long *cpu_usage, long long *pids_usage) {
  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);
  char safe_name[256];
  sanitize_container_name(cfg->container_name, safe_name, sizeof(safe_name));

  if (mem_usage) *mem_usage = -1;
  if (cpu_usage) *cpu_usage = -1;
  if (pids_usage) *pids_usage = -1;

  for (int i = 0; i < n; i++) {
    char cg_path[PATH_MAX];
    safe_strncpy(cg_path, hosts[i].mountpoint, sizeof(cg_path));
    strncat(cg_path, "/droidspaces/", sizeof(cg_path) - strlen(cg_path) - 1);
    strncat(cg_path, safe_name, sizeof(cg_path) - strlen(cg_path) - 1);

    if (access(cg_path, F_OK) != 0) continue;

    char file_path[PATH_MAX];
    char buf[256];

    if (hosts[i].version == 2) {
      if (mem_usage && *mem_usage == -1) {
        snprintf(file_path, sizeof(file_path), "%s/memory.current", cg_path);
        if (read_file(file_path, buf, sizeof(buf)) > 0) *mem_usage = atoll(buf);
      }
      if (cpu_usage && *cpu_usage == -1) {
        snprintf(file_path, sizeof(file_path), "%s/cpu.stat", cg_path);
        if (read_file(file_path, buf, sizeof(buf)) > 0) {
          char *usage_usec = strstr(buf, "usage_usec ");
          if (usage_usec) *cpu_usage = atoll(usage_usec + 11);
        }
      }
      if (pids_usage && *pids_usage == -1) {
        snprintf(file_path, sizeof(file_path), "%s/pids.current", cg_path);
        if (read_file(file_path, buf, sizeof(buf)) > 0) *pids_usage = atoll(buf);
      }
    } else {
      if (mem_usage && *mem_usage == -1 &&
          ds_cgroup_match_controller(hosts[i].controllers, "memory")) {
        snprintf(file_path, sizeof(file_path), "%s/memory.usage_in_bytes",
                 cg_path);
        if (read_file(file_path, buf, sizeof(buf)) > 0)
          *mem_usage = atoll(buf);
      }
      if (cpu_usage && *cpu_usage == -1 &&
          (ds_cgroup_match_controller(hosts[i].controllers, "cpuacct"))) {
        snprintf(file_path, sizeof(file_path), "%s/cpuacct.usage", cg_path);
        if (read_file(file_path, buf, sizeof(buf)) > 0)
          *cpu_usage = atoll(buf) / 1000; // ns to us
      }
      if (pids_usage && *pids_usage == -1 &&
          ds_cgroup_match_controller(hosts[i].controllers, "pids")) {
        snprintf(file_path, sizeof(file_path), "%s/pids.current", cg_path);
        if (read_file(file_path, buf, sizeof(buf)) > 0)
          *pids_usage = atoll(buf);
      }
    }
  }
  return 0;
}
