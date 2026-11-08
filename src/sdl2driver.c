/************************************************************************/
/*			KEGS: Apple //gs Emulator			*/
/*			Copyright 2002-2023 by Kent Dickey		*/
/*									*/
/*	This code is covered by the GNU GPL v3				*/
/*	See the file COPYING.txt or https://www.gnu.org/licenses/	*/
/*	This program is provided with no warranty			*/
/*									*/
/*	The KEGS web page is kegs.sourceforge.net			*/
/*	You may contact the author at: kadickey@alumni.princeton.edu	*/
/************************************************************************/

// SDL2 video/keyboard/mouse driver for KEGS.
// Lets KEGS run without X11: e.g. on a Raspberry Pi console using the
//  SDL2 KMSDRM backend, or under Wayland/X11 via SDL.
// Modeled after xdriver.c.  Sound still uses pulseaudio_driver.c.

#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include "defc.h"

extern int g_audio_enable;
extern int Verbose;

typedef struct sdl_window_info {
	SDL_Window	*win;
	SDL_Renderer	*renderer;
	SDL_Texture	*texture;
	Kimage		*kimage_ptr;
	word32		*pixels;
	const char	*name_str;
	int		active;
	int		width;
	int		height;
	int		pixels_per_line;
} Sdl_window_info;

#include "protos_sdl2driver.h"

Sdl_window_info g_sdl_mainwin = { 0 };

int	g_sdl_warp_pointer = 0;
int	g_sdl_hide_pointer = 0;
int	g_sdl_fullscreen = 0;
int	g_sdl_is_kmsdrm = 0;
int	g_sdl_mouse_buttons = 0;
int	g_sdl_quit_requested = 0;

