#!/bin/sh
# Raspberry Pi OS Lite: get the latest KEGS from GitHub and build sdlkegs.
# Usage:  ~/kegs/pi-update.sh        (then run ~/kegs/sdlkegs)
# One-time setup: see doc/README.linux.sdl.txt
set -e
cd "$(dirname "$0")"

if ! command -v sdl2-config >/dev/null 2>&1; then
	echo "Missing build tools. Run once:"
	echo "  sudo apt install -y git build-essential libsdl2-dev"
	exit 1
fi

git pull --ff-only

cd src
cp vars_linux_sdl vars		# Linux SDL build settings (vars is the Mac one)
trap 'git checkout -q vars' EXIT
make clean >/dev/null
make -j"$(nproc)" sdlkegs
echo
echo "Built $(cd .. && pwd)/sdlkegs - run it from the Pi's own screen and keyboard."
