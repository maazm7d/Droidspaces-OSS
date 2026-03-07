/*
 * Droidspaces v5 — High-performance Container Runtime
 *
 * Pure-C RTNETLINK client: link, address, route, and policy-rule management.
 * Replaces all `ip link/addr/route/rule` shell invocations.
 *
 * Kernel compatibility: 3.18+ (Android & Linux)
 * No external dependencies beyond musl/glibc.
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <arpa/inet.h>
#include <linux/if_link.h>
#include <linux/rtnetlink.h>
#include <linux/veth.h>
#include <net/if.h>
#include <sys/socket.h>

/* ---------------------------------------------------------------------------
 * Netlink rule attributes (linux/fib_rules.h — not always in Android sysroot)
 * ---------------------------------------------------------------------------*/
#ifndef FRA_DST
#define FRA_DST 1      /* destination address */
#define FRA_SRC 2      /* source address */
#define FRA_PRIORITY 6 /* rule priority / preference */
#define FRA_TABLE 15   /* extended table id */
#endif

/* ---------------------------------------------------------------------------
 * Internal constants
 * ---------------------------------------------------------------------------*/

#define NL_BUFSIZE 8192

/* ---------------------------------------------------------------------------
 * Netlink message helpers
 * ---------------------------------------------------------------------------*/

/* Pointer to byte just past the last written byte in a netlink message */
#define NLMSG_TAIL(n)                                                          \
  ((struct rtattr *)(((uint8_t *)(n)) + NLMSG_ALIGN((n)->nlmsg_len)))

/* Append an rtattr with optional payload to a netlink message */
static struct rtattr *nl_addattr(struct nlmsghdr *n, int maxlen, int type,
                                 const void *data, int dlen) {
  int len = RTA_LENGTH(dlen);
  if ((int)(NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len)) > maxlen)
    return NULL;
  struct rtattr *rta = NLMSG_TAIL(n);
  rta->rta_type = (unsigned short)type;
  rta->rta_len = (unsigned short)len;
  if (dlen && data)
    memcpy(RTA_DATA(rta), data, (size_t)dlen);
  n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
  return rta;
}

/* Begin a nested rtattr (container with length fixed up by nl_nest_end) */
static struct rtattr *nl_nest_begin(struct nlmsghdr *n, int maxlen, int type) {
  return nl_addattr(n, maxlen, type, NULL, 0);
}

/* Fix up the length of a nested rtattr opened by nl_nest_begin */
static void nl_nest_end(struct nlmsghdr *n, struct rtattr *nest) {
  nest->rta_len = (unsigned short)((uint8_t *)NLMSG_TAIL(n) - (uint8_t *)nest);
}

/* ---------------------------------------------------------------------------
 * Context lifecycle
 * ---------------------------------------------------------------------------*/

struct ds_nl_ctx {
  int fd;       /* AF_NETLINK / NETLINK_ROUTE socket */
  uint32_t seq; /* monotonically increasing sequence number */
  pid_t pid;    /* our PID used as nl_portid */
};