// SDL scancode (physical key position) -> Apple ADB keycode.
//  Using scancodes means the emulated keyboard follows the physical
//  US layout regardless of the host keyboard layout (Hebrew, etc).
// Note: like xdriver.c, Left-Alt is sent as "Option" and Right-Alt as
//  "Command"; adb.c swaps them by default on Linux
//  (g_adb_swap_command_option) so Left-Alt acts as Open-Apple.
const int g_sdl_a2_key_to_scancode[][2] = {
	{ 0x35, SDL_SCANCODE_ESCAPE },
	{ 0x7a, SDL_SCANCODE_F1 },
	{ 0x78, SDL_SCANCODE_F2 },
	{ 0x63, SDL_SCANCODE_F3 },
	{ 0x76, SDL_SCANCODE_F4 },
	{ 0x60, SDL_SCANCODE_F5 },
	{ 0x61, SDL_SCANCODE_F6 },
	{ 0x62, SDL_SCANCODE_F7 },
	{ 0x64, SDL_SCANCODE_F8 },
	{ 0x65, SDL_SCANCODE_F9 },
	{ 0x6d, SDL_SCANCODE_F10 },
	{ 0x67, SDL_SCANCODE_F11 },
	{ 0x6f, SDL_SCANCODE_F12 },
	{ 0x69, SDL_SCANCODE_F13 },
	{ 0x6b, SDL_SCANCODE_F14 },
	{ 0x71, SDL_SCANCODE_F15 },
	{ 0x7f, SDL_SCANCODE_PAUSE },
	{ 0x32, SDL_SCANCODE_GRAVE },
	{ 0x12, SDL_SCANCODE_1 },
	{ 0x13, SDL_SCANCODE_2 },
	{ 0x14, SDL_SCANCODE_3 },
	{ 0x15, SDL_SCANCODE_4 },
	{ 0x17, SDL_SCANCODE_5 },
	{ 0x16, SDL_SCANCODE_6 },
	{ 0x1a, SDL_SCANCODE_7 },
	{ 0x1c, SDL_SCANCODE_8 },
	{ 0x19, SDL_SCANCODE_9 },
	{ 0x1d, SDL_SCANCODE_0 },
	{ 0x1b, SDL_SCANCODE_MINUS },
	{ 0x18, SDL_SCANCODE_EQUALS },
	{ 0x33, SDL_SCANCODE_BACKSPACE },
	{ 0x72, SDL_SCANCODE_INSERT },		// Help
	{ 0x74, SDL_SCANCODE_PAGEUP },
	{ 0x47, SDL_SCANCODE_NUMLOCKCLEAR },	// Clear
	{ 0x51, SDL_SCANCODE_HOME },		// alias Home to KP_Equal
	{ 0x51, SDL_SCANCODE_KP_EQUALS },
	{ 0x4b, SDL_SCANCODE_KP_DIVIDE },
	{ 0x43, SDL_SCANCODE_KP_MULTIPLY },
	{ 0x30, SDL_SCANCODE_TAB },
	{ 0x0c, SDL_SCANCODE_Q },
	{ 0x0d, SDL_SCANCODE_W },
	{ 0x0e, SDL_SCANCODE_E },
	{ 0x0f, SDL_SCANCODE_R },
	{ 0x11, SDL_SCANCODE_T },
	{ 0x10, SDL_SCANCODE_Y },
	{ 0x20, SDL_SCANCODE_U },
	{ 0x22, SDL_SCANCODE_I },
	{ 0x1f, SDL_SCANCODE_O },
	{ 0x23, SDL_SCANCODE_P },
	{ 0x21, SDL_SCANCODE_LEFTBRACKET },
	{ 0x1e, SDL_SCANCODE_RIGHTBRACKET },
	{ 0x2a, SDL_SCANCODE_BACKSLASH },
	{ 0x75, SDL_SCANCODE_DELETE },
	{ 0x77, SDL_SCANCODE_END },
	{ 0x79, SDL_SCANCODE_PAGEDOWN },
	{ 0x59, SDL_SCANCODE_KP_7 },
	{ 0x5b, SDL_SCANCODE_KP_8 },
	{ 0x5c, SDL_SCANCODE_KP_9 },
	{ 0x4e, SDL_SCANCODE_KP_MINUS },
	{ 0x39, SDL_SCANCODE_CAPSLOCK },
	{ 0x00, SDL_SCANCODE_A },
	{ 0x01, SDL_SCANCODE_S },
	{ 0x02, SDL_SCANCODE_D },
	{ 0x03, SDL_SCANCODE_F },
	{ 0x05, SDL_SCANCODE_G },
	{ 0x04, SDL_SCANCODE_H },
	{ 0x26, SDL_SCANCODE_J },
	{ 0x28, SDL_SCANCODE_K },
	{ 0x25, SDL_SCANCODE_L },
	{ 0x29, SDL_SCANCODE_SEMICOLON },
	{ 0x27, SDL_SCANCODE_APOSTROPHE },
	{ 0x24, SDL_SCANCODE_RETURN },
	{ 0x56, SDL_SCANCODE_KP_4 },
	{ 0x57, SDL_SCANCODE_KP_5 },
	{ 0x58, SDL_SCANCODE_KP_6 },
	{ 0x45, SDL_SCANCODE_KP_PLUS },
	{ 0x38, SDL_SCANCODE_LSHIFT },
	{ 0x38, SDL_SCANCODE_RSHIFT },
	{ 0x06, SDL_SCANCODE_Z },
	{ 0x07, SDL_SCANCODE_X },
	{ 0x08, SDL_SCANCODE_C },
	{ 0x09, SDL_SCANCODE_V },
	{ 0x0b, SDL_SCANCODE_B },
	{ 0x2d, SDL_SCANCODE_N },
	{ 0x2e, SDL_SCANCODE_M },
	{ 0x2b, SDL_SCANCODE_COMMA },
	{ 0x2f, SDL_SCANCODE_PERIOD },
	{ 0x2c, SDL_SCANCODE_SLASH },
	{ 0x3e, SDL_SCANCODE_UP },
	{ 0x53, SDL_SCANCODE_KP_1 },
	{ 0x54, SDL_SCANCODE_KP_2 },
	{ 0x55, SDL_SCANCODE_KP_3 },
	{ 0x36, SDL_SCANCODE_LCTRL },
	{ 0x36, SDL_SCANCODE_RCTRL },
	{ 0x3a, SDL_SCANCODE_LALT },		// Option (swapped->Cmd)
	{ 0x3a, SDL_SCANCODE_RGUI },
	{ 0x37, SDL_SCANCODE_RALT },		// Command (swapped->Option)
	{ 0x37, SDL_SCANCODE_LGUI },
	{ 0x37, SDL_SCANCODE_APPLICATION },
	{ 0x31, SDL_SCANCODE_SPACE },
	{ 0x3b, SDL_SCANCODE_LEFT },
	{ 0x3d, SDL_SCANCODE_DOWN },
	{ 0x3c, SDL_SCANCODE_RIGHT },
	{ 0x52, SDL_SCANCODE_KP_0 },
	{ 0x41, SDL_SCANCODE_KP_PERIOD },
	{ 0x4c, SDL_SCANCODE_KP_ENTER },
	{ -1, -1 }
};

