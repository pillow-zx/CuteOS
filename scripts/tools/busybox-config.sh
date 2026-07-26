#!/bin/bash
set -e

BUSYBOX_DIR=user/busybox
CONFIG_FILE=configs/busybox_defconfig

busybox_config_dir="$(mktemp -d)"

echo "Using temporary config dir:"
echo "$busybox_config_dir"

cp "$CONFIG_FILE" "$busybox_config_dir/.config"

make -C "$BUSYBOX_DIR" O="$busybox_config_dir" oldconfig

make -C "$BUSYBOX_DIR" O="$busybox_config_dir" menuconfig HOSTCC="gcc -Wno-error=implicit-int"

cp "$busybox_config_dir/.config" "$CONFIG_FILE"

echo "Busybox config updated:"
echo "$CONFIG_FILE"

rm -rf "$busybox_config_dir"