ds_nl_ctx_t *ds_nl_open(void) {
  ds_nl_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
    return NULL;

  ctx->fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (ctx->fd < 0) {
    free(ctx);
    return NULL;
  }

  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  if (bind(ctx->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
    close(ctx->fd);
    free(ctx);
    return NULL;
  }

  ctx->pid = getpid();
  ctx->seq = 1;
  return ctx;
}

void ds_nl_close(ds_nl_ctx_t *ctx) {
  if (ctx) {
    close(ctx->fd);
    free(ctx);
  }
}

/* ---------------------------------------------------------------------------
 * Send + blocking receive with full multi-part / ACK loop
 *
 * Returns 0 on success, negative errno on error.
 * NLMSG_ERROR with error==0 is an explicit ACK (success).
 * ---------------------------------------------------------------------------*/

static int ds_nl_talk(ds_nl_ctx_t *ctx, struct nlmsghdr *req) {
  req->nlmsg_seq = ++ctx->seq;
  req->nlmsg_pid = (uint32_t)ctx->pid;

  struct sockaddr_nl sa;
  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;

  struct iovec iov = {req, req->nlmsg_len};
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_name = &sa;
  msg.msg_namelen = sizeof(sa);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  if (sendmsg(ctx->fd, &msg, 0) < 0)
    return -errno;

  uint8_t buf[NL_BUFSIZE];
  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -errno;
    }

    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      /* Ignore responses for other in-flight requests */
      if (h->nlmsg_seq != req->nlmsg_seq)
        continue;

      if (h->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = NLMSG_DATA(h);
        return err->error; /* 0 = ACK/success, negative = error */
      }
      if (h->nlmsg_type == NLMSG_DONE)
        return 0;
      if (h->nlmsg_flags & NLM_F_MULTI)
        continue; /* more fragments coming */
      return 0;
    }
    break;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Kernel capability probe for NAT networking
 *
 * Tests whether the running kernel supports:
 *   1. Network namespaces    (CONFIG_NET_NS)
 *   2. Bridge devices        (CONFIG_BRIDGE)
 *   3. Veth pairs            (CONFIG_VETH)
 *
 * Does NOT test iptables nat — that has a separate binary fallback path.
 *
 * Returns 0 if all supported.
 * Returns -1 and writes a human-readable reason into reason[rsz].
 * ---------------------------------------------------------------------------*/

int ds_nl_probe_nat_capability(char *reason, size_t rsz) {
  int ret;
  /* ── Step 1: CONFIG_NET_NS ── */
  if (access("/proc/self/ns/net", F_OK) != 0) {
    snprintf(reason, rsz,
             "CONFIG_NET_NS not compiled in. "
             "Network namespaces are required for --net=nat. "
             "Rebuild your kernel with CONFIG_NET_NS=y.");
    return -1;
  }

  ds_nl_ctx_t *ctx = ds_nl_open();
  if (!ctx) {
    snprintf(reason, rsz, "Failed to open NETLINK_ROUTE socket: %s",
             strerror(errno));
    return -1;
  }

  /* ── Step 2: CONFIG_BRIDGE ── */
  int has_bridge = 1;
  const char *probe_br = "ds-cap-br0";
  ret = ds_nl_create_bridge(ctx, probe_br);
  if (ret < 0) {
    if (ret == -EOPNOTSUPP) {
      has_bridge = 0;
      ds_log("[NET] CONFIG_BRIDGE not supported — will fallback to bridgeless "
             "NAT");
    } else {
      snprintf(reason, rsz, "Bridge probe failed unexpectedly: %s",
               strerror(-ret));
      ds_nl_close(ctx);
      return -1;
    }
  }

  /* ── Step 3: CONFIG_VETH ── */
  ret = ds_nl_create_veth(ctx, "ds-cap-h0", "ds-cap-p0");
  int has_veth = (ret == 0);
  int veth_err = ret;

  /* ── Cleanup Probe Interfaces ── */
  if (has_bridge)
    ds_nl_del_link(ctx, probe_br);
  if (has_veth)
    ds_nl_del_link(ctx, "ds-cap-h0");

  ds_nl_close(ctx);

  if (!has_veth) {
    if (veth_err == -EOPNOTSUPP) {
      snprintf(reason, rsz,
               "CONFIG_VETH not enabled (kernel returned EOPNOTSUPP). "
               "Virtual Ethernet pairs are required for --net=nat. "
               "Rebuild your kernel with CONFIG_VETH=y.");
    } else {
      snprintf(reason, rsz, "Veth probe failed unexpectedly: %s",
               strerror(-veth_err));
    }
    return -1;
  }

  if (has_bridge) {
    ds_log("[NET] Kernel capability probe passed: NET_NS + BRIDGE + VETH OK.");
    if (reason)
      snprintf(reason, rsz, "OK (Full NAT)");
    return 0;
  } else {
    ds_log(
        "[NET] Kernel capability probe limited: NET_NS + VETH OK (No BRIDGE).");
    if (reason)
      snprintf(reason, rsz, "OK (Bridgeless NAT Fallback)");
    return 1;
  }
}

/* ---------------------------------------------------------------------------
 * Link existence check
 * ---------------------------------------------------------------------------*/

