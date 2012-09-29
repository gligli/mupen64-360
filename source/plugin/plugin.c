/**
 * Mupen64 - plugin.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <debug.h>

#include "winlnxdefs.h"
#include "plugin.h"
#include "main/rom.h"
#include "main/main.h"
#include "memory/memory.h"
#include "r4300/interupt.h"
#include "r4300/r4300.h"

CONTROL Controls[4];

gfx_plugin_functions gfx = {NULL};
audio_plugin_functions audio = {NULL};
input_plugin_functions input = {NULL};
rsp_plugin_functions rsp = {NULL};

static GFX_INFO gfx_info = {NULL};
AUDIO_INFO audio_info = {NULL};
static CONTROL_INFO control_info = {NULL};
static RSP_INFO rsp_info = {NULL};

extern void ChangeWindow();
extern BOOL InitiateGFX(GFX_INFO Gfx_Info);
extern void ProcessDList();
extern void ProcessRDPList();
extern void RomClosed();
extern void RomOpen();
extern void ShowCFB();
extern void UpdateScreen();
extern void ViStatusChanged();
extern void ViWidthChanged();

extern void aiDacrateChanged(int SystemType);
extern void aiLenChanged();
extern DWORD aiReadLength();
extern BOOL initiateAudio(AUDIO_INFO Audio_Info);
extern void processAList();
extern void romClosed_audio();
extern void romOpen_audio();

extern void getKeys(int Control, BUTTONS *Keys);
extern void initiateControllers(CONTROL_INFO ControlInfo);
extern void controllerCommand(int Control, unsigned char *Command);
extern void readController(int Control, unsigned char *Command);

extern DWORD doRspCycles(DWORD Cycles);
extern void initiateRSP(RSP_INFO Rsp_Info, DWORD * CycleCount);
extern void romClosed_RSP();

static unsigned int dummy;

/* local functions */
static void EmptyFunc(void)
{
}

