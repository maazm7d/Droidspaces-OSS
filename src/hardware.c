/*
 * Droidspaces — Hardware Access Module
 *
 * GPU group auto-detection, permission setup, and X11 socket mounting.
 *
 * Inspired by: https://github.com/shedowe19 's original implementation
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif


/*
 * scan_host_gpu_gids()
 *
 * Scan known GPU device paths on the HOST and collect unique non-root GIDs.
 * Must be called BEFORE pivot_root while /dev still refers to the host.
 *
 * Returns: number of unique GIDs found (0 = no GPU devices)
 */
int scan_host_gpu_gids(gid_t *gids, int max_gids) {
  const char *gpu_devices[] = {
      /* DRI (Intel, AMD, Mesa) */
      "/dev/dri/renderD128", "/dev/dri/renderD129", "/dev/dri/renderD130",
      "/dev/dri/card0", "/dev/dri/card1", "/dev/dri/card2",

      /* NVIDIA Proprietary Driver */
      "/dev/nvidia0", "/dev/nvidia1", "/dev/nvidia2", "/dev/nvidiactl",
      "/dev/nvidia-uvm", "/dev/nvidia-uvm-tools", "/dev/nvidia-modeset",
      "/dev/nvidia-caps/nvidia-cap1", "/dev/nvidia-caps/nvidia-cap2",

      /* ARM Mali */
      "/dev/mali0", "/dev/mali", "/dev/mali1",

      /* Qualcomm Adreno */
      "/dev/kgsl-3d0", "/dev/kgsl", "/dev/genlock",

      /* AMD Compute */
      "/dev/kfd",

      /* PowerVR */
      "/dev/pvr_sync",

      /* NVIDIA Tegra */
      "/dev/nvhost-ctrl", "/dev/nvhost-gpu", "/dev/nvmap",

      /* DMA Heaps (Modern Android) */
      "/dev/dma_heap/system", "/dev/dma_heap/linux,cma",
      "/dev/dma_heap/reserved", "/dev/dma_heap/qcom,system",

      /* Sync devices */
      "/dev/sw_sync",

      NULL};

  int count = 0;

  for (int i = 0; gpu_devices[i] != NULL; i++) {
    struct stat st;
    if (stat(gpu_devices[i], &st) < 0)
      continue;

    gid_t gid = st.st_gid;

    /* Skip root group (0) — no special group needed */
    if (gid == 0)
      continue;

    /* De-duplicate: check if we already have this GID */
    int duplicate = 0;
    for (int j = 0; j < count; j++) {
      if (gids[j] == gid) {
        duplicate = 1;
        break;
      }
    }

    if (!duplicate && count < max_gids) {
      gids[count++] = gid;
      ds_log("GPU device %-30s → GID %d", gpu_devices[i], (int)gid);
    }
  }

  if (count > 0)
    ds_log("Discovered %d unique GPU group(s) on host", count);

  return count;
}

/*
 * setup_gpu_groups()
 *
 * After pivot_root, create matching groups inside the container's /etc/group
 * and add root to each. Groups are named "gpu_<gid>" to avoid conflicts
 * with existing groups.
 *
 * Idempotent: safe to call on container restart (skips existing groups).
 */

/* Helper to check if a username exists in a comma-separated list of users */
static int has_user(const char *users, const char *username) {
  if (!users || !username)
    return 0;

  size_t len = strlen(username);
  const char *p = users;

  while ((p = strstr(p, username)) != NULL) {
    /* Check if it's a whole word match */
    int at_start = (p == users);
    int prev_comma = (p > users && *(p - 1) == ',');
    int next_comma = (*(p + len) == ',' || *(p + len) == '\0');

    if ((at_start || prev_comma) && next_comma) {
      return 1;
    }
    p++;
  }
  return 0;
}