int ds_nl_link_exists(ds_nl_ctx_t *ctx, const char *ifname) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[512];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_GETLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, ifname,
             (int)strlen(ifname) + 1);
  return (ds_nl_talk(ctx, &req.n) == 0) ? 1 : 0;
}

/* ---------------------------------------------------------------------------
 * Get interface index by name
 * (uses if_nametoindex — one ioctl, no netlink round-trip needed)
 * ---------------------------------------------------------------------------*/

int ds_nl_get_ifindex(ds_nl_ctx_t *ctx, const char *ifname) {
  (void)ctx;
  unsigned int idx = if_nametoindex(ifname);
  return (idx > 0) ? (int)idx : -ENODEV;
}

/* ---------------------------------------------------------------------------
 * Create bridge
 * ---------------------------------------------------------------------------*/

int ds_nl_create_bridge(ds_nl_ctx_t *ctx, const char *name) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[1024];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;

  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, name,
             (int)strlen(name) + 1);

  struct rtattr *linfo = nl_nest_begin(&req.n, (int)sizeof(req), IFLA_LINKINFO);
  nl_addattr(&req.n, (int)sizeof(req), IFLA_INFO_KIND, "bridge", 7);
  nl_nest_end(&req.n, linfo);

  int ret = ds_nl_talk(ctx, &req.n);
  /* -EEXIST means bridge already present — idempotent */
  return (ret == 0 || ret == -EEXIST) ? 0 : ret;
}

/* ---------------------------------------------------------------------------
 * Create veth pair
 *
 * The VETH_INFO_PEER attribute wraps a full struct ifinfomsg header followed
 * by IFLA_* sub-attributes — exactly as iproute2/ip/link_veth.c does it.
 * We write the ifinfomsg directly at NLMSG_TAIL (it is NOT an rtattr payload)
 * then append IFLA_IFNAME as a normal sub-rtattr.
 * ---------------------------------------------------------------------------*/

int ds_nl_create_veth(ds_nl_ctx_t *ctx, const char *host, const char *peer) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[2048];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;

  /* Host-side name */
  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, host,
             (int)strlen(host) + 1);

  /* LINKINFO → INFO_KIND="veth" → INFO_DATA → VETH_INFO_PEER */
  struct rtattr *linfo = nl_nest_begin(&req.n, (int)sizeof(req), IFLA_LINKINFO);
  nl_addattr(&req.n, (int)sizeof(req), IFLA_INFO_KIND, "veth", 5);

  struct rtattr *ldata =
      nl_nest_begin(&req.n, (int)sizeof(req), IFLA_INFO_DATA);
  struct rtattr *peer_rta =
      nl_nest_begin(&req.n, (int)sizeof(req), VETH_INFO_PEER);

  /* Embed peer ifinfomsg header directly (not as rtattr payload) */
  {
    struct ifinfomsg peer_ifi;
    memset(&peer_ifi, 0, sizeof(peer_ifi));
    peer_ifi.ifi_family = AF_UNSPEC;

    uint8_t *base = (uint8_t *)&req.n;
    size_t off = NLMSG_ALIGN(req.n.nlmsg_len);
    if (off + sizeof(peer_ifi) > sizeof(req))
      return -ENOSPC;
    memcpy(base + off, &peer_ifi, sizeof(peer_ifi));
    req.n.nlmsg_len = (uint32_t)(off + sizeof(peer_ifi));

    /* Peer-side IFLA_IFNAME */
    nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, peer,
               (int)strlen(peer) + 1);
  }

  nl_nest_end(&req.n, peer_rta);
  nl_nest_end(&req.n, ldata);
  nl_nest_end(&req.n, linfo);

  return ds_nl_talk(ctx, &req.n);
}

/* ---------------------------------------------------------------------------
 * Attach an interface to a bridge (IFLA_MASTER)
 * ---------------------------------------------------------------------------*/

