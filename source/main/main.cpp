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
#define VERSION "0.96 Beta"

#define MUPEN_DIR "uda0:/mupen64-360/"

#include <stdlib.h>
#include <unistd.h>

extern "C" {
	#include "main.h"
	#include "guifuncs.h"
	#include "rom.h"
	#include "../r4300/r4300.h"
	#include "../r4300/recomph.h"
	#include "../memory/memory.h"
	#include "winlnxdefs.h"
	#include "plugin.h"
	#include "savestates.h"
	#include "../memory/Saves.h"
}

#include <malloc.h>
#include <debug.h>
#include <diskio/ata.h>
#include <ppc/cache.h>
#include <ppc/timebase.h>
#include <pci/io.h>
#include <input/input.h>
#include <xenon_soc/xenon_power.h>
#include <usb/usbmain.h>
#include <console/console.h>
#include <xenon_sound/sound.h>
#include <xenon_smc/xenon_smc.h>
#include <time/time.h>
#include <xenos/xe.h>
#include "../xenos_gfx/Textures.h"
#include "config.h"

#undef X_OK
#include <zlx/Browser.h>
#include <zlx/Draw.h>
#include <zlx/Hw.h>

#undef hi
#undef lo
#define hi (reg[32])
#define lo (reg[33])

#if defined (__linux__)
#include <signal.h>
#endif

extern unsigned char inc_about[];
XenosSurface * tex_about;

ZLX::Font Font;
ZLX::Browser Browser;
lpBrowserActionEntry enh_action;
lpBrowserActionEntry cpu_action;
lpBrowserActionEntry lim_action;

int regular_quit=0;
int use_framelimit = 1;

int autoinc_slot = 0;
int *autoinc_save_slot = &autoinc_slot;

static char cwd[1024];
int p_noask=TRUE;

char g_WorkingDir[PATH_MAX];

char txtbuffer[1024];

int run_rom(char * romfile);

void ActionLaunchFile(char * filename) {
	if( run_rom(filename) ){
		sprintf(txtbuffer,"Could not load file:\n\n%s\n\nIt is probably not a N64 rom.",filename);
		Browser.Alert(txtbuffer);
	}else{
	}
}

void ActionShutdown(void * unused) {
    xenon_smc_power_shutdown();
	for(;;);
}

void ActionReboot(void * unused) {
    xenon_smc_power_reboot();
	for(;;);
}

void ActionXell(void * unused) {
    exit(0);
}

void SetEnhName(){
	if(cache.enable2xSaI)
        enh_action->name = "Texture enhancement: 2xSAI";
	else
        enh_action->name = "Texture enhancement: None";
}

