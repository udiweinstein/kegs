#!/bin/sh
# Raspberry Pi OS: one-time setup for KEGS's F4 "Network Configuration"
# menu (Wi-Fi and SMB file sharing).  Run as your normal user:
#     ~/kegs/pi-network-setup.sh        (or: ... IL  to set the Wi-Fi country)
# It installs ksmbd-tools (the small SMB file server) and lets KEGS start
# and stop file sharing without asking for a password.
set -e
cd "$(dirname "$0")"
DIR=$(pwd -P)
HELPER="$DIR/src/kegs-net.sh"
COUNTRY=${1:-IL}

sudo apt install -y ksmbd-tools

# Allow this user to run only the KEGS network helper as root (file
#  sharing needs root), without a password
RULE="$(id -un) ALL=(root) NOPASSWD: $HELPER"
echo "$RULE" | sudo tee /etc/sudoers.d/kegs-net >/dev/null
sudo chmod 440 /etc/sudoers.d/kegs-net
sudo visudo -cf /etc/sudoers.d/kegs-net

# Wi-Fi stays switched off on Pi OS until a country is set
if command -v raspi-config >/dev/null 2>&1; then
	sudo raspi-config nonint do_wifi_country "$COUNTRY" || true
fi
sudo rfkill unblock wifi 2>/dev/null || true

chmod +x "$HELPER"
echo
echo "Done.  In KEGS press F4 and choose Network Configuration."
