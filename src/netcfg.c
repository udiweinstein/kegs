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

// F4 "Network Configuration" menu (Linux only: Raspberry Pi OS and the
//  Buildroot SD image): Wi-Fi network and password, scan, connect, SMB
//  file sharing of the disks folder, and the IP addresses.
// All the real work is done by the shell script kegs-net (kegs-net.sh),
//  always started in the background, which writes its results to files
//  in /tmp/kegs-net.  KEGS only reads those small files, so the
//  emulator never waits for the network.

#include "defc.h"

// Saved in config.kegs (see g_cfg_net_menu in config.c)
char	*g_cfg_wifi_ssid = "";
char	*g_cfg_wifi_pass = "";
int	g_cfg_net_smb = 0;

#ifdef __linux__

#include <sys/wait.h>
#include <time.h>

#define NET_STATE_DIR	"/tmp/kegs-net"
#define NET_MAX_SCAN	6

extern char g_config_kegs_name[];

char	g_net_helper[1024] = { 0 };
time_t	g_net_last_status_req = 0;
int	g_net_scanning = 0;
char	g_net_scan_ssid[NET_MAX_SCAN][80];
char	g_net_wifi[40];
char	g_net_ssid[80];
char	g_net_ip[200];
char	g_net_smb_state[100];

// Find kegs-net: next to the KEGS executable (installed or built), in
//  src/ below it (git checkout), else in the PATH
static void
net_find_helper()
{
	char	exe[1024], try[1100];
	char	*slash;
	ssize_t	len;

	if(g_net_helper[0]) {
		return;
	}
	snprintf(g_net_helper, sizeof(g_net_helper), "kegs-net");
	len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if(len <= 0) {
		return;
	}
	exe[len] = 0;
	slash = strrchr(exe, '/');
	if(!slash) {
		return;
	}
	*slash = 0;
	snprintf(try, sizeof(try), "%s/kegs-net", exe);
	if(access(try, X_OK) == 0) {
		cfg_strncpy(g_net_helper, try, sizeof(g_net_helper));
		return;
	}
	snprintf(try, sizeof(try), "%s/src/kegs-net.sh", exe);
	if(access(try, X_OK) == 0) {
		cfg_strncpy(g_net_helper, try, sizeof(g_net_helper));
	}
}

// Run the helper with up to 5 arguments, in the background, not waiting
//  for it.  Arguments go straight to exec (no shell), so SSIDs and
//  passwords with spaces or quotes are safe.
static void
net_run(const char *a1, const char *a2, const char *a3, const char *a4,
						const char *a5)
{
	pid_t	pid;

	net_find_helper();
	pid = fork();
	if(pid < 0) {
		return;
	}
	if(pid == 0) {
		// Child: fork again so the helper is adopted by init and
		//  never becomes a zombie of KEGS
		setsid();
		if(fork() == 0) {
			int fd = open("/dev/null", O_RDWR);
			if(fd >= 0) {
				dup2(fd, 0);
				dup2(fd, 1);
				dup2(fd, 2);
			}
			execlp(g_net_helper, g_net_helper, a1, a2, a3, a4, a5,
								(char *)0);
			_exit(127);
		}
		_exit(0);
	}
	(void)waitpid(pid, 0, 0);
}

// Folder to share: the disk images folder (F4 main menu), without '/'
static void
net_share_dir(char *buf, int len)
{
	int	cur;

	kegs_disks_dir(buf, len);
	cur = (int)strlen(buf);
	if((cur > 1) && (buf[cur - 1] == '/')) {
		buf[cur - 1] = 0;
	}
}

