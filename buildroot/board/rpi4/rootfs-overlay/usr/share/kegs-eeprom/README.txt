Raspberry Pi 4 bootloader (EEPROM) files, unmodified, from
https://github.com/raspberrypi/rpi-eeprom (commit 3831e51d, Nov 2026):
  pieeprom.bin   = firmware-2711/default/pieeprom-2026-10-30.bin
  recovery.bin   = firmware-2711/default/recovery.bin
/usr/bin/rpi-eeprom-config and rpi-eeprom-digest come from the same
place.  See LICENSE.

KEGS's F4 "Disable splash" uses them (kegs-net splash 0|1): it makes
pieeprom.upd with DISABLE_HDMI set in the bootloader settings and puts
it with recovery.bin on the boot partition; the Pi installs it at the
next power-on.
