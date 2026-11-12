#!/bin/sh
# Raspberry Pi OS Lite: update KEGS from GitHub (if there is a new version)
# and run it.  Install once, then just type "kegs" (or KEGS) after login:
#
#   sudo ln -sf ~/kegs/pi-kegs.sh /usr/local/bin/kegs
#   sudo ln -sf ~/kegs/pi-kegs.sh /usr/local/bin/KEGS
#
# Extra arguments are passed to sdlkegs.  "kegs -n" skips the update.

DIR=$(dirname "$(readlink -f "$0")")
cd "$DIR" || exit 1

if [ "$1" = "-n" ]; then
	shift
else
	OLD=$(git rev-parse HEAD 2>/dev/null)
	echo "Checking for a new KEGS version..."
	if timeout 20 git pull -q --ff-only 2>/dev/null; then
		NEW=$(git rev-parse HEAD)
		if [ "$OLD" != "$NEW" ] || [ ! -x sdlkegs ]; then
			echo "New version - building:"
			git log --oneline "$OLD..$NEW" 2>/dev/null | head -10
			./pi-update.sh || {
				echo "Build failed."
				[ -x sdlkegs ] || exit 1
				echo "Running the previous version in 5 seconds..."
				sleep 5
			}
		else
			echo "Up to date."
		fi
	else
		echo "No network or GitHub not reachable - running the current version."
	fi
fi

if [ ! -x sdlkegs ]; then
	echo "sdlkegs is not built yet. Run: $DIR/pi-update.sh"
	exit 1
fi
exec ./sdlkegs "$@"