int	g_sdl_scancode_to_a2[SDL_NUM_SCANCODES];

int
main(int argc, char **argv)
{
	int	ret;

	ret = parse_argv(argc, argv, 1);
	if(ret) {
		printf("kegsmain ret: %d, stopping\n", ret);
		exit(1);
	}

	sdl_video_init_sdl();

	// SDL texture is ARGB8888: set masks before kegs_init so the
	//  palettes are built correctly
	video_set_alpha_mask(0xff000000);
	video_set_red_mask(0x00ff0000);
	video_set_green_mask(0x0000ff00);
	video_set_blue_mask(0x000000ff);
	video_set_palette();

	ret = kegs_init(32);
	printf("kegs_init done\n");
	if(ret) {
		printf("kegs_init ret: %d, stopping\n", ret);
		exit(1);
	}
	sdl_video_init();

	// Main loop: run_16ms() runs one 60Hz video frame of the emulation
	while(1) {
		ret = run_16ms();
		if(ret != 0) {
			printf("run_16ms returned: %d\n", ret);
			break;
		}
		if(sdl_input_events()) {
			break;
		}
		sdl_update_display(&g_sdl_mainwin);
		sdl_check_debugwin();
	}
	xdriver_end();
	exit(0);
}

void
xdriver_end()
{
	static int g_sdl_ended = 0;

	if(g_sdl_ended) {
		return;
	}
	g_sdl_ended = 1;
	printf("sdl2driver_end\n");
	SDL_SetRelativeMouseMode(SDL_FALSE);
	SDL_ShowCursor(SDL_ENABLE);
	SDL_Quit();
}

void
sdl_video_init_sdl()
{
	const char *drv;

	SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");	// nearest
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		exit(1);
	}
	atexit(xdriver_end);
	drv = SDL_GetCurrentVideoDriver();
	printf("SDL video driver: %s\n", drv ? drv : "(none)");
	if(drv && (strcmp(drv, "KMSDRM") == 0)) {
		g_sdl_is_kmsdrm = 1;
		g_sdl_fullscreen = 1;	// Console: always full screen
	}
	if(getenv("KEGS_FULLSCREEN")) {
		g_sdl_fullscreen = 1;
	}
}

void
sdl_video_init()
{
	int	i, a2code, sc;

	for(i = 0; i < SDL_NUM_SCANCODES; i++) {
		g_sdl_scancode_to_a2[i] = -1;
	}
	for(i = 0; g_sdl_a2_key_to_scancode[i][0] >= 0; i++) {
		a2code = g_sdl_a2_key_to_scancode[i][0];
		sc = g_sdl_a2_key_to_scancode[i][1];
		if((sc >= 0) && (sc < SDL_NUM_SCANCODES)) {
			g_sdl_scancode_to_a2[sc] = a2code;
		}
	}

	sdl_init_window(&g_sdl_mainwin, video_get_kimage(0), "KEGS");
	sdl_create_window(&g_sdl_mainwin);
}

