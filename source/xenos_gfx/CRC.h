/**
 * glN64_GX - CRC.h
 * Copyright (C) 2003 Orkin
 * Copyright (C) 2008, 2009 sepp256 (Port to Wii/Gamecube/PS3)
 *
 * glN64 homepage: http://gln64.emulation64.com
 * Wii64 homepage: http://www.emulatemii.com
 * email address: sepp256@gmail.com
 *
**/

# include "../main/winlnxdefs.h"

void CRC_BuildTable();

DWORD CRC_Calculate( DWORD crc, void *buffer, DWORD count );
DWORD CRC_CalculatePalette( DWORD crc, void *buffer, DWORD count );
