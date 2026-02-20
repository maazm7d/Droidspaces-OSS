/*
 * Droidspaces v4 — High-performance Container Runtime
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "droidspace.h"

/* ---------------------------------------------------------------------------
 * Internal Helpers
 * ---------------------------------------------------------------------------*/

static void set_container_defaults(const char *term) {
  setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
         1);
  setenv("TERM", term ? term : "xterm-256color", 1);
  setenv("HOME", "/root", 1);
  setenv("container", "droidspaces", 1);
}

/* ---------------------------------------------------------------------------
 * Shared helpers for environment setup (used by enter, run, boot)
 * ---------------------------------------------------------------------------*/

void setup_container_env(void) {
  /* Capture TERM before clearenv() */
  const char *saved_term = getenv("TERM");
  char term_buf[64] = "xterm-256color";
  if (saved_term)
    safe_strncpy(term_buf, saved_term, sizeof(term_buf));

  clearenv();
  set_container_defaults(term_buf);
}

void load_etc_environment(void) {
  FILE *envf = fopen("/etc/environment", "r");
  if (!envf)
    return;

  char line[512];
  while (fgets(line, sizeof(line), envf)) {
    /* Strip newline */
    char *nl = strchr(line, '\n');
    if (nl)
      *nl = '\0';
    /* Skip comments and empty lines */
    if (line[0] == '#' || line[0] == '\0')
      continue;
    /* Parse KEY=VALUE */
    char *eq = strchr(line, '=');
    if (eq) {
      *eq = '\0';
      char *val = eq + 1;
      /* Strip quotes */
      size_t vlen = strlen(val);
      if (vlen >= 2 && ((val[0] == '"' && val[vlen - 1] == '"') ||
                        (val[0] == '\'' && val[vlen - 1] == '\''))) {
        val[vlen - 1] = '\0';
        val++;
      }
      setenv(line, val, 1);
    }
  }
  fclose(envf);
}

void ds_env_boot_setup(struct ds_config *cfg) {
  /* Capture TERM before clearenv() */
  const char *saved_term = getenv("TERM");
  char term_buf[64] = "xterm-256color";
  if (saved_term)
    safe_strncpy(term_buf, saved_term, sizeof(term_buf));

  clearenv();
  set_container_defaults(term_buf);

  /* Set container_ttys for systemd/openrc if ttys were allocated */
  if (cfg->tty_count > 0) {
    char ttys_str[256];
    build_container_ttys_string(cfg->ttys, cfg->tty_count, ttys_str,
                                sizeof(ttys_str));
    setenv("container_ttys", ttys_str, 1);
  }

  /* Standard Linux LANG default */
  setenv("LANG", "en_US.UTF-8", 0);
}
