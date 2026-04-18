        }
        free(it);
      }
    }
  }

  /* 2. FORCED SYSTEMD SUPPORT: If we are booting a systemd rootfs but no
   * systemd hierarchy was found on the host, we MUST create one manually.
   * On modern kernels (or if V2 is active), we skip this because systemd
   * will use the unified hierarchy. */
  if (is_systemd && !systemd_setup_done && !v2_active) {
    mkdir("sys/fs/cgroup/systemd", 0755);
    if (mount("cgroup", "sys/fs/cgroup/systemd", "cgroup",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "none,name=systemd") < 0) {
      ds_error("Failed to mount systemd cgroup: %s", strerror(errno));
      return -1;
    }
    systemd_setup_done = 1;
  }

  /* If it's a systemd container and we still don't have a systemd cgroup, fail
   * early. */
  if (is_systemd && !systemd_setup_done) {
    ds_error("Systemd cgroup setup failed. Systemd containers cannot boot.");
    return -1;
  }

  /* Final isolation: Both Pure V2 and Legacy V1 environments stay Read-Write.
   * - Systemd-v2 needs write access to the root to manage scopes.
   * - Systemd-v1/OpenRC needs write access to create controller symlinks.
   * The "Hybrid-RO" middle-ground is now removed. */

  return 0;
}

/**
 * Move a process (usually self) into the same cgroup hierarchy as target_pid.
 * This is used by 'enter' to ensure the process is physically inside the
 * container's cgroup subtree on the host, which is required for D-Bus/logind
 * inside the container to correctly move the process into session scopes.
 */
int ds_cgroup_attach(pid_t target_pid) {
  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);

  for (int i = 0; i < n; i++) {
    const char *ctrl = (hosts[i].version == 2) ? NULL : hosts[i].controllers;
    char first_ctrl[64];

    if (hosts[i].version == 1 && ctrl) {
      if (sscanf(ctrl, "%63[^,]", first_ctrl) == 1)
        ctrl = first_ctrl;
    }

    /* 1. Discover where target_pid lives in this hierarchy */
    char proc_path[PATH_MAX];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/cgroup", target_pid);

    FILE *f = fopen(proc_path, "re");
    if (!f)
      continue;

    char line[1024];
    char subpath[PATH_MAX] = {0};
    while (fgets(line, sizeof(line), f)) {
      char *col1 = strchr(line, ':');
      if (!col1)
        continue;
      char *col2 = strchr(col1 + 1, ':');
      if (!col2)
        continue;

      char *subsys = col1 + 1;
      *col2 = '\0';
      char *path = col2 + 1;

      int match = 0;
      if (hosts[i].version == 2 && subsys[0] == '\0') {
        match = 1;
      } else if (hosts[i].version == 1 && ctrl && strstr(subsys, ctrl)) {
        match = 1;
      }

      if (match) {
        char *nl = strchr(path, '\n');
        if (nl)
          *nl = '\0';
        safe_strncpy(subpath, path, sizeof(subpath));

        /* Professional refinement: if the path ends in a systemd management
         * unit (.scope, .service, .slice), strip that component. This ensures
         * the 'ds-enter-PID' cgroup is created as a peer to 'init.scope'
         * (the container root) rather than being nested inside it. This is
         * cleaner for systemd's accounting and avoids "non-leaf" V2 errors. */
        char *last_slash = strrchr(subpath, '/');
        if (last_slash && last_slash != subpath) {
          if (strstr(last_slash, ".scope") || strstr(last_slash, ".service") ||
              strstr(last_slash, ".slice")) {
            *last_slash = '\0';
          }
        }
        break;
      }
    }
    fclose(f);

    if (subpath[0] == '\0')
      continue;

    /* 2. Create a fresh leaf cgroup under init's path.
     *
     * Writing directly to init's cgroup.procs fails with EPERM on cgroupv1
     * legacy kernels (and for systemd-managed scopes on v2): the cgroup is
     * either non-leaf or systemd holds a delegation lock on it.  The correct
     * approach - which is exactly what lxc-attach uses - is to mkdir a new
     * child cgroup under the target's subtree and write into THAT.  We own
     * the new directory so the write always succeeds, and the process appears
     * in the hierarchy as a proper descendant of init's cgroup rather than
     * leaking to the cgroup root ("/"). */
    /* Build: <mountpoint>/<subpath>/ds-enter-<pid>
     * subpath always starts with '/' so we skip the extra separator.
     * Use strncat chains - snprintf of two PATH_MAX strings into one
     * PATH_MAX buffer triggers -Wformat-truncation=2 at compile time. */
    char leaf_dir[PATH_MAX];
    char enter_suffix[32];
    safe_strncpy(leaf_dir, hosts[i].mountpoint, sizeof(leaf_dir));
    /* subpath begins with '/', append directly - no extra '/' needed. */
    strncat(leaf_dir, subpath, sizeof(leaf_dir) - strlen(leaf_dir) - 1);
    snprintf(enter_suffix, sizeof(enter_suffix), "/ds-enter-%d", (int)getpid());
    strncat(leaf_dir, enter_suffix, sizeof(leaf_dir) - strlen(leaf_dir) - 1);

    if (mkdir(leaf_dir, 0755) < 0 && errno != EEXIST) {
      continue;
    }

    /* 3. Move self into the leaf via cgroup.procs (moves whole process,
     *    not just the calling thread - unlike the legacy /tasks interface). */
    char procs_path[PATH_MAX];
    safe_strncpy(procs_path, leaf_dir, sizeof(procs_path));
    strncat(procs_path, "/cgroup.procs",
            sizeof(procs_path) - strlen(procs_path) - 1);

    int fd = open(procs_path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
      continue;
    }

    char pid_s[32];
    int len = snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());
    if (write(fd, pid_s, len) < 0) {
    }
    close(fd);
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * ds_cgroup_detach
 *
 * Removes the ds-enter-<pid> leaf cgroup directories that ds_cgroup_attach()
 * created for a single enter/run session.  Must be called by the parent after
 * waitpid() so the leaf is guaranteed to be empty.
 * ---------------------------------------------------------------------------*/
