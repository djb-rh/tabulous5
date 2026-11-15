/*
Tabulous5: the sound device.

The engine mixes into a ring, a tenth of a second ahead of wherever the
device says it is playing (snd_dma.c). This is the device: a ring of
sixteen-bit mono samples at the rate the game's sounds were made at, and a
pump that hands the next block of it to the platform's speaker whenever the
speaker has room. "Where it is playing" is simply how far the pump has got.

The pump is called from the platform's own audio task, never from the
engine, so sound keeps its pace however long a frame takes. The platform
supplies the two calls at the bottom.
*/
#include "quakedef.h"

#define SND_SAMPLES 8192     /* in the ring: three quarters of a second; a power of two */
#define SND_CHUNK   256      /* handed over at a time: 23 ms */
#define SND_BLOCKS  4        /* the speaker reads a block after the call returns */
#define SND_RATE    11025

int QG_AudioWants(void);                                  /* room for another block? */
void QG_AudioPush(const short *pcm, int count, int rate); /* the block must stay put until played */

/* The engine's own `sn` (snd_dma.c) is the device record. */
static volatile int snd_pos;        /* samples handed over so far, modulo the ring */
static volatile int snd_running;
static short snd_blocks[SND_BLOCKS][SND_CHUNK];
static int snd_block;
static volatile unsigned snd_pushed;   /* blocks handed over, ever */
static volatile int snd_peak;          /* loudest sample since last asked */

/* For the platform's statistics line: how many blocks have gone to the
   speaker, and how loud the loudest sample was since the last call. A
   device that works shows about 43 blocks a second and a peak above zero
   whenever anything in the game is making a noise. */
unsigned QG_AudioBlocks(void) { return snd_pushed; }
int QG_AudioPeak(void) { int p = snd_peak; snd_peak = 0; return p; }

qboolean SNDDMA_Init(void)
{
	memset((void *)&sn, 0, sizeof(sn));
	sn.buffer = calloc(SND_SAMPLES, sizeof(short));
	if (!sn.buffer)
		return false;
	sn.splitbuffer = 0;
	sn.speed = SND_RATE;
	sn.samplebits = 16;
	sn.channels = 1;
	sn.samples = SND_SAMPLES;
	sn.samplepos = 0;
	sn.submission_chunk = 1;
	sn.soundalive = true;
	shm = &sn;
	snd_pos = 0;
	snd_running = 1;
	Con_Printf("16 bit mono sound at %d Hz\n", SND_RATE);
	return true;
}

int SNDDMA_GetDMAPos(void)
{
	if (!snd_running)
		return 0;
	sn.samplepos = snd_pos;
	return snd_pos;
}

void SNDDMA_Shutdown(void)
{
	snd_running = 0;
}

void SNDDMA_Submit(void)
{
}

void QG_AudioPump(void)
{
	const short *ring;
	int i, p;

	if (!snd_running || !sn.buffer)
		return;
	ring = (const short *)sn.buffer;
	while (QG_AudioWants())
	{
		short *out = snd_blocks[snd_block];
		snd_block = (snd_block + 1) % SND_BLOCKS;
		p = snd_pos;
		for (i = 0; i < SND_CHUNK; i++)
		{
			out[i] = ring[p];
			if (out[i] > snd_peak) snd_peak = out[i];
			else if (-out[i] > snd_peak) snd_peak = -out[i];
			p = (p + 1) & (SND_SAMPLES - 1);
		}
		snd_pos = p;
		snd_pushed++;
		QG_AudioPush(out, SND_CHUNK, SND_RATE);
	}
}
