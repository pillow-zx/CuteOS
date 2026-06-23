#!/bin/sh

set -eu

if [ "$#" -lt 3 ]; then
	echo "usage: $0 <image> <init-elf> <shell-elf> [bin-elf...]" >&2
	exit 1
fi

img=$1
init_elf=$2
shell_elf=$3
size_mb=${MKIMG_SIZE_MB:-16}
debugfs_cmds=$(mktemp)

cleanup()
{
	rm -f "$debugfs_cmds"
}
trap cleanup EXIT INT HUP TERM

mkdir -p "$(dirname "$img")"
rm -f "$img"
dd if=/dev/zero of="$img" bs=1M count="$size_mb" 2>/dev/null
mkfs.ext2 -q -F -b 4096 -I 256 -O none,filetype,sparse_super "$img"

cat >"$debugfs_cmds" <<EOF
mkdir /bin
mkdir /dev
mkdir /fixtures
cd /dev
mknod console c 5 1
mknod null c 1 3
cd /fixtures
symlink readlink-link readlink-target
cd /
write $init_elf /init
write $init_elf /bin/init
write $shell_elf /bin/sh
EOF

shift 3
for elf in "$@"; do
	name=$(basename "$elf" .elf)
	printf 'write %s /bin/%s\n' "$elf" "$name" >>"$debugfs_cmds"
done

debugfs -w -f "$debugfs_cmds" "$img" >/dev/null
