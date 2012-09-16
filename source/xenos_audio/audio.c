/**
 * Wii64 - audio.c
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * Copyright (C) 2007, 2008, 2009 emu_kidid
 * 
 * Low-level Audio plugin with linear interpolation & 
 * resampling to 32/48KHz for the GC/Wii
 *
 * Wii64 homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
 *                emukidid@gmail.com
 *
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
**/

/*  MEMORY USAGE:
     STATIC:
   	Audio Buffer: 4x BUFFER_SIZE (currently ~3kb each)
   	LWP Control Buffer: 1Kb
*/

#include "../main/winlnxdefs.h"
#include "../main/main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <xenon_sound/sound.h>
#include <xenon_soc/xenon_power.h>
#include <ppc/atomic.h>
#include <byteswap.h>
#include <time/time.h>

#include "AudioPlugin.h"
#include "Audio_#1.1.h"

AUDIO_INFO AudioInfo;

#define MAX_UNPLAYED 16384

#define BUFFER_SIZE 65536
static char buffer[BUFFER_SIZE];
static volatile unsigned int freq;
static volatile unsigned int real_freq;
static volatile double freq_ratio;
static volatile int is_60Hz;
// NOTE: 32khz actually uses ~2136 bytes/frame @ 60hz
static volatile enum { BUFFER_SIZE_32_60 = 2112, BUFFER_SIZE_48_60 = 3200,
              BUFFER_SIZE_32_50 = 2560, BUFFER_SIZE_48_50 = 3840 } buffer_size = 1024;


static unsigned char thread_stack[0x100000];

static volatile unsigned char thread_buffer[65536];
static volatile int thread_bufsize=0;
static volatile int thread_terminate=0;


#ifdef AIDUMP
FILE *AIdump=NULL;
char *toggle_audiodump()
{
  if(AIdump) {
    fclose(AIdump);
    AIdump=NULL;
    return "Stopped Dumping";
  }
  else {
    AIdump = fopen("sd:/dump.raw","wb");
    if(AIdump)
      return "Starting Audio Dumping";
    return "Error creating file";
  }
}
#endif

char audioEnabled = 1;

EXPORT void CALL
AiDacrateChanged( int SystemType )
{
	// Taken from mupen_audio
	freq = 32000; //default to 32khz incase we get a bad systemtype
	switch (SystemType){
		case SYSTEM_NTSC:
			freq = 48681812 / (*AudioInfo.AI_DACRATE_REG + 1);
			break;
	    case SYSTEM_PAL:
			freq = 49656530 / (*AudioInfo.AI_DACRATE_REG + 1);
			break;
	    case SYSTEM_MPAL:
			freq = 48628316 / (*AudioInfo.AI_DACRATE_REG + 1);
			break;
	}

	real_freq = 48000;
	
	freq_ratio = (double)freq / real_freq;

	is_60Hz = (SystemType != SYSTEM_PAL);
}

static void inline play_buffer(void){
	if(use_framelimit){
		while(xenon_sound_get_unplayed()>MAX_UNPLAYED);
	}
	
	int i;
	for(i=0;i<buffer_size/4;++i) ((int*)buffer)[i]=bswap_32(((int*)buffer)[i]);
	
	//printf("%8d %8d\n",xenon_sound_get_free(),xenon_sound_get_unplayed());
	xenon_sound_submit(buffer,buffer_size);
}