void
sdl_init_window(Sdl_window_info *win_info_ptr, Kimage *kimage_ptr,
						const char *name_str)
{
	int	width, height;

	width = video_get_a2_width(kimage_ptr);
	height = video_get_a2_height(kimage_ptr);

	memset(win_info_ptr, 0, sizeof(*win_info_ptr));
	win_info_ptr->kimage_ptr = kimage_ptr;
	win_info_ptr->name_str = name_str;
	win_info_ptr->width = width;
	win_info_ptr->height = height;
	win_info_ptr->pixels_per_line = width;

	// We always render at native Apple size, SDL scales to the screen
	video_update_scale(kimage_ptr, width, height, 1);
}

void
sdl_create_window(Sdl_window_info *win_info_ptr)
{
	Uint32	flags;
	int	width, height, scale;

	width = win_info_ptr->width;
	height = win_info_ptr->height;
	flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if(g_sdl_fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	scale = 1;
	if(!g_sdl_fullscreen && (height < 600)) {
		scale = 2;
	}
	win_info_ptr->win = SDL_CreateWindow(win_info_ptr->name_str,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width * scale, height * scale, flags);
	if(!win_info_ptr->win) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n",
							SDL_GetError());
		exit(1);
	}
	win_info_ptr->renderer = SDL_CreateRenderer(win_info_ptr->win, -1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(!win_info_ptr->renderer) {
		printf("Accelerated renderer failed (%s), trying software\n",
							SDL_GetError());
		win_info_ptr->renderer = SDL_CreateRenderer(win_info_ptr->win,
					-1, SDL_RENDERER_SOFTWARE);
	}
	if(!win_info_ptr->renderer) {
		fprintf(stderr, "SDL_CreateRenderer failed: %s\n",
							SDL_GetError());
		exit(1);
	}
	SDL_RenderSetLogicalSize(win_info_ptr->renderer, width, height);
	win_info_ptr->texture = SDL_CreateTexture(win_info_ptr->renderer,
		SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		width, height);
	if(!win_info_ptr->texture) {
		fprintf(stderr, "SDL_CreateTexture failed: %s\n",
							SDL_GetError());
		exit(1);
	}
	win_info_ptr->pixels = calloc(1, (size_t)width * height * 4);
	if(!win_info_ptr->pixels) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}
	if(g_sdl_is_kmsdrm) {
		SDL_ShowCursor(SDL_DISABLE);
	}

	video_set_active(win_info_ptr->kimage_ptr, 1);
	win_info_ptr->active = 1;
	video_set_x_refresh_needed(win_info_ptr->kimage_ptr, 1);
	SDL_RaiseWindow(win_info_ptr->win);
}

void
sdl_update_display(Sdl_window_info *win_info_ptr)
{
	Change_rect rect;
	int	did_copy, valid;
	int	i;

	if(!win_info_ptr->active) {
		return;
	}
	if(video_change_aspect_needed(win_info_ptr->kimage_ptr,
				win_info_ptr->width, win_info_ptr->height)) {
		video_update_scale(win_info_ptr->kimage_ptr,
				win_info_ptr->width, win_info_ptr->height, 1);
		video_set_x_refresh_needed(win_info_ptr->kimage_ptr, 1);
	}
	did_copy = 0;
	for(i = 0; i < MAX_CHANGE_RECTS; i++) {
		valid = video_out_data(win_info_ptr->pixels,
				win_info_ptr->kimage_ptr,
				win_info_ptr->pixels_per_line, &rect, i);
		if(!valid) {
			break;
		}
		did_copy = 1;
	}
	if(did_copy) {
		// Simplest and fast enough: upload the whole frame
		SDL_UpdateTexture(win_info_ptr->texture, 0,
				win_info_ptr->pixels,
				win_info_ptr->pixels_per_line * 4);
		SDL_SetRenderDrawColor(win_info_ptr->renderer, 0, 0, 0, 255);
		SDL_RenderClear(win_info_ptr->renderer);
		SDL_RenderCopy(win_info_ptr->renderer, win_info_ptr->texture,
									0, 0);
		SDL_RenderPresent(win_info_ptr->renderer);
	}
}

