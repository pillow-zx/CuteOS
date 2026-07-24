#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 <readelf> <user-elf>" >&2
	exit 2
fi

readelf=$1
elf=$2

fail()
{
	echo "ERROR: $elf: $*" >&2
	exit 1
}

header=$(LC_ALL=C "$readelf" -h "$elf")
program_headers=$(LC_ALL=C "$readelf" -l "$elf")
attributes=$(LC_ALL=C "$readelf" -A "$elf")

printf '%s\n' "$header" | grep -q 'Class:.*ELF64' || \
	fail "not an ELF64 executable"
printf '%s\n' "$header" | grep -q 'Type:.*EXEC' || \
	fail "user ELF must be ET_EXEC"
printf '%s\n' "$header" | grep -q 'Machine:.*RISC-V' || \
	fail "not a RISC-V executable"
printf '%s\n' "$header" | grep -q 'Flags:.*soft-float ABI' || \
	fail "user ELF must use the lp64 soft-float ABI"

if printf '%s\n' "$program_headers" | grep -Eq '(^|[[:space:]])(INTERP|DYNAMIC)([[:space:]]|$)'; then
	fail "dynamic ELF segments are not supported"
fi

arch=$(printf '%s\n' "$attributes" | \
	sed -n 's/.*Tag_RISCV_arch: "\([^"]*\)".*/\1/p')
[ -n "$arch" ] || fail "missing Tag_RISCV_arch attribute"

if printf '%s\n' "$arch" | \
	grep -Eq '^rv(32|64)[^_]*[fd]|(^|_)[fd][0-9]+p[0-9]+(_|$)'; then
	fail "F/D ISA extensions are forbidden: $arch"
fi
