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
timeout_s=${KTEST_TIMEOUT:-60}
log=${KTEST_LOG:-}
run_image=

if [ -z "$log" ]; then
	log=$(mktemp "${TMPDIR:-/tmp}/cuteos-ktest.XXXXXX.log")
fi

if ! command -v timeout >/dev/null 2>&1; then
	echo "ERROR: timeout command not found" >&2
	exit 2
fi

run_image=$(mktemp "${TMPDIR:-/tmp}/cuteos-ktest-img.XXXXXX")
cleanup() {
	rm -f "$run_image"
}
trap cleanup EXIT
if ! cp "$image" "$run_image"; then
	echo "ERROR: failed to copy test image: $image" >&2
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
	echo "ERROR: kernel self-test timed out after ${timeout_s}s" >&2
	echo "log: $log" >&2
	exit 1
fi

if [ "$qemu_status" -ne 0 ]; then
	echo "ERROR: QEMU exited with status $qemu_status" >&2
	echo "log: $log" >&2
	exit 1
fi

sentinel=$(grep -E '\[KTEST\] done ' "$log" | tail -n 1)
if [ -z "$sentinel" ]; then
	echo "ERROR: missing kernel self-test sentinel" >&2
	echo "log: $log" >&2
	exit 1
fi

if [[ ! "$sentinel" =~ failed_modules=([0-9]+)[[:space:]]+cases=([0-9]+)[[:space:]]+failed_cases=([0-9]+) ]]; then
	echo "ERROR: malformed kernel self-test sentinel: $sentinel" >&2
	echo "log: $log" >&2
	exit 1
fi

failed_modules=${BASH_REMATCH[1]}
failed_cases=${BASH_REMATCH[3]}

if [ "$failed_modules" -ne 0 ] || [ "$failed_cases" -ne 0 ]; then
	echo "ERROR: kernel self-test failures: $sentinel" >&2
	echo "log: $log" >&2
	exit 1
fi
