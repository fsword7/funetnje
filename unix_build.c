/* UNIX_BUILD.C V1.0
 | Copyright (c) 1988,1989 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use of misuse of this software.
 |
 | Primitive program to build the routing table for the NJEF.
 | Should be improved...
 |
 |  WARNING: The format of the standard routing table is assumed to be:
 |           _ROUTE_Site-name_other things
 |           Where _ is a space.
 | The format of the header file is:
 |  SITE  LINE  FORMAT
 |
*/

#include <stdio.h>

#ifndef	GDBM
/* For the DMB routines: */
struct datum { char	*address;
		int	length;
	};
#else
#include <gdbm.h>
static GDBM_FILE gdbf;
#define address dptr
#define length  dsize
#endif

char	*strchr();

main(argc,argv)
     int argc;
     char *argv[];
{
#ifdef	GDBM
	datum	Key, Value;
	int	st;
#else
	struct	datum	Key, Value;
#endif
	char	*c, line[132],
		site[21], LineName[21], format[20],
		temp_line[132];	/* Just a place holder */
	char	in_file[80], header_file[80], out_file[80];
	FILE	*Ifd, *fd;
	long	i, counter, Dcount;
	extern int errno;
	extern char *sys_errlist[],*gdbm_errlist[];

/* Get files' names */
	if( argc != 4 ) {
	  printf("%s  Creates HUyNJE routing tables.\n",argv[0]);
	  printf("%s  Given 3 arguments the interactive mode is not activated:\n",argv[0]);
	  printf("%s  Args are in order:  Header-Fname, RoutingTbl-Fname, OutputFname\n", argv[0]);
	  

	  printf("Header file: "); gets(header_file);
	  printf("Routing table: "); gets(in_file);
	  printf("Output file: "); gets(out_file);
	} else  {
	  strncpy( header_file,argv[1],sizeof header_file -1);
	  strncpy( in_file,argv[2],sizeof in_file -1);
	  strncpy( out_file,argv[3],sizeof out_file -1);
	}	  
	
	if((c = strchr(header_file, '\n')) != NULL) *c = '\0';
	if((c = strchr(in_file, '\n')) != NULL) *c = '\0';
	if((c = strchr(out_file, '\n')) != NULL) *c = '\0';

	if((Ifd = fopen(header_file, "r")) == NULL) {
		printf("Can't open header file '%s'\n", header_file);
		exit(1);
	}
	if((fd = fopen(in_file, "r")) == NULL) {
		printf("Can't open input file: %s\n", in_file);
		exit(1);
	}

/* Open the database file */
#ifdef	GDBM
	if( NULL == (gdbf = gdbm_open( out_file,512,GDBM_NEWDB,0644,NULL))) {
		printf("Failed to open GDBM database in NEWDB-write mode. gdbm_errno=%d\n",
		       gdbm_errno);
		perror("GDBM_Open(NEWDB)");
		exit(1);
	}
#else
	printf("*** The database file MUST be empty!!!\n");
	if(dbminit(out_file) != 0) {
		printf("Can't open database\n"); perror("dmbinit");
		exit(1);
	}
#endif


	counter = 0;  Dcount = 0;
	printf("Records read:\n");

/* The header file */
	while(fgets(line, sizeof line, Ifd) != NULL) {
		if((c = strchr(line, '\n')) != 0) *c = 0;
/*printf("Got line: '%s'\n",line);*/
		if(*line == '*') continue;		/* Comment line */
		if(*line == '#') continue;		/* Comment line */
		if(*line == 0 ) continue;		/* Empty line */
		counter++;  if(counter % 100 == 0) printf("%d ", counter);
		if(sscanf(line, "%s %s %s",
				    site, LineName, format) < 3) {
			printf("Illegal line: %s\n", line);
			continue;
		}
		for(i = strlen(site); i < 8; i++) site[i] = ' '; site[8] = 0;
		sprintf(temp_line, "%s %s %s",
			site, LineName, format);

		Key.address = temp_line;
		Key.length = 8;
		Value.address = temp_line;
		Value.length = strlen(temp_line);
#ifdef	GDBM
/*printf("Storing entry: %s\n",temp_line);*/
		if(0 > ( st = gdbm_store( gdbf, Key, Value, GDBM_INSERT ))) {
			printf("Can't store, gdbm_errno=%d, errno=%d\n",
			       gdbm_errno,errno);
			gdbm_close( gdbf );
			exit(2);
		}
/*printf("Store ok...\n");*/
		Dcount += st;
#else
		if(store(Key, Value) != 0) {
			perror("Can't store");
		}
#endif
	}

printf("Processing routing table\n");
	
/* Now process the routing table */
	while(fgets(line, sizeof line, fd) != NULL) {
		if((c = strchr(line, '\n')) != 0) *c = 0;
/*printf("Got line: '%s'\n",line);*/
		if(*line == '*') continue;		/* Comment line */
		if(*line == '#') continue;		/* Comment line */
		if(*line == 0 ) continue;		/* Empty line */
		counter++;  if(counter % 100 == 0) printf("%d ", counter);
		if(sscanf(line, "%s %s %s",
				    temp_line, site, LineName) < 2) {
			printf("Illegal line: %s\n", line);
			continue;
		}
		for(i = strlen(site); i < 8; i++) site[i] = ' '; site[8] = 0;
		sprintf(temp_line, "%s %s EBCDIC",
			site, LineName);

		Key.address = temp_line;
		Key.length = 8;
		Value.address = temp_line;
		Value.length = strlen(temp_line);
#ifdef	GDBM
/*printf("Storing entry: %s\n",temp_line);*/
		if(0 > (st = gdbm_store( gdbf, Key, Value, GDBM_INSERT ))) {
			printf("Can't store, gdbm_errno=%d, errno=%d\n",
			       gdbm_errno,errno);
			gdbm_close( gdbf );
			exit(2);
		}
/*printf("Store ok...\n");*/
		Dcount += st;
#else
		if(store(Key, Value) != 0) {
			perror("Can't store");
		}
#endif
	}

	fclose(fd);  fclose(Ifd);
#ifdef	GDBM
	gdbm_close( gdbf );
#else
	dbmclose();
#endif
	printf("\nTotal records inserted: %d, %d duplicates in DUPLICATE.TMP\n",
	       counter, Dcount);
	return 0;
}

