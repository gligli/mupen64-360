/**
 * Wii64 - Saves.h
 * Copyright (C) 2007, 2008, 2009 Mike Slegeir
 * 
 * Defines/globals for saving files
 *
 * Wii64 homepage: http://www.emulatemii.com
 * email address: tehpola@gmail.com
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


#ifndef SAVES_H
#define SAVES_H

/*
extern char saveEnabled;
extern int  savetype;
extern char savepath[];

#define SELECTION_SLOT_A    0
#define SELECTION_SLOT_B    1
#define SELECTION_TYPE_SD   2
#define SELECTION_TYPE_MEM  0*/

// Return 0 if load/save fails, 1 otherwise

#include <stdint.h>

#define FILE_BROWSER_MAX_PATH_LEN 256

#define FILE_BROWSER_ATTR_DIR     0x10

#define FILE_BROWSER_ERROR         -1
#define FILE_BROWSER_ERROR_NO_FILE -2

#define FILE_BROWSER_SEEK_SET 1
#define FILE_BROWSER_SEEK_CUR 2
#define FILE_BROWSER_SEEK_END 3

typedef struct {
        char         name[FILE_BROWSER_MAX_PATH_LEN];
        uint64_t discoffset; // Only necessary for DVD
        unsigned int offset; // Keep track of our offset in the file
        unsigned int size;
        unsigned int attr;
} fileBrowser_file;


int loadEeprom(fileBrowser_file* savepath);
int saveEeprom(fileBrowser_file* savepath);

int loadMempak(fileBrowser_file* savepath);
int saveMempak(fileBrowser_file* savepath);

int loadSram(fileBrowser_file* savepath);
int saveSram(fileBrowser_file* savepath);

int loadFlashram(fileBrowser_file* savepath);
int saveFlashram(fileBrowser_file* savepath);

int saveFile_readFile(fileBrowser_file* file, void* buffer, unsigned int length);
int saveFile_writeFile(fileBrowser_file* file, void* buffer, unsigned int length);

#endif

