#!/bin/bash
# Build the Raspberry Pi 4 SD card image on a Mac (Apple Silicon or Intel).
#
#   ./buildroot/build-on-mac.sh          build (first time ~30-45 min,
#                                        after a KEGS change ~1-2 min)
#   ./buildroot/build-on-mac.sh clean    delete everything it keeps
#                                        (~15-20 GB) to free the disk
#
# Needs OrbStack (recommended, https://orbstack.dev) or Docker Desktop.
# Buildroot runs inside a Linux container (as x86-64, via Rosetta on
# Apple Silicon).  Its work files live in a Docker volume called
# "kegs-buildroot", so later builds only redo what changed.
# Result: sdcard/kegs-pi4-sdcard.img in this repository folder.
set -e

BUILDROOT_VERSION=2026.02.3
VOLUME=kegs-buildroot
IMAGE=kegs-buildroot-env
REPO=$(cd "$(dirname "$0")/.." && pwd)
STARTED_ENGINE=0

say() { printf '\n=== %s\n' "$*"; }

# --- Make sure a container engine is running --------------------------
if ! command -v docker >/dev/null 2>&1; then
	echo "docker not found.  Install OrbStack (https://orbstack.dev) or"
	echo "Docker Desktop, open it once, then run this script again."
	exit 1
fi
if ! docker info >/dev/null 2>&1; then
	say "Starting the container engine"
	if [ -d /Applications/OrbStack.app ]; then
		open -g -a OrbStack
	else
		open -g -a Docker
	fi
	STARTED_ENGINE=1
	for _ in $(seq 1 90); do
		docker info >/dev/null 2>&1 && break
		sleep 2
	done
	docker info >/dev/null 2>&1 || {
		echo "The container engine did not start."; exit 1; }
fi

quit_engine() {
	# Only quit what this script started, so nothing keeps running
	if [ "$STARTED_ENGINE" = 1 ]; then
		say "Stopping the container engine"
		if [ -d /Applications/OrbStack.app ]; then
			osascript -e 'quit app "OrbStack"' 2>/dev/null || true
		else
			osascript -e 'quit app "Docker"' 2>/dev/null || true
		fi
	fi
}
trap quit_engine EXIT

if [ "$1" = clean ]; then
	say "Deleting the build volume and environment"
	docker volume rm -f "$VOLUME" >/dev/null 2>&1 || true
	docker image rm -f "$IMAGE" >/dev/null 2>&1 || true
	echo "Done."
	exit 0
fi

# --- Build environment (made once, then cached by Docker) -------------
say "Preparing the Linux build environment"
docker build -q --platform linux/amd64 -t "$IMAGE" - >/dev/null <<'DOCKERFILE'
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
	build-essential bc bison flex cpio rsync unzip file wget curl \
	libssl-dev python3 git perl ca-certificates xz-utils \
	&& rm -rf /var/lib/apt/lists/*
RUN useradd -m -s /bin/bash builder
DOCKERFILE

docker volume create "$VOLUME" >/dev/null
mkdir -p "$REPO/sdcard"

# --- Build inside the container ---------------------------------------
say "Building (Buildroot $BUILDROOT_VERSION)"
# The volume starts out owned by root: hand it to the build user
docker run --rm --platform linux/amd64 -v "$VOLUME":/work "$IMAGE" \
	chown builder: /work
docker run --rm -i --platform linux/amd64 --user builder \
	-v "$VOLUME":/work \
	-v "$REPO":/src:ro \
	-v "$REPO/sdcard":/result \
	-e BRV="$BUILDROOT_VERSION" \
	"$IMAGE" bash -s <<'INNER'
set -e
export HOME=/work/home		# keeps the compiler cache in the volume
mkdir -p "$HOME"
cd /work
if [ ! -d br ]; then
	git clone -q --depth 1 -b "$BRV" \
		https://github.com/buildroot/buildroot.git br
fi
# (Re)configure when the defconfig changed
SUM=$(md5sum /src/buildroot/configs/kegs_rpi4_defconfig | cut -c1-32)
if [ ! -f out/.config ] ||
		[ "$(cat out/.defconfig.md5 2>/dev/null)" != "$SUM" ]; then
	make -s -C br O=/work/out BR2_EXTERNAL=/src/buildroot \
		BR2_DL_DIR=/work/dl kegs_rpi4_defconfig
	echo "$SUM" > out/.defconfig.md5
fi
# Always take the current KEGS source from the Mac
make -s -C out BR2_DL_DIR=/work/dl kegs-dirclean
set +e
make -C out BR2_DL_DIR=/work/dl 2>&1 | tee /work/build.log |
	grep --line-buffered '^>>>'
status=${PIPESTATUS[0]}
set -e
if [ "$status" != 0 ]; then
	echo
	echo "*** Build failed.  Last lines of the log:"
	tail -40 /work/build.log
	exit 1
fi
cp out/images/sdcard.img /result/kegs-pi4-sdcard.img
INNER

say "Done: $REPO/sdcard/kegs-pi4-sdcard.img"
echo "Write it to a micro-SD card with Raspberry Pi Imager (Use custom)."