void
sdl_check_debugwin()
{
	Kimage	*kimage_ptr;

	// The debugger window (F7) is not supported by this driver: run
	//  KEGS from a terminal and use the text debugger instead.
	kimage_ptr = video_get_kimage(1);
	if(video_get_active(kimage_ptr)) {
		printf("Debugger window is not supported in the SDL2 "
							"driver\n");
		video_set_active(kimage_ptr, 0);
	}
}

void
sdl_toggle_fullscreen()
{
	if(g_sdl_is_kmsdrm) {
		return;
	}
	g_sdl_fullscreen = !g_sdl_fullscreen;
	SDL_SetWindowFullscreen(g_sdl_mainwin.win,
			g_sdl_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	video_set_x_refresh_needed(g_sdl_mainwin.kimage_ptr, 1);
}

void
sdl_do_paste()
{
	char	*str;
	int	i;

	if(!SDL_HasClipboardText()) {
		return;
	}
	str = SDL_GetClipboardText();
	if(!str) {
		return;
	}
	for(i = 0; str[i]; i++) {
		if(adb_paste_add_buf((byte)str[i])) {
			printf("Paste buffer full!\n");
			break;
		}
	}
	SDL_free(str);
}

void
sdl_do_copy()
{
	char	*str;

	(void)cfg_text_screen_str();
	str = cfg_get_current_copy_selection();
	if(str) {
		SDL_SetClipboardText(str);
	}
}

void
sdl_update_modifier_state(Kimage *kimage_ptr)
{
	SDL_Keymod mod;
	word32	c025_val;

	mod = SDL_GetModState();
	c025_val = 0;
	if(mod & KMOD_SHIFT) {
		c025_val |= 1;
	}
	if(mod & KMOD_CTRL) {
		c025_val |= 2;
	}
	if(mod & KMOD_CAPS) {
		c025_val |= 4;
	}
	adb_update_c025_mask(kimage_ptr, c025_val, 7);
}

void
sdl_handle_key(SDL_KeyboardEvent *kev)
{
	Kimage	*kimage_ptr;
	int	sc, a2code, is_up;

	kimage_ptr = g_sdl_mainwin.kimage_ptr;
	is_up = (kev->type == SDL_KEYUP);
	if(kev->repeat) {
		return;		// KEGS does its own key repeat
	}
	sc = kev->keysym.scancode;
	sdl_update_modifier_state(kimage_ptr);
	if((sc == SDL_SCANCODE_F10) && !is_up &&
					(SDL_GetModState() & KMOD_CTRL)) {
		// Ctrl-F10: quit KEGS (there is no window to close on the
		//  console)
		g_sdl_quit_requested = 1;
		return;
	}
	if((sc == SDL_SCANCODE_F11) && !is_up) {
		sdl_toggle_fullscreen();
		return;
	}
	if((sc < 0) || (sc >= SDL_NUM_SCANCODES)) {
		return;
	}
	a2code = g_sdl_scancode_to_a2[sc];
	if(a2code < 0) {
		vid_printf("Unknown SDL scancode %d (%s)\n", sc,
						SDL_GetScancodeName(sc));
		return;
	}
	adb_physical_key_update(kimage_ptr, a2code, 0, is_up);
}

int
sdl_buttons_from_state(Uint32 state)
{
	int	buttons;

	buttons = 0;
	if(state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
		buttons |= 1;
	}
	if(state & SDL_BUTTON(SDL_BUTTON_MIDDLE)) {
		buttons |= 2;
	}
	if(state & SDL_BUTTON(SDL_BUTTON_RIGHT)) {
		buttons |= 4;
	}
	return buttons;
}

void
sdl_update_mouse_abs(int raw_x, int raw_y, int buttons, int valid)
{
	Kimage	*kimage_ptr;
	int	x, y;

	kimage_ptr = g_sdl_mainwin.kimage_ptr;
	x = video_scale_mouse_x(kimage_ptr, raw_x, 0);
	y = video_scale_mouse_y(kimage_ptr, raw_y, 0);
	adb_update_mouse(kimage_ptr, x, y, buttons, valid);
}

int
sdl_input_events()
{
	SDL_Event ev;
	Kimage	*kimage_ptr;
	int	key_or_mouse, buttons, hide, warp, quit;

	kimage_ptr = g_sdl_mainwin.kimage_ptr;
	if(adb_get_copy_requested()) {
		sdl_do_copy();
	}
	key_or_mouse = 0;
	quit = 0;
	while(SDL_PollEvent(&ev)) {
		switch(ev.type) {
		case SDL_QUIT:
			quit = 1;
			break;
		case SDL_WINDOWEVENT:
			switch(ev.window.event) {
			case SDL_WINDOWEVENT_FOCUS_LOST:
				adb_kbd_repeat_off();
				g_adb_mainwin_has_focus_set(0);
				break;
			case SDL_WINDOWEVENT_FOCUS_GAINED:
				g_adb_mainwin_has_focus_set(1);
				break;
			case SDL_WINDOWEVENT_EXPOSED:
			case SDL_WINDOWEVENT_SIZE_CHANGED:
			case SDL_WINDOWEVENT_RESTORED:
				video_set_x_refresh_needed(kimage_ptr, 1);
				break;
			}
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			sdl_handle_key(&ev.key);
			key_or_mouse = 1;
			break;
		case SDL_MOUSEMOTION:
			buttons = sdl_buttons_from_state(ev.motion.state);
			if(g_sdl_warp_pointer) {
				// Relative mode: send deltas
				adb_update_mouse(kimage_ptr, ev.motion.xrel,
					ev.motion.yrel, buttons, 0x1000 | 7);
			} else {
				sdl_update_mouse_abs(ev.motion.x, ev.motion.y,
								buttons, 7);
			}
			g_sdl_mouse_buttons = buttons;
			key_or_mouse = 1;
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			buttons = sdl_buttons_from_state(SDL_GetMouseState(0,
									0));
			if((ev.type == SDL_MOUSEBUTTONDOWN) &&
				(ev.button.button == SDL_BUTTON_MIDDLE)) {
				sdl_do_paste();
			}
			buttons &= ~2;
			if(g_sdl_warp_pointer) {
				adb_update_mouse(kimage_ptr, 0, 0, buttons,
								0x1000 | 7);
			} else {
				sdl_update_mouse_abs(ev.button.x, ev.button.y,
								buttons, 7);
			}
			g_sdl_mouse_buttons = buttons;
			key_or_mouse = 1;
			break;
		default:
			break;
		}
	}

	if(key_or_mouse) {
		hide = adb_get_hide_warp_info(kimage_ptr, &warp);
		if(warp != g_sdl_warp_pointer) {
			g_sdl_warp_pointer = warp;
			SDL_SetRelativeMouseMode(warp ? SDL_TRUE : SDL_FALSE);
			if(warp) {
				// Recenter, this is not real motion
				adb_update_mouse(kimage_ptr, 0, 0, 0, -1);
			}
		}
		if(!g_sdl_is_kmsdrm && (hide != g_sdl_hide_pointer)) {
			SDL_ShowCursor(hide ? SDL_DISABLE : SDL_ENABLE);
		}
		g_sdl_hide_pointer = hide;
	}

	return quit | g_sdl_quit_requested;
}

void
g_adb_mainwin_has_focus_set(int has_focus)
{
	extern int g_adb_mainwin_has_focus;

	g_adb_mainwin_has_focus = has_focus;
}