int ds_nl_set_master(ds_nl_ctx_t *ctx, const char *ifname, const char *master) {
  int master_idx = ds_nl_get_ifindex(ctx, master);
  if (master_idx <= 0)
    return -ENODEV;

  int if_idx = ds_nl_get_ifindex(ctx, ifname);
  if (if_idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = if_idx;

  nl_addattr(&req.n, (int)sizeof(req), IFLA_MASTER, &master_idx,
             (int)sizeof(int));
  return ds_nl_talk(ctx, &req.n);
}

/* ---------------------------------------------------------------------------
 * Bring link UP / DOWN
 * ---------------------------------------------------------------------------*/

int ds_nl_link_up(ds_nl_ctx_t *ctx, const char *ifname) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;
  req.i.ifi_flags = IFF_UP;
  req.i.ifi_change = IFF_UP;
  return ds_nl_talk(ctx, &req.n);
}

int ds_nl_link_down(ds_nl_ctx_t *ctx, const char *ifname) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;
  req.i.ifi_flags = 0;
  req.i.ifi_change = IFF_UP;
  return ds_nl_talk(ctx, &req.n);
}

/* ---------------------------------------------------------------------------
 * Delete a link by name (idempotent — ENODEV/ENOENT treated as success)
 * ---------------------------------------------------------------------------*/

int ds_nl_del_link(ds_nl_ctx_t *ctx, const char *ifname) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return 0; /* already gone */

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_DELLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;

  int ret = ds_nl_talk(ctx, &req.n);
  return (ret == 0 || ret == -ENODEV || ret == -ENOENT) ? 0 : ret;
}

/* ---------------------------------------------------------------------------
 * Rename an interface
 * Note: interface must be DOWN before rename; we bring it down first.
 * ---------------------------------------------------------------------------*/

int ds_nl_rename(ds_nl_ctx_t *ctx, const char *ifname, const char *newname) {
  ds_nl_link_down(ctx, ifname); /* must be down or EBUSY */

  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;
  nl_addattr(&req.n, (int)sizeof(req), IFLA_IFNAME, newname,
             (int)strlen(newname) + 1);
  return ds_nl_talk(ctx, &req.n);
}

/* ---------------------------------------------------------------------------
 * Add an IPv4 address to an interface
 * ip_be and bcast_be are in network byte order.
 * ---------------------------------------------------------------------------*/

int ds_nl_add_addr4(ds_nl_ctx_t *ctx, const char *ifname, uint32_t ip_be,
                    uint8_t prefix) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifaddrmsg ifa;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
  req.n.nlmsg_type = RTM_NEWADDR;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
  req.ifa.ifa_family = AF_INET;
  req.ifa.ifa_prefixlen = prefix;
  req.ifa.ifa_index = (unsigned int)idx;
  req.ifa.ifa_scope = RT_SCOPE_UNIVERSE;

  nl_addattr(&req.n, (int)sizeof(req), IFA_LOCAL, &ip_be, 4);
  nl_addattr(&req.n, (int)sizeof(req), IFA_ADDRESS, &ip_be, 4);

  uint32_t bcast = ip_be | htonl(0xffffffffu >> prefix);
  nl_addattr(&req.n, (int)sizeof(req), IFA_BROADCAST, &bcast, 4);

  return ds_nl_talk(ctx, &req.n);
}

/* ---------------------------------------------------------------------------
 * Add an IPv4 route
 * dst_be=0 + dst_len=0 → default route.
 * gw_be=0               → connected/link-scope route.
 * ---------------------------------------------------------------------------*/

