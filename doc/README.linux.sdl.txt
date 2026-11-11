KEGS with SDL2 (no X11 needed) - e.g. Raspberry Pi OS Lite
==========================================================

The SDL2 driver (src/sdl2driver.c) lets KEGS run straight on the Linux
console using SDL2's KMSDRM backend, so no X server or desktop is
needed.  It also works under X11 or Wayland desktops.

Install build packages (Raspberry Pi OS / Debian / Ubuntu):

	sudo apt install build-essential libsdl2-dev

Build:

	cd src
	rm -f vars
	ln -s vars_linux_sdl vars
	make

This creates "sdlkegs" in the top directory.  Put your ROM file and
config.kegs next to it (see README.ROM.files.txt), then run:

	./sdlkegs

On the console it runs full screen.  On a desktop it opens a window;
F11 toggles full screen there.  To force full screen set
KEGS_FULLSCREEN=1.  To choose an SDL backend explicitly:

	SDL_VIDEODRIVER=kmsdrm ./sdlkegs

Raspberry Pi notes:
- Enable the KMS graphics driver (the default on current Pi OS:
  "dtoverlay=vc4-kms-v3d" in /boot/firmware/config.txt).
- Your user must be in the "video" and "input" groups to use the
  display and keyboard from the console:
	sudo usermod -aG video,input $USER    (then log out/in)
- Sound goes through SDL, which uses ALSA directly on Pi OS Lite and
  PulseAudio/PipeWire on a desktop.  Your user must be in the "audio"
  group on the console.  Run with "-audio 0" to disable sound.
- For an SD card that boots straight into KEGS with no Pi OS at all,
  see buildroot/README.md.

Keys: the emulated keyboard follows the physical US key positions, so
it works the same with any host keyboard layout.  Left-Alt is
Open-Apple (Command), Right-Alt / Windows key is Option (Closed-Apple),
like the X11 version (the F4 menu option "Swap Command/Option" changes
this).  F4 or Option-ESC opens the config panel, Ctrl-ESC is RESET.
To quit, press Ctrl-F10 (or close the window on a desktop, or press
Ctrl-C in the terminal you started it from).

Changing disks quickly:
- F10 (or Ctrl-Open-Apple-E, which is Ctrl-Left-Alt-E here) opens the
  file selector straight away.  Enter inserts the image and goes right
  back to the emulation; the slot is chosen by the kind of image
  (5.25" -> s6, 3.5" -> s5, hard disk -> s7).  ESC cancels.  In the
  selector, 5/6/7 forces a slot and 1/2 picks the drive.  Shift-F10
  ejects the last used drive.
- Drag a disk image onto the KEGS window (when running on a desktop):
  5.25" -> s6, 3.5" -> s5, larger images or a folder -> s7.  A second
  dropped file, or holding Alt, goes to drive 2.

The F7 debugger window is not supported by this driver.

Quick testing on a Raspberry Pi (Pi OS Lite card)
-------------------------------------------------

Keep a second SD card with Raspberry Pi OS Lite (64-bit) for trying out
changes without building a whole SD image.  One-time setup, on the Pi:

	sudo apt install -y git build-essential libsdl2-dev
	sudo usermod -aG video,input,audio $USER     (then log out/in)
	git clone -b udi https://github.com/udiweinstein/kegs.git ~/kegs
	cp <your ROM file> ~/kegs/ROM

After pushing changes from the Mac, on the Pi:

	~/kegs/pi-update.sh
	~/kegs/sdlkegs

pi-update.sh pulls from GitHub and rebuilds (about a minute on a Pi 4).
Run sdlkegs from the Pi's own screen and keyboard, not over ssh.

Network: Wi-Fi and file sharing (F4 -> Network Configuration)
-------------------------------------------------------------

Linux only.  The menu lets you pick a Wi-Fi network (Scan, then Enter
on a network), type its password, Connect, and switch SMB file sharing
of the disks folder On/Off.  It shows the IP addresses and the address
to type on the other computer:
	Windows:  \\<ip>\disks        (File Explorer address bar)
	Mac:      smb://<ip>/disks    (Finder: Go -> Connect to Server)
	user: kegs   password: apple2
"Shared folder" is relative to the folder of config.kegs (default:
disks).  Use "Save changes to config.kegs" in the main F4 menu to keep
the settings.

The work is done by src/kegs-net.sh in the background.  On Raspberry Pi
OS run this once first (installs ksmbd-tools, allows starting the file
server, sets the Wi-Fi country):
	~/kegs/pi-network-setup.sh
On the Buildroot SD image everything is already included.
