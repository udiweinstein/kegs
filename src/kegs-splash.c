/************************************************************************/
/*			KEGS: Apple //gs Emulator			*/
/*	This code is covered by the GNU GPL v3				*/
/*	See the file COPYING.txt or https://www.gnu.org/licenses/	*/
/************************************************************************/

// Raspberry Pi SD image: show the saved IIgs start-up screen on the
//  Linux framebuffer while the system boots, before KEGS takes over.
//  The picture is saved by KEGS itself (sdl_maybe_save_splash), so it is
//  exactly what KEGS shows, scaled the same way (fit, centered, black
//  bars, nearest pixel).
//
//	kegs-splash FILE	draw it on /dev/fb0 now
//	kegs-splash -w FILE	wait (up to 15 s) for the vc4 display driver
//				to replace the boot framebuffer, then draw
//				it again

#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static uint32_t *g_img;
static uint32_t g_w, g_h;

static int
load(const char *path)
{
	FILE	*fp;
	uint32_t hdr[3];
	size_t	size;

	fp = fopen(path, "rb");
	if(!fp) {
		return 0;
	}
	if((fread(hdr, 1, 12, fp) != 12) || memcmp(&hdr[0], "KSPL", 4) ||
			(hdr[1] == 0) || (hdr[2] == 0) || (hdr[1] > 4096) ||
			(hdr[2] > 4096)) {
		fclose(fp);
		return 0;
	}
	g_w = hdr[1];
	g_h = hdr[2];
	size = (size_t)g_w * g_h * 4;
	g_img = malloc(size);
	if(!g_img || (fread(g_img, 1, size, fp) != size)) {
		fclose(fp);
		return 0;
	}
	fclose(fp);
	return 1;
}

static int
fb_is_vc4()
{
	char	buf[256];
	FILE	*fp;
	int	ret;

	fp = fopen("/proc/fb", "r");
	if(!fp) {
		return 0;
	}
	ret = 0;
	while(fgets(buf, sizeof(buf), fp)) {
		if(!strncmp(buf, "0 ", 2) && strstr(buf, "vc4")) {
			ret = 1;
		}
	}
	fclose(fp);
	return ret;
}

static int
draw()
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	uint8_t	*fb, *line;
	uint32_t c, sw, sh, dw, dh, x0, y0, x, y, bpp;
	size_t	len;
	int	fd;

	fd = open("/dev/fb0", O_RDWR);
	if(fd < 0) {
		return 0;
	}
	if(ioctl(fd, FBIOGET_VSCREENINFO, &var) ||
				ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
		close(fd);
		return 0;
	}
	bpp = var.bits_per_pixel;
	if((bpp != 32) && (bpp != 16)) {
		close(fd);
		return 0;
	}
	len = (size_t)fix.line_length * var.yres_virtual;
	fb = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if(fb == MAP_FAILED) {
		return 0;
	}
	sw = var.xres;
	sh = var.yres;
	// Fit inside the screen keeping the shape, like SDL's logical size
	if((uint64_t)sw * g_h <= (uint64_t)sh * g_w) {
		dw = sw;
		dh = (uint32_t)((uint64_t)g_h * sw / g_w);
	} else {
		dh = sh;
		dw = (uint32_t)((uint64_t)g_w * sh / g_h);
	}
	x0 = (sw - dw) / 2;
	y0 = (sh - dh) / 2;
	for(y = 0; y < sh; y++) {
		line = fb + (size_t)(y + var.yoffset) * fix.line_length +
					(size_t)var.xoffset * (bpp / 8);
		for(x = 0; x < sw; x++) {
			c = 0;
			if((x >= x0) && (x < x0 + dw) && (y >= y0) &&
							(y < y0 + dh)) {
				c = g_img[(uint64_t)(y - y0) * g_h / dh * g_w +
					(uint64_t)(x - x0) * g_w / dw];
			}
			if(bpp == 32) {
				((uint32_t *)line)[x] = c | 0xff000000;
			} else {
				((uint16_t *)line)[x] = (uint16_t)(
					((c >> 8) & 0xf800) |
					((c >> 5) & 0x07e0) |
					((c >> 3) & 0x001f));
			}
		}
	}
	munmap(fb, len);
	return 1;
}

int
main(int argc, char **argv)
{
	int	wait_vc4, i;

	wait_vc4 = 0;
	if((argc > 2) && !strcmp(argv[1], "-w")) {
		wait_vc4 = 1;
		argv++;
		argc--;
	}
	if((argc != 2) || !load(argv[1])) {
		return 1;
	}
	if(wait_vc4) {
		// The vc4 driver replaces the boot framebuffer (and blanks
		//  the screen) about a second after start-up
		for(i = 0; (i < 300) && !fb_is_vc4(); i++) {
			usleep(50000);
		}
		usleep(100000);		// let fbcon finish switching
	}
	return draw() ? 0 : 1;
}