void ActionAbout(void * other) {
	struct controller_data_s ctrl;
	float x=0.2,y=-0.75,nl=0.1;
	
	// Begin to draw
	Browser.Begin();

	ZLX::Draw::DrawTexturedRect(-1.0f, -1.0f, 2.0f, 2.0f, tex_about);

	Browser.getFont()->Begin();

	Browser.getFont()->DrawTextF("Mupen64-360 version " VERSION, -1, x,y);y+=nl;
	y+=nl*2;
	Browser.getFont()->DrawTextF("Credits:", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("Wii64 / Mupen64 teams (guess why :)", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("GliGli (Xbox 360 port)", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("Ced2911 (GUI library)", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("Razkar (Backgrounds)", -1, x,y);y+=nl;
	Browser.getFont()->DrawTextF("Everyone that contributed to libxenon", -1, x,y);y+=nl;

	Browser.getFont()->End();

	// Draw all text + form
	Browser.Render();

	// Draw is finished
	Browser.End();

	// Update code ...
	for(;;){
		usb_do_poll();
		if(get_controller_data(&ctrl, 0) && (ctrl.a || ctrl.b)){
			return;
		}
	}
}

void ActionToggleEnh(void * other) {
	cache.enable2xSaI=!cache.enable2xSaI;
	
	SetEnhName();
}

void SetCpuName(){
	if(dynacore==CORE_DYNAREC)
        cpu_action->name = "CPU core: Dynarec";
	else
        cpu_action->name = "CPU core: Interpreter";
}

void ActionToggleCpu(void * other) {
	if(dynacore==CORE_DYNAREC)
		dynacore=CORE_PURE_INTERPRETER;
	else
		dynacore=CORE_DYNAREC;
	
	SetCpuName();
}

void SetLimName(){
	if(use_framelimit)
        lim_action->name = "Framerate limiting: Yes";
	else
        lim_action->name = "Framerate limiting: No";
}

void ActionToggleLim(void * other) {
	use_framelimit=!use_framelimit;
	
	SetLimName();
}

void cls_GUI() {
	Browser.Begin();
	ZLX::Draw::DrawColoredRect(-1,-1,2,2,0xff000000);
	Xe_SetClearColor(ZLX::g_pVideoDevice,0xff000000);
	Browser.End();
}

void do_GUI() {

	tex_about = ZLX::loadPNGFromMemory(inc_about);
	
	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Controls / about Mupen64-360 ...";
        action->action = ActionAbout;
        action->param = NULL;
        Browser.AddAction(action);
    }
	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "-";
        action->action = NULL;
        action->param = NULL;
        Browser.AddAction(action);
    }
    {
        enh_action = new BrowserActionEntry();
        enh_action->param = NULL;
		SetEnhName();
        enh_action->action = ActionToggleEnh;
        Browser.AddAction(enh_action);
    }

    {
        cpu_action = new BrowserActionEntry();
        cpu_action->param = NULL;
		SetCpuName();
        cpu_action->action = ActionToggleCpu;
        Browser.AddAction(cpu_action);
    }

    {
        lim_action = new BrowserActionEntry();
        lim_action->param = NULL;
		SetLimName();
        lim_action->action = ActionToggleLim;
        Browser.AddAction(lim_action);
    }

	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "-";
        action->action = NULL;
        action->param = NULL;
        Browser.AddAction(action);
    }
	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Shutdown";
        action->action = ActionShutdown;
        action->param = NULL;
        Browser.AddAction(action);
    }
	{
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Reboot";
        action->action = ActionReboot;
        action->param = NULL;
        Browser.AddAction(action);
    }
    {
        lpBrowserActionEntry action = new BrowserActionEntry();
        action->name = "Return to Xell";
        action->action = ActionXell;
        action->param = NULL;
        Browser.AddAction(action);
    }

    Browser.SetLaunchAction(ActionLaunchFile);
    Browser.Run(MUPEN_DIR);
}


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
   Browser.SetProgressValue(p/100.0f);
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


int run_rom(char * romfile)
{
    if (rom_read(romfile)){
		if(rom_buf) free(rom_buf);
		if(ROM_HEADER) free(ROM_HEADER);
		return 1;
	}

	regular_quit=0;
	
	fileBrowser_file saveFile_dir;
	memset(&saveFile_dir,0,sizeof(fileBrowser_file));
	strcpy(saveFile_dir.name,MUPEN_DIR"saves/");
	
	cls_GUI();
	
    printf("Goodname:%s\n", ROM_SETTINGS.goodname);
	
	init_memory();

	plugin_load_plugins(NULL,NULL,NULL,NULL);

	romOpen_gfx();
	romOpen_audio();
	romOpen_input();

#ifdef USE_TLB_CACHE
	TLBCache_init();
#else
	tlb_mem2_init();
#endif

	cpu_init();
   
	if (loadEeprom(&saveFile_dir)==1) printf("eeprom loaded!\n");
	if (loadSram(&saveFile_dir)==1) printf("sram loaded!\n");
	if (loadMempak(&saveFile_dir)==1) printf("mempak loaded!\n");
	if (loadFlashram(&saveFile_dir)==1) printf("flash loaded!\n");
				
	go();
   
	if(regular_quit){
		if (saveEeprom(&saveFile_dir)==1) printf("eeprom saved!\n");
		if (saveSram(&saveFile_dir)==1) printf("sram saved!\n");
		if (saveMempak(&saveFile_dir)==1) printf("mempak saved!\n");
		if (saveFlashram(&saveFile_dir)==1) printf("flash saved!\n");
	}
				
	cpu_deinit();
 
	romClosed_RSP();
	romClosed_input();
	romClosed_audio();
	romClosed_gfx();
	
	free(rom_buf);
	free(ROM_HEADER);
	rom_buf=NULL;
	ROM_HEADER=NULL;
	
	free_memory();

	return 0;
}

