KEGS Apple IIgs for Raspberry Pi 4
==================================

This partition belongs to KEGS.  You can edit it from a Mac or PC.

1. Copy your IIgs ROM file here, named ROM (or ROM.01 / ROM.03).
2. Copy disk images (.2mg .po .dsk .woz .gz ...) into the "disks"
   folder.
   A disk image named AUTOEXEC.img in "disks" is inserted by itself
   at start-up (5.25" -> slot 6, 3.5" -> slot 5, hard disk -> slot 7).
3. Put the card in the Pi and switch it on.  KEGS starts by itself.

Inside KEGS:
  F4         configuration menu: choose ROM, insert disks, settings
  F10        quick disk swap
  F1 / F2    Open-Apple / Option (also Left-Alt / Right-Alt)

F4 -> Network Configuration: join Wi-Fi and share the "disks" folder
over the network (SMB), to copy disk images from a Mac or PC:
  Windows: \\<ip shown>\disks    Mac: smb://<ip shown>/disks
  user: kegs   password: apple2
  On a Mac, run once in Terminal (else Finder hangs on "Connecting"):
  printf '[default]\nprotocol_vers_map=6\n' | sudo tee /etc/nsmb.conf

F4 -> Video Setting -> Disable splash = Yes hides the Raspberry Pi
boot screen (bootloader EEPROM setting).  Then turn the Pi off and on
twice: the first start installs the setting.

KEGS saves its settings to config.kegs on this partition (F4 -> Save
changes to config.kegs).

Optional: create a file named kegs.env here to change settings
at start-up, one per line, for example:
  AUDIO=Headphones      sound from the 3.5mm jack (default: HDMI if
                        the screen has speakers, else the jack)
  KEGS_ARGS=-audio 0    extra KEGS command line options

Before switching the Pi off, let disk activity finish: settings and
disk images are written to this partition.
