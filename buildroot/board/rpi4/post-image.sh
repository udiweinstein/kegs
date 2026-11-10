#!/bin/bash
# Builds sdcard.img: boot (FAT) + root (ext4, read-only) + KEGS (FAT).
set -e

BOARD_DIR="$(dirname "$0")"
GENIMAGE_CFG="${BINARIES_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

# Boot partition: firmware, overlays, device trees, kernel
FILES=()
for i in "${BINARIES_DIR}"/*.dtb "${BINARIES_DIR}"/rpi-firmware/*; do
	FILES+=( "${i#${BINARIES_DIR}/}" )
done
KERNEL=$(sed -n 's/^kernel=//p' "${BINARIES_DIR}/rpi-firmware/config.txt")
FILES+=( "${KERNEL}" )
BOOT_FILES=$(printf '\\t\\t\\t"%s",\\n' "${FILES[@]}")
sed "s|#BOOT_FILES#|${BOOT_FILES}|" "${BOARD_DIR}/genimage.cfg" \
	> "${GENIMAGE_CFG}"

# KEGS data partition: starts with a README and an empty disks folder
rm -rf "${BINARIES_DIR}/kegs-part"
mkdir -p "${BINARIES_DIR}/kegs-part/disks"
cp "${BOARD_DIR}/kegs-partition-README.txt" \
	"${BINARIES_DIR}/kegs-part/README.txt"
touch "${BINARIES_DIR}/kegs-part/disks/.keep"

trap 'rm -rf "${ROOTPATH_TMP}"' EXIT
ROOTPATH_TMP="$(mktemp -d)"
rm -rf "${GENIMAGE_TMP}"

genimage \
	--rootpath "${ROOTPATH_TMP}"   \
	--tmppath "${GENIMAGE_TMP}"    \
	--inputpath "${BINARIES_DIR}"  \
	--outputpath "${BINARIES_DIR}" \
	--config "${GENIMAGE_CFG}"
