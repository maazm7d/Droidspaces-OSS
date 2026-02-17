/*
 * Droidspaces v3 — Networking setup
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Host-side networking setup (before container boot)
 * ---------------------------------------------------------------------------*/

int fix_networking_host(struct ds_config *cfg) {
  ds_log("Configuring host-side networking for %s...", cfg->container_name);

  /* Enable IPv4 forwarding */
  write_file("/proc/sys/net/ipv4/ip_forward", "1");

  /* Enable IPv6 forwarding if requested */
  if (cfg->enable_ipv6) {
    write_file("/proc/sys/net/ipv6/conf/all/forwarding", "1");
  }

  if (is_android()) {
    /* Android specific NAT and firewall */
    android_configure_iptables();
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Rootfs-side networking setup (inside container, after pivot_root)
 * ---------------------------------------------------------------------------*/

int fix_networking_rootfs(struct ds_config *cfg) {
  /* 1. Hostname */
  if (cfg->hostname[0]) {
    if (sethostname(cfg->hostname, strlen(cfg->hostname)) < 0) {
      ds_warn("Failed to set hostname to %s: %s", cfg->hostname,
              strerror(errno));
    }
    /* Persist to /etc/hostname */
    char hn_buf[128];
    snprintf(hn_buf, sizeof(hn_buf), "%s\n", cfg->hostname);
    write_file("/etc/hostname", hn_buf);
  }

  /* 2. /etc/hosts */
  char hosts_content[1024];
  snprintf(hosts_content, sizeof(hosts_content),
           "127.0.0.1\tlocalhost\n"
           "::1\t\tlocalhost ip6-localhost ip6-loopback\n"
           "127.0.1.1\t%s\n",
           cfg->hostname);
  write_file("/etc/hosts", hosts_content);

  /* 3. resolv.conf */
  char dns1[64] = "8.8.8.8", dns2[64] = "8.8.4.4";
  if (is_android()) {
    android_fill_dns_from_props(dns1, dns2, sizeof(dns1));
  }

  char resolv_content[256];
  if (dns2[0])
    snprintf(resolv_content, sizeof(resolv_content),
             "nameserver %s\nnameserver %s\n", dns1, dns2);
  else
    snprintf(resolv_content, sizeof(resolv_content), "nameserver %s\n", dns1);

  write_file("/etc/resolv.conf", resolv_content);

  /* 4. Android Network Groups */
  if (is_android()) {
    /* If /etc/group exists, ensure aid_inet and other groups are present
     * so the user can actually use the network. */
    const char *etc_group = "/etc/group";
    if (access(etc_group, F_OK) == 0) {
      if (!grep_file(etc_group, "aid_inet")) {
        FILE *fg = fopen(etc_group, "a");
        if (fg) {
          fprintf(
              fg,
              "aid_inet:x:3003:\naid_net_raw:x:3004:\naid_net_admin:x:3005:\n");
          fclose(fg);
        }
      }
    }
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Runtime introspection
 * ---------------------------------------------------------------------------*/

int detect_ipv6_in_container(pid_t pid) {
  char path[PATH_MAX];
  build_proc_root_path(pid, "/proc/sys/net/ipv6/conf/all/disable_ipv6", path,
                       sizeof(path));

  char buf[16];
  if (read_file(path, buf, sizeof(buf)) < 0)
    return -1;

  /* 0 means enabled, 1 means disabled */
  return (buf[0] == '0') ? 1 : 0;
}
