#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OS_IMAGE="$ROOT_DIR/os-image.bin"
FS_IMAGE="$ROOT_DIR/fs.img"
TMP_OS="/tmp/minerva-os-smoke.bin"
TMP_FS="/tmp/minerva-fs-smoke.img"
LOG_FILE=$(mktemp /tmp/minerva-smoke.XXXXXX)
TIMEOUT_SECONDS=${MINERVA_SMOKE_TIMEOUT:-8}
QEMU_BIN=${QEMU:-qemu-system-i386}
QEMU_PATH=${QEMU_PATH:-/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin}

cleanup() {
    rm -f "$LOG_FILE"
}
trap cleanup EXIT

cp "$OS_IMAGE" "$TMP_OS"
cp "$FS_IMAGE" "$TMP_FS"

set +e
timeout "${TIMEOUT_SECONDS}s" \
    env -i HOME="${HOME:-/tmp}" USER="${USER:-user}" PATH="$QEMU_PATH" \
    "$QEMU_BIN" \
    -drive file="$TMP_OS",format=raw,if=floppy \
    -drive file="$TMP_FS",format=raw,if=ide,index=0 \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -boot a \
    -no-reboot \
    -no-shutdown \
    -display none \
    -serial stdio >"$LOG_FILE" 2>&1
status=$?
set -e

cat "$LOG_FILE"

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    echo "smoke: qemu exited with status $status" >&2
    exit "$status"
fi

for line in \
    "MinervaOS booting..." \
    "e1000 Ethernet device found" \
    "e1000 MAC address read" \
    "e1000 RX ring ready" \
    "e1000 TX ring ready" \
    "FAT32 filesystem mounted"
do
    if ! grep -q "$line" "$LOG_FILE"; then
        echo "smoke: missing serial line: $line" >&2
        exit 1
    fi
done

echo "smoke: passed"
