KEGS Apple IIgs for Raspberry Pi 4
==================================

This partition belongs to KEGS.  You can edit it from a Mac or PC.

1. Copy your IIgs ROM file here, named ROM (or ROM.01 / ROM.03).
2. Copy disk images (.2mg .po .dsk .woz .gz ...) into the "disks"
   folder.
3. Put the card in the Pi and switch it on.  KEGS starts by itself.

Inside KEGS:
  F4         configuration menu: choose ROM, insert disks, settings
  F10        quick disk swap
  F1 / F2    Open-Apple / Option (also Left-Alt / Right-Alt)

KEGS saves its settings to config.kegs on this partition.

Optional: create a file named kegs.env here to change settings
at start-up, one per line, for example:
  AUDIO=Headphones      sound from the 3.5mm jack (default: HDMI)
  KEGS_ARGS=-audio 0    extra KEGS command line options

Before switching the Pi off, let disk activity finish: settings and
disk images are written to this partition.
