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

#ifdef SDL_AUDIO
// Ignore entire file if SDL_AUDIO is not defined!

// SDL2 sound output.  Used by the SDL2 build (sdlkegs) so it needs no
//  PulseAudio: SDL talks to ALSA directly on a minimal system (Buildroot,
//  Pi OS Lite), and to PulseAudio/PipeWire on a desktop.
// Modeled after pulseaudio_driver.c: the emulator writes stereo 16-bit
//  samples into a ring buffer, and SDL's audio thread pulls them out.

#include <SDL.h>
#include "defc.h"
#include "sound.h"


#define SDLAUDIO_REBUF_SIZE	(64*1024)	// Must be a power of 2

word32	g_sdlaudio_rebuf[SDLAUDIO_REBUF_SIZE];
volatile int g_sdlaudio_rd_pos = 0;
volatile int g_sdlaudio_wr_pos = 0;
volatile int g_sdlaudio_playing = 0;
SDL_AudioDeviceID g_sdlaudio_dev = 0;

extern int g_preferred_rate;
extern int g_sound_min_samples;
extern int g_sound_max_multiplier;

int
sdlaudio_send_audio(byte *ptr, int in_size)
{
	word32	*wptr;
	int	samps, pos, i;

	samps = in_size / 4;
	wptr = (word32 *)ptr;
	pos = g_sdlaudio_wr_pos;
	for(i = 0; i < samps; i++) {
		g_sdlaudio_rebuf[pos] = *wptr++;
		pos = (pos + 1) & (SDLAUDIO_REBUF_SIZE - 1);
	}
	g_sdlaudio_wr_pos = pos;

	return in_size;
}

// Runs on SDL's audio thread
void
sdlaudio_callback(void *userdata, Uint8 *stream, int len)
{
	word32	*out;
	int	samps_needed, samps_avail, pos, min_samples, max_samples;
	int	num, i;

	if(userdata) {			// Avoid unused var warning
	}
	out = (word32 *)stream;
	samps_needed = len / 4;
	pos = g_sdlaudio_rd_pos;
	samps_avail = (g_sdlaudio_wr_pos - pos) & (SDLAUDIO_REBUF_SIZE - 1);
	min_samples = g_sound_min_samples;
	if(samps_needed > min_samples) {
		min_samples = samps_needed;
	}
	max_samples = min_samples * g_sound_max_multiplier;

	if(!g_sdlaudio_playing && (samps_avail > min_samples)) {
		g_sdlaudio_playing = 1;		// Enough buffered, start
	}
	if(g_sdlaudio_playing && (samps_avail > max_samples)) {
		// Too far behind: skip ahead to keep latency low
		pos = (pos + max_samples / 2) & (SDLAUDIO_REBUF_SIZE - 1);
		samps_avail -= max_samples / 2;
	}

	num = 0;
	if(g_sdlaudio_playing) {
		num = MY_MIN(samps_avail, samps_needed);
		for(i = 0; i < num; i++) {
			out[i] = g_sdlaudio_rebuf[pos];
			pos = (pos + 1) & (SDLAUDIO_REBUF_SIZE - 1);
		}
		g_sdlaudio_rd_pos = pos;
		if(num < samps_needed) {
			g_sdlaudio_playing = 0;	// Ran dry, wait to refill
		}
	}
	for(i = num; i < samps_needed; i++) {
		out[i] = 0;			// Silence
	}
}

int
sdlaudio_init()
{
	SDL_AudioSpec want, have;

	g_sdlaudio_rd_pos = 0;
	g_sdlaudio_wr_pos = 0;
	g_sdlaudio_playing = 0;

	if(SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		printf("SDL audio init failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_zero(want);
	want.freq = g_preferred_rate;
	want.format = AUDIO_S16LSB;
	want.channels = 2;
	want.samples = 1024;
	want.callback = sdlaudio_callback;
	g_sdlaudio_dev = SDL_OpenAudioDevice(0, 0, &want, &have,
					SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if(g_sdlaudio_dev == 0) {
		printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return 1;
	}
	printf("SDL audio: driver %s, rate %d, buffer %d samples\n",
		SDL_GetCurrentAudioDriver(), have.freq, have.samples);
	sound_set_audio_rate(have.freq);
	SDL_PauseAudioDevice(g_sdlaudio_dev, 0);

	return 0;
}

void
sdlaudio_shutdown()
{
	if(g_sdlaudio_dev) {
		SDL_CloseAudioDevice(g_sdlaudio_dev);
		g_sdlaudio_dev = 0;
	}
}

#endif		/* SDL_AUDIO */
