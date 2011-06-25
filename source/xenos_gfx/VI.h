/**
 * glN64_GX - VI.h
 * Copyright (C) 2003 Orkin
 * Copyright (C) 2008, 2009 sepp256 (Port to Wii/Gamecube/PS3)
 *
 * glN64 homepage: http://gln64.emulation64.com
 * Wii64 homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
**/

#ifndef VI_H
#define VI_H
#include "Types.h"

struct VIInfo
{
	u32 width, height;
	u32 lastOrigin;
#ifdef __GX__
	unsigned int* xfb[2];
	int which_fb;
	bool updateOSD;
	bool enableLoadIcon;
	bool EFBcleared;
	bool copy_fb;
#endif // __GX__
};

extern VIInfo VI;

void VI_UpdateSize();
void VI_UpdateScreen();

#endif