int ds_nl_add_route4(ds_nl_ctx_t *ctx, uint32_t dst_be, uint8_t dst_len,
                     uint32_t gw_be, int oif_idx) {
  struct {
    struct nlmsghdr n;
    struct rtmsg r;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  req.n.nlmsg_type = RTM_NEWROUTE;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
  req.r.rtm_family = AF_INET;
  req.r.rtm_dst_len = dst_len;
  req.r.rtm_table = RT_TABLE_MAIN;
  req.r.rtm_protocol = RTPROT_BOOT;
  req.r.rtm_scope = (gw_be == 0) ? RT_SCOPE_LINK : RT_SCOPE_UNIVERSE;
  req.r.rtm_type = RTN_UNICAST;

  if (dst_len > 0)
    nl_addattr(&req.n, (int)sizeof(req), RTA_DST, &dst_be, 4);
  if (gw_be)
    nl_addattr(&req.n, (int)sizeof(req), RTA_GATEWAY, &gw_be, 4);
  nl_addattr(&req.n, (int)sizeof(req), RTA_OIF, &oif_idx, (int)sizeof(int));

  return ds_nl_talk(ctx, &req.n);
}

/* ---------------------------------------------------------------------------
 * Move an interface into a network namespace (by fd)
 * ---------------------------------------------------------------------------*/

int ds_nl_move_to_netns(ds_nl_ctx_t *ctx, const char *ifname, int netns_fd) {
  int idx = ds_nl_get_ifindex(ctx, ifname);
  if (idx <= 0)
    return -ENODEV;

  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_NEWLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = idx;

  nl_addattr(&req.n, (int)sizeof(req), IFLA_NET_NS_FD, &netns_fd,
             (int)sizeof(int));
  return ds_nl_talk(ctx, &req.n);
}

/* ---------------------------------------------------------------------------
 * Flush stale Droidspaces veth interfaces via RTM_GETLINK dump
 *
 * Collects all matching interfaces first, then deletes in a second pass to
 * avoid corrupting the dump mid-iteration.
 * ---------------------------------------------------------------------------*/

void ds_nl_flush_stale_veths(ds_nl_ctx_t *ctx, const char *prefix) {
  struct {
    struct nlmsghdr n;
    struct ifinfomsg i;
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_type = RTM_GETLINK;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.i.ifi_family = AF_UNSPEC;

  /* Manual send (not ds_nl_talk) — DUMP responses don't have ACK semantics */
  req.n.nlmsg_seq = ++ctx->seq;
  req.n.nlmsg_pid = (uint32_t)ctx->pid;
  if (send(ctx->fd, &req, req.n.nlmsg_len, 0) < 0)
    return;

  char stale[64][IFNAMSIZ];
  int stale_count = 0;
  size_t prefix_len = strlen(prefix);
  uint8_t buf[NL_BUFSIZE];

  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;

    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE)
        goto flush_collected;
      if (h->nlmsg_type != RTM_NEWLINK)
        continue;

      struct ifinfomsg *ifi = NLMSG_DATA(h);
      struct rtattr *rta = IFLA_RTA(ifi);
      int rlen = (int)IFLA_PAYLOAD(h);
      char ifname[IFNAMSIZ] = {0};

      for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
        if (rta->rta_type == IFLA_IFNAME) {
          safe_strncpy(ifname, RTA_DATA(rta), IFNAMSIZ);
          break;
        }
      }
      if (ifname[0] && strncmp(ifname, prefix, prefix_len) == 0 &&
          stale_count < 64) {
        safe_strncpy(stale[stale_count++], ifname, IFNAMSIZ);
      }
    }
  }

flush_collected:
  for (int i = 0; i < stale_count; i++) {
    ds_log("[NET] Flushing stale veth: %s", stale[i]);
    ds_nl_del_link(ctx, stale[i]);
  }
}

/* ---------------------------------------------------------------------------
 * Find the default-route table used for internet connectivity
 *
 * On Android, the internet default route is in a policy table with id > 100
 * (named "wlan0", "rmnet1", etc.) rather than in the main table (254).
 * On desktop Linux, the default route is in RT_TABLE_MAIN (254).
 *
 * We dump all routes and return the first default route in a table > 100
 * that is not "dummy0" (Android placeholder interface).
 * Falls back to RT_TABLE_MAIN if no policy table is found.
 *
 * ifname_out and table_out may be NULL.
 * Returns 0 on success, -ENOENT if no default route found.
 * ---------------------------------------------------------------------------*/

