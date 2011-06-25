/**
 * Mupen64 - main.c
 * Copyright (C) 2002 Hacktarux
 *
 * Mupen64 homepage: http://mupen64.emulation64.com
 * email address: hacktarux@yahoo.fr
 * 
 * If you want to contribute to the project please contact
 * me first (maybe someone is already making what you are
 * planning to do).
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
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
**/

/* This is the command line version of the MUPEN64's entry point,
 * if you want to implement an interface, you should look here
 */

// Emulateur Nintendo 64, MUPEN64, Fichier Principal 
// main.c
#define VERSION "0.5\0"

#include <stdlib.h>
#include <unistd.h>

#include "main.h"
#include "guifuncs.h"
#include "rom.h"
#include "../r4300/r4300.h"
#include "../r4300/recomph.h"
#include "../memory/memory.h"
#include "winlnxdefs.h"
#include "plugin.h"
#include "savestates.h"


#include <debug.h>
#include <xenos/xenos.h>
#include <xenos/xe.h>
#include <xenon_sound/sound.h>
#include <diskio/dvd.h>
#include <diskio/ata.h>
#include <ppc/cache.h>
#include <ppc/timebase.h>
#include <pci/io.h>
#include <input/input.h>
#include <xenon_smc/xenon_smc.h>
#include <console/console.h>
#include <xenon_soc/xenon_power.h>
#include <usb/usbmain.h>


#define NOGUI_VERSION

#undef hi
#undef lo
#define hi (reg[32])
#define lo (reg[33])

#if defined (__linux__)
#include <signal.h>
#endif

#define stop_it() stop = 1

int autoinc_slot = 0;
int *autoinc_save_slot = &autoinc_slot;

static char cwd[1024];
int p_noask=TRUE;

char g_WorkingDir[PATH_MAX];

char txtbuffer[1024];

char *get_currentpath()
{
   return cwd;
}

char *get_savespath()
{
   static char path[1024];
#if 0
   strcpy(path, get_currentpath());
   strcat(path, "save/");
#else
   path[0]='\0';
#endif
   return path;
}

void display_loading_progress(int p)
{
   printf("loading rom : %d%%\r", p);
   fflush(stdout);
   if (p==100) printf("\n");
}

void display_MD5calculating_progress(int p)
{
}

int ask_bad()
{
   char c;
   printf("The rom you are trying to load is probably a bad dump\n");
   printf("Be warned that this will probably give unexpected results.\n");
   printf("Do you still want to run it (y/n) ?");
   
   if(p_noask) return 1;
   else
     {
	c = getchar();
	getchar();
	if (c=='y' || c=='Y') return 1;
	else return 0;
     }
}

int ask_hack()
{
   char c;
   printf("The rom you are trying to load is not a good verified dump\n");
   printf("It can be a hacked dump, trained dump or anything else that \n");
   printf("may work but be warned that it can give unexpected results.\n");
   printf("Do you still want to run it (y/n) ?");
   
   if(p_noask) return 1;
   else
     {
	c = getchar();
	getchar();
	if (c=='y' || c=='Y') return 1;
	else return 0;
     }
}

void warn_savestate_from_another_rom()
{
   printf("Error: You're trying to load a save state from either another rom\n");
   printf("       or another dump.\n");
}

void warn_savestate_not_exist()
{
   printf("Error: The save state you're trying to load doesn't exist\n");
}

void new_frame()
{
}

void new_vi()
{
}


