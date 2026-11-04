// basic_ext.c - New Applesoft BASIC commands for KEGS (Udi's fork)
//
// How it works: Applesoft sends any statement it doesn't recognize to LET
//  ($DA46).  config.c patches the ROM so LET starts with WDM $56,$00, which
//  lands in basic_let_trap().  If the statement is one of ours, we handle
//  it here; otherwise we do LET's original first instruction (JSR PTRGET).
//
// To evaluate expressions, we call Applesoft's own FRMNUM with a return
//  address pointing back at the WDM, so we get control again afterwards
//  (g_bx_state says where to continue).
//
// Applesoft tokenizes keywords anywhere in a word, so "SHGR" is stored as
//  'S' + token HGR, "SHPLOT" as 'S' + HPLOT, "SHCOLOR=" as 'S' + HCOLOR=.
//
// Commands:
//  VER                       print the version
//  SHGR                      Super Hi-Res 320x200 on, clear, default palette
//  SHCOLOR= n                color 0-15
//  SHPLOT x,y [TO x,y ...]   x 0-319, y 0-199
//  SHPLOT TO x,y [TO ...]    continue from the last point
//  TEXT                      (Applesoft's own) also turns Super Hi-Res off

#include "defc.h"
#include <math.h>

extern Engine_reg engine;
extern int g_cur_a2_stat;
extern word32 g_c029_val_some;
extern dword64 g_cur_dfcyc;

#define BX_LET_ADDR	0xda46		// Applesoft LET (patched with WDM)
#define BX_PTRGET	0xdfe3		// LET's original JSR target
#define BX_FRMNUM	0xdd67		// Evaluate numeric expression -> FAC
#define BX_SYNERR	0xdec9		// ?SYNTAX ERROR
#define BX_IQERR	0xe199		// ?ILLEGAL QUANTITY ERROR
#define BX_COUT		0xfded		// Monitor character output

#define TOK_HGR		0x91
#define TOK_HCOLOR	0x92
#define TOK_HPLOT	0x93
#define TOK_TO		0xc1

#define SHR_PIXELS	0xe12000
#define SHR_SCB		0xe19d00
#define SHR_PALETTE	0xe19e00

enum { BX_IDLE = 0, BX_VER_OUT, BX_COLOR, BX_PLOT_X, BX_PLOT_Y };

static const char g_bx_ver_str[] = "1.0\r";

// Default IIgs 320-mode palette, $0RGB
static const word32 g_bx_palette[16] = {
	0x000, 0x777, 0x841, 0x72c, 0x00f, 0x080, 0xf70, 0xd00,
	0xfa9, 0xff0, 0x0e0, 0x4df, 0xdaf, 0x78f, 0xccc, 0xfff
};

int	g_basic_shr_on = 0;		// SHR was turned on by SHGR

static int	g_bx_state = BX_IDLE;
static word32	g_bx_wait_stack = 0;	// Stack ptr when our ROM call returns
static int	g_bx_out_pos = 0;
static int	g_bx_plot_line = 0;	// Draw a line from the last point
static int	g_bx_plot_x = 0;
static int	g_bx_last_x = 0;
static int	g_bx_last_y = 0;
static int	g_bx_color = 15;

// ---- 6502 helpers ----

static void
bx_push8(word32 val)
{
	set_memory_c(engine.stack, val & 0xff, 1);
	engine.stack = (engine.stack - 1) & 0xffff;
	if(engine.psr & 0x100) {
		engine.stack = 0x100 | (engine.stack & 0xff);
	}
}

static word32
bx_pull8()
{
	engine.stack = (engine.stack + 1) & 0xffff;
	if(engine.psr & 0x100) {
		engine.stack = 0x100 | (engine.stack & 0xff);
	}
	return get_memory_c(engine.stack);
}

static void
bx_jump(word32 addr)
{
	engine.kpc = (engine.kpc & 0xff0000) | (addr & 0xffff);
}

