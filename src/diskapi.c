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

// Disk image API for Apple II programs running in KEGS (used by YANC):
//  list the disk images in the host's disk images folder, insert one
//  into a drive, eject, and see what is in each drive.
//
// Call it with the 3 bytes $42,$58,$EA ("WDM $58" followed by NOP):
//	A = command, X/Y = address (low/high) of a parameter block in bank 0
//	Returns A = result, carry clear = OK, carry set = error (A = code)
// On a real 65C02 or 65816, $42,$58 is a 2-byte NOP and $EA a NOP, so a
//  program can safely test for KEGS: LDA #0, call it, KEGS answers 'K'.
//
// Commands:
//  0 ID	A='K' X=API version.  No parameter block.
//  1 LIST	block: +0,+1 buffer, +2 max entries, +3 sub-folder (length
//		byte + up to 63 chars, "" = the disk images folder itself).
//		Fills the buffer with 32-byte entries, folders first, then
//		images, by name: +0 kind (1=folder, 2=image), +1 drive it is
//		in (slot*16 + drive 1-2..), 0 if none, +2,+3 size in KB,
//		+4 name length (up to 26), +5..+30 name, +31 the slot it
//		belongs in by its type: 6 = 5.25", 5 = 3.5", 7 = hard disk.
//		A = number of entries.  The list is kept for MOUNT.
//  2 MOUNT	block: +0 entry number (from the last LIST), +1 drive
//		(slot*16 + drive, 0 = choose by image type), +2 a drive
//		that must not be used (the caller's own disk), 0 = none.
//		A = drive used.
//  3 EJECT	block: +1 drive (slot*16 + drive).
//  4 DRIVES	block: +0,+1 buffer.  Fills 32-byte entries for S5D1, S5D2,
//		S6D1, S6D2, S7D1-S7D8: +0 drive, +1 flags (1 = disk in it,
//		2 = write protected), +2,+3 size in KB, +4 name length,
//		+5..+31 image file name.  A = number of entries (12).

#include "defc.h"
#include <dirent.h>

// Saved in config.kegs: folder with the disk images, relative to the
//  folder config.kegs is in, or a full path
char	*g_cfg_disks_dir = "disks";

extern char g_config_kegs_name[];
extern Engine_reg engine;

#define DAPI_MAX_ENTRIES	255
#define DAPI_ENTRY_SIZE		32
#define DAPI_NAME_MAX		27

#define DAPI_ERR_BADCMD		1
#define DAPI_ERR_NOFOLDER	2
#define DAPI_ERR_BADENTRY	3
#define DAPI_ERR_BADDRIVE	4
#define DAPI_ERR_NOTIMAGE	5
#define DAPI_ERR_PROTECTED	6

typedef struct dapi_entry {
	char	*name;
	int	is_dir;
	dword64	size;
} Dapi_entry;

Dapi_entry g_dapi_list[DAPI_MAX_ENTRIES];
int	g_dapi_list_num = 0;
char	g_dapi_list_dir[1024];

// The disk images folder as a full path, ending with '/'
void
kegs_disks_dir(char *buf, int len)
{
	char	*slash;
	int	cur;

	if(g_cfg_disks_dir[0] == '/') {
		cfg_strncpy(buf, g_cfg_disks_dir, len);
	} else {
		cfg_strncpy(buf, g_config_kegs_name, len);
		slash = strrchr(buf, '/');
		if(slash) {
			slash[1] = 0;
		} else {
			if(!getcwd(buf, len - 2)) {
				buf[0] = 0;
			}
			cfg_strlcat(buf, "/", len);
		}
		cfg_strlcat(buf, g_cfg_disks_dir, len);
	}
	cur = (int)strlen(buf);
	if(cur && (buf[cur - 1] != '/')) {
		cfg_strlcat(buf, "/", len);
	}
}

static int
dapi_is_image_name(const char *name)
{
	static const char *exts[] = { ".po", ".2mg", ".2img", ".dsk", ".do",
		".hdv", ".woz", ".nib", ".img", ".gz", ".zip", ".sdk",
		".shk", 0 };
	const char *dot;
	int	i;

	dot = strrchr(name, '.');
	if(!dot) {
		return 0;
	}
	for(i = 0; exts[i]; i++) {
		if(!strcasecmp(dot, exts[i])) {
			return 1;
		}
	}
	return 0;
}

static int
dapi_compare(const void *a, const void *b)
{
	const Dapi_entry *ea = a;
	const Dapi_entry *eb = b;

	if(ea->is_dir != eb->is_dir) {
		return eb->is_dir - ea->is_dir;		// Folders first
	}
	return strcasecmp(ea->name, eb->name);
}

static word32
dapi_get16(word32 addr)
{
	return get_memory_c(addr & 0xffff) |
				(get_memory_c((addr + 1) & 0xffff) << 8);
}

static void
dapi_put(word32 addr, word32 val)
{
	set_memory_c(addr & 0xffff, val & 0xff, 1);
}