// Read /tmp/kegs-net/status (KEY=value lines); ask for a fresh one at
//  most every 2 seconds while the menu is shown
static void
net_update_status()
{
	char	line[256];
	FILE	*f;
	time_t	now;
	int	len;

	now = time(0);
	if((now - g_net_last_status_req) >= 2) {
		g_net_last_status_req = now;
		net_run("status", 0, 0, 0, 0);
	}
	f = fopen(NET_STATE_DIR "/status", "r");
	if(!f) {
		cfg_strncpy(g_net_wifi, "?", sizeof(g_net_wifi));
		return;
	}
	while(fgets(line, sizeof(line), f)) {
		len = (int)strlen(line);
		while(len && ((line[len-1] == '\n') || (line[len-1] == '\r'))) {
			line[--len] = 0;
		}
		if(!strncmp(line, "WIFI=", 5)) {
			cfg_strncpy(g_net_wifi, line + 5, sizeof(g_net_wifi));
		} else if(!strncmp(line, "SSID=", 5)) {
			cfg_strncpy(g_net_ssid, line + 5, sizeof(g_net_ssid));
		} else if(!strncmp(line, "IP=", 3)) {
			cfg_strncpy(g_net_ip, line + 3, sizeof(g_net_ip));
		} else if(!strncmp(line, "SMB=", 4)) {
			cfg_strncpy(g_net_smb_state, line + 4,
						sizeof(g_net_smb_state));
		}
	}
	fclose(f);
}

static void
net_read_scan()
{
	char	line[256];
	FILE	*f;
	int	i, len;

	for(i = 0; i < NET_MAX_SCAN; i++) {
		g_net_scan_ssid[i][0] = 0;
	}
	f = fopen(NET_STATE_DIR "/scan", "r");
	if(!f) {
		return;
	}
	i = 0;
	while((i < NET_MAX_SCAN) && fgets(line, sizeof(line), f)) {
		len = (int)strlen(line);
		while(len && ((line[len-1] == '\n') || (line[len-1] == '\r'))) {
			line[--len] = 0;
		}
		cfg_strncpy(&g_net_scan_ssid[i][0], line, 80);
		i++;
	}
	fclose(f);
}

// First IP address in g_net_ip ("1.2.3.4 wlan0;5.6.7.8 eth0")
static void
net_first_ip(char *buf, int len)
{
	int	i;

	for(i = 0; (i < len - 1) && g_net_ip[i] && (g_net_ip[i] != ' '); i++) {
		buf[i] = g_net_ip[i];
	}
	buf[i] = 0;
}

static char *
net_dup(const char *str)
{
	return kegs_malloc_str(str);
}

// Called by KEGS at start-up, after config.kegs is read: join Wi-Fi
//  (only on the Buildroot image, NetworkManager does it on Pi OS) and
//  start file sharing if it was left on
void
cfg_net_boot()
{
	char	dir[1024];

	net_share_dir(dir, sizeof(dir));
	unlink(NET_STATE_DIR "/scan");
	net_run("boot", g_cfg_wifi_ssid, g_cfg_wifi_pass,
				g_cfg_net_smb ? "1" : "0", dir);
}

// The SMB On/Off line changed
void
cfg_net_smb_changed()
{
	char	dir[1024];

	if(g_cfg_net_smb) {
		net_share_dir(dir, sizeof(dir));
		net_run("smb", "on", dir, 0, 0);
	} else {
		net_run("smb", "off", 0, 0, 0);
	}
	g_net_last_status_req = 0;
}

char *
cfg_net_connect(int get_status)
{
	char	buf[160];

	if(!get_status) {
		if(g_cfg_wifi_ssid[0]) {
			cfg_strncpy(g_net_wifi, "connecting",
							sizeof(g_net_wifi));
			net_run("connect", g_cfg_wifi_ssid, g_cfg_wifi_pass,
								0, 0);
		}
		return 0;
	}
	net_update_status();
	if(!strcmp(g_net_wifi, "connected")) {
		snprintf(buf, sizeof(buf), "Connected to %s", g_net_ssid);
	} else if(!strcmp(g_net_wifi, "connecting")) {
		snprintf(buf, sizeof(buf), "Connecting...");
	} else if(!strcmp(g_net_wifi, "none")) {
		snprintf(buf, sizeof(buf), "No Wi-Fi hardware");
	} else if(!strcmp(g_net_wifi, "?")) {
		snprintf(buf, sizeof(buf), "...");
	} else {
		snprintf(buf, sizeof(buf), "Not connected (press Enter)");
	}
	return net_dup(buf);
}

