# Droidspaces — Internal Architecture & Implementation Reference

---

## Abstract

Droidspaces is a lightweight, zero-virtualization container runtime designed to run full Linux distributions (Ubuntu, Alpine, etc.) with systemd or openrc as PID 1, natively on Android devices. It achieves process isolation through Linux PID, IPC, MNT, and UTS namespaces — the same kernel primitives used by Docker and LXC — but targets the constrained and idiosyncratic Android kernel environment where many standard container tools refuse to operate.

This document is a complete internal architecture reference, written by a kernel engineer who has fully internalized the Droidspaces codebase. Every struct, every syscall, every mount, and every design decision is documented here with the intent that a future implementer could rewrite this project from scratch without ever reading the original source. Where the implementation is elegant, I say so. Where it is broken or fragile, I say so with equal honesty.

The codebase is roughly **4,700 lines of C** across 16 `.c` files and 3 headers, compiled as a single static binary against musl libc.

---

## 1. Project Overview

### 1.1 What It Does

Droidspaces takes a Linux rootfs directory (or ext4 image) and boots it inside a set of Linux namespaces as if it were a tiny virtual machine — except there is no hypervisor, no emulated hardware, and no performance penalty. The host kernel is shared. The container gets:

- Its own PID tree (PID 1 = `/sbin/init`)
- Its own mount table (the rootfs becomes `/`)
- Its own hostname (UTS namespace)
- Its own IPC resources (semaphores, shared memory)
- Full networking via the host kernel's network stack (no NET namespace)

### 1.2 What It Does NOT Do

- **No user namespace.** The container runs as root from the host kernel's perspective. This is deliberate — Android kernels often lack user namespace support, and Droidspaces requires root anyway.
- **No network namespace.** The container shares the host's network stack. This simplifies setup enormously but means no per-container firewall rules via network namespaces.
- **No cgroup isolation.** The code creates some cgroup mount points for systemd compatibility, but does not actually constrain CPU, memory, or I/O.

### 1.3 Source Structure

```
src/
├── droidspace.h        Main header — all structs, globals, prototypes
├── parallel.h          Thread worker context structs
├── thread_pool.h       Thread pool API
├── main.c              CLI parsing, command dispatch
├── container.c         start/stop/enter/run/info/show commands
├── boot.c              internal_boot() — the PID 1 boot sequence
├── mount.c             Mount helpers, /dev setup, rootfs.img handling
├── console.c           epoll-based console I/O monitor loop
├── terminal.c          PTY allocation, /dev/console + /dev/ttyN setup
├── network.c           DNS, routing, hostname, IPv6 configuration
├── android.c           Android-specific: SELinux, optimizations, storage
├── utils.c             File I/O, UUID generation, firmware path mgmt
├── pid.c               PID file management, workspace, container naming
├── container_env.c     Environment variable setup for container
├── fd_passing.c        SCM_RIGHTS FD passing over Unix sockets
├── parallel.c          Worker functions for parallel scanning/checking
├── thread_pool.c       pthread-based thread pool implementation
├── check.c             System requirements checker
└── documentation.c     Interactive docs viewer
```

---

## 2. System Requirements & Assumptions

Droidspaces performs a formal requirements check via the `check` command (implemented in `check.c`). Here's what actually matters:

### Must-Have (Hard Requirements)

| Requirement | How It's Checked | Why |
|---|---|---|
| Root privileges | `geteuid() == 0` | Namespace creation and mount operations require CAP_SYS_ADMIN |
| PID namespace | `unshare(CLONE_NEWPID)` succeeds | Container PID isolation |
| Mount namespace | `unshare(CLONE_NEWNS)` succeeds | Filesystem isolation |
| UTS namespace | `unshare(CLONE_NEWUTS)` succeeds | Hostname isolation |
| IPC namespace | `unshare(CLONE_NEWIPC)` succeeds | IPC isolation |
| devtmpfs | grep `/proc/filesystems` | Device node creation |
| cgroup devices | grep `/proc/cgroups` | systemd requires this |
| `pivot_root` syscall | `syscall(SYS_pivot_root, "/", "/")` returns `EINVAL` not `ENOSYS` | Root filesystem switching |
| `/proc` and `/sys` | `access()` check | Essential virtual filesystems |

### Recommended

epoll, signalfd, PTY/devpts support, loop device, ext4 — all tested in parallel via thread pool.

### Assumptions

- The binary is compiled statically against musl libc (no glibc dependency)
- The host is Android (detected via `/system/build.prop` or `ANDROID_ROOT` env var)
- The rootfs contains `/sbin/init` (systemd or openrc)
- The rootfs contains `/etc/os-release` for auto-naming

---

## 3. Container Lifecycle Overview

Here's the high-level flow from `start` to `stop`, distilled to its essence:

