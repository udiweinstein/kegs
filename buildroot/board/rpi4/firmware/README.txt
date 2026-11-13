Raspberry Pi 4 GPU firmware, unmodified, from
https://github.com/raspberrypi/firmware (commit 896fc67d, 12 Nov 2026),
boot/start4.elf and boot/fixup4.dat.  See LICENCE.broadcom.

post-image.sh puts these on the boot partition instead of the older ones
from Buildroot's rpi-firmware package, so they match the bootloader that
F4 "Disable splash" installs (/usr/share/kegs-eeprom/pieeprom.bin).
With the older firmware and the new bootloader the USB controller does
not start (xhci_hcd: can't setup: -110).