int main ()
{
	ZLX::InitialiseVideo();
	console_set_colors(0xD8444E00,0x00ffff00); // yellow on blue
	console_init();

	printf("\nMupen64-360 version : %s\n\n", VERSION);

	xenon_sound_init();

	ZLX::Hw::SystemInit(ZLX::INIT_USB|ZLX::INIT_ATA|ZLX::INIT_ATAPI|ZLX::INIT_FILESYSTEM);
	ZLX::Hw::SystemPoll();

	strcpy(cwd, MUPEN_DIR);
	while(cwd[strlen(cwd)-1] != '/') cwd[strlen(cwd)-1] = '\0';
	strcpy(g_WorkingDir, cwd);

	//dynacore=CORE_INTERPRETER; // interpreter
	//dynacore=CORE_PURE_INTERPRETER; // pure interpreter
	dynacore=CORE_DYNAREC; //  dynamic recompiler

	console_close();

	do_GUI();

	return 0;
}

static CONTROL_INFO control_info;

void initiateControllers(CONTROL_INFO ControlInfo)
{
	int i;
	control_info = ControlInfo;
	for(i=0;i<4;++i) control_info.Controls[i].Present = TRUE;
	control_info.Controls[0].Plugin = PLUGIN_MEMPAK;
}

#define	STICK_DEAD_ZONE (32768*0.4)
#define HANDLE_STICK_DEAD_ZONE(x) ((((x)>-STICK_DEAD_ZONE) && (x)<STICK_DEAD_ZONE)?0:(x-x/abs(x)*STICK_DEAD_ZONE))

#define	TRIGGER_DEAD_ZONE (256*0.3)
#define HANDLE_TRIGGER_DEAD_ZONE(x) (((x)<TRIGGER_DEAD_ZONE)?0:(x-TRIGGER_DEAD_ZONE))

#define TRIGGER_THRESHOLD 100
#define STICK_THRESHOLD 25000

void getKeys(int Control, BUTTONS *Keys)
{
    static struct controller_data_s cdata[3],*c;
    BUTTONS b;

	usb_do_poll();

	get_controller_data(&cdata[Control], Control);
	c=&cdata[Control];

    if (c->back){
		stop=1;
		regular_quit=1;
	}
	
    b.START_BUTTON=c->start;
        
    b.A_BUTTON=c->a;
    b.B_BUTTON=c->b;
    b.Z_TRIG=c->x || c->y || (c->rt>TRIGGER_THRESHOLD) || (c->lt>TRIGGER_THRESHOLD);
        
    b.L_TRIG=c->lb;
    b.R_TRIG=c->rb;
        
    b.U_DPAD=c->up;
    b.D_DPAD=c->down;
    b.L_DPAD=c->left;
    b.R_DPAD=c->right;

    b.X_AXIS=HANDLE_STICK_DEAD_ZONE(c->s1_x)/256;
    b.Y_AXIS=HANDLE_STICK_DEAD_ZONE(c->s1_y)/256;

    b.D_CBUTTON=c->s2_y<-STICK_THRESHOLD;
    b.U_CBUTTON=c->s2_y>STICK_THRESHOLD;
    b.L_CBUTTON=c->s2_x<-STICK_THRESHOLD;
    b.R_CBUTTON=c->s2_x>STICK_THRESHOLD;

    Keys->Value = b.Value;
}

int saveFile_readFile(fileBrowser_file* file, void* buffer, unsigned int length){
        FILE* f = fopen( file->name, "rb" );
        if(!f) return FILE_BROWSER_ERROR;
        
        fseek(f, file->offset, SEEK_SET);
        int bytes_read = fread(buffer, 1, length, f);
        if(bytes_read > 0) file->offset += bytes_read;
        
        fclose(f);
        return bytes_read;
}

int saveFile_writeFile(fileBrowser_file* file, void* buffer, unsigned int length){
        FILE* f = fopen( file->name, "wb" );
        if(!f) return FILE_BROWSER_ERROR;
        
        fseek(f, file->offset, SEEK_SET);
        int bytes_read = fwrite(buffer, 1, length, f);
        if(bytes_read > 0) file->offset += bytes_read;
        
        fclose(f);
        return bytes_read;
}
