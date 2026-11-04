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
//  Same for "DHGR", "DHPLOT", "DHCOLOR=" with 'D'.
//
// Commands:
//  VER                       print the version
//  SHGR                      Super Hi-Res 320x200 on, clear, default palette
//  SHCOLOR= n                color 0-15
//  SHPLOT x,y [TO x,y ...]   x 0-319, y 0-199
//  SHPLOT TO x,y [TO ...]    continue from the last point
//  DHGR                      Double Hi-Res 140x192 16 colors on (page 1), clear
//  DHCOLOR= n                color 0-15 (Lo-Res color numbers)
//  DHPLOT x,y [TO x,y ...]   x 0-139, y 0-191 (also DHPLOT TO x,y)
//  TEXT                      (Applesoft's own) also turns SHR / DHR off
//  SOUND f,d                 play f Hz for d/100 sec on the Ensoniq DOC
//                             (waits until done; f=0 is a rest)
//  VOLUME= v                 0-15 (default 10)
//  WAVE= w                   0 square, 1 sine, 2 triangle, 3 sawtooth

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
#define BX_WAIT		0xfca8		// Monitor delay loop

#define TOK_HGR		0x91
#define TOK_HCOLOR	0x92
#define TOK_HPLOT	0x93
#define TOK_TO		0xc1
#define TOK_EQUAL	0xd0

#define SHR_PIXELS	0xe12000
#define SHR_SCB		0xe19d00
#define SHR_PALETTE	0xe19e00

enum { BX_IDLE = 0, BX_VER_OUT, BX_COLOR, BX_PLOT_X, BX_PLOT_Y,
	BX_SOUND_F, BX_SOUND_D, BX_SOUND_WAIT, BX_VOLUME, BX_WAVE };

static const char g_bx_ver_str[] = "1.0\r";

// Default IIgs 320-mode palette, $0RGB
static const word32 g_bx_palette[16] = {
	0x000, 0x777, 0x841, 0x72c, 0x00f, 0x080, 0xf70, 0xd00,
	0xfa9, 0xff0, 0x0e0, 0x4df, 0xdaf, 0x78f, 0xccc, 0xfff
};

extern byte doc_ram[];
extern int g_doc_num_osc_en;
extern word32 g_doc_sound_ctl;

int	g_basic_shr_on = 0;		// SHR was turned on by SHGR
int	g_basic_dhr_on = 0;		// DHR was turned on by DHGR
static int	g_bx_dhr_saved_vid80 = 0;	// 80-col video before DHGR

enum { BX_MODE_SHR = 0, BX_MODE_DHR };
static int	g_bx_mode = BX_MODE_SHR;	// Which SH/DH command is running
static const int g_bx_max_x[2] = { 320, 140 };
static const int g_bx_max_y[2] = { 200, 192 };
static int	g_bx_colors[2] = { 15, 15 };	// SHCOLOR, DHCOLOR

static int	g_bx_state = BX_IDLE;
static word32	g_bx_wait_stack = 0;	// Stack ptr when our ROM call returns
static int	g_bx_out_pos = 0;
static int	g_bx_plot_line = 0;	// Draw a line from the last point
static int	g_bx_plot_x = 0;
static int	g_bx_last_x = 0;
static int	g_bx_last_y = 0;
static int	g_bx_sound_f = 0;
static int	g_bx_volume = 10;
static int	g_bx_wave = 0;
static dword64	g_bx_sound_end_dfcyc = 0;

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
	basic_text_hook();		// Undo DHGR if it is on
	basic_shr_set(1);
}

static void
bx_shr_pixel(int x, int y, int color)
{
	word32	addr, val;

	addr = SHR_PIXELS + y*160 + (x >> 1);
	val = get_memory_c(addr);
	if(x & 1) {
		val = (val & 0xf0) | color;
	} else {
		val = (val & 0x0f) | (color << 4);
	}
	set_memory_c(addr, val, 0);
}

// ---- Double Hi-Res ----
// Each line is 80 bytes, alternating aux ($E1) and main ($E0): aux0, main0,
//  aux1, main1...  Only bits 0-6 are shown, so a line is 560 bits.  Pixel
//  x (0-139) is bits 4x..4x+3, first bit = color bit 0.

#define DHR_MAIN	0xe02000
#define DHR_AUX		0xe12000

static word32
bx_hgr_line_offset(int y)
{
	return ((y & 7) << 10) | (((y >> 3) & 7) << 7) | ((y >> 6) * 40);
}

