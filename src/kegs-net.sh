#!/bin/sh
# kegs-net: network helper for KEGS on Linux (Raspberry Pi).
#
# KEGS's F4 "Network Configuration" menu runs this in the background and
# reads the results from files in /tmp/kegs-net, so the emulator never
# waits for the network.  It works on:
#   - Raspberry Pi OS (NetworkManager / nmcli manages Wi-Fi)
#   - the Buildroot SD image (wpa_supplicant + udhcpc)
# File sharing uses ksmbd, the small SMB server built into the kernel.
#
# Usage:
#   kegs-net status                  write /tmp/kegs-net/status
#   kegs-net scan                    write /tmp/kegs-net/scan (SSIDs)
#   kegs-net connect SSID [PASSWORD] join a Wi-Fi network
#   kegs-net smb on DIR | off        start/stop sharing DIR as "disks"
#   kegs-net splash 1|0              Raspberry Pi 4: hide (1) or show (0)
#                                    the bootloader screen (EEPROM setting
#                                    DISABLE_HDMI); result in $STATE/eeprom
#   kegs-net boot SSID PASSWORD SMB DIR
#                                    at KEGS start-up: join Wi-Fi (only
#                                    without NetworkManager) and start
#                                    sharing if SMB is 1

STATE=/tmp/kegs-net
SMB_USER=kegs
SMB_PASS=apple2
mkdir -p "$STATE" 2>/dev/null
chmod 777 "$STATE" 2>/dev/null
# Errors go to a log (KEGS runs this with no terminal)
echo "--- $(date +%T) kegs-net $1" >> "$STATE/kegs-net.err" 2>/dev/null
exec 2>>"$STATE/kegs-net.err"

have() { command -v "$1" >/dev/null 2>&1; }

as_root() {
	if [ "$(id -u)" = 0 ]; then
		"$@"
	else
		sudo -n "$@"
	fi
}

# Write stdin to $STATE/$1 in one step, so KEGS never reads half a file
put() {
	cat > "$STATE/$1.tmp.$$" && mv -f "$STATE/$1.tmp.$$" "$STATE/$1"
}