// Write a name (ASCII, at most max chars) at addr: length, chars
static void
dapi_put_name(word32 addr, const char *name, int max)
{
	int	len, i, c;

	len = (int)strlen(name);
	if(len > max) {
		len = max;
	}
	dapi_put(addr, len);
	for(i = 0; i < max; i++) {
		c = 0;
		if(i < len) {
			c = name[i] & 0xff;
			if((c < 0x20) || (c > 0x7e)) {
				c = '?';	// Not plain ASCII
			}
		}
		dapi_put(addr + 1 + i, c);
	}
}

// Which drive (slot*16 + drive 1..) holds the image at path, else 0
static int
dapi_drive_with(const char *path)
{
	Disk	*dsk;
	int	slot, drive, max_drive;

	for(slot = 5; slot <= 7; slot++) {
		max_drive = (slot == 7) ? 8 : 2;
		for(drive = 0; drive < max_drive; drive++) {
			dsk = iwm_get_dsk_from_slot_drive(slot, drive);
			if(dsk && dsk->name_ptr && (dsk->fd >= 0) &&
					!strcmp(dsk->name_ptr, path)) {
				return (slot << 4) | (drive + 1);
			}
		}
	}
	return 0;
}

static void
dapi_free_list()
{
	int	i;

	for(i = 0; i < g_dapi_list_num; i++) {
		free(g_dapi_list[i].name);
	}
	g_dapi_list_num = 0;
}

static int
dapi_list(word32 blk)
{
	char	path[1100], sub[80];
	struct stat stat_buf;
	struct dirent *dent;
	DIR	*dir;
	word32	buf, kb;
	int	max, sub_len, num, i, c;

	buf = dapi_get16(blk);
	max = get_memory_c((blk + 2) & 0xffff);
	sub_len = get_memory_c((blk + 3) & 0xffff) & 0x3f;
	for(i = 0; i < sub_len; i++) {
		c = get_memory_c((blk + 4 + i) & 0xffff) & 0x7f;
		sub[i] = c;
	}
	sub[sub_len] = 0;
	if(strstr(sub, "..") || (sub[0] == '/')) {
		return -DAPI_ERR_NOFOLDER;	// Stay in the images folder
	}
	kegs_disks_dir(g_dapi_list_dir, sizeof(g_dapi_list_dir));
	cfg_strlcat(g_dapi_list_dir, sub, sizeof(g_dapi_list_dir));
	if(sub_len && (sub[sub_len - 1] != '/')) {
		cfg_strlcat(g_dapi_list_dir, "/", sizeof(g_dapi_list_dir));
	}

	dapi_free_list();
	dir = opendir(g_dapi_list_dir);
	if(!dir) {
		return -DAPI_ERR_NOFOLDER;
	}
	while((dent = readdir(dir)) && (g_dapi_list_num < DAPI_MAX_ENTRIES)) {
		if(dent->d_name[0] == '.') {
			continue;		// Hidden, '.' and '..'
		}
		snprintf(path, sizeof(path), "%s%s", g_dapi_list_dir,
								dent->d_name);
		if(stat(path, &stat_buf) != 0) {
			continue;
		}
		i = g_dapi_list_num;
		if(S_ISDIR(stat_buf.st_mode)) {
			g_dapi_list[i].is_dir = 1;
		} else if(S_ISREG(stat_buf.st_mode) &&
					dapi_is_image_name(dent->d_name)) {
			g_dapi_list[i].is_dir = 0;
		} else {
			continue;
		}
		g_dapi_list[i].name = kegs_malloc_str(dent->d_name);
		g_dapi_list[i].size = stat_buf.st_size;
		g_dapi_list_num++;
	}
	closedir(dir);
	qsort(&g_dapi_list[0], g_dapi_list_num, sizeof(Dapi_entry),
								dapi_compare);

	num = MY_MIN(g_dapi_list_num, max);
	for(i = 0; i < num; i++) {
		word32 e = buf + i * DAPI_ENTRY_SIZE;
		snprintf(path, sizeof(path), "%s%s", g_dapi_list_dir,
							g_dapi_list[i].name);
		dapi_put(e + 0, g_dapi_list[i].is_dir ? 1 : 2);
		dapi_put(e + 1, g_dapi_list[i].is_dir ? 0 :
							dapi_drive_with(path));
		kb = (word32)MY_MIN((g_dapi_list[i].size + 1023) >> 10, 65535);
		if(g_dapi_list[i].is_dir) {
			kb = 0;
		}
		dapi_put(e + 2, kb);
		dapi_put(e + 3, kb >> 8);
		dapi_put_name(e + 4, g_dapi_list[i].name, DAPI_NAME_MAX - 1);
		dapi_put(e + 31, g_dapi_list[i].is_dir ? 0 :
			cfg_guess_disk_slot(path, g_dapi_list[i].size));
	}
	return num;
}

// First empty drive in slot, else the first one that isn't protected
static int
dapi_free_drive(int slot, int prot)
{
	Disk	*dsk;
	int	drive, max_drive;

	max_drive = (slot == 7) ? 8 : 2;
	for(drive = 0; drive < max_drive; drive++) {
		dsk = iwm_get_dsk_from_slot_drive(slot, drive);
		if(dsk && (dsk->fd < 0) &&
				(((slot << 4) | (drive + 1)) != prot)) {
			return drive;
		}
	}
	for(drive = 0; drive < max_drive; drive++) {
		if(((slot << 4) | (drive + 1)) != prot) {
			return drive;
		}
	}
	return -1;
}