```
                          ┌─────────────────────┐
                          │   droidspaces start  │
                          │     (CLI parsing)    │
                          └──────────┬──────────┘
                                     │
                          ┌──────────▼──────────┐
                          │  check_requirements  │
                          │  android_optimizations│
                          │  generate UUID       │
                          │  create socketpair   │
                          └──────────┬──────────┘
                                     │
                              fork() │
                    ┌────────────────┼────────────────┐
                    │ PARENT         │ INTERMEDIATE   │
                    │                │ CHILD          │
                    │                │                │
                    │                ▼                │
                    │     setsid()                    │
                    │     unshare(PID|NS|UTS|IPC)     │
                    │                │                │
                    │           fork()│                │
                    │         ┌──────┼──────┐         │
                    │         │      │ PID 1│         │
                    │         │      │      │         │
                    │         │      ▼      │         │
                    │         │ internal_boot()       │
                    │         │   │ mount(/ PRIVATE)   │
                    │         │   │ bind mount rootfs  │
                    │         │   │ setup_dev()        │
                    │         │   │ mount proc,sys,run │
                    │         │   │ write UUID marker  │
                    │         │   │ fix_networking     │
                    │         │   │ setup_cgroups      │
                    │         │   │ pivot_root(".",    │
                    │         │   │   ".old_root")     │
                    │         │   │ chdir("/")         │
                    │         │   │ setup_devpts       │
                    │         │   │ allocate_console   │
                    │         │   │ allocate_ttys      │
                    │         │   │ send master FDs    │
                    │         │   │   via socketpair   │
                    │         │   │ fix_networking_    │
                    │         │   │   rootfs           │
                    │         │   │ umount .old_root   │
                    │         │   │ redirect stdio     │
                    │         │   │   to /dev/console  │
                    │         │   │ execve(/sbin/init) │
                    │         │   ▼                    │
                    │         │ waitpid(init)          │
                    │         │ (proxy exit)           │
                    │         └───────────────┘        │
                    │                                  │
                    ▼                                  │
          recv master FDs via socketpair               │
          find_and_save_pid() via UUID                 │
                    │                                  │
            ┌───────┴────────┐                         │
            │ FOREGROUND?    │                         │
            ├────YES─────┐   │                         │
            │            ▼   │                         │
            │  console_monitor_loop()                  │
            │  (epoll stdin↔pty_master)                │
            │                │                         │
            ├────NO──────┐   │                         │
            │            ▼   │                         │
            │  fork() ds-monitor                       │
            │  (holds FDs, waits for PID death)        │
            │  parent exits with info                  │
            └────────────────┘                         │
```

**Key insight:** The intermediate process exists solely to create the PID namespace (since `unshare(CLONE_NEWPID)` affects children, not the caller) and to proxy the exit status. The *second* `fork()` after `unshare()` creates the actual PID 1 of the new namespace.

---

## 4. Start: pivot_root, Namespace Creation, and Mount Setup

### 4.1 Namespace Creation

The namespace creation happens in `start_rootfs()` (`container.c`, line 309):

```c
setsid();
if (unshare(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID) < 0) {
    error("unshare failed: %s", strerror(errno));
    exit(1);
}
```

**Flags used:** `CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID`

**What is NOT used and why:**
- `CLONE_NEWNET` — The container shares the host network. This is a deliberate design choice for simplicity on Android.
- `CLONE_NEWUSER` — Not used because Android kernels often lack support, and the tool requires root anyway.

The `setsid()` call before `unshare()` is important: it creates a new session, which disconnects the intermediate process from the parent's controlling terminal. This is necessary because the child will later set up its own controlling terminal via `TIOCSCTTY`.

### 4.2 The Double-Fork Pattern

After `unshare()`, the code does a second `fork()`:

```c
pid_t init_pid = fork();
if (init_pid == 0) {
    /* Child becomes PID 1 in the new PID namespace */
    exit(internal_boot());
}
/* Intermediate process waits for init and proxies exit status */
waitpid(init_pid, &status, 0);
_exit(WIFEXITED(status) ? WEXITSTATUS(status) : 1);
```

This is the classic Linux container double-fork: `unshare(CLONE_NEWPID)` only affects *new children*, not the calling process. The intermediate process itself still lives in the host PID namespace. Only the child born after `unshare()` gets PID 1 in the new namespace.

### 4.3 The internal_boot() Sequence

This is the critical function — it runs as PID 1 inside the new namespace. Here is the exact order of operations:

**Step 1 — Make root mount private:**
```c
mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
```
This prevents mount events from propagating back to the host. Without this, every mount inside the container would be visible to Android.

**Step 2 — Bind mount rootfs to itself (directory mode only):**
```c
// Only for directory-based rootfs, not rootfs.img
mount(g_rootfs_path, g_rootfs_path, NULL, MS_BIND, NULL);
```
This is required by `pivot_root(2)` — the new root must be a mount point. For rootfs.img, the ext4 image is already loop-mounted, so it's already a mount point.

**Step 3 — chdir to rootfs:**
```c
chdir(g_rootfs_path);
```

**Step 4 — Read UUID from `.droidspaces-uuid`:**
```c
read_file(".droidspaces-uuid", container_uuid, sizeof(container_uuid));
unlink(".droidspaces-uuid");
```
The UUID was written by the parent process before `fork()`. It's read here and cleaned up immediately.

**Step 5 — Create `.old_root` directory:**
```c
mkdir(".old_root", 0755);
```

**Step 6 — Setup /dev:**
```c
setup_dev(g_rootfs_path);     // Mount tmpfs or devtmpfs at <rootfs>/dev
create_devices(g_rootfs_path); // Create device nodes via mknod()
```

**Step 7 — Mount /proc:**
```c
domount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
```

**Step 8 — Mount /sys:**
- Without `--hw-access`: sysfs is mounted RW initially, then a separate sysfs instance is mounted at `sys/devices/virtual/net` for networking, and finally the parent `/sys` is remounted read-only via `mount(NULL, "sys", NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL)`.
- With `--hw-access`: sysfs is simply mounted RW.

**Step 9 — Mount /run as tmpfs:**
```c
domount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755");
```

**Step 10 — Create UUID marker file:**
```c
write_file("run/<uuid>", "");
```
This is the file the parent will scan for in `/proc` to discover the container's PID. More on this in Section 5.

**Step 11 — Network configuration (host side):**
```c
fix_networking_host();
```
DNS resolution, routing, iptables on Android.

**Step 12 — Setup cgroups:**
```c
setup_cgroups();
```
Creates `sys/fs/cgroup` as tmpfs with `devices` and `systemd` subdirectories.

**Step 13 — Optional: Android storage bind mount:**
```c
if (g_android_storage) android_setup_storage(g_rootfs_path);
```

**Step 14 — pivot_root:**
```c
syscall(SYS_pivot_root, ".", ".old_root");
chdir("/");
```

Here's what this does: the current directory (`.`, which is the rootfs path) becomes the new root filesystem. The old root (`/` from Android's perspective) is moved to `.old_root` under the new root. After `chdir("/")`, we're now standing inside the container's filesystem.