void ds_cgroup_detach(pid_t child_pid) {
  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);

  char enter_suffix[32];
  snprintf(enter_suffix, sizeof(enter_suffix), "/ds-enter-%d", (int)child_pid);

  for (int i = 0; i < n; i++) {
    char ds_dir[PATH_MAX];
    safe_strncpy(ds_dir, hosts[i].mountpoint, sizeof(ds_dir));
    strncat(ds_dir, "/droidspaces", sizeof(ds_dir) - strlen(ds_dir) - 1);

    DIR *top = opendir(ds_dir);
    if (!top) {
      char direct[PATH_MAX];
      safe_strncpy(direct, hosts[i].mountpoint, sizeof(direct));
      strncat(direct, enter_suffix, sizeof(direct) - strlen(direct) - 1);
      rmdir(direct);
      continue;
    }

    struct dirent *de;
    while ((de = readdir(top)) != NULL) {
      if (de->d_name[0] == '.')
        continue;
      char leaf[PATH_MAX];
      safe_strncpy(leaf, ds_dir, sizeof(leaf));
      strncat(leaf, "/", sizeof(leaf) - strlen(leaf) - 1);
      strncat(leaf, de->d_name, sizeof(leaf) - strlen(leaf) - 1);
      strncat(leaf, enter_suffix, sizeof(leaf) - strlen(leaf) - 1);
      rmdir(leaf);
    }
    closedir(top);
  }
}

/* ---------------------------------------------------------------------------
 * ds_cgroup_cleanup_container
 *
 * Removes the entire /sys/fs/cgroup/droidspaces/<container_name>/ subtree
 * that was created at container start for cgroup namespace isolation.
 *
 * The kernel requires a bottom-up rmdir walk - a cgroup directory can only
 * be removed after all its children are gone.  All container processes are
 * dead by the time cleanup_container_resources() calls this, so every leaf
 * is empty and the walk always succeeds.
 *
 * Safe to call on every stop regardless of whether the directory exists
 * (all rmdir calls are silently ignored on ENOENT).
 * ---------------------------------------------------------------------------*/

/* Recursive bottom-up rmdir of a cgroup subtree.  cgroup directories can
 * only be removed from the leaves upward - attempting to rmdir a non-empty
 * cgroup returns EBUSY.
 *
 * Even after all processes exit, cgroup state is destroyed asynchronously
 * by the kernel.  Child dirs enter a "dying" state that is invisible to
 * readdir() but still causes the parent's rmdir() to return EBUSY.
 *
 * We handle this with two mechanisms:
 *   1. cgroup.kill (kernel 5.14+): write "1" to kill all remaining
 *      processes in the subtree atomically, then poll cgroup.events
 *      until populated=0 before attempting rmdir.
 *   2. Retry loop: for older kernels without cgroup.kill, retry rmdir
 *      with short sleeps to let the async cleanup complete. */
static void rmdir_cgroup_tree(const char *path) {
  DIR *d = opendir(path);
  if (!d) {
    rmdir(path);
    return;
  }

  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.')
      continue;
    if (de->d_type != DT_DIR)
      continue;

    char child[PATH_MAX];
    safe_strncpy(child, path, sizeof(child));
    strncat(child, "/", sizeof(child) - strlen(child) - 1);
    strncat(child, de->d_name, sizeof(child) - strlen(child) - 1);
    rmdir_cgroup_tree(child);
  }
  closedir(d);

  /* 1. cgroup.kill - available on kernel 5.14+.
   *    Writing "1" sends SIGKILL to every process in the subtree
   *    atomically, including those in dying child cgroups. */
  char kill_path[PATH_MAX];
  safe_strncpy(kill_path, path, sizeof(kill_path));
  strncat(kill_path, "/cgroup.kill", sizeof(kill_path) - strlen(kill_path) - 1);
  if (access(kill_path, W_OK) == 0) {
    int kfd = open(kill_path, O_WRONLY | O_CLOEXEC);
    if (kfd >= 0) {
      if (write(kfd, "1", 1) < 0) {
      }
      close(kfd);
    }
  }

  /* 2. Poll cgroup.events for populated=0.
   *    Bail out after ~500ms (50 × 10ms) to avoid blocking forever. */
  char events_path[PATH_MAX];
  safe_strncpy(events_path, path, sizeof(events_path));
  strncat(events_path, "/cgroup.events",
          sizeof(events_path) - strlen(events_path) - 1);
  for (int i = 0; i < 50; i++) {
    char buf[256] = {0};
    if (read_file(events_path, buf, sizeof(buf)) > 0) {
      if (strstr(buf, "populated 0"))
        break;
    }
    usleep(10000); /* 10 ms */
  }

  /* 3. rmdir with retry - handles residual dying descendants on older
   *    kernels that lack cgroup.kill.  10 attempts × 20 ms = 200 ms max. */
  for (int attempt = 0; attempt < 10; attempt++) {
    if (rmdir(path) == 0 || errno == ENOENT)
      return;
    if (errno != EBUSY)
      return;      /* unexpected error - give up */
    usleep(20000); /* 20 ms */
  }
}