static int
dapi_check_drive(int sd, int *slot_ptr, int *drive_ptr)
{
	int	slot, drive;

	slot = (sd >> 4) & 0xf;
	drive = (sd & 0xf) - 1;
	if((slot < 5) || (slot > 7) || (drive < 0) ||
			((slot < 7) && (drive > 1)) || ((slot == 7) && (drive > 7))) {
		return 0;
	}
	*slot_ptr = slot;
	*drive_ptr = drive;
	return 1;
}

static int
dapi_mount(word32 blk)
{
	char	path[1100];
	int	idx, sd, slot, drive, prot;

	idx = get_memory_c(blk & 0xffff);
	sd = get_memory_c((blk + 1) & 0xffff);
	prot = get_memory_c((blk + 2) & 0xffff);
	if((idx >= g_dapi_list_num) || g_dapi_list[idx].is_dir) {
		return -DAPI_ERR_BADENTRY;
	}
	snprintf(path, sizeof(path), "%s%s", g_dapi_list_dir,
							g_dapi_list[idx].name);
	if(sd == 0) {
		slot = cfg_guess_disk_slot(path, g_dapi_list[idx].size);
		drive = dapi_free_drive(slot, prot);
		if(drive < 0) {
			return -DAPI_ERR_PROTECTED;
		}
	} else if(!dapi_check_drive(sd, &slot, &drive)) {
		return -DAPI_ERR_BADDRIVE;
	} else if(sd == prot) {
		return -DAPI_ERR_PROTECTED;
	}
	printf("Disk API: insert %s into s%dd%d\n", path, slot, drive + 1);
	insert_disk(slot, drive, path, 0, 0, -1, 0);
	if(!dapi_drive_with(path)) {
		return -DAPI_ERR_NOTIMAGE;	// KEGS could not open it
	}
	return (slot << 4) | (drive + 1);
}

static int
dapi_eject(word32 blk)
{
	Disk	*dsk;
	int	sd, slot, drive;

	sd = get_memory_c((blk + 1) & 0xffff);
	if(!dapi_check_drive(sd, &slot, &drive)) {
		return -DAPI_ERR_BADDRIVE;
	}
	dsk = iwm_get_dsk_from_slot_drive(slot, drive);
	if(dsk && (dsk->fd >= 0)) {
		printf("Disk API: eject s%dd%d\n", slot, drive + 1);
		iwm_eject_disk(dsk);
	}
	return sd;
}

static int
dapi_drives(word32 blk)
{
	Disk	*dsk;
	const char *name, *slash;
	word32	buf, e, kb;
	int	slot, drive, max_drive, num, flags;

	buf = dapi_get16(blk);
	num = 0;
	for(slot = 5; slot <= 7; slot++) {
		max_drive = (slot == 7) ? 8 : 2;
		for(drive = 0; drive < max_drive; drive++) {
			dsk = iwm_get_dsk_from_slot_drive(slot, drive);
			e = buf + num * DAPI_ENTRY_SIZE;
			flags = 0;
			name = "";
			kb = 0;
			if(dsk && dsk->name_ptr && (dsk->fd >= 0)) {
				flags = 1;
				if(dsk->write_prot) {
					flags |= 2;
				}
				slash = strrchr(dsk->name_ptr, '/');
				name = slash ? slash + 1 : dsk->name_ptr;
				kb = (word32)MY_MIN((dsk->dimage_size + 1023)
								>> 10, 65535);
			}
			dapi_put(e + 0, (slot << 4) | (drive + 1));
			dapi_put(e + 1, flags);
			dapi_put(e + 2, kb);
			dapi_put(e + 3, kb >> 8);
			dapi_put_name(e + 4, name, DAPI_NAME_MAX);
			num++;
		}
	}
	return num;
}

// Called from do_wdm() for WDM $58,$EA
void
disk_api_call()
{
	word32	blk;
	int	cmd, ret;

	cmd = engine.acc & 0xff;
	blk = (engine.xreg & 0xff) | ((engine.yreg & 0xff) << 8);
	switch(cmd) {
	case 0:
		engine.xreg = (engine.xreg & 0xff00) | 1;	// API version
		ret = 'K';
		break;
	case 1:
		ret = dapi_list(blk);
		break;
	case 2:
		ret = dapi_mount(blk);
		break;
	case 3:
		ret = dapi_eject(blk);
		break;
	case 4:
		ret = dapi_drives(blk);
		break;
	default:
		ret = -DAPI_ERR_BADCMD;
		break;
	}
	if(ret < 0) {
		engine.acc = (engine.acc & 0xff00) | ((-ret) & 0xff);
		engine.psr |= 1;		// Carry set: error
	} else {
		engine.acc = (engine.acc & 0xff00) | (ret & 0xff);
		engine.psr &= ~1;		// Carry clear: OK
	}
}