void plugin_load_plugins(const char *gfx_name,
		const char *audio_name,
		const char *input_name,
		const char *RSP_name) {
	int i;

    /* fill in the GFX_INFO data structure */
    gfx_info.HEADER = (unsigned char *) rom;
    gfx_info.RDRAM = (unsigned char *) rdram;
    gfx_info.DMEM = (unsigned char *) SP_DMEM;
    gfx_info.IMEM = (unsigned char *) SP_IMEM;
    gfx_info.MI_INTR_REG = &(MI_register.mi_intr_reg);
    gfx_info.DPC_START_REG = &(dpc_register.dpc_start);
    gfx_info.DPC_END_REG = &(dpc_register.dpc_end);
    gfx_info.DPC_CURRENT_REG = &(dpc_register.dpc_current);
    gfx_info.DPC_STATUS_REG = &(dpc_register.dpc_status);
    gfx_info.DPC_CLOCK_REG = &(dpc_register.dpc_clock);
    gfx_info.DPC_BUFBUSY_REG = &(dpc_register.dpc_bufbusy);
    gfx_info.DPC_PIPEBUSY_REG = &(dpc_register.dpc_pipebusy);
    gfx_info.DPC_TMEM_REG = &(dpc_register.dpc_tmem);
    gfx_info.VI_STATUS_REG = &(vi_register.vi_status);
    gfx_info.VI_ORIGIN_REG = &(vi_register.vi_origin);
    gfx_info.VI_WIDTH_REG = &(vi_register.vi_width);
    gfx_info.VI_INTR_REG = &(vi_register.vi_v_intr);
    gfx_info.VI_V_CURRENT_LINE_REG = &(vi_register.vi_current);
    gfx_info.VI_TIMING_REG = &(vi_register.vi_burst);
    gfx_info.VI_V_SYNC_REG = &(vi_register.vi_v_sync);
    gfx_info.VI_H_SYNC_REG = &(vi_register.vi_h_sync);
    gfx_info.VI_LEAP_REG = &(vi_register.vi_leap);
    gfx_info.VI_H_START_REG = &(vi_register.vi_h_start);
    gfx_info.VI_V_START_REG = &(vi_register.vi_v_start);
    gfx_info.VI_V_BURST_REG = &(vi_register.vi_v_burst);
    gfx_info.VI_X_SCALE_REG = &(vi_register.vi_x_scale);
    gfx_info.VI_Y_SCALE_REG = &(vi_register.vi_y_scale);
    gfx_info.CheckInterrupts = EmptyFunc;
	
	gfx.changeWindow = ChangeWindow;
	gfx.initiateGFX = InitiateGFX;
	gfx.processDList = ProcessDList;
	gfx.processRDPList = ProcessRDPList;
	gfx.romClosed = RomClosed;
	gfx.romOpen = RomOpen;
	gfx.showCFB = ShowCFB;
	gfx.updateScreen = UpdateScreen;
	gfx.viStatusChanged = ViStatusChanged;
	gfx.viWidthChanged = ViWidthChanged;

	gfx.initiateGFX(gfx_info);

    /* fill in the AUDIO_INFO data structure */
    audio_info.RDRAM = (unsigned char *) rdram;
    audio_info.DMEM = (unsigned char *) SP_DMEM;
    audio_info.IMEM = (unsigned char *) SP_IMEM;
    audio_info.MI_INTR_REG = &(MI_register.mi_intr_reg);
    audio_info.AI_DRAM_ADDR_REG = &(ai_register.ai_dram_addr);
    audio_info.AI_LEN_REG = &(ai_register.ai_len);
    audio_info.AI_CONTROL_REG = &(ai_register.ai_control);
    audio_info.AI_STATUS_REG = &dummy;
    audio_info.AI_DACRATE_REG = &(ai_register.ai_dacrate);
    audio_info.AI_BITRATE_REG = &(ai_register.ai_bitrate);
    audio_info.CheckInterrupts = EmptyFunc;
	
	audio.aiDacrateChanged = aiDacrateChanged;
	audio.aiLenChanged = aiLenChanged;
	audio.initiateAudio = initiateAudio;
	audio.processAList = processAList;
	audio.romClosed = romClosed_audio;
	audio.romOpen = romOpen_audio;

	audio.initiateAudio(audio_info);

	control_info.Controls = Controls;
	for (i = 0; i < 4; i++) {
		Controls[i].Present = FALSE;
		Controls[i].RawData = FALSE;
		Controls[i].Plugin = PLUGIN_NONE;
	}
	
	input.getKeys = getKeys;
	input.initiateControllers = initiateControllers;
	input.controllerCommand = controllerCommand;
	input.readController = readController;
	
	input.initiateControllers(control_info);

    /* fill in the RSP_INFO data structure */
    rsp_info.RDRAM = (unsigned char *) rdram;
    rsp_info.DMEM = (unsigned char *) SP_DMEM;
    rsp_info.IMEM = (unsigned char *) SP_IMEM;
    rsp_info.MI_INTR_REG = &MI_register.mi_intr_reg;
    rsp_info.SP_MEM_ADDR_REG = &sp_register.sp_mem_addr_reg;
    rsp_info.SP_DRAM_ADDR_REG = &sp_register.sp_dram_addr_reg;
    rsp_info.SP_RD_LEN_REG = &sp_register.sp_rd_len_reg;
    rsp_info.SP_WR_LEN_REG = &sp_register.sp_wr_len_reg;
    rsp_info.SP_STATUS_REG = &sp_register.sp_status_reg;
    rsp_info.SP_DMA_FULL_REG = &sp_register.sp_dma_full_reg;
    rsp_info.SP_DMA_BUSY_REG = &sp_register.sp_dma_busy_reg;
    rsp_info.SP_PC_REG = &rsp_register.rsp_pc;
    rsp_info.SP_SEMAPHORE_REG = &sp_register.sp_semaphore_reg;
    rsp_info.DPC_START_REG = &dpc_register.dpc_start;
    rsp_info.DPC_END_REG = &dpc_register.dpc_end;
    rsp_info.DPC_CURRENT_REG = &dpc_register.dpc_current;
    rsp_info.DPC_STATUS_REG = &dpc_register.dpc_status;
    rsp_info.DPC_CLOCK_REG = &dpc_register.dpc_clock;
    rsp_info.DPC_BUFBUSY_REG = &dpc_register.dpc_bufbusy;
    rsp_info.DPC_PIPEBUSY_REG = &dpc_register.dpc_pipebusy;
    rsp_info.DPC_TMEM_REG = &dpc_register.dpc_tmem;
    rsp_info.CheckInterrupts = EmptyFunc;
    rsp_info.ProcessDlistList = gfx.processDList;
    rsp_info.ProcessAlistList = audio.processAList;
    rsp_info.ProcessRdpList = gfx.processRDPList;
    rsp_info.ShowCFB = gfx.showCFB;
	
	rsp.doRspCycles = doRspCycles;
	rsp.initiateRSP = initiateRSP;
	rsp.romClosed = romClosed_RSP;	
	
	rsp.initiateRSP(rsp_info, NULL);

}
