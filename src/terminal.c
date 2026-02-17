/*
 * Droidspaces v3 — Terminal and PTY handling (LXC-inspired)
 */

#include "droidspace.h"
#include <sys/uio.h>

/* ---------------------------------------------------------------------------
 * PTY Allocation
 * ---------------------------------------------------------------------------*/

int ds_terminal_create(struct ds_tty_info *tty) {
  /* openpty() allocates a master/slave pair.
   * slave name is returned in tty->name. */
  if (openpty(&tty->master, &tty->slave, tty->name, NULL, NULL) < 0) {
    ds_error("openpty failed: %s", strerror(errno));
    return -1;
  }

  /* Set FD_CLOEXEC so they don't leak to the container's init */
  fcntl(tty->master, F_SETFD, FD_CLOEXEC);
  fcntl(tty->slave, F_SETFD, FD_CLOEXEC);

  return 0;
}

/* ---------------------------------------------------------------------------
 * Terminal Setup (Inside Container)
 * ---------------------------------------------------------------------------*/

int ds_terminal_setup_console(struct ds_tty_info *console) {
  /* Bind-mount the PTY slave to /dev/console */
  const char *target = "/dev/console";

  /* Ensure target exists (created by mknod in mount.c) */
  if (mount(console->name, target, NULL, MS_BIND, NULL) < 0) {
    ds_error("Failed to bind mount %s to %s: %s", console->name, target,
             strerror(errno));
    return -1;
  }

  return 0;
}

int ds_terminal_setup_ttys(struct ds_tty_info *ttys, int count) {
  char target[32];
  for (int i = 0; i < count; i++) {
    snprintf(target, sizeof(target), "/dev/tty%d", i + 1);
    if (mount(ttys[i].name, target, NULL, MS_BIND, NULL) < 0) {
      ds_warn("Failed to bind mount %s to %s: %s", ttys[i].name, target,
              strerror(errno));
    }
  }
  return 0;
}

int ds_terminal_set_stdfds(int fd) {
  if (dup2(fd, STDIN_FILENO) < 0)
    return -1;
  if (dup2(fd, STDOUT_FILENO) < 0)
    return -1;
  if (dup2(fd, STDERR_FILENO) < 0)
    return -1;
  return 0;
}

int ds_terminal_make_controlling(int fd) {
  /* Drop existing controlling terminal and session */
  setsid();

  /* Make fd the new controlling terminal */
  if (ioctl(fd, TIOCSCTTY, (char *)NULL) < 0) {
    ds_error("TIOCSCTTY failed: %s", strerror(errno));
    return -1;
  }

  return 0;
}

/* ---------------------------------------------------------------------------
 * Termios / TIOS
 * ---------------------------------------------------------------------------*/

int ds_setup_tios(int fd, struct termios *old) {
  struct termios new_tios;

  if (!isatty(fd))
    return -1;

  if (tcgetattr(fd, old) < 0)
    return -1;

  /* Ignore signals during transition */
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);

  new_tios = *old;

  /* Raw mode - mirroring LXC/SSH settings for best compatibility */
  new_tios.c_iflag |= IGNPAR;
  new_tios.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
#ifdef IUCLC
  new_tios.c_iflag &= ~IUCLC;
#endif
  new_tios.c_lflag &= ~(TOSTOP | ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL);
#ifdef IEXTEN
  new_tios.c_lflag &= ~IEXTEN;
#endif
  new_tios.c_oflag &= ~ONLCR;
  new_tios.c_oflag |= OPOST;
  new_tios.c_cc[VMIN] = 1;
  new_tios.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSAFLUSH, &new_tios) < 0)
    return -1;

  return 0;
}

/* ---------------------------------------------------------------------------
 * Runtime Utilities
 * ---------------------------------------------------------------------------*/

void build_container_ttys_string(struct ds_tty_info *ttys, int count, char *buf,
                                 size_t size) {
  buf[0] = '\0';
  for (int i = 0; i < count; i++) {
    if (i > 0)
      strncat(buf, " ", size - strlen(buf) - 1);
    strncat(buf, ttys[i].name, size - strlen(buf) - 1);
  }
}