**Step 15 — Setup devpts:**
```c
setup_devpts(g_rootfs_path);
```
This MUST happen after `pivot_root` because devpts `newinstance` needs to be mounted inside the new root. The mount is at `/dev/pts` (absolute, inside the container).

**Step 16 — Allocate console and TTYs:**
```c
allocate_console(&console);
setup_console(g_rootfs_path, &console);
allocate_ttys(g_ttys, 4);
setup_ttys(g_rootfs_path, g_ttys, 4);
```

**Step 17 — Send master FDs to parent via Unix socket:**
```c
int masters[5] = { console.master, g_ttys[0].master, ..., g_ttys[3].master };
send_fds(sock_fd, masters, 5);
close(console.master);
for (int i = 0; i < 4; i++) close(g_ttys[i].master);
```

This is the FD passing mechanism: the master side of each PTY is sent to the parent process via `SCM_RIGHTS` over the socketpair created before `fork()`. After sending, the master FDs are closed in the container — only the parent needs them.

**Step 18 — Network configuration (rootfs side):**
```c
fix_networking_rootfs();
```
Hostname, DNS resolv.conf, Android network groups.

**Step 19 — Write container marker:**
```c
mkdir("/run/systemd", 0755);
write_file("/run/systemd/container", "droidspaces\n");
```
This file is how systemd detects it's running in a container, and how Droidspaces validates a PID belongs to one of its containers (`is_valid_container_pid()` checks this file).

**Step 20 — Setup environment:**
```c
setup_container_env(container_ttys_value);
```
Calls `clearenv()` then sets:
- `PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin`
- `TERM=xterm`
- `HOME=/root`
- `container=droidspaces`
- `container_ttys=/dev/tty1 /dev/tty2 /dev/tty3 /dev/tty4` (tells systemd which TTYs are available)

**Step 21 — Unmount old root:**
```c
umount2("/.old_root", MNT_DETACH);
rmdir("/.old_root");
```

**Step 22 — Redirect stdio to console:**
```c
int console_fd = open("/dev/console", O_RDWR);
dup2(console_fd, STDIN_FILENO);
dup2(console_fd, STDOUT_FILENO);
dup2(console_fd, STDERR_FILENO);
setsid();
ioctl(STDIN_FILENO, TIOCSCTTY, 0);
```

**Step 23 — Exec init:**
```c
execve("/sbin/init", argv, environ);
```

This is it. After `execve`, the container is running. PID 1 is now systemd (or openrc), its stdio is connected to the console PTY, and the parent process holds the master side of that PTY.

---

## 5. UUID-Based PID Discovery and Multi-Container Support

### 5.1 The Problem

After `fork() + unshare() + fork()`, the parent process needs to know the _global_ PID of the container's init process. But the parent doesn't directly get this PID — the intermediate process does the second fork, and the grandchild's PID in the host namespace isn't passed back.

### 5.2 The UUID Marker Trick

Here's the elegant solution Droidspaces uses:

1. **Before fork:** Parent generates a 32-character hex UUID and writes it to `<rootfs>/.droidspaces-uuid`
2. **Inside container (Step 10):** After mounting `/run` as tmpfs, `internal_boot()` creates an empty marker file at `/run/<uuid>`
3. **Parent scans `/proc`:** After the container starts, the parent iterates over every PID in `/proc` and checks if `/proc/<pid>/root/run/<uuid>` exists

```c
// Pseudocode for the UUID scan:
for each pid in /proc:
    path = "/proc/<pid>/root/run/<uuid>"
    if access(path, F_OK) == 0:
        found_pid = pid   // This PID's mount namespace contains our marker
        break
```

### 5.3 Parallel Scanning

The scan is parallelized using a thread pool. `collect_pids()` reads all numeric entries from `/proc`, then distributes them across `sysconf(_SC_NPROCESSORS_ONLN)` threads (capped at 8). Each thread scans its assigned PID range using `uuid_scan_worker()`.

### 5.4 Retry Logic

The UUID scan is retried up to `UUID_RETRY_COUNT` (6) times with `UUID_RETRY_DELAY_MS` (500ms) between attempts. If that fails, an additional 10 retries at 100ms intervals are attempted.

### 5.5 Why This Works for Multi-Container

Each container gets a unique UUID. The marker file exists only inside that specific container's mount namespace (in its `/run` tmpfs). The `/proc/<pid>/root` path traverses into the container's root filesystem from the host. So two containers with different rootfs paths will have different UUIDs, and the scan will find the correct PID for each.

### 5.6 Cleanup

After the PID is discovered and saved, the marker file is deleted:
```c
// delete_uuid_marker() in container.c
unlink("/proc/<pid>/root/run/<uuid>");
```

### 5.7 PID Persistence

The discovered PID is written to a pidfile at `<workspace>/Pids/<name>.pid`:
- Android: `/data/local/Droidspaces/Pids/ubuntu-24.04.pid`
- Linux: `/var/lib/Droidspaces/Pids/ubuntu-24.04.pid`

Container names are auto-generated from `/etc/os-release` (`ID-VERSION_ID`, e.g., `ubuntu-24.04`). Duplicate names get a numeric suffix (`ubuntu-24.04-1`, `ubuntu-24.04-2`, etc.).

---

## 6. Init Execution and Terminal I/O Forwarding

### 6.1 The exec Call

```c
const char *init_path = "/sbin/init";
char *argv[] = { (char *)init_path, NULL };
extern char **environ;
execve(init_path, argv, environ);
```

No arguments are passed to init. The environment is clean (set by `setup_container_env()`). The `container=droidspaces` environment variable tells systemd it's in a container.

### 6.2 Foreground Mode

When `--foreground` is specified:

1. The parent process puts the terminal into raw mode:
   ```c
   tios.c_iflag |= IGNPAR;
   tios.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
   tios.c_lflag &= ~(TOSTOP | ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL);
   tios.c_oflag &= ~ONLCR;
   tios.c_oflag |= OPOST;
   ```