void ds_cgroup_cleanup_container(const char *container_name) {
  if (!container_name || !container_name[0])
    return;

  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);

  char safe_name[256];
  sanitize_container_name(container_name, safe_name, sizeof(safe_name));

  for (int i = 0; i < n; i++) {
    char cg_path[PATH_MAX];
    safe_strncpy(cg_path, hosts[i].mountpoint, sizeof(cg_path));
    strncat(cg_path, "/droidspaces/", sizeof(cg_path) - strlen(cg_path) - 1);
    strncat(cg_path, safe_name, sizeof(cg_path) - strlen(cg_path) - 1);

    if (access(cg_path, F_OK) != 0)
      continue; /* nothing to clean on this hierarchy */
    rmdir_cgroup_tree(cg_path);
  }
}

int ds_cgroup_host_create(struct ds_config *cfg) {
  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);
  if (n == 0) {
    ds_warn("[CGROUP] No cgroup hierarchies found on host.");
    return -1;
  }

  char safe_name[256];
  sanitize_container_name(cfg->container_name, safe_name, sizeof(safe_name));

  int joined = 0;

  for (int i = 0; i < n; i++) {
    char base_ds_path[PATH_MAX];
    safe_strncpy(base_ds_path, hosts[i].mountpoint, sizeof(base_ds_path));
    strncat(base_ds_path, "/droidspaces",
            sizeof(base_ds_path) - strlen(base_ds_path) - 1);

    if (mkdir(base_ds_path, 0755) < 0 && errno != EEXIST) {
      continue;
    }

    /* For Cgroup V2, enable controllers in the parent group so they are
     * available in the container group. */
    if (hosts[i].version == 2) {
      char subtree_ctrl[PATH_MAX];
      safe_strncpy(subtree_ctrl, base_ds_path, sizeof(subtree_ctrl));
      strncat(subtree_ctrl, "/cgroup.subtree_control",
              sizeof(subtree_ctrl) - strlen(subtree_ctrl) - 1);
      (void)write_file(subtree_ctrl, "+cpuset +cpu +io +memory +pids");
    }

    char cg_path[PATH_MAX];
    safe_strncpy(cg_path, base_ds_path, sizeof(cg_path));
    strncat(cg_path, "/", sizeof(cg_path) - strlen(cg_path) - 1);
    strncat(cg_path, safe_name, sizeof(cg_path) - strlen(cg_path) - 1);

    if (mkdir(cg_path, 0755) < 0 && errno != EEXIST) {
      ds_warn("[CGROUP] Failed to create cgroup directory %s: %s", cg_path,
              strerror(errno));
      continue;
    }

    char procs_path[PATH_MAX];
    safe_strncpy(procs_path, cg_path, sizeof(procs_path));
    strncat(procs_path, "/cgroup.procs",
            sizeof(procs_path) - strlen(procs_path) - 1);

    char pid_s[32];
    snprintf(pid_s, sizeof(pid_s), "%d", (int)getpid());
    if (write_file(procs_path, pid_s) == 0) {
      joined++;
    } else {
      ds_warn("[CGROUP] Failed to join cgroup %s: %s", cg_path, strerror(errno));
    }
  }

  if (joined == 0) {
    ds_error("[CGROUP] Failed to join any cgroup hierarchies.");
    return -1;
  }

  return 0;
}

int ds_cgroup_apply_limits(struct ds_config *cfg) {
  struct host_cgroup hosts[32];
  int n = get_host_cgroups(hosts, 32);
  char safe_name[256];
  sanitize_container_name(cfg->container_name, safe_name, sizeof(safe_name));

  int errors = 0;

  for (int i = 0; i < n; i++) {
    char cg_path[PATH_MAX];
    safe_strncpy(cg_path, hosts[i].mountpoint, sizeof(cg_path));
    strncat(cg_path, "/droidspaces/", sizeof(cg_path) - strlen(cg_path) - 1);
    strncat(cg_path, safe_name, sizeof(cg_path) - strlen(cg_path) - 1);

    if (access(cg_path, F_OK) != 0)
      continue;

    char file_path[PATH_MAX];
    char val[64];
