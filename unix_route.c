/* UNIX_ROUTE.C (Formerly UNIX_SEARCH_ROUTE)	V1.2
 | Copyright (c) 1988,1989,1990 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use or misuse of this software.
 | The parts dealing with Gnu-DBM were donated by Matti Aarnio (mea@mea.utu.fi).
 |
 | Search the routing entry for a given node, using the DMB routines.
 |
 | V1.1 - 5/3/90 - If the route is not found and DefaultRoute contains a value,
 |        use it instead.
 | V1.2 - 7/3/90 - Add Change_route() procedure to be able to change routes
 |        in database during program run.
 */
#include <stdio.h>
#include "consts.h"
#include <sys/types.h>
#ifdef	GDBM
# include <gdbm.h>
#endif

#ifdef	GDBM
static GDBM_FILE	gdbf;
#define address dptr
#define length  dsize
#else
struct datum {	char	*address;
		int	length;
		};
#endif

extern int	errno;
extern int	sys_nerr;	/* Maximum error number recognised */
extern char	*sys_errlist[];	/* List of error messages */
#define	PRINT_ERRNO	(errno > sys_nerr ? "***" : sys_errlist[errno])

/*
 | Open the address file.
 */
open_route_file()
{

#ifndef	GDBM
	if(dbminit(TABLE_FILE) != 0) {
		logger((int)(1), "UNIX_SRCH_RT, Can't open DBM file, Error: %s\n",
			PRINT_ERRNO);;
		return 0;
	}
#else
	/* Open it in WRITER mode so we can update it online */
	if( (gdbf = gdbm_open(TABLE_FILE,0,GDBM_WRITER,0,NULL)) == NULL ) {
		logger((int)(1), "UNIX_SRCH_RT, Can't open GDBM file, gdbm_errno= %d, Error: %s\n",
			gdbm_errno,PRINT_ERRNO);;
		return 0;
	 }
#endif
	return 1;	/* Success */
}

/*
 | Close the file. Since the descriptor is static, we need this routine.
 */
close_route_file()
{
#ifndef	GDBM
	dbmclose();
#else
	gdbm_close( gdbf );
#endif
}


/*
 | Get the record whose key is the node we look for. Return the line
 | which corresponds to it, or 0 if not found.
 */
get_route_record(key, line, LineSize)
char	*key, *line;
int	LineSize;
{
	int	i;
	char	TempLine[LINESIZE];
#ifndef	GDBM
	struct	datum	Key, Value;
	extern struct datum fetch();
#else
	datum	Key, Value;
#endif

/*  Copy the site name and pad it with blanks to 8 characters */
	strcpy(TempLine, key);
	for(i = strlen(TempLine); i < 8; i++) TempLine[i] = ' ';
	TempLine[8] = '\0';

/* Rewind the file, and search for the line we need */
	Key.address = TempLine;
	Key.length = strlen(TempLine);
#ifndef	GDBM
	Value = fetch(Key);
#else
	Value = gdbm_fetch( gdbf,Key );
#endif
	if(Value.address == NULL) {
/* Not found. If there is default route - use it now and return success */
		if(*DefaultRoute != '\0') {
#ifdef DEBUG
			logger(4, "Using default route for '%s'\n", key);
#endif
			sprintf(line, "%s %s EBCDIC", key, DefaultRoute);
			return 1;
		}
		return 0;	/* Not found */
	}

	strncpy(line, Value.address, Value.length);
	line[Value.length] = '\0';
#ifdef	GDBM
	free( Value.address ); /* GDBM doesn't free it for us */
#endif
	return 1;
}



/*
 | Change a route of node. If it exists, update its record. If not, add it.
 | If the node is not local, the format used is EBCDIC.
 */
change_route(Node, Route)
char	*Node, *Route;
{
	int	i, status;
	char	key[16], *p, line[LINESIZE],
		NewRoute[LINESIZE];
#ifndef	GDBM
	struct	datum	Key, Value;
	extern struct datum fetch();
#else
	datum	Key, Value;
#endif

/* Convert to upper case and add trailing blanks. */
	strncpy(key, Node, sizeof key);
	for(p = key; *p != NULL; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';
	for(i = strlen(key); i < 8; i++) key[i] = ' '; key[8] = '\0';

/* Convert new route to upper case */
	for(p = Route; *p != NULL; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';

	if(compare(Route, "LOCAL") == 0)
		sprintf(NewRoute, "%s LOCAL ASCII", key);
	else
		sprintf(NewRoute, "%s %s EBCDIC", key, Route);

/* Try retrieving it. If exists - use update */
	Key.address = key;
	Key.length = strlen(key);
#ifndef	GDBM
	Value = fetch(Key);
#else
	Value = gdbm_fetch( gdbf,Key );
#endif
	if(Value.address == NULL)	/* Doesn't exist */
		*line = '\0';
	else {
		strncpy(line, Value.address, Value.length);	/* Save old contents */
#ifndef GDBM
		delete(Key);	/* remove it so we can write the new one */
#endif
	}

/* Write now the new value */
	Value.address = NewRoute;
	Value.length = strlen(NewRoute);
	Key.address = key;
	Key.length = strlen(key);
#ifndef GDBM
	if(store(Key,Value) != 0)
#else
	if(gdbm_store(gdbf, Key,Value, GDBM_REPLACE) != 0)
#endif
		logger(1, "SEARCH_ROUTE, Can't update new route: '%s'. Error: %s\n",
				NewRoute, PRINT_ERRNO);
	else
		if(*line == '\0')	/* New route */
			logger(1, "SEARCH_ROUTE, New routing record added: '%s'\n",
				NewRoute);
		else		/* Updated route - show the old one also */
			logger(1, "SEARCH_ROUTE, New routing record added: '%s' \
instead of '%s'\n",
				NewRoute, line);
}