// JSR to a ROM routine; its RTS comes back to the WDM at $DA46
static void
bx_call(word32 addr, int next_state)
{
	g_bx_wait_stack = engine.stack;
	bx_push8((BX_LET_ADDR - 1) >> 8);
	bx_push8((BX_LET_ADDR - 1) & 0xff);
	g_bx_state = next_state;
	bx_jump(addr);
}

// Act like LET's RTS: back to Applesoft's statement loop
static void
bx_return()
{
	word32	addr;

	g_bx_state = BX_IDLE;
	addr = bx_pull8();
	addr |= bx_pull8() << 8;
	bx_jump(addr + 1);
}

static void
bx_error(word32 addr)
{
	g_bx_state = BX_IDLE;
	bx_jump(addr);
}

static word32
bx_txtptr()
{
	return get_memory16_c(0xb8);
}

static void
bx_set_txtptr(word32 addr)
{
	set_memory_c(0xb8, addr & 0xff, 1);
	set_memory_c(0xb9, (addr >> 8) & 0xff, 1);
}

static word32
bx_cur_char()
{
	return get_memory_c(bx_txtptr());
}

static void
bx_skip(int num)
{
	bx_set_txtptr(bx_txtptr() + num);
}

static int
bx_upper(word32 c)
{
	c = c & 0x7f;
	if((c >= 'a') && (c <= 'z')) {
		c = c - 0x20;
	}
	return (int)c;
}

// Statement must end here; then return to Applesoft
static void
bx_end_statement()
{
	word32	c;

	c = bx_cur_char();
	if((c == 0) || (c == ':')) {
		bx_return();
	} else {
		bx_error(BX_SYNERR);
	}
}

// Get FAC ($9D-$A2, Applesoft 5-byte float) as an int in 0..limit-1.
//  Returns -1 if out of range
static int
bx_fac_int(int limit)
{
	double	val;
	word32	expon, mant;

	expon = get_memory_c(0x9d);
	if(expon == 0) {
		return 0;
	}
	mant = (get_memory_c(0x9e) << 24) | (get_memory_c(0x9f) << 16) |
			(get_memory_c(0xa0) << 8) | get_memory_c(0xa1);
	val = ldexp((double)mant, (int)expon - 0x80 - 32);
	if(get_memory_c(0xa2) & 0x80) {
		val = -val;
	}
	if((val < 0.0) || (val >= (double)limit)) {
		return -1;
	}
	return (int)val;
}

// ---- Super Hi-Res ----

void
basic_shr_set(int on)
{
	int	new_stat;

	g_basic_shr_on = on;
	new_stat = g_cur_a2_stat & (~0xa0);
	if(on) {
		new_stat |= 0x80;		// $C029 bit 7: SHR on
		g_c029_val_some |= 0x40;	// $C029 bit 6: linear video
	}
	if(new_stat != g_cur_a2_stat) {
		g_cur_a2_stat = new_stat;
		change_display_mode(g_cur_dfcyc);
	}
}

static void
bx_shgr()
{
	word32	rgb;
	int	i;

	for(i = 0; i < 200; i++) {
		set_memory_c(SHR_SCB + i, 0x00, 0);	// 320 mode, palette 0
	}
	for(i = 0; i < 16; i++) {
		rgb = g_bx_palette[i];
		set_memory_c(SHR_PALETTE + 2*i, rgb & 0xff, 0);
		set_memory_c(SHR_PALETTE + 2*i + 1, (rgb >> 8) & 0x0f, 0);
	}
	for(i = 0; i < 200*160; i++) {
		set_memory_c(SHR_PIXELS + i, 0x00, 0);
	}
	basic_shr_set(1);
}

static void
bx_pixel(int x, int y)
{
	word32	addr, val;

	addr = SHR_PIXELS + y*160 + (x >> 1);
	val = get_memory_c(addr);
	if(x & 1) {
		val = (val & 0xf0) | g_bx_color;
	} else {
		val = (val & 0x0f) | (g_bx_color << 4);
	}
	set_memory_c(addr, val, 0);
}