2. Then it enters `console_monitor_loop()`, which uses epoll to shuttle data between:
   - `user_stdin` (fd 0) → `pty_master` (container input)
   - `pty_master` (container output) → `user_stdout` (fd 1)

3. The epoll loop also monitors a `signalfd` for:
   - `SIGCHLD` — detects when the intermediate process exits (container shutdown or reboot)
   - `SIGINT` / `SIGTERM` — forwarded to the container's init PID
   - `SIGWINCH` — terminal resize is forwarded via `ioctl(TIOCSWINSZ)`

4. **Reboot detection:** When the kernel receives a `reboot()` syscall from PID 1 inside the namespace, it sends `SIGINT` to the namespace creator (the intermediate process). The monitor loop detects this and performs cleanup.

### 6.3 Background Mode

When foreground is not requested:

1. The parent forks a "monitor" daemon (`[ds-monitor]`):
   ```c
   if (fork() == 0) {
       setsid();
       prctl(PR_SET_NAME, "[ds-monitor]", ...);
       // redirect stdio to /dev/null
       while (kill(container_pid, 0) == 0) sleep(60);
       // close master FDs when container dies
   }
   ```

2. This daemon's sole purpose is to **hold the PTY master FDs open**. Without this, the PTY slave devices inside the container would become invalid, and getty/systemd would fail to use them.

3. The original parent prints status info and exits.

### 6.4 How Getty/Login Works

After systemd boots, it spawns `agetty` on the TTY devices listed in `container_ttys`. The `container_ttys` environment variable tells systemd which `/dev/ttyN` devices are available. `agetty` opens `/dev/ttyN`, which is bind-mounted to a PTY slave. The PTY master is held open by either the foreground console monitor or the background `[ds-monitor]` daemon.

