/*
 * Droidspaces v3 — Android-specific helpers
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Android detection
 * ---------------------------------------------------------------------------*/

int is_android(void) {
  static int cached_result = -1;
  if (cached_result != -1)
    return cached_result;

  /* Check for ANDROID_ROOT env var or presence of /system/bin/app_process */
  if (getenv("ANDROID_ROOT") || access("/system/bin/app_process", F_OK) == 0)
    cached_result = 1;
  else
    cached_result = 0;

  return cached_result;
}

/* ---------------------------------------------------------------------------
 * Android optimizations
 * ---------------------------------------------------------------------------*/

void android_optimizations(void) {
  if (!is_android())
    return;

  ds_log("Applying Android system optimizations...");

  /* Disable phantom process killer (Android 12+) */
  system("device_config put activity_manager max_phantom_processes 2147483647 "
         "2>/dev/null");

  /* Disable battery optimizations (Doze mode) for shell/root if possible */
  system("dumpsys deviceidle whitelist +com.android.shell 2>/dev/null");
}

/* ---------------------------------------------------------------------------
 * SELinux management
 * ---------------------------------------------------------------------------*/

int android_get_selinux_status(void) {
  char buf[16];
  if (read_file("/sys/fs/selinux/enforce", buf, sizeof(buf)) < 0)
    return -1;
  return atoi(buf);
}

void android_set_selinux_permissive(void) {
  if (!is_android())
    return;

  int status = android_get_selinux_status();
  if (status == 1) {
    ds_log("Setting SELinux to permissive...");
    if (write_file("/sys/fs/selinux/enforce", "0") < 0) {
      /* Try setenforce command as fallback */
      system("setenforce 0 2>/dev/null");
    }
  }
}

/* ---------------------------------------------------------------------------
 * Data partition remount (for suid support)
 * ---------------------------------------------------------------------------*/

void android_remount_data_suid(void) {
  if (!is_android())
    return;

  ds_log("Ensuring /data is mounted with suid support...");
  /* On some Android versions, /data is mounted nosuid. We need suid for
   * sudo/su/ping within the container if it's stored on /data. */
  system("mount -o remount,suid /data 2>/dev/null");
}

/* ---------------------------------------------------------------------------
 * DNS property retrieval
 * ---------------------------------------------------------------------------*/

int android_fill_dns_from_props(char *dns1, char *dns2, size_t size) {
  if (!is_android())
    return -1;

  char cmd1[128], cmd2[128];
  FILE *fp;

  /* Try common Android DNS properties */
  const char *props[] = {"net.dns1",
                         "net.dns2",
                         "net.eth0.dns1",
                         "net.eth0.dns2",
                         "net.wlan0.dns1",
                         "net.wlan0.dns2",
                         NULL};

  dns1[0] = dns2[0] = '\0';

  for (int i = 0; props[i] && !dns1[0]; i += 2) {
    snprintf(cmd1, sizeof(cmd1), "getprop %s", props[i]);
    fp = popen(cmd1, "r");
    if (fp) {
      if (fgets(dns1, size, fp)) {
        /* remove newline */
        char *nl = strchr(dns1, '\n');
        if (nl)
          *nl = '\0';
      }
      pclose(fp);
    }

    if (dns1[0] && props[i + 1]) {
      snprintf(cmd2, sizeof(cmd2), "getprop %s", props[i + 1]);
      fp = popen(cmd2, "r");
      if (fp) {
        if (fgets(dns2, size, fp)) {
          char *nl = strchr(dns2, '\n');
          if (nl)
            *nl = '\0';
        }
        pclose(fp);
      }
    }
  }

  return (dns1[0]) ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Networking / Firewall
 * ---------------------------------------------------------------------------*/

void android_configure_iptables(void) {
  if (!is_android())
    return;

  ds_log("Configuring iptables for container networking...");
  system("iptables -P FORWARD ACCEPT 2>/dev/null");
  system("iptables -t nat -A POSTROUTING -s 10.0.3.0/24 ! -d 10.0.3.0/24 -j "
         "MASQUERADE 2>/dev/null");
}

void android_setup_paranoid_network_groups(void) {
  if (!is_android())
    return;

  /* Android's "Paranoid Network" (CONFIG_ANDROID_PARANOID_NETWORK)
   * requires specific GIDs to access internet.
   * AID_INET (3003), AID_NET_RAW (3004), AID_NET_ADMIN (3005) */

  /* This is usually handled by adding the user to these groups inside the
   * rootfs. We can do it broadly for the process here if needed, but it's
   * better to use 'fix_networking_rootfs' to ensure /etc/group is correct. */
}

/* ---------------------------------------------------------------------------
 * Storage
 * ---------------------------------------------------------------------------*/

int android_setup_storage(const char *rootfs_path) {
  if (!is_android())
    return 0;

  char internal_storage[PATH_MAX];
  snprintf(internal_storage, sizeof(internal_storage), "%s/sdcard",
           rootfs_path);
  mkdir(internal_storage, 0777);

  ds_log("Mounting Android internal storage to /sdcard...");
  /* Attempt to mount /storage/emulated/0 (common internal storage path) */
  if (domount("/storage/emulated/0", internal_storage, NULL, MS_BIND | MS_REC,
              NULL) < 0) {
    /* Fallback to /sdcard */
    domount("/sdcard", internal_storage, NULL, MS_BIND | MS_REC, NULL);
  }

  return 0;
}