char *
cfg_net_scan(int get_status)
{
	char	buf[80];
	int	i, num;

	if(!get_status) {
		unlink(NET_STATE_DIR "/scan");
		g_net_scanning = 1;
		net_run("scan", 0, 0, 0, 0);
		return 0;
	}
	net_read_scan();
	num = 0;
	for(i = 0; i < NET_MAX_SCAN; i++) {
		if(g_net_scan_ssid[i][0]) {
			num++;
		}
	}
	if(!g_net_scanning) {
		return net_dup("Press Enter, then pick a network below");
	}
	if(num == 0 || !strcmp(&g_net_scan_ssid[0][0], "Scanning...")) {
		return net_dup("Scanning...");
	}
	if(!strcmp(&g_net_scan_ssid[0][0], "No Wi-Fi hardware")) {
		return net_dup("No Wi-Fi hardware");
	}
	snprintf(buf, sizeof(buf), "%d found, pick one below", num);
	return net_dup(buf);
}

// The 6 lines below "Scan": show a found network, Enter picks it
static char *
net_pick(int get_status, int num)
{
	const char *ssid;

	if(!g_net_scanning) {
		return 0;
	}
	ssid = &g_net_scan_ssid[num][0];
	if(!ssid[0] || !strcmp(ssid, "Scanning...") ||
				!strcmp(ssid, "No Wi-Fi hardware")) {
		return 0;
	}
	if(!get_status) {
		cfg_file_update_ptr(&g_cfg_wifi_ssid, (char *)ssid, 1);
		return 0;
	}
	return net_dup(ssid);
}

char *cfg_net_pick0(int get_status) { return net_pick(get_status, 0); }
char *cfg_net_pick1(int get_status) { return net_pick(get_status, 1); }
char *cfg_net_pick2(int get_status) { return net_pick(get_status, 2); }
char *cfg_net_pick3(int get_status) { return net_pick(get_status, 3); }
char *cfg_net_pick4(int get_status) { return net_pick(get_status, 4); }
char *cfg_net_pick5(int get_status) { return net_pick(get_status, 5); }

char *
cfg_net_smb_status(int get_status)
{
	char	buf[160];

	if(!get_status) {
		return 0;
	}
	net_update_status();
	if(!strcmp(g_net_smb_state, "on")) {
		snprintf(buf, sizeof(buf), "On  (user: kegs  password: apple2)");
	} else if(!strncmp(g_net_smb_state, "error", 5)) {
		snprintf(buf, sizeof(buf), "%s", g_net_smb_state);
	} else if(g_cfg_net_smb) {
		snprintf(buf, sizeof(buf), "Starting...");
	} else {
		snprintf(buf, sizeof(buf), "Off");
	}
	return net_dup(buf);
}

char *
cfg_net_smb_windows(int get_status)
{
	char	ip[64], buf[160];

	if(!get_status) {
		return 0;
	}
	net_update_status();
	net_first_ip(ip, sizeof(ip));
	if(!ip[0] || strcmp(g_net_smb_state, "on")) {
		return 0;
	}
	snprintf(buf, sizeof(buf), "\\\\%s\\disks", ip);
	return net_dup(buf);
}

char *
cfg_net_smb_mac(int get_status)
{
	char	ip[64], buf[160];

	if(!get_status) {
		return 0;
	}
	net_update_status();
	net_first_ip(ip, sizeof(ip));
	if(!ip[0] || strcmp(g_net_smb_state, "on")) {
		return 0;
	}
	snprintf(buf, sizeof(buf), "smb://%s/disks", ip);
	return net_dup(buf);
}

char *
cfg_net_share_status(int get_status)
{
	char	dir[1024];

	if(!get_status) {
		return 0;
	}
	net_share_dir(dir, sizeof(dir));
	return net_dup(dir);
}

char *
cfg_net_ip(int get_status)
{
	char	buf[200];
	int	i, j;

	if(!get_status) {
		return 0;
	}
	net_update_status();
	if(!g_net_ip[0]) {
		return net_dup("No network");
	}
	// "1.2.3.4 wlan0;5.6.7.8 eth0" -> "1.2.3.4 (wlan0)  5.6.7.8 (eth0)"
	j = 0;
	for(i = 0; g_net_ip[i] && (j < (int)sizeof(buf) - 6); i++) {
		if(g_net_ip[i] == ' ') {
			buf[j++] = ' ';
			buf[j++] = '(';
		} else if(g_net_ip[i] == ';') {
			buf[j++] = ')';
			buf[j++] = ' ';
			buf[j++] = ' ';
		} else {
			buf[j++] = g_net_ip[i];
		}
	}
	buf[j++] = ')';
	buf[j] = 0;
	return net_dup(buf);
}

#endif	/* __linux__ */
