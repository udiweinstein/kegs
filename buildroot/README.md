# KEGS for Raspberry Pi 4 – boots straight into the emulator

This directory is a [Buildroot](https://buildroot.org) external tree.  It
builds a small Linux system for the Raspberry Pi 4 that does one thing:
start KEGS full screen on HDMI a few seconds after power-on.  No desktop,
no X11, no PulseAudio, no login.

* **Display:** SDL2 on KMS/DRM (Mesa v3d driver).
* **Sound:** SDL2 → ALSA, HDMI by default, the 3.5 mm jack optionally.
* **Keyboard/mouse:** any USB keyboard and mouse.
* **Root filesystem is read-only**, so cutting the power can't damage the
  system.  Only the KEGS partition is ever written.

## SD card layout

| # | Label | Type | Contents |
|---|-------|------|----------|
| 1 | BOOT  | FAT  | Pi firmware, kernel, `config.txt` |
| 2 | –     | ext4 | the system (read-only) |
| 3 | KEGS  | FAT  | your ROM, `config.kegs`, `disks/` – edit it from your Mac |

## Getting the image

**From GitHub (easiest):** push a tag named `pi-v…`:

    git tag pi-v2 && git push origin pi-v2

After about 1½ hours the image appears under *Releases*.

**Building it yourself** needs Linux (on a Mac use a Debian/Ubuntu VM or
Docker container) and about 15 GB of disk:

    git clone -b 2026.02.3 https://github.com/buildroot/buildroot.git br
    make -C br O=$PWD/out BR2_EXTERNAL=$PWD/kegs/buildroot kegs_rpi4_defconfig
    make -C out
    # result: out/images/sdcard.img

## Using it

1. Write the image to a micro-SD card (Raspberry Pi Imager → *Use custom*,
   or balenaEtcher).
2. Put the card back in the Mac.  On the **KEGS** volume copy your IIgs ROM
   file as `ROM` (or `ROM.01` / `ROM.03`) and your disk images into
   `disks/`.
3. Boot the Pi.  Use **F4** for the KEGS menu (insert disks, settings) and
   **F10** for a quick disk swap.

Optional `kegs.env` on the KEGS volume (one `KEY=value` per line):

    AUDIO=Headphones        # 3.5 mm jack instead of HDMI (vc4hdmi0)
    KEGS_ARGS=-audio 0      # extra command line options for KEGS
    SDL_VIDEO_KMSDRM_DEVICE_INDEX=1   # any SDL_* variable is passed on

If KEGS quits it is restarted automatically.  Nothing is shown on the
screen while it starts, except a message when there is no ROM file.  Its output goes to
`kegs.log` on the KEGS partition, and `boot-log.txt` there has the kernel
messages and display status of the last start.  For debugging there is a root login (no password) on
the serial port (GPIO 14/15, 115200 baud).

## Files

* `configs/kegs_rpi4_defconfig` – the Buildroot configuration
* `package/kegs/` – builds `sdlkegs` from this repository's `src/`
* `board/rpi4/` – `config.txt`, kernel command line, partition layout,
  start-up script (`rootfs-overlay/usr/bin/kegs-start`)

## Building on a Mac (fast rebuilds)

Install [OrbStack](https://orbstack.dev) (or Docker Desktop) once and turn
off its "Start at login".  Then, from the repository folder:

    ./buildroot/build-on-mac.sh

The first build takes about 30-45 minutes; after a KEGS change only KEGS
is rebuilt (1-2 minutes).  The image is written to
`sdcard/kegs-pi4-sdcard.img`.  The script starts OrbStack if needed and
quits it again at the end.  `./buildroot/build-on-mac.sh clean` deletes
the build files (~15-20 GB).
