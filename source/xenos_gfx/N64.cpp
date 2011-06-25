/**
 * glN64_GX - N64.cpp
 * Copyright (C) 2003 Orkin
 *
 * glN64 homepage: http://gln64.emulation64.com
 * Wii64 homepage: http://www.emulatemii.com
 *
**/

#include "../main/winlnxdefs.h"
#include "N64.h"
#include "Types.h"

u8 *DMEM;
u8 *IMEM;
u64 TMEM[512];
u8 *RDRAM;
u32 RDRAMSize;

N64Regs REG;