int main ()
{
   char romfile[PATH_MAX];
   
   xenos_init(VIDEO_MODE_AUTO);
   console_init();

   xenon_make_it_faster(XENON_SPEED_FULL);  
   usb_init();

   usb_do_poll();

   dvd_init();
   xenon_ata_init();
   
   strcpy(cwd, "uda:/mupen64-360/");
//   strcpy(romfile, "dvd:/Super Mario 64.zip");
//   strcpy(romfile, "dvd:/Mario Kart 64.zip");
//   strcpy(romfile, "dvd:/Legend of Zelda, The - Ocarina of Time.zip");

//   strcpy(romfile, "uda:/sm64.v64");
//   strcpy(romfile, "uda:/Mario Kart 64.zip");

//   strcpy(romfile, "sda:/n64roms/Super Mario 64.zip");
   strcpy(romfile, "sda:/n64roms/Mario Kart 64.zip");
//   strcpy(romfile, "sda:/n64roms/Legend of Zelda, The - Ocarina of Time.zip");


#if 0
   {
			float time0,timecur;
//			FILE * f=fopen("sda:/n64roms/Resident Evil 2.zip","rb");
			FILE * f=fopen("dvd:/Resident Evil 2.zip","rb");
			if (f){
				//setvbuf (f , NULL, _IOFBF, 65536);
				char * buf=malloc(128*1024*1024);
				
				fseek(f,0,SEEK_END);
				int size=ftell(f);
				fseek(f,0,SEEK_SET);

				printf("ok %d\n",size);

				time0=(float)mftb()/(float)PPC_TIMEBASE_FREQ;

				fread(buf,size,1,f);
				timecur=(float)mftb()/(float)PPC_TIMEBASE_FREQ-time0;
				printf("%f KB/s\n",(size*1.0f/1024.0f)/timecur);
				free(buf);
				fclose(f);
			}
   }			
#endif
      
   while(cwd[strlen(cwd)-1] != '/') cwd[strlen(cwd)-1] = '\0';
   strcpy(g_WorkingDir, cwd);
   
   printf("\nMupen64 version : %s\n", VERSION);

#ifdef USE_TLB_CACHE
	TLBCache_init();
#else
	tlb_mem2_init();
#endif
	
	if (rom_read(romfile))
     {
	if(rom) free(rom);
	if(ROM_HEADER) free(ROM_HEADER);
	return 1;
     }
   printf("Goodname:%s\n", ROM_SETTINGS.goodname);
   printf("16kb eeprom=%d\n", ROM_SETTINGS.eeprom_16kb);

   //dynacore=0; // interpreter
   //dynacore=2; // pure interpreter
   dynacore=1; //  dynamic recompiler
   
   console_close();
   
   init_memory();

   // --------------------- loading plugins ----------------------
   plugin_load_plugins(NULL,NULL,NULL,NULL);
   romOpen_gfx();
   romOpen_audio();
   romOpen_input();
   // ------------------------------------------------------------

   cpu_init();
   
   go();
   
   cpu_deinit();
   
   romClosed_RSP();
   romClosed_input();
   romClosed_audio();
   romClosed_gfx();
   closeDLL_RSP();
   closeDLL_input();
   closeDLL_audio();
   closeDLL_gfx();
   free(rom);
   free(ROM_HEADER);
   free_memory();
   return 0;
}

static CONTROL_INFO control_info;

void initiateControllers(CONTROL_INFO ControlInfo)
{
   control_info = ControlInfo;
   control_info.Controls[0].Present = TRUE;
   control_info.Controls[0].Plugin = PLUGIN_MEMPAK;
}

#define	STICK_DEAD_ZONE (32768*0.3)
#define HANDLE_STICK_DEAD_ZONE(x) ((((x)>-STICK_DEAD_ZONE) && (x)<STICK_DEAD_ZONE)?0:(x-x/abs(x)*STICK_DEAD_ZONE))

#define	TRIGGER_DEAD_ZONE (256*0.3)
#define HANDLE_TRIGGER_DEAD_ZONE(x) (((x)<TRIGGER_DEAD_ZONE)?0:(x-TRIGGER_DEAD_ZONE))

#define TRIGGER_THRESHOLD 100
#define STICK_THRESHOLD 25000

void getKeys(int Control, BUTTONS *Keys)
{
    static struct controller_data_s c;
    BUTTONS b;

    usb_do_poll();

    if(get_controller_data(&c, 0)){
    }

    if (c.select){
	printf("shutdown!\n");
	xenon_smc_power_shutdown();
	for(;;);
    }

    b.START_BUTTON=c.start;
        
    b.A_BUTTON=c.a;
    b.B_BUTTON=c.b;
    b.Z_TRIG=c.x || c.y || (c.rt>TRIGGER_THRESHOLD) || (c.lt>TRIGGER_THRESHOLD);
        
    b.L_TRIG=c.lb;
    b.R_TRIG=c.rb;
        
    b.U_DPAD=c.up;
    b.D_DPAD=c.down;
    b.L_DPAD=c.left;
    b.R_DPAD=c.right;

    b.X_AXIS=HANDLE_STICK_DEAD_ZONE(c.s1_x)/256;
    b.Y_AXIS=HANDLE_STICK_DEAD_ZONE(c.s1_y)/256;

    b.U_CBUTTON=c.s2_y<-STICK_THRESHOLD;
    b.D_CBUTTON=c.s2_y>STICK_THRESHOLD;
    b.L_CBUTTON=c.s2_x<-STICK_THRESHOLD;
    b.R_CBUTTON=c.s2_x>STICK_THRESHOLD;

    //printf("%08x\n",b.Value);

    if (Control == 0) Keys->Value = b.Value;

}

