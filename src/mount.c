/*
 * Droidspaces v3 — Mounting logic
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Generic mount wrappers
 * ---------------------------------------------------------------------------*/

int domount(const char *src, const char *tgt, const char *fstype,
            unsigned long flags, const char *data) {
  if (mount(src, tgt, fstype, flags, data) < 0) {
    /* Don't log if it's already mounted (EBUSY) */
    if (errno != EBUSY) {
      ds_error("Failed to mount %s on %s (%s): %s", src ? src : "none", tgt,
               fstype ? fstype : "none", strerror(errno));
      return -1;
    }
  }
  return 0;
}

int bind_mount(const char *src, const char *tgt) {
  /* Ensure target exists */
  struct stat st_src, st_tgt;
  if (stat(src, &st_src) < 0)
    return -1;

  if (stat(tgt, &st_tgt) < 0) {
    if (S_ISDIR(st_src.st_mode))
      mkdir(tgt, 0755);
    else
      write_file(tgt, ""); /* Create empty file as mount point */
  }

  return domount(src, tgt, NULL, MS_BIND | MS_REC, NULL);
}

/* ---------------------------------------------------------------------------
 * /dev setup
 * ---------------------------------------------------------------------------*/

int setup_dev(const char *rootfs, int hw_access) {
  char dev_path[PATH_MAX];
  snprintf(dev_path, sizeof(dev_path), "%s/dev", rootfs);

  if (hw_access) {
    /* If hw_access is enabled, we mount host's devtmpfs.
     * WARNING: This is insecure but provides full hardware access. */
    return domount("devtmpfs", dev_path, "devtmpfs", MS_NOSUID | MS_NOEXEC,
                   NULL);
  } else {
    /* Secure isolated /dev using tmpfs */
    if (domount("none", dev_path, "tmpfs", MS_NOSUID | MS_NOEXEC,
                "size=4M,mode=755") < 0)
      return -1;

    /* Create minimal set of device nodes */
    return create_devices(rootfs);
  }
}

int create_devices(const char *rootfs) {
  const struct {
    const char *name;
    mode_t mode;
    dev_t dev;
  } devices[] = {{"null", S_IFCHR | 0666, makedev(1, 3)},
                 {"zero", S_IFCHR | 0666, makedev(1, 5)},
                 {"full", S_IFCHR | 0666, makedev(1, 7)},
                 {"random", S_IFCHR | 0666, makedev(1, 8)},
                 {"urandom", S_IFCHR | 0666, makedev(1, 9)},
                 {"tty", S_IFCHR | 0666, makedev(5, 0)},
                 {"console", S_IFCHR | 0600, makedev(5, 1)},
                 {"ptmx", S_IFCHR | 0666, makedev(5, 2)},
                 {"tty1", S_IFREG | 0000, 0}, /* Mount targets for PTYs */
                 {"tty2", S_IFREG | 0000, 0},
                 {"tty3", S_IFREG | 0000, 0},
                 {"tty4", S_IFREG | 0000, 0},
                 {NULL, 0, 0}};

  char path[PATH_MAX];
  for (int i = 0; devices[i].name; i++) {
    snprintf(path, sizeof(path), "%s/dev/%s", rootfs, devices[i].name);
    if (mknod(path, devices[i].mode, devices[i].dev) < 0 && errno != EEXIST) {
      if (S_ISREG(devices[i].mode)) {
        write_file(path, ""); /* Create empty file if mknod failed for reg */
      } else {
        /* If mknod fails for char dev, try bind mounting from host */
        char host_path[PATH_MAX];
        snprintf(host_path, sizeof(host_path), "/dev/%s", devices[i].name);
        bind_mount(host_path, path);
      }
    }
  }
  /* Standard symlinks */
  char tgt[PATH_MAX];
  snprintf(tgt, sizeof(tgt), "%s/dev/fd", rootfs);
  symlink("/proc/self/fd", tgt);
  snprintf(tgt, sizeof(tgt), "%s/dev/stdin", rootfs);
  symlink("/proc/self/fd/0", tgt);
  snprintf(tgt, sizeof(tgt), "%s/dev/stdout", rootfs);
  symlink("/proc/self/fd/1", tgt);
  snprintf(tgt, sizeof(tgt), "%s/dev/stderr", rootfs);
  symlink("/proc/self/fd/2", tgt);

  return 0;
}