static void
bx_line(int x0, int y0, int x1, int y1)
{
	int	dx, dy, sx, sy, err, e2;

	dx = abs(x1 - x0);
	dy = -abs(y1 - y0);
	sx = (x0 < x1) ? 1 : -1;
	sy = (y0 < y1) ? 1 : -1;
	err = dx + dy;
	while(1) {
		bx_pixel(x0, y0);
		if((x0 == x1) && (y0 == y1)) {
			break;
		}
		e2 = 2*err;
		if(e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if(e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

// ---- The trap ----

static void
bx_ver_out_next()
{
	int	c;

	c = g_bx_ver_str[g_bx_out_pos++];
	if(c == 0) {
		bx_return();
		return;
	}
	engine.acc = (engine.acc & 0xff00) | 0x80 | c;
	bx_call(BX_COUT, BX_VER_OUT);
}

static void
bx_continue()
{
	int	val;

	switch(g_bx_state) {
	case BX_VER_OUT:
		bx_ver_out_next();
		return;
	case BX_COLOR:
		val = bx_fac_int(16);
		if(val < 0) {
			bx_error(BX_IQERR);
			return;
		}
		g_bx_color = val;
		bx_end_statement();
		return;
	case BX_PLOT_X:
		val = bx_fac_int(320);
		if(val < 0) {
			bx_error(BX_IQERR);
			return;
		}
		g_bx_plot_x = val;
		if(bx_cur_char() != ',') {
			bx_error(BX_SYNERR);
			return;
		}
		bx_skip(1);
		bx_call(BX_FRMNUM, BX_PLOT_Y);
		return;
	case BX_PLOT_Y:
		val = bx_fac_int(200);
		if(val < 0) {
			bx_error(BX_IQERR);
			return;
		}
		if(g_bx_plot_line) {
			bx_line(g_bx_last_x, g_bx_last_y, g_bx_plot_x, val);
		} else {
			bx_pixel(g_bx_plot_x, val);
		}
		g_bx_last_x = g_bx_plot_x;
		g_bx_last_y = val;
		if(bx_cur_char() == TOK_TO) {
			bx_skip(1);
			g_bx_plot_line = 1;
			bx_call(BX_FRMNUM, BX_PLOT_X);
			return;
		}
		bx_end_statement();
		return;
	}
	bx_error(BX_SYNERR);
}

// Called from do_wdm() for WDM $56,$00 at Applesoft LET ($DA46)
void
basic_let_trap()
{
	word32	txt, c0, c1, c3;

	if(g_bx_state != BX_IDLE) {
		if(engine.stack == g_bx_wait_stack) {
			bx_continue();
			return;
		}
		// Stale: an Applesoft error happened inside our ROM call
		g_bx_state = BX_IDLE;
	}

	// TXTPTR points to the first char of the statement
	txt = bx_txtptr();
	c0 = bx_upper(get_memory_c(txt));
	c1 = get_memory_c(txt + 1);
	c3 = get_memory_c(txt + 3);

	if((c0 == 'V') && (bx_upper(c1) == 'E') &&
			(bx_upper(get_memory_c(txt + 2)) == 'R') &&
			((c3 == 0) || (c3 == ':'))) {
		bx_skip(3);
		g_bx_out_pos = 0;
		bx_ver_out_next();
		return;
	}
	if(c0 == 'S') {
		switch(c1) {
		case TOK_HGR:			// SHGR
			bx_skip(2);
			bx_shgr();
			bx_end_statement();
			return;
		case TOK_HCOLOR:		// SHCOLOR=
			bx_skip(2);
			bx_call(BX_FRMNUM, BX_COLOR);
			return;
		case TOK_HPLOT:			// SHPLOT
			bx_skip(2);
			g_bx_plot_line = 0;
			if(bx_cur_char() == TOK_TO) {
				bx_skip(1);
				g_bx_plot_line = 1;
			}
			bx_call(BX_FRMNUM, BX_PLOT_X);
			return;
		}
	}

	// Not ours: do LET's original JSR PTRGET, returning to $DA49
	bx_push8((BX_LET_ADDR + 2) >> 8);
	bx_push8((BX_LET_ADDR + 2) & 0xff);
	bx_jump(BX_PTRGET);
}