int ds_nl_get_default_gw_table(ds_nl_ctx_t *ctx, char *ifname_out,
                               int *table_out) {
  uint8_t buf[NL_BUFSIZE];

  /* ── Phase 1: dump IPv4 policy rules to build the "active table" set ──────
   *
   * On Android, network switches primarily change POLICY RULES (RTM_NEWRULE /
   * RTM_DELRULE), not the per-table route entries.  Both wlan0 and ccmni1 can
   * simultaneously have a default route, but only one set of policy rules is
   * active.  Picking the first high-numbered table from the route dump is
   * therefore ambiguous — we must cross-reference with the rule set.
   *
   * "Active" rule criteria:
   *   - family  = AF_INET
   *   - action  = FR_ACT_TO_TBL (1)  → maps to r->rtm_type via fib_rule_hdr
   *   - table   > 100
   *   - priority 1000 – 31000  (Android's per-network rule band)
   * -------------------------------------------------------------------------*/

#define MAX_RULE_TABLES 32
  int rule_tables[MAX_RULE_TABLES];
  int rule_count = 0;

  {
    struct {
      struct nlmsghdr n;
      struct rtmsg r;
    } rreq;
    memset(&rreq, 0, sizeof(rreq));
    rreq.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    rreq.n.nlmsg_type = RTM_GETRULE;
    rreq.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    rreq.r.rtm_family = AF_INET;
    rreq.n.nlmsg_seq = ++ctx->seq;
    rreq.n.nlmsg_pid = (uint32_t)ctx->pid;

    if (send(ctx->fd, &rreq, rreq.n.nlmsg_len, 0) >= 0) {
      for (;;) {
        ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
        if (n <= 0)
          break;
        struct nlmsghdr *h = (struct nlmsghdr *)buf;
        for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
          if (h->nlmsg_type == NLMSG_DONE)
            goto rules_done;
          if (h->nlmsg_type != RTM_NEWRULE)
            continue;

          struct rtmsg *r = NLMSG_DATA(h);
          if (r->rtm_family != AF_INET)
            continue;
          /* fib_rule_hdr.action overlaps rtmsg.rtm_type; FR_ACT_TO_TBL = 1 */
          if (r->rtm_type != 1)
            continue;

          int r_table = r->rtm_table;
          int r_prio = 0;

          struct rtattr *rta = RTM_RTA(r);
          int rlen = (int)RTM_PAYLOAD(h);
          for (; RTA_OK(rta, rlen); rta = RTA_NEXT(rta, rlen)) {
            if (rta->rta_type == FRA_TABLE)
              r_table = *(int *)RTA_DATA(rta);
            if (rta->rta_type == FRA_PRIORITY)
              r_prio = *(int *)RTA_DATA(rta);
          }

          if (r_table > 100 && r_prio >= 1000 && r_prio <= 31000) {
            int dup = 0;
            for (int i = 0; i < rule_count; i++)
              if (rule_tables[i] == r_table) {
                dup = 1;
                break;
              }
            if (!dup && rule_count < MAX_RULE_TABLES)
              rule_tables[rule_count++] = r_table;
          }
        }
      }
    }
  }
rules_done:;

  /* ── Phase 2: route dump — cross-reference against the active rule set ── */

  {
    struct {
      struct nlmsghdr n;
      struct rtmsg r;
    } req;
    memset(&req, 0, sizeof(req));
    req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.n.nlmsg_type = RTM_GETROUTE;
    req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.r.rtm_family = AF_INET;
    req.n.nlmsg_seq = ++ctx->seq;
    req.n.nlmsg_pid = (uint32_t)ctx->pid;
    if (send(ctx->fd, &req, req.n.nlmsg_len, 0) < 0)
      return -errno;
  }

  int found = 0; /* 1 = rule-matched route, 2 = fallback route */
  char best_ifname[IFNAMSIZ] = {0};
  int best_table = 0;

  for (;;) {
    ssize_t n = recv(ctx->fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;

    struct nlmsghdr *h = (struct nlmsghdr *)buf;
    for (; NLMSG_OK(h, (uint32_t)n); h = NLMSG_NEXT(h, n)) {
      if (h->nlmsg_type == NLMSG_DONE)
        goto gw_done;
      if (h->nlmsg_type != RTM_NEWROUTE)
        continue;

      struct rtmsg *r = NLMSG_DATA(h);
      if (r->rtm_dst_len != 0)
        continue; /* skip non-default routes */

      int r_table = r->rtm_table;
      char r_if[IFNAMSIZ] = {0};

      struct rtattr *rta = RTM_RTA(r);
      int len = (int)RTM_PAYLOAD(h);
      for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        if (rta->rta_type == RTA_TABLE)
          r_table = *(int *)RTA_DATA(rta);
        if (rta->rta_type == RTA_OIF) {
          int ifidx = *(int *)RTA_DATA(rta);
          if_indextoname((unsigned int)ifidx, r_if);
        }
      }

      if (!r_if[0] || strcmp(r_if, "dummy0") == 0)
        continue;

      if (r_table > 100) {
        /* Check if this table has an active policy rule (Phase-1 result) */
        int rule_active = 0;
        for (int i = 0; i < rule_count; i++)
          if (rule_tables[i] == r_table) {
            rule_active = 1;
            break;
          }

        if (rule_active) {
          /* Best possible match: route table confirmed by policy rule */
          best_table = r_table;
          safe_strncpy(best_ifname, r_if, IFNAMSIZ);
          found = 1;
          goto gw_done;
        } else if (found < 1) {
          /* No rule confirmation yet — keep as fallback */
          best_table = r_table;
          safe_strncpy(best_ifname, r_if, IFNAMSIZ);
          found = 2;
        }
      } else if (found == 0) {
        /* Desktop Linux: main table (254) — lowest priority fallback */
        best_table = r_table;
        safe_strncpy(best_ifname, r_if, IFNAMSIZ);
        found = 2;
      }
    }
  }

