/* VMS_SEARCH_ROUTE.C	V1.3
 | Copyright (c) 1988,1989,1990,1991 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use or misuse of this software.
 |
 | Search in the indexed routing file for the correct route to destination.
 | The database file is opened by this module and kept opened untill program's
 | shutdown.
 |
 | V1.1 - Add default route. If the requested node is not found in the table and
 |        there is a default route, return it.
 | V1.2 - 7/3/90 - Add Change_route() routine which allows to chage dynamically
 |        the route during program's run.
 | V1.3 - Replace some Logger(1) with Logger(2).
 */

#include "consts.h"

static struct FAB	route_fab;
static struct RAB	route_rab;
static struct XABKEY	route_xabkey;


/*
 | Open the address file. Leave it open permanently.
 */
open_route_file()
{
	int	status;

/* Open the indexed file for input */
	route_fab = cc$rms_fab;  route_rab = cc$rms_rab;
	route_xabkey = cc$rms_xabkey;
	route_fab.fab$l_fna = TABLE_FILE;
	route_fab.fab$b_fns = strlen(TABLE_FILE);
	route_fab.fab$b_fac = (FAB$M_GET | FAB$M_PUT | FAB$M_UPD);
	route_fab.fab$b_org = FAB$C_IDX;
	route_fab.fab$b_rfm = FAB$C_VAR;	/* Variable records length */
	route_fab.fab$l_xab = &route_xabkey;
	route_rab.rab$l_fab = &route_fab;
	route_rab.rab$b_ksz = 8;		/* Length of key */
	route_rab.rab$b_krf = 0;		/* Main key */
	route_rab.rab$b_rac = RAB$C_KEY;
	route_xabkey.xab$w_dfl = 0;
	route_xabkey.xab$b_dtp = XAB$C_STG;	/* Key is of String type */
	route_xabkey.xab$b_flg = (XAB$M_DAT_NCMPR |
				    XAB$M_DAT_NCMPR | XAB$M_KEY_NCMPR);
	route_xabkey.xab$b_prolog = 3;
	route_xabkey.xab$b_ref = 0;		/* Main key */
	route_xabkey.xab$b_siz0 = 8;		/* 8 characters long */
	route_xabkey.xab$l_knm = 0;

	if(((status = sys$open(&route_fab)) & 0x1) == 0) {
		logger(1,
			"VMS_SEARCH: Can't open indexed address file '%s', $OPEN status=%d\n",
			TABLE_FILE, status);
		return 0;
	}
	if(((status = sys$connect(&route_rab)) & 0x1) == 0) {
		logger(1,
			"VMS_SEARCH: Can't connect indexed address file, $CONNECT status=%d\n",
				status);
		return 0;
	}

	logger(2, "VMS_SEARCH, Using indexed routing table file '%s'\n", TABLE_FILE);
	return 1;		/* All OK */
}


/*
 | Close the routing file.
 */
close_route_file()
{
	sys$close(&route_fab);
}


/*
 | Get the route record based on the site name.
 | Return 0 if not found, 1 if found.
 */
int
get_route_record(key, line, line_size)
char	*key, *line;
int	line_size;
{
	int	i, status;

	route_rab.rab$l_kbf = key;		/* Key buffer */
	route_rab.rab$l_ubf = line; route_rab.rab$w_usz = line_size - 1;
	route_rab.rab$b_rac = RAB$C_KEY;
	if(((status = sys$get(&route_rab)) & 0x1) == 0) {
/* Not found. If there is default route - use it now and return success */
		if(*DefaultRoute != '\0') {
#ifdef DEBUG
			logger(4, "Using default route for '%s'\n", key);
#endif
			sprintf(line, "%s %s EBCDIC", key, DefaultRoute);
			return 1;
		}
#ifdef DEBUG
		logger((int)(3),
			"VMS_SEARCH: Get from indexed file: status=%d\nkey='%s'\n",
			status, key);
#endif
		return 0;		/* Record not found */
	}
	line[route_rab.rab$w_rsz] = 0;
#ifdef DEBUG
	logger((int)(4), "VMS_SEARCH: Found indexed-file record: '%s'\n", line);
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
	route_rab.rab$l_kbf = key;		/* Key buffer */
	route_rab.rab$l_ubf = line; route_rab.rab$w_usz = sizeof line;
	route_rab.rab$b_rac = RAB$C_KEY;
	if(((status = sys$get(&route_rab)) & 0x1) == 0) {
		/* Not there - add new node */
		route_rab.rab$l_rbf = NewRoute;
		route_rab.rab$w_rsz = strlen(NewRoute);
		if(((status = sys$put(&route_rab)) & 0x1) == 0)
			logger(1, "SEARCH_ROUTE, Can't write new route: '%s'. Status=%d\n",
				NewRoute, status);
		else
			logger(2, "SEARCH_ROUTE, New routing record added: '%s'\n",
				NewRoute);
	}
	else {	/* Exists - update */
		route_rab.rab$l_rbf = NewRoute;
		route_rab.rab$w_rsz = strlen(NewRoute);
		if(((status = sys$update(&route_rab)) & 0x1) == 0)
			logger(1, "SEARCH_ROUTE, Can't update new route: '%s'. Status=%d\n",
				NewRoute, status);
		else
			logger(2, "SEARCH_ROUTE, New routing record added: '%s' \
instead of '%s'\n",
				NewRoute, line);
	}
}
