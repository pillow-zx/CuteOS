#!/usr/bin/env bash
set -u

if [ "$#" -ne 5 ]; then
	echo "usage: $0 <qemu> <kernel> <image> <mem-mb> <cpus>" >&2
	exit 2
fi

qemu=$1
kernel=$2
image=$3
mem_mb=$4
cpus=$5
timeout_s=${UTEST_TIMEOUT:-180}
log=${UTEST_LOG:-}
run_image=

if [ -z "$log" ]; then
	log=$(mktemp "${TMPDIR:-/tmp}/cuteos-utest.XXXXXX.log")
fi

if ! command -v timeout >/dev/null 2>&1; then
	echo "ERROR: timeout command not found" >&2
	exit 2
fi

run_image=$(mktemp "${TMPDIR:-/tmp}/cuteos-utest-img.XXXXXX")
cleanup()
{
	rm -f "$run_image"
}
trap cleanup EXIT
if ! cp "$image" "$run_image"; then
	echo "ERROR: failed to copy user-test image: $image" >&2
	echo "log: $log" >&2
	exit 2
fi

set +e
timeout --foreground "$timeout_s" "$qemu" \
	-machine virt \
	-kernel "$kernel" \
	-m "${mem_mb}M" \
	-smp "$cpus" \
	-nographic \
	-global virtio-mmio.force-legacy=false \
	-drive "file=${run_image},if=none,format=raw,id=x0" \
	-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
	2>&1 | tee "$log"
qemu_status=${PIPESTATUS[0]}
set -e

if [ "$qemu_status" -eq 124 ]; then
	echo "ERROR: user-space regression suite timed out after ${timeout_s}s" >&2
	echo "log: $log" >&2
	exit 1
fi

if [ "$qemu_status" -ne 0 ]; then
	echo "ERROR: QEMU exited with status $qemu_status" >&2
	echo "log: $log" >&2
	exit 1
fi

sentinel=$(grep -E '\[UTEST\] done ' "$log" | tail -n 1)
if [ -z "$sentinel" ]; then
	echo "ERROR: missing user-test sentinel" >&2
	echo "log: $log" >&2
	exit 1
fi

if [[ ! "$sentinel" =~ pass=([0-9]+)[[:space:]]+fail=([0-9]+)[[:space:]]+skip=([0-9]+)[[:space:]]+xfail=([0-9]+)[[:space:]]+xpass=([0-9]+)[[:space:]]+crash=([0-9]+)[[:space:]]+timeout=([0-9]+) ]]; then
	echo "ERROR: malformed user-test sentinel: $sentinel" >&2
	echo "log: $log" >&2
	exit 1
fi

failed=${BASH_REMATCH[2]}
xpass=${BASH_REMATCH[5]}
crash=${BASH_REMATCH[6]}
timeout_cases=${BASH_REMATCH[7]}

if [ "$failed" -ne 0 ] || [ "$xpass" -ne 0 ] || [ "$crash" -ne 0 ] || \
	[ "$timeout_cases" -ne 0 ]; then
	echo "ERROR: user-space regression failures: $sentinel" >&2
	echo "log: $log" >&2
	exit 1
fi