In foreground mode, the user sees the login prompt because `console_monitor_loop()` relays all output from `pty_master` (which is the master side of `/dev/console`'s PTY) to stdout.

---

## 7. TTY/PTY/Console Setup

This is the most complex part of the codebase, and the part most deserving of critique.

### 7.1 Console PTY

After `pivot_root` and `setup_devpts()`, the code allocates a console PTY:

```c
// terminal.c: allocate_console()
openpty(&console->master, &console->slave, NULL, NULL, &ws);
ttyname_r(console->slave, console->name, sizeof(console->name));
// name is something like "/dev/pts/0"
```

The slave end is then bind-mounted to `/dev/console`:

```c
// terminal.c: setup_console()
mknod("/dev/console", S_IFREG | 0000, 0);   // create bind mount target
mount(console->name, "/dev/console", NULL, MS_BIND, NULL);
// e.g., mount("/dev/pts/0", "/dev/console", ...)
```

### 7.2 TTY Devices (tty1-tty4)

Four additional PTY pairs are allocated for virtual terminals:

```c
// terminal.c: allocate_ttys()
for (size_t i = 0; i < 4; i++) {
    openpty(&ttys[i].master, &ttys[i].slave, NULL, NULL, &ws);
    ttyname_r(ttys[i].slave, ttys[i].name, sizeof(ttys[i].name));
}

// terminal.c: setup_ttys()
for (size_t i = 0; i < 4; i++) {
    char tty_path[32];
    snprintf(tty_path, sizeof(tty_path), "/dev/tty%zu", i + 1);
    mknod(tty_path, S_IFREG | 0000, 0);
    mount(ttys[i].name, tty_path, NULL, MS_BIND, NULL);
    // e.g., mount("/dev/pts/1", "/dev/tty1", ...)
}
```

### 7.3 FD Passing

All 5 master FDs (1 console + 4 TTYs) are sent from the container process (PID 1) to the parent via `SCM_RIGHTS` over a Unix socketpair:

```c
// fd_passing.c: send_fds()
struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
cmsg->cmsg_level = SOL_SOCKET;
cmsg->cmsg_type = SCM_RIGHTS;
cmsg->cmsg_len = CMSG_LEN(sizeof(int) * count);
memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * count);
sendmsg(sock, &msg, 0);
```

After sending, the master FDs are closed inside the container. Only the slave FDs remain open, inherited by `execve(/sbin/init)`.

### 7.4 Device Node Persistence

A critical detail: PTY device nodes in `/dev/pts/` are backed by the devpts filesystem. A node like `/dev/pts/0` only exists as long as *at least one file descriptor* referencing it remains open. The slave FDs are intentionally left open (no `FD_CLOEXEC`) so they survive the `execve()` call to init. The master FDs are held by the parent process.

### 7.5 devpts Mount Options

```c
// mount.c: setup_devpts()
mount("devpts", "/dev/pts", "devpts",
      MS_NOSUID | MS_NOEXEC,
      "gid=5,newinstance,ptmxmode=0666,mode=0620");
```

The `newinstance` flag is **critical**. It creates a new devpts instance isolated from the host's `/dev/pts`. Without it, the container would see the host's PTY devices.

After mounting, `/dev/ptmx` is bind-mounted from `/dev/pts/ptmx`:
```c
mount("/dev/pts/ptmx", "/dev/ptmx", NULL, MS_BIND, NULL);
```

### 7.6 /dev Population (non-hw-access mode)

```c
// mount.c: create_devices()
mknod("<rootfs>/dev/null",    S_IFCHR | 0666, makedev(1, 3));
mknod("<rootfs>/dev/zero",    S_IFCHR | 0666, makedev(1, 5));
mknod("<rootfs>/dev/full",    S_IFCHR | 0666, makedev(1, 7));
mknod("<rootfs>/dev/random",  S_IFCHR | 0666, makedev(1, 8));
mknod("<rootfs>/dev/urandom", S_IFCHR | 0666, makedev(1, 9));
mknod("<rootfs>/dev/tty",     S_IFCHR | 0666, makedev(5, 0));
mknod("<rootfs>/dev/console",  S_IFCHR | 0600, makedev(5, 1));
mknod("<rootfs>/dev/net/tun", S_IFCHR | 0666, makedev(10, 200));
mknod("<rootfs>/dev/fuse",    S_IFCHR | 0666, makedev(10, 229));
```

---

### ⚠️ Known Issue: Current TTY/PTY Approach is Broken — LXC Model Needed

The current implementation manually allocates PTY pairs via `openpty()`, bind-mounts the slave end to `/dev/ttyN`, then holds the master FDs open in the parent process (either the foreground console monitor or the `[ds-monitor]` daemon). This is fragile, wasteful, and architecturally wrong.

**What's wrong, specifically:**

1. **Manual FD babysitting.** The code opens 5 PTY pairs (1 console + 4 TTYs) at boot time, sends the 5 master FDs from the container to the parent via SCM_RIGHTS, and then either the foreground monitor or a background daemon holds them open for the lifetime of the container. If that process dies (e.g., the user kills `[ds-monitor]`), the PTY devices inside the container silently become invalid. `agetty` will fail, console I/O stops, and the container becomes headless with no way to recover.

2. **Static TTY count.** Four TTYs are hardcoded. If you want 6, you recompile. If you want 1, you still pay for 4.

3. **The `container_ttys` environment hack.** The code builds a `container_ttys=/dev/tty1 /dev/tty2 /dev/tty3 /dev/tty4` environment variable to tell systemd which TTYs exist. This works, but it's a fragile coupling between the runtime and systemd's container detection logic.

4. **No `O_CLOEXEC` management.** The slave FDs are intentionally left without `FD_CLOEXEC` so they survive `execve()`. While this is necessary to keep the PTY nodes alive, it means init and all its children inherit 9+ file descriptors they never use. This is a file descriptor leak.

**What LXC does differently (and correctly):**

LXC's approach to TTY/console setup is fundamentally different and should be studied for the next implementation:

1. **LXC allocates PTYs in the monitor (parent) process**, not inside the container. The monitor opens PTY pairs, then passes the *slave* FDs into the container via bind mounts to `/dev/console` and `/dev/ttyN`.

2. **The master side stays in the monitor naturally.** No `SCM_RIGHTS` FD passing is needed because the PTYs are opened by the monitor, which already has the master FDs. The child inherits the slave FDs via the bind-mounted device nodes.

3. **LXC's `lxc.tty.max` config** controls how many TTYs are created, configurable per-container, not hardcoded.

4. **LXC properly handles PTY lifecycle.** When getty exits, the PTY slave is closed, but the master remains in the monitor. When getty respawns, it re-opens `/dev/ttyN`, which still works because the bind mount points to a devpts node kept alive by the master in the monitor.

5. **LXC's console setup** uses `lxc_terminal_create()` to allocate PTYs, `lxc_terminal_setup()` to bind-mount them, and `lxc_terminal_mainloop_add()` to integrate them with the event loop. The monitor handles window resize, HUP/reconnect, and clean teardown.

**The correct approach for Droidspaces v3:**

The next implementation must allocate PTYs in the parent/monitor process *before* creating the container, pass the slave names into the container (via environment or a config protocol), and let the container's `internal_boot()` bind-mount the already-existing device nodes. The master FDs naturally live in the parent. No FD passing, no daemon to hold FDs, no fragile `[ds-monitor]` process.

---

## 8. Hardware Access Mode (`--hw-access`)

### 8.1 What Changes

The `--hw-access` flag modifies exactly two things in the container setup:

**1. /dev mount type:**

| Mode | /dev Mount |
|---|---|
| Default | `mount("none", "<rootfs>/dev", "tmpfs", 0, "size=500000,mode=755")` |
| `--hw-access` | `mount("dev", "<rootfs>/dev", "devtmpfs", MS_NOSUID \| MS_NOEXEC, "mode=755")` |

With `devtmpfs`, the kernel auto-populates `/dev` with device nodes for all hardware — block devices, input devices, GPU, USB, etc. The container can talk to real hardware.

**2. /sys access:**

| Mode | /sys Mount |
|---|---|
| Default | sysfs mounted RW, then remounted RO via `MS_REMOUNT \| MS_BIND \| MS_RDONLY`. A separate sysfs instance is mounted RW at `/sys/devices/virtual/net` for networking. |
| `--hw-access` | sysfs mounted RW without restriction |

### 8.2 devtmpfs Conflict Handling

When devtmpfs is mounted, it may contain host device nodes that conflict with the container's needs (e.g., `/dev/console`, `/dev/tty`). The code explicitly removes these conflicts:

```c
const char *conflicts[] = {
    "console", "tty", "full", "null", "zero",
    "random", "urandom", "ptmx", NULL
};
for (int i = 0; conflicts[i]; i++) {
    umount2(path, MNT_DETACH);
    unlink(path);
}
```

Then `create_devices()` recreates them with correct permissions.

### 8.3 Firmware Path Modification

With `--hw-access`, the host's kernel firmware search path is modified to include the container's `/lib/firmware`:

```c
// utils.c: firmware_path_add_rootfs()
// Reads /sys/module/firmware_class/parameters/path
// Appends "<rootfs>/lib/firmware" to the comma-separated list
```

This is reversed on `stop` via `firmware_path_remove_rootfs()`.

### 8.4 Security Implications

**`--hw-access` is a security disaster.** It gives the container:
- Direct access to every block device (can overwrite partitions)
- Access to `/dev/kmem` and `/dev/mem` (can read/write kernel memory)
- Read-write access to sysfs (can trigger kernel actions)
- Access to USB, GPU, and input devices

This is appropriate for development and testing but should never be used on a shared or production system.

### 8.5 Detection at Runtime

HW access mode is detected for the `info` command by checking the container's `/dev` filesystem type:

```c
// mount.c: detect_hw_access_in_container()
get_container_mount_fstype(pid, "/dev", fstype, sizeof(fstype));
return strcmp(fstype, "devtmpfs") == 0;
```

---

## 9. Entering a Running Container

### 9.1 Overview

The `enter` command (`enter_rootfs()` in `container.c`) attaches an interactive shell to a running container by entering its namespaces via `setns(2)`.

### 9.2 PID Resolution

The target PID is read from the pidfile and validated:

```c
check_status(&pid);  // reads pidfile, checks kill(pid, 0), validates is_valid_container_pid()
```

### 9.3 Namespace Entry

Four namespace FDs are opened and entered in order:

```c
// 1. Open namespace FDs
mnt_fd = open("/proc/<pid>/ns/mnt", O_RDONLY);   // Mount namespace
uts_fd = open("/proc/<pid>/ns/uts", O_RDONLY);   // UTS namespace
ipc_fd = open("/proc/<pid>/ns/ipc", O_RDONLY);   // IPC namespace
pid_fd = open("/proc/<pid>/ns/pid", O_RDONLY);   // PID namespace

// 2. Enter namespaces (mount is mandatory, others are best-effort)
syscall(SYS_setns, mnt_fd, CLONE_NEWNS);    // Must succeed
syscall(SYS_setns, uts_fd, CLONE_NEWUTS);    // Warned if fails
syscall(SYS_setns, ipc_fd, CLONE_NEWIPC);    // Warned if fails
syscall(SYS_setns, pid_fd, CLONE_NEWPID);    // Warned if fails
```

**Important:** `setns()` for PID namespace (like `unshare(CLONE_NEWPID)`) only affects children. So the code must fork after `setns()`:

```c
pid_t child = fork();
if (child > 0) {
    waitpid(child, &status, 0);
    return WEXITSTATUS(status);
}
// child is now in the container's PID namespace
```

### 9.4 Shell Discovery

```c
const char *container_shells[] = {
    "/bin/bash",
    "/bin/ash",
    "/bin/sh",
    NULL
};
```

The code iterates through this list, calls `access(shell, X_OK)` for each, and `execve()`s the first one that exists and is executable. For non-root users, it calls `execve("/bin/su", {"su", "-l", user, NULL}, environ)`.

### 9.5 Environment

`setup_container_env(NULL)` is called, which clears the environment and sets `PATH`, `TERM`, `HOME`, and `container`.

---

## 10. Stop, Restart, and Timeout Handling

### 10.1 Stop Sequence

`stop_rootfs()` in `container.c`:

**Step 1 — Firmware path cleanup:**
```c
get_pid_rootfs_path(pid, root_path, sizeof(root_path));
firmware_path_remove_rootfs(root_path);
```

**Step 2 — Graceful shutdown attempt via `poweroff`:**

The code tries to run `poweroff` inside the container namespace by calling `run_in_rootfs(1, {"poweroff", NULL})` in a forked child. It waits up to 1 second for this to complete.

```c
poweroff_pid = fork();
if (poweroff_pid == 0) {
    exit(run_in_rootfs(1, argv));
}
// Wait up to 1 second (10 * 100ms)
for (int i = 0; i < 10; i++) {
    pid_t ret = waitpid(poweroff_pid, NULL, WNOHANG);
    if (ret == poweroff_pid) { poweroff_done = 1; break; }
    usleep(100000);
}
if (!poweroff_done) {
    kill(poweroff_pid, SIGKILL);
    kill(pid, SIGTERM);  // Fall back to SIGTERM to init
}
```

**Commentary:** This is interesting — the code doesn't send `SIGRTMIN+3` (which is what systemd documents as "halt now"), nor does it send `SIGTERM` first. Instead, it runs `poweroff` inside the container, which tells init to shut down gracefully via the normal init flow. If that times out, it falls back to `SIGTERM`.

**Step 3 — Wait for process exit (5 second timeout):**
```c
for (int i = 0; i < 50; i++) {           // 50 * 100ms = 5 seconds
    pid_t ret = waitpid(pid, &status, WNOHANG);
    if (ret == pid) break;                 // Exited
    if (kill(pid, 0) < 0) break;           // Already dead
    usleep(100000);
}
```

**Step 4 — Force kill on timeout:**
```c
if (kill(pid, 0) == 0) {
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}
```

`SIGKILL` to the container's init PID is the nuclear option. It kills PID 1, which causes the kernel to tear down the entire PID namespace and kill all processes within it.

**Step 5 — Cleanup:**
- Remove pidfile from Pids directory
- Unmount rootfs.img if applicable (unless `skip_unmount` for restart)
- Remove `.hw` sidecar file
- Remove pidfile
- Call `android_optimizations(0)` to restore Android settings

### 10.2 Restart

Restart is simply stop + start with a 1-second sleep in between:

```c
stop_rootfs(is_rootfs_img ? 1 : 0);  // skip_unmount for rootfs.img
sleep(1);
return start_rootfs();
```

For rootfs.img, the mount path is preserved across the restart by reading it from the `.mount` sidecar file before stopping, then restoring it after.

### 10.3 Multi-Container Stop

The `--name` flag accepts comma-separated names:
```
droidspaces --name=web,db,cache stop
```

Each name is resolved to a pidfile and stopped individually via `stop_container_by_name()`.

---

## 11. Status Reporting

### 11.1 Status Command

`check_status()` reads the pidfile, gets the PID, checks `kill(pid, 0)`, and validates via `is_valid_container_pid()`:

```c
int check_status(pid_t *pid_out) {
    read_file(g_pidfile, buf, sizeof(buf));
    pid_t pid = atoi(buf);
    if (kill(pid, 0) == 0 && is_valid_container_pid(pid)) {
        *pid_out = pid;
        return 1;  // Running
    }
    // Stale pidfile — clean up
    unlink(g_pidfile);
    return 0;  // Stopped
}
```

### 11.2 Info Command

`show_info()` displays:
- Host info (Android/Linux, architecture)
- Feature flags (detected from the running container, not from CLI flags):
  - SELinux status (read from `/sys/fs/selinux/enforce`)
  - IPv6 (read from `/proc/<pid>/root/proc/sys/net/ipv6/conf/all/disable_ipv6`)
  - Android storage (check `/storage/emulated/0` mount in `/proc/<pid>/mounts`)
  - HW access (check `/dev` fstype in `/proc/<pid>/mounts` — `devtmpfs` = hw-access)
  - Firmware path status
- Container name, PID, OS info (from `/proc/<pid>/root/etc/os-release`)

### 11.3 Show Command

`show_containers()` lists all running containers in a table:

```
┌──────────────┬──────────┐
│ NAME         │ PID      │
├──────────────┼──────────┤
│ ubuntu-24.04 │ 12345    │
├──────────────┼──────────┤
│ alpine-3.19  │ 23456    │
└──────────────┴──────────┘
```

It reads all `.pid` files from the Pids directory, validates each in parallel, and aggregates results.

### 11.4 Scan Command

`scan_containers()` scans *all* PIDs in `/proc` for processes that:
1. Have `/etc/os-release` accessible via `/proc/<pid>/root/etc/os-release`
2. Pass `is_valid_container_pid()` (have `/run/systemd/container` containing "droidspaces")
3. Are not already tracked in the Pids directory
4. Are not child processes of already-tracked containers (same rootfs)

Found containers are automatically registered with auto-generated names.

---

## 12. Build System Notes

### Compiler and Libc

The Makefile compiles with musl libc for maximum portability across Android devices:

```makefile
CFLAGS = -Wall -Wextra -O2 -flto -std=gnu99 -Isrc -no-pie -pthread
LDFLAGS = -static -no-pie -flto -pthread
```

Key flags:
- **`-static`**: Statically linked binary. No shared library dependencies.
- **`-no-pie`**: Non-position-independent executable. Some Android kernels have issues with PIE static binaries.
- **`-flto`**: Link-time optimization for smaller binary size.
- **`-std=gnu99`**: GNU C99 dialect (needed for `_GNU_SOURCE` features).
- **`-pthread`**: Thread support for the thread pool.

### Cross-Compilation

Four architectures are supported:
- `x86_64`: `musl-gcc`
- `x86`: `i686-linux-musl-gcc`
- `aarch64`: `aarch64-linux-musl-gcc`
- `armhf`: `arm-linux-musleabihf-gcc`

Cross-compiler search order: `$MUSL_CROSS`, `~/toolchains/<triple>-cross/bin/`, `PATH`, `/opt/cross/bin/`.

### Output

All binaries go to `output/droidspaces`. An `all-build` target creates architecture-specific binaries (`droidspaces-x86_64`, `droidspaces-aarch64`, etc.). Tarballs can be created for distribution.

---

## 13. Engineering Commentary: What Works, What's Broken, What Should Be Rewritten

### What Works Well

1. **The namespace creation and pivot_root sequence is solid.** The double-fork pattern, the mount propagation setup (`MS_REC | MS_PRIVATE`), and the pivot_root with `.old_root` cleanup — this is textbook correct and matches how LXC does it.

2. **The UUID-based PID discovery is clever.** It solves a real problem (finding the grandchild PID after double-fork through unshare) without relying on fragile mechanisms like parsing `/proc/<pid>/status` for NSPid. The parallel scan is efficient.

3. **The socketpair FD passing is clean.** Using `SCM_RIGHTS` to pass PTY master FDs from child to parent across the fork boundary is the right approach (though as discussed in Section 7, the *direction* of PTY allocation should be reversed).

4. **The sysfs mixed-mode mount (Section 4.3 Step 8) is excellent engineering.** Mounting a separate sysfs instance at `/sys/devices/virtual/net` (read-write) while the parent `/sys` is read-only gives networking tools what they need without exposing hardware to udevd.

5. **The multi-container support via auto-naming from os-release is user-friendly.** No need to specify container names manually.

### What Needs Improvement

1. **The TTY/PTY handling (Section 7.6).** As detailed above, this current approach of allocating PTYs inside the container, sending masters out, and holding them open in a daemon is fragile and should be replaced with the LXC model.

2. **The `[ds-monitor]` daemon.** A zombie-like process that sleeps in a `while(kill(pid, 0) == 0) sleep(60)` loop just to hold FDs open. If this process is killed, the container's TTYs die. This is a direct consequence of the backward PTY allocation direction.

3. **No signal handling before init launches.** Between `pivot_root` and `execve(/sbin/init)`, the process is vulnerable. If the parent sends a signal during this window, the container setup could be left in a half-initialized state.

4. **Global state.** The codebase uses 8+ global variables (`g_rootfs_path`, `g_pidfile`, `g_foreground`, etc.). This makes the code harder to reason about and impossible to run multiple operations in a single process. A `struct container_config` would be cleaner.

5. **Shell calls to `system()` in Android helpers.** Calls like `system("iptables -t filter -F 2>/dev/null")` bypass proper error handling and are susceptible to shell injection (though the arguments are all hardcoded, so this is more of a style issue).

6. **The `run_in_rootfs` function duplicates 80% of `enter_rootfs`.** The namespace entry code is copied nearly verbatim. This should be factored into a shared `enter_namespace()` helper.

7. **No SIGRTMIN+3 for systemd shutdown.** The stop sequence runs `poweroff` as a command inside the container, which works but is slower than sending `SIGRTMIN+3` directly to PID 1 (the documented signal for "halt now" in systemd).

### What's Over-Engineered

1. **Thread pool for requirements checking.** The `check` command parallelizes 14 simple checks (most of which are just `access()` or `grep_file()`) across a thread pool. Each check takes microseconds. The thread creation overhead is larger than the checks themselves. This is a premature optimization that adds complexity for zero measurable benefit.

2. **Thread pool for UUID scanning.** For a typical Android system with < 500 processes, scanning PIDs serially takes < 10ms. Parallelizing this across 8 threads adds complexity with minimal improvement.

---

## 14. Recommended Reimplementation Approach

If you're rewriting Droidspaces from scratch, here is the checklist of every decision and every syscall, in order.

### Phase 1: Configuration

1. Parse CLI arguments into a `struct container_config`:
   - `rootfs_path` or `rootfs_img_path`
   - `container_name` (auto-generate from os-release if not provided)
   - `hostname`
   - `foreground`, `hw_access`, `enable_ipv6`, `android_storage`, `selinux_permissive`

2. Validate rootfs path exists and contains `/sbin/init`

3. Resolve pidfile path: `<workspace>/Pids/<name>.pid`

4. Check if already running: `kill(saved_pid, 0)` + validate

5. Run requirements check: namespaces, devtmpfs, pivot_root

### Phase 2: Pre-Fork Setup (in parent)

6. Generate UUID (32 hex chars from `/dev/urandom`)

7. If `rootfs.img`: `e2fsck -f -y <img>`, then `mount -t ext4 -o loop,rw,noatime <img> /mnt/Droidspaces/<N>`

8. Write UUID to `<rootfs>/.droidspaces-uuid`

9. Create socketpair for console FD passing: `socketpair(AF_UNIX, SOCK_STREAM, 0, sock)`

10. **Allocate PTYs in the parent** (LXC model — do NOT allocate inside the container):
    - `openpty()` × (1 console + N TTYs)
    - Record slave names (e.g., `/dev/pts/0`, `/dev/pts/1`, ...)

11. Android optimizations: `max_phantom_processes`, disable `deviceidle`, `remount /data suid`

12. `prctl(PR_SET_CHILD_SUBREAPER, 1)` — adopt orphaned grandchildren

13. `write_file("/proc/sys/net/ipv4/ip_forward", "1")`

### Phase 3: Fork + Unshare + Fork (namespace creation)

14. `fork()` → intermediate child

15. In intermediate: `setsid()`, `unshare(CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID)`

16. `fork()` again → PID 1 child (this is the process that becomes init)

17. Intermediate: `waitpid(pid1_child)` and proxy exit status

### Phase 4: internal_boot() (runs as PID 1)

18. `mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)` — prevent mount propagation

19. Bind mount rootfs to itself (directory mode only): `mount(rootfs, rootfs, NULL, MS_BIND, NULL)`

20. `chdir(rootfs)`

21. Read and delete `.droidspaces-uuid`

22. `mkdir(".old_root", 0755)`

23. Setup `/dev`:
    - Without `--hw-access`: `mount("none", "<rootfs>/dev", "tmpfs", ...)` + `mknod()` for null, zero, full, random, urandom, tty, console, net/tun, fuse
    - With `--hw-access`: `mount("dev", "<rootfs>/dev", "devtmpfs", ...)`, clean conflicting nodes, then recreate with `mknod()`

24. `mount("proc", "proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL)`

25. Mount sysfs:
    - Without `--hw-access`: mount RW → mount separate sysfs at `sys/devices/virtual/net` → remount parent RO
    - With `--hw-access`: mount RW

26. `mount("tmpfs", "run", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "mode=755")`

27. `write_file("run/<uuid>", "")` — UUID marker for PID discovery

28. Configure networking (host side): DNS, routing, iptables

29. Setup cgroups: tmpfs at `sys/fs/cgroup`, then `cgroup` mounts for `devices` and `systemd`

30. Optional: bind mount Android storage

31. **`syscall(SYS_pivot_root, ".", ".old_root")`**

32. `chdir("/")`

33. `mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "gid=5,newinstance,ptmxmode=0666,mode=0620")`

34. Setup `/dev/ptmx`: bind mount `/dev/pts/ptmx` → `/dev/ptmx`

35. **Receive PTY slave names from parent** (or if using LXC model: bind-mount the PTY slaves passed from parent to `/dev/console` and `/dev/ttyN`)

36. Configure networking (rootfs side): hostname, DNS, resolv.conf, Android groups

37. `write_file("/run/systemd/container", "droidspaces\n")`

38. `clearenv()`, set `PATH`, `TERM`, `HOME`, `container`, `container_ttys`

39. `umount2("/.old_root", MNT_DETACH)`, `rmdir("/.old_root")`

40. Redirect stdio:
    ```c
    int fd = open("/dev/console", O_RDWR);
    dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
    setsid();
    ioctl(0, TIOCSCTTY, 0);
    ```

41. `execve("/sbin/init", {"init", NULL}, environ)`

### Phase 5: Parent — Post-Fork

42. Receive any needed information from child (e.g., confirmation of setup)

43. UUID scan: scan `/proc/*/root/run/<uuid>` to find container init PID

44. Save PID to pidfile

45. If foreground: enter console monitor loop (epoll: stdin↔pty_master, signalfd for SIGCHLD/SIGINT/SIGTERM/SIGWINCH)

46. If background: fork daemon to hold master FDs (or better: just hold them in the parent if it stays alive)

### Phase 6: Stop

47. Read PID from pidfile, validate

48. Send `poweroff` command inside container (or better: `kill(pid, SIGRTMIN+3)` for systemd, `kill(pid, SIGTERM)` for openrc)

49. Wait up to 5 seconds with `WNOHANG` polling

50. If still alive: `kill(pid, SIGKILL)`, `waitpid(pid, NULL, 0)`

51. Cleanup: remove pidfile, unmount rootfs.img, restore firmware path, restore Android settings

### Phase 7: Enter

52. Read PID from pidfile
53. Open `/proc/<pid>/ns/{mnt,uts,ipc,pid}`
54. `setns()` for each
55. `fork()` (required after `setns(CLONE_NEWPID)`)
56. `setup_container_env()`
57. `execve()` shell: try `/bin/bash`, `/bin/ash`, `/bin/sh`

---

**End of Document**

*This document was written by analyzing v2.8.0 of the Droidspaces source code — approximately 4,700 lines of C across 19 files. Every syscall, every mount, and every design decision described here was verified against the actual implementation.*
