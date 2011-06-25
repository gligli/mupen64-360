/***************************************************************************
                          config.c  -  description
                             -------------------
    begin                : Fri Nov 8 2002
    copyright            : (C) 2002 by blight
    email                : blight@Ashitaka
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "config.h"

#include "main.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define log_warn( args... ) printf( args )

typedef struct _SConfigValue
{
	char *key;		// key - string
	char *cValue;	// value - string
	int   iValue;	// value - integer
	int   bValue;	// value - bool

	struct _SConfigValue *next;
} SConfigValue;

typedef struct _SConfigSection
{
	char         *name;
	SConfigValue *values;

	struct _SConfigSection *next;
} SConfigSection;

SConfigSection *m_config = 0;
SConfigSection *m_configSection = 0; // selected section

/** helper funcs **/

static void
trim( char *str )
{
	char *p = str;
	while (isspace(*p))
		p++;
	if (str != p)
		strcpy( str, p );

	p = str + strlen( str ) - 1;
	if (p > str)
	{
		while (isspace(*p))
			p--;
		*(++p) = '\0';
	}
}

static SConfigValue *
config_newValue()
{
	SConfigValue *val = malloc( sizeof( SConfigValue ) );
	if (val == 0)
		return 0;
	memset( val, 0, sizeof( SConfigValue ) );

	if (m_configSection->values == 0)
		m_configSection->values = val;
	else
	{
		SConfigValue *p = m_configSection->values;
		while (p->next != 0)
			p = p->next;
		p->next = val;
	}

	return val;
}


static SConfigValue *
config_findValue( const char *key )
{
	if (!m_configSection)
		config_set_section( "Default" );

	SConfigValue *val = m_configSection->values;
	for (; val != 0; val = val->next)
	{
		if (strcasecmp( key, val->key ) == 0)
			return val;
	}
	return 0;
}


static SConfigSection *
config_newSection()
{
	SConfigSection *sec = malloc( sizeof( SConfigSection ) );
	if (sec == 0)
		return 0;
	memset( sec, 0, sizeof( SConfigSection ) );

	if (m_config == 0)
		m_config = sec;
	else
	{
		SConfigSection *p = m_config;
		while (p->next != 0)
			p = p->next;
		p->next = sec;
	}

	return sec;
}


static SConfigSection *
config_findSection( const char *section )
{
	SConfigSection *sec = m_config;
	for (; sec != 0; sec = sec->next)
	{
		if (strcasecmp( section, sec->name ) == 0)
			return sec;
	}
	return 0;
}


/* config API */

int
config_set_section( const char *section )
{
	SConfigSection *sec = config_findSection( section );
	int ret = 0;

	if (sec == 0)
	{
		ret = -1;
		sec = config_newSection();
		sec->name = strdup( section );
	}
	m_configSection = sec;

	return ret;
}


const char *
config_get_string( const char *key, const char *def )
{
	SConfigValue *val = config_findValue( key );
	if (!val)
		return def;

	return val->cValue;
}

int config_get_number( const char *key, int def )
{
	SConfigValue *val = config_findValue( key );
	if (!val)
		return def;

	return val->iValue;
}

int config_get_bool( const char *key, int def )
{
	SConfigValue *val = config_findValue( key );
	if (!val)
		return def;

	return val->bValue;
}


void config_put_string( const char *key, const char *value )
{
	SConfigValue *val = config_findValue( key );
	if (!val)
	{
		val = config_newValue();
		val->key = strdup( key );
	}

	if (val->cValue)
		free( val->cValue );
	val->cValue = strdup( value );
	val->iValue = atoi( val->cValue );
	val->bValue = val->iValue;
	if (strcasecmp( val->cValue, "yes" ) == 0)
		val->bValue = 1;
	else if (strcasecmp( val->cValue, "true" ) == 0)
		val->bValue = 1;
}

void config_put_number( const char *key, int value )
{
	char buf[50];
	snprintf( buf, 50, "%d", value );
	config_put_string( key, buf );
}

void config_put_bool( const char *key, int value )
{
	config_put_string( key, (value != 0) ? ("true") : ("false") );
}


void
config_read( void )
{
	FILE *f;
	char filename[PATH_MAX];
	char line[2048];
	char *p;
	int linelen;

	config_set_section( "Default" );

	snprintf( filename, PATH_MAX, "%s/mupen64.conf", g_WorkingDir );
	f = fopen( filename, "r" );
	if( f == NULL )
	{
		log_warn( "Couldn't read config file '%s': %s\n", filename, strerror( errno ) );
		return;
	}

	while( !feof( f ) )
	{
		if( !fgets( line, 2048, f ) )
			break;

		trim( line );
		linelen = strlen( line );
		if (line[0] == '#')		// comment
			continue;

		if (line[0] == '[' && line[linelen-1] == ']')
		{
			line[linelen-1] = '\0';
			config_set_section( line+1 );
			continue;
		}

		p = strchr( line, '=' );
		if( !p )
			continue;

		*(p++) = '\0';
		trim( line );
		trim( p );
		config_put_string( line, p );
	}

	fclose( f );
}

void
config_write( void )
{
	FILE *f;
	char filename[PATH_MAX];
	SConfigSection *sec;
	SConfigValue *val;

	sprintf( filename, "%smupen64.conf", g_WorkingDir );
	f = fopen( filename, "w" );
	if( !f )
		return;

	sec = m_config;
	for (; sec != 0; sec = sec->next)
	{
		if (!sec->values)
			continue;
		fprintf( f, "[%s]\n", sec->name );
		val = sec->values;
		for (; val != 0; val = val->next)
		{
			fprintf( f, "%s = %s\n", val->key, val->cValue );
		}
		fprintf( f, "\n" );
	}

	fclose( f );
}
