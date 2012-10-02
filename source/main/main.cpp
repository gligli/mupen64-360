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

#define M64P_CORE_PROTOTYPES 1

#include "version.h"

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "winlnxdefs.h"

extern "C" {
	#include "main.h"
	#include "guifuncs.h"
	#include "rom.h"
	#include "r4300/r4300.h"
	#include "r4300/recomph.h"
	#include "memory/memory.h"
	#include "plugin/plugin.h"
	#include "savestates.h"

	#include "api/config_core.h"
	#include "api/callbacks.h"
}

#include "api/m64p_config.h"


#include <malloc.h>
#include <signal.h>
#include <math.h>
#include <stdarg.h>

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

#undef MAX_PATH
#undef X_OK
#include <zlx/Browser.h>
#include <zlx/Draw.h>
#include <zlx/Hw.h>

#include "zip/unzip.h"

#include "Rice_GX_Xenos/math.h"
#include "Rice_GX_Xenos/COLOR.h"
#include "Rice_GX_Xenos/Config.h"
#include "Rice_GX_Xenos/Config.h"
#include "xenon_input/input.h"

/* version number for Core config section */
#define CONFIG_PARAM_VERSION 1.01

extern unsigned char inc_about[];
XenosSurface * tex_about;

ZLX::Font Font;
ZLX::Browser Browser;
lpBrowserActionEntry enh_action;
lpBrowserActionEntry cpu_action;
lpBrowserActionEntry lim_action;
lpBrowserActionEntry pad_action;

m64p_handle g_CoreConfig = NULL;
int g_MemHasBeenBSwapped = 0;

int use_framelimit = 1;
int use_expansion = 1;
int pad_mode = 0;

extern SettingInfo TextureEnhancementSettings[];

char txtbuffer[1024];

int run_rom(char * romfile);

void WaitNoButtonPress() {
	struct controller_data_s c = {0};

	for(;;){
		usb_do_poll();
		get_controller_data(&c, 0);
		
		if(!(c.a || c.b || c.x || c.y || c.back || c.logo || c.start ||
				c.rb || c.lb || c.up || c.down || c.left || c.right))
			return;
	}
}

void ActionAbout(void * other) {
	struct controller_data_s ctrl = {0};
	float x=0.2,y=-0.75,nl=0.1;
	
	WaitNoButtonPress();

	// Begin to draw
	Browser.Begin();

	ZLX::Draw::DrawTexturedRect(-1.0f, -1.0f, 2.0f, 2.0f, tex_about);

	Browser.getFont()->Begin();

	Browser.getFont()->DrawTextF(MUPEN_NAME " version " MUPEN_VERSION, -1, x,y);y+=nl;
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

	for(;;){
		usb_do_poll();
		get_controller_data(&ctrl, 0);
		
		if(ctrl.a || ctrl.b){
			return;
		}
	}

	WaitNoButtonPress();
}

void ActionLaunchFile(char * filename) {

	WaitNoButtonPress();

	if( run_rom(filename) )
	{
		sprintf(txtbuffer,"Could not load file:\n\n%s\n\nIt is probably not a N64 rom.",filename);
		Browser.Alert(txtbuffer);
	}
	
	WaitNoButtonPress();
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
#ifdef XENOS_GFX
	if(cache.enable2xSaI)
        enh_action->name = "Texture enhancement: 2xSAI";
	else
        enh_action->name = "Texture enhancement: None";
#else
	static char en[256]="";
	
	strcpy(en,"Textures: ");
	strcat(en,TextureEnhancementSettings[options.textureEnhancement].description);
	enh_action->name = en;
#endif
}

void ActionToggleEnh(void * other) {
#ifdef XENOS_GFX
	cache.enable2xSaI=!cache.enable2xSaI;
#else
	options.textureEnhancement=(options.textureEnhancement+1)%TEXTURE_SHARPEN_ENHANCEMENT;
#endif
	
	SetEnhName();
}

void SetCpuName(){
	switch(r4300emu)
	{
		case CORE_DYNAREC:
	        cpu_action->name = "CPU core: Dynarec";
			break;
		case CORE_INTERPRETER:
	        cpu_action->name = "CPU core: Interpreter (cached)";
			break;
		case CORE_PURE_INTERPRETER:
	        cpu_action->name = "CPU core: Interpreter";
			break;
	}
}

