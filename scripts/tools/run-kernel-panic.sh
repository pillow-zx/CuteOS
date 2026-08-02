#!/usr/bin/env bash
set -u

if [ "$#" -ne 5 ]; then
	echo "usage: $0 <qemu> <kernel> <mem-mb> <cpus> <case>" >&2
	exit 2
fi

qemu=$1
kernel=$2
mem_mb=$3
cpus=$4
case_name=$5
timeout_s=${KPANIC_TIMEOUT:-15}
log=${KPANIC_LOG:-}

if [ -z "$log" ]; then
	log=$(mktemp "${TMPDIR:-/tmp}/cuteos-kpanic.XXXXXX.log")
fi

if ! command -v timeout >/dev/null 2>&1; then
	echo "ERROR: timeout command not found" >&2
	exit 2
fi

set +e
timeout --foreground "$timeout_s" "$qemu" \
	-machine virt \
	-kernel "$kernel" \
	-m "${mem_mb}M" \
	-smp "$cpus" \
	-nographic \
	2>&1 | tee "$log"
qemu_status=${PIPESTATUS[0]}
set -e

if ! grep -Fq "[KPANIC] case=${case_name}" "$log"; then
	echo "ERROR: panic case did not reach its trigger: ${case_name}" >&2
	echo "log: $log" >&2
	exit 1
fi

if ! grep -Fq "KERNEL PANIC" "$log"; then
	echo "ERROR: expected kernel panic was not observed: ${case_name}" >&2
	echo "log: $log" >&2
	exit 1
fi

if [ "$qemu_status" -ne 124 ]; then
	echo "ERROR: panic harness expected timeout status 124, got $qemu_status" >&2
	echo "log: $log" >&2
	exit 1
fi

echo "[KPANIC] PASS ${case_name}"
