/**
 * glN64_GX - Debug.h
 * Copyright (C) 2003 Orkin
 *
 * glN64 homepage: http://gln64.emulation64.com
 * Wii64 homepage: http://www.emulatemii.com
 *
**/
#if !defined( DEBUGGFX_H ) && defined(DEBUG)
#define DEBUGGFX_H

#include "../main/winlnxdefs.h"
#include <stdio.h>
#include <stdarg.h>

#define		DEBUG_LOW		0x1000
#define		DEBUG_MEDIUM	0x2000
#define		DEBUG_HIGH		0x4000
#define		DEBUG_DETAIL	0x8000

#define		DEBUG_HANDLED	0x0001
#define		DEBUG_UNHANDLED 0x0002
#define		DEBUG_IGNORED	0x0004
#define		DEBUG_UNKNOWN	0x0008
#define		DEBUG_ERROR		0x0010
#define		DEBUG_COMBINE	0x0020
#define		DEBUG_TEXTURE	0x0040
#define		DEBUG_VERTEX	0x0080
#define		DEBUG_TRIANGLE	0x0100
#define		DEBUG_MATRIX	0x0200

#define OpenDebugDlg()
#define CloseDebugDlg()
#define DebugRSPState(pci, pc, cmd, w0, w1 )
static inline void DebugMsg( WORD type, const char * format, ... ){
  va_list args;
  
  if (type&DEBUG_DETAIL) return;
  
  va_start (args, format);
  printf("[xenos_gfx] ");
  vprintf (format, args);
  va_end (args);
}

#endif // DEBUG_H