wifi_if() {
	for d in /sys/class/net/*; do
		if [ -d "$d/wireless" ]; then
			basename "$d"
			return
		fi
	done
}

use_nm() {
	have nmcli && nmcli -t -f RUNNING general 2>/dev/null | grep -q running
}

WPA_CONF=$STATE/wpa.conf
WPA_CTRL=$STATE/wpa

wpa() { wpa_cli -p "$WPA_CTRL" -i "$IF" "$@"; }

# Start wpa_supplicant on $IF if it isn't running (no network configured)
wpa_start() {
	if [ ! -S "$WPA_CTRL/$IF" ]; then
		if [ ! -f "$WPA_CONF" ]; then
			printf 'ctrl_interface=%s\nupdate_config=0\n' \
				"$WPA_CTRL" > "$WPA_CONF"
		fi
		ip link set "$IF" up 2>/dev/null
		wpa_supplicant -B -i "$IF" -c "$WPA_CONF" \
					-P "$STATE/wpa.pid" >/dev/null 2>&1
		sleep 1
	fi
}

status() {
	IF=$(wifi_if)
	WIFI=none
	SSID=
	if [ -n "$IF" ]; then
		WIFI=disconnected
		if use_nm; then
			line=$(nmcli -t -f DEVICE,STATE,CONNECTION device status \
				2>/dev/null | grep "^$IF:")
			st=$(echo "$line" | cut -d: -f2)
			case "$st" in
			connected) WIFI=connected
				SSID=$(nmcli -t -f ACTIVE,SSID device wifi list \
					--rescan no 2>/dev/null |
					sed -n 's/^yes://p' | head -1) ;;
			connecting*) WIFI=connecting ;;
			esac
		elif [ -S "$WPA_CTRL/$IF" ]; then
			st=$(wpa status | sed -n 's/^wpa_state=//p')
			case "$st" in
			COMPLETED) WIFI=connected
				SSID=$(wpa status | sed -n 's/^ssid=//p') ;;
			ASSOCIAT*|AUTHENT*|*HANDSHAKE*|SCANNING)
				WIFI=connecting ;;
			esac
		fi
	fi
	IPS=$(ip -4 addr show 2>/dev/null | awk '
		/^[0-9]+:/ { dev = $2; sub(":", "", dev); sub("@.*", "", dev) }
		/ inet / && dev != "lo" { split($2, a, "/");
			printf "%s%s %s", sep, a[1], dev; sep = ";" }')
	SMB=off
	if [ -f "$STATE/smb.on" ]; then
		SMB=on
	elif [ -f "$STATE/smb.err" ]; then
		SMB="error: $(head -1 "$STATE/smb.err")"
	fi
	{
		echo "WIFI=$WIFI"
		echo "SSID=$SSID"
		echo "IP=$IPS"
		echo "SMB=$SMB"
	} | put status
}

scan() {
	IF=$(wifi_if)
	if [ -z "$IF" ]; then
		echo "No Wi-Fi hardware" | put scan
		return
	fi
	echo "Scanning..." | put scan
	if use_nm; then
		# SSID:SIGNAL, ':' inside an SSID is written as '\:'
		nmcli -t -f SIGNAL,SSID device wifi list --rescan yes \
				2>/dev/null |
			sed 's/\\:/:/g' | sort -t: -k1,1 -nr |
			cut -d: -f2- | grep -v '^$' | awk '!seen[$0]++' |
			head -6 | put scan
	else
		wpa_start
		wpa scan >/dev/null 2>&1
		sleep 5
		# bssid freq signal flags ssid (tab separated), strongest first
		wpa scan_results 2>/dev/null | tail -n +2 |
			sort -t "$(printf '\t')" -k3,3 -nr |
			cut -f5 | grep -v '^$' | awk '!seen[$0]++' |
			head -6 | put scan
	fi
}

connect() {
	SSID=$1
	PASS=$2
	IF=$(wifi_if)
	[ -n "$IF" ] && [ -n "$SSID" ] || return 1
	if use_nm; then
		if [ -n "$PASS" ]; then
			nmcli device wifi connect "$SSID" password "$PASS" \
				ifname "$IF" >/dev/null 2>&1 ||
			as_root nmcli device wifi connect "$SSID" \
				password "$PASS" ifname "$IF" >/dev/null 2>&1
		else
			nmcli device wifi connect "$SSID" ifname "$IF" \
				>/dev/null 2>&1 ||
			as_root nmcli device wifi connect "$SSID" \
				ifname "$IF" >/dev/null 2>&1
		fi
	else
		{
			printf 'ctrl_interface=%s\nupdate_config=0\n' "$WPA_CTRL"
			if [ -n "$PASS" ]; then
				wpa_passphrase "$SSID" "$PASS" | grep -v '#psk='
			else
				printf 'network={\n\tssid="%s"\n\tkey_mgmt=NONE\n}\n' \
					"$SSID"
			fi
		} > "$WPA_CONF.new" && mv -f "$WPA_CONF.new" "$WPA_CONF"
		chmod 600 "$WPA_CONF"
		if [ -S "$WPA_CTRL/$IF" ]; then
			wpa reconfigure >/dev/null 2>&1
		else
			wpa_start
		fi
		# DHCP client in the background; it keeps the lease renewed
		if [ -f "$STATE/udhcpc.pid" ]; then
			kill "$(cat "$STATE/udhcpc.pid")" 2>/dev/null
		fi
		udhcpc -b -S -i "$IF" -p "$STATE/udhcpc.pid" >/dev/null 2>&1
	fi
	status
}

smb_on() {
	DIR=$1
	rm -f "$STATE/smb.err"
	if [ ! -d "$DIR" ]; then
		echo "folder not found" | put smb.err
		return 1
	fi
	if ! have ksmbd.mountd; then
		echo "ksmbd-tools not installed" | put smb.err
		return 1
	fi
	if [ "$(id -u)" != 0 ]; then
		# Needs root: re-run this script with sudo (see
		#  pi-network-setup.sh, which allows that without a password)
		if ! sudo -n "$0" smb on "$DIR" 2>/dev/null; then
			[ -f "$STATE/smb.err" ] ||
				echo "needs root (run pi-network-setup.sh)" |
								put smb.err
			return 1
		fi
		return 0
	fi
	OWNER=$(ls -ld "$DIR" | awk '{print $3}')
	GROUP=$(ls -ld "$DIR" | awk '{print $4}')
	CONF=$STATE/ksmbd.conf
	PWDB=$STATE/ksmbdpwd.db
	cat > "$CONF" <<EOF
[global]
	netbios name = KEGS
	server string = KEGS Apple IIgs
	map to guest = bad user
	guest account = $OWNER

[disks]
	path = $DIR
	read only = no
	guest ok = yes
	force user = $OWNER
	force group = $GROUP
EOF
	rm -f "$PWDB"
	touch "$PWDB"
	chmod 600 "$PWDB"
	ksmbd.adduser -C "$CONF" -P "$PWDB" -a -p "$SMB_PASS" "$SMB_USER" \
							>/dev/null 2>&1
	modprobe ksmbd 2>/dev/null
	ksmbd.control -s >/dev/null 2>&1
	if ksmbd.mountd -C "$CONF" -P "$PWDB" >/dev/null 2>&1; then
		touch "$STATE/smb.on"
		chmod 666 "$STATE/smb.on" 2>/dev/null
	else
		echo "ksmbd failed to start" | put smb.err
		chmod 666 "$STATE/smb.err" 2>/dev/null
	fi
}

smb_off() {
	if [ "$(id -u)" != 0 ]; then
		sudo -n "$0" smb off 2>/dev/null
		rm -f "$STATE/smb.on" 2>/dev/null
		return
	fi
	ksmbd.control -s >/dev/null 2>&1
	rm -f "$STATE/smb.on" "$STATE/smb.err"
}

# Bootloader settings with DISABLE_HDMI=$1, read from stdin: the line goes
#  right after [all] (or first if there is no [all])
set_disable_hdmi() {
	awk -v v="$1" '
		/^DISABLE_HDMI=/ { next }
		{ print }
		$0 == "[all]" && !done { print "DISABLE_HDMI=" v; done = 1 }
		END { if (!done) print "DISABLE_HDMI=" v }'
}

# Raspberry Pi 4 bootloader screen: hide (1) or show (0).  The setting is
#  in the board's EEPROM; the Pi installs the update at the next power-on.
splash() {
	echo "working" | put eeprom
	EE=/usr/share/kegs-eeprom
	if [ -f "$EE/pieeprom.bin" ] && have rpi-eeprom-config; then
		# KEGS SD image: make pieeprom.upd from the bootloader shipped
		#  with it and put it on the boot partition with recovery.bin
		if ! rpi-eeprom-config "$EE/pieeprom.bin" |
			set_disable_hdmi "$1" > "$STATE/bootconf.txt" ||
		   ! rpi-eeprom-config --config "$STATE/bootconf.txt" \
			--out "$STATE/pieeprom.upd" "$EE/pieeprom.bin" \
			>/dev/null 2>&1 ||
		   ! rpi-eeprom-digest -i "$STATE/pieeprom.upd" \
			-o "$STATE/pieeprom.sig" >/dev/null 2>&1; then
			echo "error: could not make the update" | put eeprom
			return
		fi
		mkdir -p "$STATE/boot"
		if ! as_root mount -t vfat /dev/mmcblk0p1 "$STATE/boot"; then
			echo "error: could not open the boot partition" |
								put eeprom
			return
		fi
		if as_root cp "$STATE/pieeprom.upd" "$STATE/pieeprom.sig" \
				"$EE/recovery.bin" "$STATE/boot/"; then
			echo "ok" | put eeprom
		else
			echo "error: could not write the boot partition" |
								put eeprom
		fi
		sync
		as_root umount "$STATE/boot"
	elif have rpi-eeprom-config && have vcgencmd; then
		# Raspberry Pi OS: change the current settings, stage update
		if as_root rpi-eeprom-config 2>/dev/null |
			set_disable_hdmi "$1" > "$STATE/bootconf.txt" &&
		   grep -q '^\[all\]' "$STATE/bootconf.txt" &&
		   as_root rpi-eeprom-config --apply "$STATE/bootconf.txt" \
							>/dev/null 2>&1; then
			echo "ok" | put eeprom
		else
			echo "error: rpi-eeprom-config failed" | put eeprom
		fi
	else
		echo "error: only on a Raspberry Pi 4" | put eeprom
	fi
}

case "$1" in
status)		status ;;
scan)		scan ;;
connect)	connect "$2" "$3" ;;
splash)		splash "$2" ;;
smb)
	case "$2" in
	on)	smb_on "$3" ;;
	*)	smb_off ;;
	esac
	status ;;
boot)
	# $2=SSID $3=PASSWORD $4=SMB(0/1) $5=DIR
	if [ -n "$2" ] && ! use_nm; then
		connect "$2" "$3"
	fi
	if [ "$4" = 1 ]; then
		smb_on "$5"
	fi
	status ;;
*)
	echo "usage: kegs-net status|scan|connect SSID [PASS]|smb on DIR|smb off|boot ..." >&2
	exit 1 ;;
esac
