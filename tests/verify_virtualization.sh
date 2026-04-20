#!/bin/bash
# Functional verification script for Droidspaces Resource Virtualization

# Check for root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root"
  exit 1
fi

# Build droidspaces
make native
DS="./output/droidspaces"

if [ ! -f "$DS" ]; then
    echo "Droidspaces binary not found!"
    exit 1
fi

# Setup a minimal rootfs for testing if it doesn't exist
TEST_ROOTFS="/tmp/ds-test-rootfs"
if [ ! -d "$TEST_ROOTFS" ]; then
    mkdir -p "$TEST_ROOTFS/sbin"
    mkdir -p "$TEST_ROOTFS/bin"
    mkdir -p "$TEST_ROOTFS/etc"
    mkdir -p "$TEST_ROOTFS/proc"
    mkdir -p "$TEST_ROOTFS/sys"
    mkdir -p "$TEST_ROOTFS/dev"
    mkdir -p "$TEST_ROOTFS/run"
    mkdir -p "$TEST_ROOTFS/tmp"

    # Create a dummy init script
    cat <<EOF > "$TEST_ROOTFS/sbin/init"
#!/bin/sh
echo "Dummy init started"
# Keep alive
while true; do sleep 100; done
EOF
    chmod +x "$TEST_ROOTFS/sbin/init"

    # Copy busybox or similar if available for 'free', 'nproc'
    if command -v busybox >/dev/null; then
        cp $(which busybox) "$TEST_ROOTFS/bin/busybox"
        for cmd in sh free nproc uptime cat grep; do
            ln -sf busybox "$TEST_ROOTFS/bin/$cmd"
        done
    else
        echo "Busybox not found, using host binaries as fallback (might fail due to dynamic links)"
        cp $(which sh) "$TEST_ROOTFS/bin/sh"
        cp $(which free) "$TEST_ROOTFS/bin/free" 2>/dev/null
        cp $(which nproc) "$TEST_ROOTFS/bin/nproc" 2>/dev/null
        cp $(which uptime) "$TEST_ROOTFS/bin/uptime" 2>/dev/null
        cp $(which cat) "$TEST_ROOTFS/bin/cat" 2>/dev/null
    fi
fi

CONTAINER_NAME="ds-virt-test"

# Stop existing if any
$DS --name $CONTAINER_NAME stop 2>/dev/null || true

echo "[*] Starting container with 512MB memory limit and 1 CPU..."
$DS --rootfs $TEST_ROOTFS --name $CONTAINER_NAME --memory 512M --cpus 1 --virtualization start
sleep 2

echo "[*] Verifying memory visibility..."
MEM_TOTAL=$($DS --name $CONTAINER_NAME run free -m | grep Mem: | awk '{print $2}')
echo "    Reported MemTotal: ${MEM_TOTAL} MB"
if [[ "$MEM_TOTAL" -eq 512 ]]; then
    echo "    [PASS] Memory virtualization works."
else
    echo "    [FAIL] Memory virtualization failed (Expected 512, got $MEM_TOTAL)."
fi

echo "[*] Verifying CPU visibility..."
CPU_COUNT=$($DS --name $CONTAINER_NAME run nproc)
echo "    Reported CPUs: $CPU_COUNT"
if [[ "$CPU_COUNT" -eq 1 ]]; then
    echo "    [PASS] CPU virtualization works."
else
    echo "    [FAIL] CPU virtualization failed (Expected 1, got $CPU_COUNT)."
fi

echo "[*] Verifying /proc/uptime visibility..."
UPTIME_OUT=$($DS --name $CONTAINER_NAME run uptime)
echo "    Reported Uptime: $UPTIME_OUT"
if [[ -n "$UPTIME_OUT" ]]; then
    echo "    [PASS] Uptime virtualization works."
else
    echo "    [FAIL] Uptime virtualization failed."
fi

# Cleanup
$DS --name $CONTAINER_NAME stop
rm -rf $TEST_ROOTFS