static void
bx_dhr_pixel(int x, int y, int color)
{
	word32	line, addr, val;
	int	i, bitpos, byte_idx, bit;

	line = bx_hgr_line_offset(y);
	for(i = 0; i < 4; i++) {
		bitpos = 4*x + i;
		byte_idx = bitpos / 7;
		bit = bitpos % 7;
		addr = ((byte_idx & 1) ? DHR_MAIN : DHR_AUX) + line +
							(byte_idx >> 1);
		val = get_memory_c(addr);
		if((color >> i) & 1) {
			val |= (1 << bit);
		} else {
			val &= ~(1 << bit);
		}
		set_memory_c(addr, val, 0);
	}
}

static void
bx_dhgr()
{
	int	i;

	for(i = 0; i < 0x2000; i++) {
		set_memory_c(DHR_MAIN + i, 0x00, 0);
		set_memory_c(DHR_AUX + i, 0x00, 0);
	}
	basic_shr_set(0);
	if(!g_basic_dhr_on) {
		g_bx_dhr_saved_vid80 = (g_cur_a2_stat & ALL_STAT_VID80) != 0;
	}
	g_basic_dhr_on = 1;
	set_memory_c(0xc050, 0, 1);		// Graphics
	set_memory_c(0xc052, 0, 1);		// Full screen
	set_memory_c(0xc054, 0, 1);		// Page 1
	set_memory_c(0xc057, 0, 1);		// Hi-Res
	set_memory_c(0xc00d, 0, 1);		// 80-col video on
	set_memory_c(0xc05e, 0, 1);		// AN3 off: Double Hi-Res on
}

// TEXT ($C051) was accessed: undo SHGR / DHGR
void
basic_text_hook()
{
	if(g_basic_shr_on) {
		basic_shr_set(0);
	}
	if(g_basic_dhr_on) {
		g_basic_dhr_on = 0;
		g_cur_a2_stat |= ALL_STAT_ANNUNC3;	// DHR off
		if(!g_bx_dhr_saved_vid80) {
			g_cur_a2_stat &= (~ALL_STAT_VID80);
		}
		change_display_mode(g_cur_dfcyc);
	}
}