static s16 prevLastSample[2]={0,0};
// resamples pStereoSamples (taken from http://pcsx2.googlecode.com/svn/trunk/plugins/zerospu2/zerospu2.cpp)
void ResampleLinear(s16* pStereoSamples, s32 oldsamples, s16* pNewSamples, s32 newsamples)
{
		s32 newsampL, newsampR;
		s32 i;
		
		for (i = 0; i < newsamples; ++i)
        {
                s32 io = i * oldsamples;
                s32 old = io / newsamples;
                s32 rem = io - old * newsamples;

                old *= 2;
				//printf("%d %d\n",old,oldsamples);
				if (old==0){
					newsampL = prevLastSample[0] * (newsamples - rem) + pStereoSamples[0] * rem;
					newsampR = prevLastSample[1] * (newsamples - rem) + pStereoSamples[1] * rem;
				}else{
					newsampL = pStereoSamples[old-2] * (newsamples - rem) + pStereoSamples[old] * rem;
					newsampR = pStereoSamples[old-1] * (newsamples - rem) + pStereoSamples[old+1] * rem;
				}
                pNewSamples[2 * i] = newsampL / newsamples;
                pNewSamples[2 * i + 1] = newsampR / newsamples;
        }

		prevLastSample[0]=pStereoSamples[oldsamples*2-2];
		prevLastSample[1]=pStereoSamples[oldsamples*2-1];
}

static void inline add_to_buffer(void* stream, unsigned int length){
	unsigned int lengthLeft = length >> 2;
	unsigned int rlengthLeft = ceil(lengthLeft / freq_ratio);

	ResampleLinear((s16 *)stream,lengthLeft,(s16 *)buffer,rlengthLeft);
	buffer_size=rlengthLeft<<2;
	play_buffer();
}


static void thread_enqueue(void * buffer,int size)
{
	while(thread_bufsize) asm volatile("db16cyc");
	
	thread_bufsize=size;
	memcpy((void*)thread_buffer,buffer,thread_bufsize);
}

static void thread_loop()
{
	while(!thread_terminate){
		if (thread_bufsize){
			add_to_buffer((void*)thread_buffer,(unsigned int)thread_bufsize);
			thread_bufsize=0;
		}
	}
}

EXPORT void CALL
AiLenChanged( void )
{
	// FIXME: We may need near full speed before this is going to work
	if(!audioEnabled) return;

	short* stream = (short*)(AudioInfo.RDRAM +
		         (*AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF));
	unsigned int length = *AudioInfo.AI_LEN_REG;

#ifdef USE_THREADED_AUDIO
//	printf("thread_enqueue %d\n",length);
	thread_enqueue(stream,length);
#else
	add_to_buffer(stream, length);
#endif
}

EXPORT DWORD CALL
AiReadLength( void )
{
	return buffer_size;
}

EXPORT void CALL
AiUpdate( BOOL Wait )
{
}

EXPORT void CALL
CloseDLL( void )
{
}

EXPORT void CALL
DllAbout( HWND hParent )
{
	printf ("Gamecube audio plugin\n\tby Mike Slegeir" );
}

EXPORT void CALL
DllConfig ( HWND hParent )
{
}

EXPORT void CALL
DllTest ( HWND hParent )
{
}

EXPORT void CALL
GetDllInfo( PLUGIN_INFO * PluginInfo )
{
	PluginInfo->Version = 0x0101;
	PluginInfo->Type    = PLUGIN_TYPE_AUDIO;
	sprintf(PluginInfo->Name,"Gamecube audio plugin\n\tby Mike Slegeir");
	PluginInfo->NormalMemory  = TRUE;
	PluginInfo->MemoryBswaped = TRUE;
}

EXPORT BOOL CALL
InitiateAudio( AUDIO_INFO Audio_Info )
{
	AudioInfo = Audio_Info;

	thread_bufsize=0;
	thread_terminate=0;

	xenon_run_thread_task(2,&thread_stack[sizeof(thread_stack)-0x1000],thread_loop);
	atexit(RomClosed);
    return TRUE;
}

EXPORT void CALL RomOpen()
{
}

EXPORT void CALL
RomClosed( void )
{
	thread_terminate=1;
	while(xenon_is_thread_task_running(2));
}

EXPORT void CALL
ProcessAlist( void )
{
}

void pauseAudio(void){
}

void resumeAudio(void){
}