int setup_gpu_groups(gid_t *gpu_gids, int gid_count) {
  if (gid_count <= 0)
    return 0;

  /* Check if /etc/group exists — some minimal rootfs may not have it */
  if (access("/etc/group", F_OK) != 0) {
    ds_warn("No /etc/group found, skipping GPU group setup");
    return 0;
  }

  /* We'll rewrite the group file to a temporary location */
  const char *group_path = "/etc/group";
  const char *tmp_path = "/etc/group.tmp";

  FILE *fin = fopen(group_path, "r");
  if (!fin) {
    ds_warn("Cannot read /etc/group: %s", strerror(errno));
    return -1;
  }

  FILE *fout = fopen(tmp_path, "w");
  if (!fout) {
    ds_warn("Cannot create /etc/group.tmp: %s", strerror(errno));
    fclose(fin);
    return -1;
  }

  /* Track which GIDs we found in the file */
  int *found_gids = calloc((size_t)gid_count, sizeof(int));
  if (!found_gids) {
    ds_warn("Memory allocation failed for GPU group tracking");
    fclose(fin);
    fclose(fout);
    return -1;
  }

  char line[2048];
  int modified_count = 0;

  while (fgets(line, sizeof(line), fin)) {
    /* Format: name:password:GID:user_list
     * We need to find the GID (3rd field) */
    char name[256];
    int gid_val;
    char *users_ptr = NULL;

    /* Manual parsing to be safe and extract user list pointer */
    char line_copy[2048];
    safe_strncpy(line_copy, line, sizeof(line_copy));

    /* Remove trailing newline for parsing */
    char *nl = strrchr(line_copy, '\n');
    if (nl)
      *nl = '\0';

    int colons = 0;
    char *p = line_copy;
    char *gid_str = NULL;

    while (*p) {
      if (*p == ':') {
        colons++;
        if (colons == 2)
          gid_str = p + 1;
        else if (colons == 3) {
          *p = '\0';
          users_ptr = p + 1;
          break;
        }
      }
      p++;
    }

    if (gid_str && users_ptr) {
      gid_val = atoi(gid_str);

      /* Check if this GID is one of our target GPU GIDs */
      int gpu_idx = -1;
      for (int i = 0; i < gid_count; i++) {
        if (gpu_gids[i] == (gid_t)gid_val) {
          gpu_idx = i;
          break;
        }
      }

      if (gpu_idx != -1) {
        found_gids[gpu_idx] = 1;

        /* Check if 'root' is already in the user list */
        if (!has_user(users_ptr, "root")) {
          /* Extract group name for logging */
          p = line_copy;
          int i = 0;
          while (*p && *p != ':' && i < 255)
            name[i++] = *p++;
          name[i] = '\0';

          /* Add root to the members list */
          if (strlen(users_ptr) > 0)
            fprintf(fout, "%.*s:%s,root\n", (int)(users_ptr - line_copy - 1),
                    line_copy, users_ptr);
          else
            fprintf(fout, "%.*s:root\n", (int)(users_ptr - line_copy - 1),
                    line_copy);

          ds_log("Added root to existing group '%s' (GID %d)", name, gid_val);
          modified_count++;
          continue; /* Skip the default fputs below */
        }
      }
    }

    /* Print original line if not modified */
    fputs(line, fout);
  }

  /* Append absolutely missing GPU groups */
  for (int i = 0; i < gid_count; i++) {
    if (!found_gids[i]) {
      fprintf(fout, "gpu_%d:x:%d:root\n", (int)gpu_gids[i], (int)gpu_gids[i]);
      ds_log("Created new GPU group gpu_%d (GID %d)", (int)gpu_gids[i],
             (int)gpu_gids[i]);
      modified_count++;
    }
  }

  fclose(fin);
  fclose(fout);
  free(found_gids);

  /* Atomic replacement */
  if (modified_count > 0) {
    if (rename(tmp_path, group_path) < 0) {
      ds_warn("Failed to update /etc/group: %s", strerror(errno));
      unlink(tmp_path);
      return -1;
    }
    ds_log("Finalized GPU group membership (Updated %d entry/entries)",
           modified_count);
  } else {
    unlink(tmp_path);
  }

  return 0;
}

/*
 * setup_x11_socket()
 *
 * Bind mount X11 socket directory for GUI application support.
 * Supports both desktop Linux and Termux X11 (Android).
 *
 * Only the .X11-unix subdirectory is mounted — never the entire /tmp.
 * Binding /tmp causes "required key not available" errors on encrypted
 * Android devices due to FBE keyring conflicts.
 *
 * Non-fatal: silently returns 0 if no X11 socket is found.
 *
 */

/*
 * stop_termux_if_running()
 *
 * Checks if Termux is running and aggressively stops it to prevent
 * blocking during tmpfs mount.
 */
void stop_termux_if_running(void) {
  struct stat st;
  if (stat("/data/data/com.termux", &st) != 0) {
    return; /* Termux not installed */
  }

  /* Check if Termux is actually running */
  if (system("pidof com.termux >/dev/null 2>&1") != 0) {
    return; /* Not running, nothing to do */
  }

  ds_log("Stopping Termux to prepare unified /tmp...");

  /* Method 1: Use Android Activity Manager to stop app */
  int ret = system("am force-stop com.termux 2>/dev/null");

  /* Method 2: Fallback to pkill if am fails */
  if (ret != 0) {
    system("pkill -9 com.termux 2>/dev/null");
  }

  /* Give it a moment to die */
  nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 500000000}, NULL);
}

/*
 * setup_unified_tmpfs()
 *
 */
