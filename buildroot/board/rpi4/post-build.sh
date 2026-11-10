#!/bin/sh
# Runs after the root filesystem is assembled, before it is packed.
set -u
set -e

# Mount point for the KEGS data partition (3rd partition, FAT)
mkdir -p "${TARGET_DIR}/kegs"
grep -q '[[:space:]]/kegs[[:space:]]' "${TARGET_DIR}/etc/fstab" || \
	echo '/dev/mmcblk0p3	/kegs	vfat	rw,noatime,flush,umask=000,nofail	0	0' \
		>> "${TARGET_DIR}/etc/fstab"

# Start KEGS on the HDMI screen (tty1) instead of a login prompt, and
# restart it if it ever quits.  The serial port keeps its login prompt
# for debugging.
sed -i '/^tty1::/d' "${TARGET_DIR}/etc/inittab"
sed -i '/GENERIC_SERIAL/a\
tty1::respawn:/usr/bin/kegs-start' "${TARGET_DIR}/etc/inittab"
