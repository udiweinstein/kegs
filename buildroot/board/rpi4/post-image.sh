#!/bin/bash
# Builds sdcard.img: boot (FAT) + root (ext4, read-only) + KEGS (FAT).
set -e

BOARD_DIR="$(dirname "$0")"
GENIMAGE_CFG="${BINARIES_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

# Newer GPU firmware that matches the bootloader in /usr/share/kegs-eeprom
#  (see board/rpi4/firmware/README.txt)
cp "${BOARD_DIR}/firmware/start4.elf" "${BOARD_DIR}/firmware/fixup4.dat" \
	"${BINARIES_DIR}/rpi-firmware/"

# Boot partition: firmware, overlays, device trees, kernel
FILES=()
for i in "${BINARIES_DIR}"/*.dtb "${BINARIES_DIR}"/rpi-firmware/*; do
	FILES+=( "${i#${BINARIES_DIR}/}" )
done
KERNEL=$(sed -n 's/^kernel=//p' "${BINARIES_DIR}/rpi-firmware/config.txt")
FILES+=( "${KERNEL}" )
BOOT_FILES=$(printf '\\t\\t\\t"%s",\\n' "${FILES[@]}")

# KEGS data partition: starts with a README and an empty disks folder
rm -rf "${BINARIES_DIR}/kegs-part"
mkdir -p "${BINARIES_DIR}/kegs-part/disks"
cp "${BOARD_DIR}/kegs-partition-README.txt" \
	"${BINARIES_DIR}/kegs-part/README.txt"
touch "${BINARIES_DIR}/kegs-part/disks/.keep"

# Local builds: files from the repository's kegs-files folder (ROM,
#  disks/ ...), which is not in git, go onto the KEGS partition too
if [ -n "${KEGS_EXTRA_FILES:-}" ] && [ -d "${KEGS_EXTRA_FILES}" ]; then
	echo "Adding to the KEGS partition: $(ls "${KEGS_EXTRA_FILES}" | tr '\n' ' ')"
	cp -R "${KEGS_EXTRA_FILES}/." "${BINARIES_DIR}/kegs-part/"
	find "${BINARIES_DIR}/kegs-part" -name .DS_Store -delete
fi
KFILES=()
for i in "${BINARIES_DIR}"/kegs-part/*; do
	KFILES+=( "${i#${BINARIES_DIR}/}" )
done
KEGS_FILES=$(printf '\\t\\t\\t"%s",\\n' "${KFILES[@]}")
# 512 MB, or more if the files need it
USED_MB=$(du -sm "${BINARIES_DIR}/kegs-part" | cut -f1)
KEGS_MB=$(( USED_MB * 11 / 10 + 64 ))
[ "$KEGS_MB" -lt 512 ] && KEGS_MB=512

sed -e "s|#BOOT_FILES#|${BOOT_FILES}|" \
	-e "s|#KEGS_FILES#|${KEGS_FILES}|" \
	-e "s|#KEGS_SIZE#|${KEGS_MB}M|" \
	"${BOARD_DIR}/genimage.cfg" > "${GENIMAGE_CFG}"

trap 'rm -rf "${ROOTPATH_TMP}"' EXIT
ROOTPATH_TMP="$(mktemp -d)"
rm -rf "${GENIMAGE_TMP}"

genimage \
	--rootpath "${ROOTPATH_TMP}"   \
	--tmppath "${GENIMAGE_TMP}"    \
	--inputpath "${BINARIES_DIR}"  \
	--outputpath "${BINARIES_DIR}" \
	--config "${GENIMAGE_CFG}"