int setup_devpts(void) {
  mkdir("/dev/pts", 0755);
  /* Use newinstance flag to get private PTY namespace */
  return domount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC,
                 "newinstance,ptmxmode=0666,mode=0620,gid=5");
}

int setup_cgroups(void) {
  mkdir("/sys/fs/cgroup", 0755);

  /* Detect Cgroup v2 (unified hierarchy) */
  if (access("/sys/fs/cgroup/cgroup.controllers", F_OK) == 0 ||
      grep_file("/proc/mounts", "cgroup2")) {
    return domount("cgroup2", "/sys/fs/cgroup", "cgroup2",
                   MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
  }

  /* Fallback to Cgroup v1 legacy hierarchies */
  if (domount("none", "/sys/fs/cgroup", "tmpfs",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755") < 0)
    return -1;

  const char *subs[] = {"cpu",   "cpuacct", "devices", "memory", "freezer",
                        "blkio", "pids",    "systemd", NULL};
  char path[PATH_MAX];
  for (int i = 0; subs[i]; i++) {
    snprintf(path, sizeof(path), "/sys/fs/cgroup/%s", subs[i]);
    mkdir(path, 0755);
    domount("cgroup", path, "cgroup", MS_NOSUID | MS_NODEV | MS_NOEXEC,
            subs[i]);
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Rootfs Image Handling
 * ---------------------------------------------------------------------------*/

int mount_rootfs_img(const char *img_path, char *mount_point, size_t mp_size) {
  /* Use workspace/mounts/ as base for image mounts */
  snprintf(mount_point, mp_size, "%s/mounts", get_workspace_dir());
  mkdir(mount_point, 0755);

  /* Construct unique mount point based on filename hash or name */
  const char *filename = strrchr(img_path, '/');
  filename = filename ? filename + 1 : img_path;

  char name_only[128];
  safe_strncpy(name_only, filename, sizeof(name_only));
  char *dot = strrchr(name_only, '.');
  if (dot)
    *dot = '\0';

  strncat(mount_point, "/", mp_size - strlen(mount_point) - 1);
  strncat(mount_point, name_only, mp_size - strlen(mount_point) - 1);
  mkdir(mount_point, 0755);

  ds_log("Mounting rootfs image %s on %s...", img_path, mount_point);

  /* Run e2fsck first if it's an ext image */
  char cmd[PATH_MAX + 64];
  snprintf(cmd, sizeof(cmd), "e2fsck -f -y %s >/dev/null 2>&1", img_path);
  if (system(cmd) == 0) {
    ds_log("Image checked and repaired successfully.");
  }

  /* Mount via loop device */
  snprintf(cmd, sizeof(cmd), "mount -v -o loop %s %s 2>/dev/null", img_path,
           mount_point);
  if (system(cmd) != 0) {
    ds_error("Failed to mount image %s", img_path);
    return -1;
  }

  return 0;
}

int unmount_rootfs_img(const char *mount_point) {
  if (!mount_point || !mount_point[0])
    return 0;

  ds_log("Unmounting rootfs image from %s...", mount_point);

  /* Try lazy unmount first */
  if (umount2(mount_point, MNT_DETACH) < 0) {
    /* Fallback to standard umount via shell for better loop cleanup */
    char cmd[PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "umount -l %s 2>/dev/null", mount_point);
    system(cmd);
  }

  /* Try to remove the directory (will only succeed if empty) */
  rmdir(mount_point);
  return 0;
}
