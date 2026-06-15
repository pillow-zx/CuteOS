#!/bin/sh

set -eu

if [ "$#" -lt 4 ]; then
	echo "usage: $0 <image> <init-elf> <shell-elf> <bin-elf>..." >&2
	exit 1
fi

img=$1
init_elf=$2
shell_elf=$3
test_elf=$4
size_mb=${MKIMG_SIZE_MB:-16}
debugfs_cmds=$(mktemp)
slow_symlink_target=/slow-target-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-bin

cleanup()
{
	rm -f "$debugfs_cmds"
}
trap cleanup EXIT INT HUP TERM

mkdir -p "$(dirname "$img")"
rm -f "$img"
dd if=/dev/zero of="$img" bs=1M count="$size_mb" 2>/dev/null
mkfs.ext2 -q -F -b 1024 "$img"

cat >"$debugfs_cmds" <<EOF
mkdir /bin
mkdir /dev
cd /dev
mknod console c 5 1
mknod null c 1 3
cd /
write $init_elf /init
write $init_elf /bin/init
write $shell_elf /bin/sh
write $test_elf $slow_symlink_target
symlink /fast-syscall-test /bin/syscall-test
symlink /slow-syscall-test $slow_symlink_target
symlink /loop-symlink /loop-symlink
EOF

shift 3
for elf in "$@"; do
	name=$(basename "$elf" .elf)
	printf 'write %s /bin/%s\n' "$elf" "$name" >>"$debugfs_cmds"
done

debugfs -w -f "$debugfs_cmds" "$img" >/dev/null