static void
bx_pixel(int x, int y)
{
	if(g_bx_mode == BX_MODE_DHR) {
		bx_dhr_pixel(x, y, g_bx_colors[BX_MODE_DHR]);
	} else {
		bx_shr_pixel(x, y, g_bx_colors[BX_MODE_SHR]);
	}
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

// ---- Ensoniq DOC sound ----
// We use oscillator 0, playing a 256-byte wave at DOC RAM $FF00.

#define BX_DOC_OSC	0
#define BX_DOC_PAGE	0xff

// Write a DOC register through $C03C-$C03F, like a 65816 program would
static void
bx_doc_write(int reg, int val)
{
	set_memory_c(0xc03c, g_doc_sound_ctl & 0x0f, 1);	// Regs, no autoinc
	set_memory_c(0xc03e, reg & 0xff, 1);
	set_memory_c(0xc03f, 0, 1);
	set_memory_c(0xc03d, val & 0xff, 1);
}

static void
bx_doc_make_wave()
{
	double	dval;
	int	i, val;

	for(i = 0; i < 256; i++) {
		switch(g_bx_wave) {
		case 1:		// Sine
			dval = sin(2.0 * M_PI * i / 256.0);
			val = 128 + (int)(dval * 126.0);
			break;
		case 2:		// Triangle
			val = (i < 128) ? (2 + 2*i) : (2 + 2*(255 - i));
			break;
		case 3:		// Sawtooth
			val = 1 + (i * 254) / 255;
			break;
		default:	// Square
			val = (i < 128) ? 0xe0 : 0x20;
			break;
		}
		if(val < 1) {		// 0 would halt the oscillator
			val = 1;
		}
		if(val > 255) {
			val = 255;
		}
		doc_ram[(BX_DOC_PAGE << 8) + i] = val;
	}
}

static void
bx_sound_start(int freq)
{
	double	dupdate, dfc;
	int	res, fc;

	// Wave freq = FC * update_rate / 2^(17+res) for a 256-byte table,
	//  update_rate = 894886 Hz / (oscillators enabled + 2)
	dupdate = (DCYCS_28_MHZ / 32.0) / (double)(g_doc_num_osc_en + 2);
	res = 7;
	while(1) {
		dfc = (double)freq * (double)(1 << (17 + res)) / dupdate;
		if((dfc < 65536.0) || (res == 0)) {
			break;
		}
		res--;
	}
	fc = (int)(dfc + 0.5);
	if(fc > 0xffff) {
		fc = 0xffff;
	}
	bx_doc_make_wave();
	bx_doc_write(0xa0 + BX_DOC_OSC, 0x01);		// Halt while setting up
	bx_doc_write(0x00 + BX_DOC_OSC, fc & 0xff);
	bx_doc_write(0x20 + BX_DOC_OSC, fc >> 8);
	bx_doc_write(0x40 + BX_DOC_OSC, g_bx_volume * 17);
	bx_doc_write(0x80 + BX_DOC_OSC, BX_DOC_PAGE);
	bx_doc_write(0xc0 + BX_DOC_OSC, res);		// 256 bytes, bank 0
	bx_doc_write(0xa0 + BX_DOC_OSC, 0x00);		// Free run, channel 0
}

static void
bx_sound_stop()
{
	bx_doc_write(0xa0 + BX_DOC_OSC, 0x01);		// Halt
}

// Returns the length of word if the statement starts with it, else 0
static int
bx_match_word(word32 txt, const char *word)
{
	int	i;

	for(i = 0; word[i]; i++) {
		if(bx_upper(get_memory_c(txt + i)) != word[i]) {
			return 0;
		}
	}
	return i;
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
	word32	val2;
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
		g_bx_colors[g_bx_mode] = val;
		bx_end_statement();
		return;
	case BX_PLOT_X:
		val = bx_fac_int(g_bx_max_x[g_bx_mode]);
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
		val = bx_fac_int(g_bx_max_y[g_bx_mode]);
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
	case BX_SOUND_F:
		val = bx_fac_int(20001);
		if(val < 0) {
			bx_error(BX_IQERR);
			return;
		}
		g_bx_sound_f = val;
		if(bx_cur_char() != ',') {
			bx_error(BX_SYNERR);
			return;
		}
		bx_skip(1);
		bx_call(BX_FRMNUM, BX_SOUND_D);
		return;
	case BX_SOUND_D:
		val = bx_fac_int(65536);
		if(val < 0) {
			bx_error(BX_IQERR);
			return;
		}
		val2 = bx_cur_char();
		if((val2 != 0) && (val2 != ':')) {
			bx_error(BX_SYNERR);
			return;
		}
		if(g_bx_sound_f > 0) {
			bx_sound_start(g_bx_sound_f);
		}
		g_bx_sound_end_dfcyc = g_cur_dfcyc +
			((dword64)(val * (DCYCS_1_MHZ / 100.0)) << 16);
		// Fall through: start waiting
	case BX_SOUND_WAIT:
		if(g_cur_dfcyc < g_bx_sound_end_dfcyc) {
			engine.acc = (engine.acc & 0xff00) | 0x20;
			bx_call(BX_WAIT, BX_SOUND_WAIT);	// ~1ms, again
			return;
		}
		if(g_bx_sound_f > 0) {
			bx_sound_stop();
		}
		bx_end_statement();
		return;
	case BX_VOLUME:
		val = bx_fac_int(16);
		if(val < 0) {
			bx_error(BX_IQERR);
			return;
		}
		g_bx_volume = val;
		bx_end_statement();
		return;
	case BX_WAVE:
		val = bx_fac_int(4);
		if(val < 0) {
			bx_error(BX_IQERR);
			return;
		}
		g_bx_wave = val;
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
	int	len;

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
	len = bx_match_word(txt, "SOUND");
	if(len) {
		bx_skip(len);
		bx_call(BX_FRMNUM, BX_SOUND_F);
		return;
	}
	len = bx_match_word(txt, "VOLUME");
	if(len && (get_memory_c(txt + len) == TOK_EQUAL)) {
		bx_skip(len + 1);
		bx_call(BX_FRMNUM, BX_VOLUME);
		return;
	}
	len = bx_match_word(txt, "WAVE");
	if(len && (get_memory_c(txt + len) == TOK_EQUAL)) {
		bx_skip(len + 1);
		bx_call(BX_FRMNUM, BX_WAVE);
		return;
	}
	if((c0 == 'S') || (c0 == 'D')) {
		g_bx_mode = (c0 == 'D') ? BX_MODE_DHR : BX_MODE_SHR;
		switch(c1) {
		case TOK_HGR:			// SHGR / DHGR
			bx_skip(2);
			if(g_bx_mode == BX_MODE_DHR) {
				bx_dhgr();
			} else {
				bx_shgr();
			}
			bx_end_statement();
			return;
		case TOK_HCOLOR:		// SHCOLOR= / DHCOLOR=
			bx_skip(2);
			bx_call(BX_FRMNUM, BX_COLOR);
			return;
		case TOK_HPLOT:			// SHPLOT / DHPLOT
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