void ActionToggleCpu(void * other) {
	if (r4300emu==CORE_PURE_INTERPRETER)
		r4300emu=CORE_DYNAREC;
	else
		r4300emu=CORE_PURE_INTERPRETER;
	
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


void SetPadName(){
	static char pm[256]="";
	
	strcpy(pm,"Controls (l->r): ");
	strcat(pm,pad_mode_name[pad_mode]);
	
	pad_action->name=pm;
}

void ActionTogglePad(void * other) {
	pad_mode=(pad_mode+1)%6;
	
	SetPadName();
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
        pad_action = new BrowserActionEntry();
        pad_action->param = NULL;
		SetPadName();
        pad_action->action = ActionTogglePad;
        Browser.AddAction(pad_action);
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
	
	WaitNoButtonPress();
	
    Browser.Run(MUPEN_DIR);
}

void display_loading_progress(int p)
{
   Browser.SetProgressValue(p/100.0f);
}

void new_frame()
{
}

void new_vi()
{
}

const char *get_savestatepath(void)
{
    return MUPEN_DIR"sstates/";
}

const char *get_savesrampath(void)
{
    return MUPEN_DIR"saves/";
}

void main_message(m64p_msg_level level, unsigned int corner, const char *format, ...)
{
    va_list ap;
    char buffer[2049];
    va_start(ap, format);
    vsnprintf(buffer, 2047, format, ap);
    buffer[2048]='\0';
    va_end(ap);

    DebugMessage(level, "%s", buffer);
}

int main_set_core_defaults(void)
{
    float fConfigParamsVersion;
    int bSaveConfig = 0, bUpgrade = 0;

    if (ConfigGetParameter(g_CoreConfig, "Version", M64TYPE_FLOAT, &fConfigParamsVersion, sizeof(float)) != M64ERR_SUCCESS)
    {
        DebugMessage(M64MSG_WARNING, "No version number in 'Core' config section. Setting defaults.");
        ConfigDeleteSection("Core");
        ConfigOpenSection("Core", &g_CoreConfig);
        bSaveConfig = 1;
    }
    else if (((int) fConfigParamsVersion) != ((int) CONFIG_PARAM_VERSION))
    {
        DebugMessage(M64MSG_WARNING, "Incompatible version %.2f in 'Core' config section: current is %.2f. Setting defaults.", fConfigParamsVersion, (float) CONFIG_PARAM_VERSION);
        ConfigDeleteSection("Core");
        ConfigOpenSection("Core", &g_CoreConfig);
        bSaveConfig = 1;
    }
    else if ((CONFIG_PARAM_VERSION - fConfigParamsVersion) >= 0.0001f)
    {
        float fVersion = (float) CONFIG_PARAM_VERSION;
        ConfigSetParameter(g_CoreConfig, "Version", M64TYPE_FLOAT, &fVersion);
        DebugMessage(M64MSG_INFO, "Updating parameter set version in 'Core' config section to %.2f", fVersion);
        bUpgrade = 1;
        bSaveConfig = 1;
    }

    /* parameters controlling the operation of the core */
    ConfigSetDefaultFloat(g_CoreConfig, "Version", (float) CONFIG_PARAM_VERSION,  "Mupen64Plus Core config parameter set version number.  Please don't change this version number.");
    ConfigSetDefaultBool(g_CoreConfig, "OnScreenDisplay", 1, "Draw on-screen display if True, otherwise don't draw OSD");
#if defined(DYNAREC)
    ConfigSetDefaultInt(g_CoreConfig, "R4300Emulator", 2, "Use Pure Interpreter if 0, Cached Interpreter if 1, or Dynamic Recompiler if 2 or more");
#else
    ConfigSetDefaultInt(g_CoreConfig, "R4300Emulator", 1, "Use Pure Interpreter if 0, Cached Interpreter if 1, or Dynamic Recompiler if 2 or more");
#endif
    ConfigSetDefaultBool(g_CoreConfig, "NoCompiledJump", 0, "Disable compiled jump commands in dynamic recompiler (should be set to False) ");
    ConfigSetDefaultBool(g_CoreConfig, "DisableExtraMem", 0, "Disable 4MB expansion RAM pack. May be necessary for some games");
    ConfigSetDefaultBool(g_CoreConfig, "AutoStateSlotIncrement", 0, "Increment the save state slot after each save operation");
    ConfigSetDefaultBool(g_CoreConfig, "EnableDebugger", 0, "Activate the R4300 debugger when ROM execution begins, if core was built with Debugger support");
    ConfigSetDefaultInt(g_CoreConfig, "CurrentStateSlot", 0, "Save state slot (0-9) to use when saving/loading the emulator state");
    ConfigSetDefaultString(g_CoreConfig, "ScreenshotPath", "", "Path to directory where screenshots are saved. If this is blank, the default value of ${UserConfigPath}/screenshot will be used");
    ConfigSetDefaultString(g_CoreConfig, "SaveStatePath", "", "Path to directory where emulator save states (snapshots) are saved. If this is blank, the default value of ${UserConfigPath}/save will be used");
    ConfigSetDefaultString(g_CoreConfig, "SaveSRAMPath", "", "Path to directory where SRAM/EEPROM data (in-game saves) are stored. If this is blank, the default value of ${UserConfigPath}/save will be used");
    ConfigSetDefaultString(g_CoreConfig, "SharedDataPath", "", "Path to a directory to search when looking for shared data files");

    /* handle upgrades */
    if (bUpgrade)
    {
        if (fConfigParamsVersion < 1.01f)
        {  // added separate SaveSRAMPath parameter in v1.01
            const char *pccSaveStatePath = ConfigGetParamString(g_CoreConfig, "SaveStatePath");
            if (pccSaveStatePath != NULL)
                ConfigSetParameter(g_CoreConfig, "SaveSRAMPath", M64TYPE_STRING, pccSaveStatePath);
        }
    }

    if (bSaveConfig)
        ConfigSaveSection("Core");

    return 0;
}

u8 * alloc_read_file (char *filename, u32 & osize)
{
	u32 size=0;
	u8 * buf;
	
	unzFile zip;
	unz_file_info pfile_info;
	unsigned long zbuf;
	char szFileName[255], extraField[255], szComment[255];
	zip = unzOpen(filename);
	if (zip != NULL)
	{
		unzGoToFirstFile(zip);
		do
		{
			unzGetCurrentFileInfo(zip, &pfile_info, szFileName, 255,
					extraField, 255, szComment, 255);
			unzOpenCurrentFile(zip);
			if (pfile_info.uncompressed_size >= 4)
			{
				unzReadCurrentFile(zip, &zbuf, 4);
				if (zbuf != 0x40123780 && zbuf != 0x12408037 &&
					zbuf != 0x37804012 && zbuf != 0x80371240)
				{
					unzCloseCurrentFile(zip);
				}
				else
				{
					size = pfile_info.uncompressed_size;
					unzCloseCurrentFile(zip);
					break;
				}
			}
		}
		while (unzGoToNextFile(zip) != UNZ_END_OF_LIST_OF_FILE);

		if(!size)
		{
			unzClose(zip);
		}
	}	
	
	if(!size)
	{
		int f = open(filename, O_RDONLY);
		if (f < 0)
		{
			return NULL;
		}

		struct stat s;
		stat(filename, &s);

		size = s.st_size;
		buf=(u8*)malloc(size);
		
		printf("Plain ROM file, size=%d\n",size);

		int r = read(f, buf, size);
		if (r < 0)
		{
			close(f);
			free(buf);
			return NULL;
		}
	}
	else
	{
		u32 i,tmp=0;
		
		printf("Zipped ROM file, size=%d\n",size);

		buf=(u8*)malloc(size);
		unzOpenCurrentFile(zip);
		for (i = 0; i < size; i += unzReadCurrentFile(zip, buf + i, 65536))
		{
			if (tmp != (int) ((i / (float) size)*20))
			{
				tmp = (int) (i / (float) (size)*20);
				display_loading_progress(tmp*5);
			}
		}
		unzCloseCurrentFile(zip);
		unzClose(zip);
		display_loading_progress(100);
	}

	osize=size;
	
	return buf;
}

int run_rom(char * romfile)
{
    u32 rom_size;
    unsigned char * rom_data=alloc_read_file(romfile,rom_size);
	
	if (!rom_data)
	{
		return 1;
	}
	
	m64p_error err=open_rom(rom_data,rom_size);
	
	if(err!=M64ERR_SUCCESS)
	{
		return 2;
	}

	cls_GUI();
	
    if (g_MemHasBeenBSwapped == 0)
    {
        init_memory(1);
        g_MemHasBeenBSwapped = 1;
    }
    else
    {
        init_memory(0);
    }

	plugin_load_plugins(NULL,NULL,NULL,NULL);

	gfx.romOpen();
	audio.romOpen();

	cpu_init();
   
	go();
   
	rsp.romClosed();
	audio.romClosed();
	gfx.romClosed();
	
	free_memory();
	
	close_rom();

	free(rom_data);
	rom_data=NULL;
	
	return 0;
}



int main ()
{
	ZLX::InitialiseVideo();
	console_set_colors(0xD8444E00,0x00ffff00); // yellow on blue
	console_init();

	printf("\n" MUPEN_NAME " version " MUPEN_VERSION "\n\n");

	xenon_sound_init();

	ZLX::Hw::SystemInit(ZLX::INIT_USB|ZLX::INIT_ATA|ZLX::INIT_ATAPI|ZLX::INIT_FILESYSTEM);
	ZLX::Hw::SystemPoll();

	r4300emu=CORE_DYNAREC;
	
	console_close();
	
	ConfigInit(MUPEN_DIR,MUPEN_DIR);
	romdatabase_open();
	
	main_set_core_defaults();

	do_GUI();

	ConfigShutdown();
	romdatabase_close();

	return 0;
}