int setup_unified_tmpfs(void) {
  const char *termux_tmp = DS_TERMUX_TMP_DIR;
  struct stat st;
  struct statfs fs;

  /* Check if Termux exists */
  if (stat("/data/data/com.termux", &st) != 0) {
    return 0; /* Non-fatal: Termux not installed */
  }

  /* Ensure tmp directory exists */
  mkdir_p(termux_tmp, 0755);

  /* Already mounted? Just ensure ownership is correct. */
  if (statfs(termux_tmp, &fs) == 0 && fs.f_type == TMPFS_MAGIC) {
    chown(termux_tmp, st.st_uid, st.st_gid);
    chmod(termux_tmp, 01777);
    return 0;
  }

  /* Detect Termux SELinux context (including categories) */
  char context[256] = {0};
  if (get_selinux_context("/data/data/com.termux", context, sizeof(context)) < 0) {
    safe_strncpy(context, "u:object_r:app_data_file:s0", sizeof(context));
  }

  /* Mount tmpfs with proper permissions and ownership */
  char mount_opts[256];
  snprintf(mount_opts, sizeof(mount_opts), "size=256M,mode=1777,uid=%d,gid=%d",
           (int)st.st_uid, (int)st.st_gid);

  if (mount("tmpfs", termux_tmp, "tmpfs", MS_NOSUID | MS_NODEV, mount_opts) != 0) {
    ds_warn("Failed to create unified /tmp: %s", strerror(errno));
    return -1;
  }

  /* Explicitly apply SELinux context to the mount point */
  if (set_selinux_context(termux_tmp, context) < 0) {
    ds_warn("Failed to apply SELinux context to unified /tmp: %s",
            strerror(errno));
  }

  return 0;
}

/*
 * cleanup_unified_tmpfs()
 *
 */
void cleanup_unified_tmpfs(void) {
  struct statfs fs;

  /* Only unmount if a tmpfs is actually present at the target path */
  if (statfs(DS_TERMUX_TMP_DIR, &fs) == 0 && fs.f_type == TMPFS_MAGIC) {
    umount2(DS_TERMUX_TMP_DIR, MNT_DETACH);
  }
}

/*
 * setup_x11_and_virgl_sockets()
 *
 */
int setup_x11_and_virgl_sockets(struct ds_config *cfg) {
  (void)cfg;

  if (!is_android()) {
    /* Desktop Linux path */
    const char *x11_source = DS_X11_PATH_DESKTOP;
    if (access(x11_source, F_OK) == 0) {
      ds_log("Found Desktop X11 socket at %s", x11_source);
      mkdir_p("/tmp", 01777);
      mkdir_p(DS_X11_CONTAINER_DIR, 01777);
      if (mount(x11_source, DS_X11_CONTAINER_DIR, NULL, MS_BIND | MS_REC, NULL) < 0) {
        ds_warn("Failed to bind mount X11 socket: %s", strerror(errno));
        return -1;
      }
      ds_log("X11 socket directory bind-mounted successfully");
    } else {
      ds_warn("X11 support skipped: No host X11 socket detected");
    }
    return 0;
  }

  /* Android path: bridge Termux /tmp into container's /tmp */
  const char *bridge_source = DS_TERMUX_TMP_OLDROOT;  /* FIX: Use explicit macro */
  const char *container_tmp = "/tmp";

  /* Verify source exists */
  if (access(bridge_source, F_OK) != 0) {
    ds_warn("Termux not installed - X11/VirGL socket bridge unavailable");
    return 0; /* Non-fatal */
  }

  ds_log("Bridging Termux and container for X11/VirGL sockets...");

  /* Ensure container /tmp exists */
  mkdir_p(container_tmp, 01777);

  /* Bind mount entire /tmp (includes .X11-unix and .virgl_test) */
  if (mount(bridge_source, container_tmp, NULL, MS_BIND, NULL) != 0) {
    ds_warn("Failed to bridge /tmp sockets: %s", strerror(errno));
    return 0; /* Non-fatal */
  }

  /* Ensure permissions are correct */
  chmod(container_tmp, 01777);

  return 0;
}

/*
 * setup_hardware_access()
 *
 * Top-level entry point called from boot.c AFTER pivot_root.
 * Orchestrates GPU group creation and X11 socket mounting.
 *
 * All operations are non-fatal: failures produce warnings but don't
 * prevent the container from booting.
 *
 */
int setup_hardware_access(struct ds_config *cfg, gid_t *gpu_gids,
                          int gid_count) {
  if (!cfg->hw_access && !cfg->termux_x11)
    return 0;

  /* 1. Create GPU groups inside the container */
  if (cfg->hw_access)
    setup_gpu_groups(gpu_gids, gid_count);

  /* 2. Mount X11 socket for GUI applications */
  if (cfg->hw_access || cfg->termux_x11)
    setup_x11_and_virgl_sockets(cfg);

  return 0;
}
