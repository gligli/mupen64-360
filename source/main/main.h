/***************************************************************************
                          main.h  -  description
                             -------------------
    begin                : Mon 23 Mai 2005
    copyright            : (C) 2005 by jogibear
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef __MAIN_H__
#define __MAIN_H__

#define USE_FRAMELIMIT
//#define USE_VB_POOL
#define USE_THREADED_AUDIO

#define CORE_INTERPRETER		(0)
#define CORE_DYNAREC			(1)
#define CORE_PURE_INTERPRETER	(2)

#ifndef PATH_MAX
#  define PATH_MAX 1024
#endif

extern char g_WorkingDir[];
#endif // __MAIN_H__
