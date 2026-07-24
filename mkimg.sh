#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 <image> <rootfs-directory>" >&2
	exit 1
fi

img=$1
rootfs=$2
size_mb=${MKIMG_SIZE_MB:-16}
debugfs_cmds=$(mktemp)

if [ ! -d "$rootfs" ]; then
	echo "root filesystem directory not found: $rootfs" >&2
	exit 1
fi

cleanup()
{
	rm -f "$debugfs_cmds"
}
trap cleanup EXIT INT HUP TERM

mkdir -p "$(dirname "$img")"
rm -f "$img"
dd if=/dev/zero of="$img" bs=1M count="$size_mb" 2>/dev/null
mkfs.ext2 -q -F -b 4096 -I 256 -O none,filetype,sparse_super \
	-d "$rootfs" "$img"

cat >"$debugfs_cmds" <<EOF
cd /dev
mknod console c 5 1
mknod null c 1 3
EOF

debugfs -w -f "$debugfs_cmds" "$img" >/dev/null