gw_done:
#undef MAX_RULE_TABLES
  if (!found)
    return -ENOENT;
  if (table_out)
    *table_out = best_table;
  if (ifname_out)
    safe_strncpy(ifname_out, best_ifname, IFNAMSIZ);
  return 0;
}

/* ---------------------------------------------------------------------------
 * IPv4 policy rule management (RTM_NEWRULE / RTM_DELRULE)
 * ---------------------------------------------------------------------------*/

static int ds_nl_rule_op(ds_nl_ctx_t *ctx, int cmd, uint32_t src_be,
                         uint8_t src_len, uint32_t dst_be, uint8_t dst_len,
                         int table, int priority) {
  struct {
    struct nlmsghdr n;
    struct rtmsg r;
    char buf[256];
  } req;
  memset(&req, 0, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  req.n.nlmsg_type = (uint16_t)cmd;
  req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
  if (cmd == RTM_NEWRULE)
    req.n.nlmsg_flags |= NLM_F_CREATE;

  req.r.rtm_family = AF_INET;
  req.r.rtm_protocol = RTPROT_BOOT;
  req.r.rtm_scope = RT_SCOPE_UNIVERSE;
  req.r.rtm_type = RTN_UNICAST;
  req.r.rtm_src_len = src_len;
  req.r.rtm_dst_len = dst_len;
  req.r.rtm_table =
      (table > 0 && table < 256) ? (uint8_t)table : RT_TABLE_UNSPEC;

  if (src_len > 0)
    nl_addattr(&req.n, (int)sizeof(req), FRA_SRC, &src_be, 4);
  if (dst_len > 0)
    nl_addattr(&req.n, (int)sizeof(req), FRA_DST, &dst_be, 4);
  if (table > 0) {
    uint32_t t = (uint32_t)table;
    nl_addattr(&req.n, (int)sizeof(req), FRA_TABLE, &t, sizeof(uint32_t));
  }
  if (priority >= 0) {
    uint32_t p = (uint32_t)priority;
    nl_addattr(&req.n, (int)sizeof(req), FRA_PRIORITY, &p, sizeof(uint32_t));
  }

  int ret = ds_nl_talk(ctx, &req.n);
  /* Idempotent: EEXIST for NEW and ENOENT for DEL are both success */
  return (ret == -EEXIST || ret == -ENOENT) ? 0 : ret;
}

int ds_nl_add_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority) {
  return ds_nl_rule_op(ctx, RTM_NEWRULE, src_be, src_len, dst_be, dst_len,
                       table, priority);
}

int ds_nl_del_rule4(ds_nl_ctx_t *ctx, uint32_t src_be, uint8_t src_len,
                    uint32_t dst_be, uint8_t dst_len, int table, int priority) {
  return ds_nl_rule_op(ctx, RTM_DELRULE, src_be, src_len, dst_be, dst_len,
                       table, priority);
}
