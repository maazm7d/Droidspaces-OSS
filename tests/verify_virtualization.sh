#!/bin/bash
# Functional verification script for Droidspaces Resource Virtualization

# Check for root
if [ "$EUID" -ne 0 ]; then
  echo "Please run as root"
  exit 1
fi

set -e

# Build droidspaces
make debug-hardened
DS="./output/droidspaces-hardened"

if [ ! -f "$DS" ]; then
    echo "Droidspaces binary not found!"
    exit 1
fi

CONTAINER_NAME="ds-virt-test"
TEST_ROOTFS="/tmp/ds-test-rootfs"

cleanup() {
    echo "[*] Cleaning up..."
    $DS --name $CONTAINER_NAME stop 2>/dev/null || true
    rm -rf $TEST_ROOTFS
}
trap cleanup EXIT

# Setup a minimal rootfs for testing if it doesn't exist
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
        for cmd in sh free nproc uptime cat grep awk; do
            ln -sf busybox "$TEST_ROOTFS/bin/$cmd"
        done
    else
        echo "Busybox not found, tests will likely fail"
        exit 1
    fi
fi

# Stop existing if any
$DS --name $CONTAINER_NAME stop 2>/dev/null || true

echo "[*] Starting container with 512MB memory limit and 1 CPU..."
$DS --rootfs $TEST_ROOTFS --name $CONTAINER_NAME --memory 512M --cpus 1 --virtualization start
echo "[*] Waiting for container to boot..."

# Polling for boot
for i in {1..20}; do
    if $DS --name $CONTAINER_NAME status | grep -q "Running"; then
        echo "[+] Container is running."
        break
    fi
    sleep 0.5
    if [ $i -eq 20 ]; then
        echo "[-] Container failed to start"
        exit 1
    fi
done

echo "[*] Verifying memory visibility..."
# Use grep/awk from within the container
MEM_TOTAL=$($DS --name $CONTAINER_NAME run free -m | grep Mem: | awk '{print $2}')
echo "    Reported MemTotal: ${MEM_TOTAL} MB"
if [[ "$MEM_TOTAL" -ge 500 && "$MEM_TOTAL" -le 520 ]]; then
    echo "    [PASS] Memory virtualization works."
else
    echo "    [FAIL] Memory virtualization failed (Expected ~512, got $MEM_TOTAL)."
    exit 1
fi

echo "[*] Verifying CPU visibility..."
CPU_COUNT=$($DS --name $CONTAINER_NAME run nproc)
echo "    Reported CPUs: $CPU_COUNT"
if [[ "$CPU_COUNT" -eq 1 ]]; then
    echo "    [PASS] CPU virtualization works."
else
    echo "    [FAIL] CPU virtualization failed (Expected 1, got $CPU_COUNT)."
    exit 1
fi

echo "[*] Verifying /proc/uptime visibility..."
UPTIME_OUT=$($DS --name $CONTAINER_NAME run uptime)
echo "    Reported Uptime: $UPTIME_OUT"
if [[ -n "$UPTIME_OUT" ]]; then
    echo "    [PASS] Uptime virtualization works."
else
    echo "    [FAIL] Uptime virtualization failed."
    exit 1
fi

echo "[*] Verifying aggregate CPU line in /proc/stat..."
$DS --name $CONTAINER_NAME run cat /proc/stat | head -n 1
echo "[+] Verification successful!"
