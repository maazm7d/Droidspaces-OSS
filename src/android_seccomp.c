/*
 * Droidspaces v4 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

/* ---------------------------------------------------------------------------
 * Android System Call Filtering (Seccomp)
 * ---------------------------------------------------------------------------*/

/**
 * android_seccomp_setup() - Apply Seccomp filter for Android compatibility.
 *
 * This function applies a Seccomp BPF filter to intercept and modify the
 * behavior of specific system calls that are known to cause issues on Android.
 *
 * CRITICAL (Kernel 4.14 Deadlock):
 * On legacy kernels (below 5.0), some isolation features can trigger a
 * kernel deadlock in grab_super() when systemd services try to mount /proc.
 *
 * New Logic:
 * 1. reboot(2) is always trapped (for in-container reboot handling).
 * 2. Keyring syscalls are always filtered (Android compatibility).
 * 3. Namespace filtering is only applied for systemd containers on legacy kernels (< 5.0).
 */

/* Portable reboot syscall definition */
#ifndef __NR_reboot
# ifdef SYS_reboot
#  define __NR_reboot SYS_reboot
# else
#  error "reboot syscall number not defined on this architecture"
# endif
#endif

int android_seccomp_setup(int is_systemd) {
  int major = 0, minor = 0;
  get_kernel_version(&major, &minor);   /* ignore error, assume modern */

  ds_log("Applying seccomp filter (reboot trap always active)...");

  /* Namespace flags to filter on legacy kernels (only for systemd) */
  const uint32_t ns_mask = 0x7E020000;

  struct sock_filter filter[] = {
      /* [0] Load architecture */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),

  /* [1] Validate architecture */
#if defined(__aarch64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
#elif defined(__x86_64__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
#elif defined(__arm__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0),
#elif defined(__i386__)
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_I386, 1, 0),
#endif
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

      /* [2] Load syscall number */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),

      /* [3] Trap reboot(2) */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_reboot, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),

      /* [4] Filter Keyring Operations (ENOSYS) */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_keyctl, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_add_key, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_request_key, 0, 1),
      BPF_STMT(BPF_RET | BPF_K,
               SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA)),

      /* [5] Conditional Jump for Namespace Filtering (Systemd only on legacy kernels)
       * If not systemd OR kernel >= 5.0, jump over the next 5 instructions.
       */
      BPF_JUMP(BPF_JMP | BPF_JA,
               (uint16_t)((is_systemd && major < 5) ? 0 : 5), 0, 0),

      /* [6] Filter Sandboxing/Namespaces (EPERM if mask matches) */
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_unshare, 1, 0),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 3),

      /* Flag Check for unshare/clone */
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               offsetof(struct seccomp_data, args[0])),
      BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, ns_mask, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),

      /* [7] Default: Allow */
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };

  struct sock_fprog prog = {
      .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
      .filter = filter,
  };

  if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) < 0) {
    ds_warn("Failed to apply Android Seccomp filter: %s", strerror(errno));
    return -1;
  }

  return 0;
}
