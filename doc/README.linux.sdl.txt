KEGS with SDL2 (no X11 needed) - e.g. Raspberry Pi OS Lite
==========================================================

The SDL2 driver (src/sdl2driver.c) lets KEGS run straight on the Linux
console using SDL2's KMSDRM backend, so no X server or desktop is
needed.  It also works under X11 or Wayland desktops.

Install build packages (Raspberry Pi OS / Debian / Ubuntu):

	sudo apt install build-essential libsdl2-dev libpulse-dev

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
- Sound uses PulseAudio.  On Pi OS Lite install it with
	sudo apt install pulseaudio      (or pipewire-pulse)
  or run with "-audio 0" to disable sound.

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
