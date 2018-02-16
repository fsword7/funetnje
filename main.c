/* MAIN.C   V-2.8
 | Copyright (c) 1988,1989,1990,1991 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use oR misuse of this software.
 |
 | The main module for the RSCS. Initialyze the things, queues the read requests
 | and sleeps...
 | The initial state of a line is ACTIVE. When the receive QIO is queued for
 | an active line, its status is changed to DRAIN to show the real state.
 |  The IoLines database is defined here. All the other modules point to it
 | (defined as external).
 |
 | This program can be run in debug mode. For doing so, first compile it when
 | the DEBUG compilation variable is defined. Then, define it as foreign DCL
 | symbol, and run it with one parameter (1 - 4) which the logging level.
 | The default logging level is 1.
 |
 | NOTE: The buffer size defined in CONSTS.H must be larger in 20 characters
 |       than the maximum buffer size used for a line. This is because there
 |       is an overhead of less than 20 characters for each block.
 |
 | Currently, the program supports only single stream, and doesn't support
 | splitted records nor SYSIN files.
 |
 | The value of the variable MustShutDown defines whether we have to shut down
 | or not. Zero = keep running. 1 = Shutdown immediately. -1 = shutdown when
 | all lines are signed-off.
 |
 | Unix notes: There is a problem with the timer. An ALARM call wakes-up the
 | sleep call. Hence, the timer for Unix is implemented in the main loop - it
 | sleeps 1 seconds, calls the timer AST routine, sleeps again, and so on...
 |
 | Sections: MAIN - The main section.
 |           INIT-LINE-DB - Read the permanent database into memory.
 |           LOGGER - The logging routines.
 |           INIT - Initialyze some commonly used variables.
 |
 | V1.1 - Lock the area of IoLines structure in memory.
 | V1.2 - Zero InformUsersCount.
 | V1.3 - remove the lock mechanism.
 | V1.4 - Move the definition of ADDRESS_FILE to the configuration file.
 | V1.5 - Queue the AUTO-Restart timer entry upon startup in order to restart
 |        links which are in INACTIVE state and have the Auto-Retsart flag.
 | V1.6 - Add a call to init_headers. Remove (int) & (short) declarations.
 | V1.7 - Replace BCOPY macro with a call to memcpy() function.
 | V1.8 - 23/2/90 - When calling Queue_timer() for general timers (not for
 |        a specific line) change the index to -1. Otherwise, it'll ruin the
 |        action of the routine Delete_line_timeouts().
 | V1.9 - 5/3/90 - If compiled on Unix call Detach function to run in background.
 |        This function was donated by Matti Aarnio (mea@mea.utu.fi).
 | V2.0 - 7/3/90 - On VMS systems add a call to Init_crc_table() for DECnte links.
 | V2.1 - 26/3/90 - On Unix, replace the sleep in the main loop with a call
 |        to poll_sockets() which will do the 1 second wait. Waiting in
 |        select() instead of sleep gives much better response.
 | V2.2 - 1/4/90 - Add a call to Init_cluster_listener() is ClusterNode is
 |        defined.
 | V2.3 - 2/5/90 - Replace Memchr() with Memcpy(). Why it worked before? God knows...
 | V2.4 - 8/5/90 - Detach only if there is or less parameters passed when running
 |        this program.
 | V2.5 - 1/10/90 - Modify Trace() to show also clear text whenever possible.
 |        Code is translated from EBCDIC to ASCII.
 | V2.6 - 16/10/90 - Modify Init_variables() to conform with multi-stream
 |        changes.
 | V2.7 - 11/3/91 - Initialize CurrentStream, ActiveStream, FreeStreams area.
 | V2.8 - 7/5/91 - Fix a bug in Trace(): it printed one more character at the
 |        text region.
 */
#include <stdio.h>
FILE	*AddressFd;	/* Temporary - for DMF routine address */

#define	MAIN		/* So the global variables will be defined here */
#include "consts.h"	/* Our RSCS constants */
#include "headers.h"
#include <time.h>

INTERNAL int	LogLevel;	/* Refferenced in READ_CONFIG.C also */
INTERNAL int	MustShutDown;	/* For UNIX - When we have to exit the main loop */
FILE	*LogFd;

INTERNAL struct	LINE	IoLines[MAX_LINES];	/* For the line's database */

/* For the routine that initialize static variables: */
EXTERNAL struct	JOB_HEADER	NetworkJobHeader;
EXTERNAL struct	DATASET_HEADER	NetworkDatasetHeader;

/*====================== MAIN =====================================*/
/*
 | The main engine. Initialize all things, fire read requests, and then hiber.
 | All the work will be done in AST level.
 */
main(cc, vv)
char	**vv;
{
	char	line[512];		/* Long buffer for OPCOM messages */
	long	LOAD_DMF();		/* The loading routine of DMF framing
					   routine */
	int	i;

/* Init the command mailbox, the timer process.
 */
	LogFd = 0; MustShutDown = 0;	/* =0 - Run. NonZero = Shut down */
	InformUsersCount = 0;
	LogLevel = 1;
	if(cc > 1) {	/* Read the log level as the first parameter */
		switch(**++vv) {
		case '2': LogLevel = 2; break;
		case '3': LogLevel = 3; break;
		case '4': LogLevel = 4; break;
		}
	}

/* Read our configuration from the configuration file */
	init_lines_data_base();		/* Clear it. Next routine will fill it */
	if(read_configuration() == 0)
		exit(1);

/* We got all configuration, so we can now spit error messages into our logfile.
   This is the time to detach... */
#ifdef UNIX
	if(cc < 3)	/* Detach only if not other parameter after loglevel */
		detach();
#endif

	if(open_route_file() == 0) {	/* Open the routing table */
		send_opcom("HUJI-NJE aborting. Problems openning routing table");
		exit(1);
	}

#ifdef VMS
	init_crc_table();		/* For DECnet links */
#endif
	init_headers();			/* Init byte-swapped values in NJE
					   headers */
	init_variables();		/* Init some commonly used variables */
	init_command_mailbox();		/* To get operator's command and file
					   queueing */
	init_timer();			/* Our own timer mechanism. Will tick
					   each second (VMS only) */
	init_files_queue();		/* Init the in-memory file's queue */
#ifdef VMS
	if(*ClusterNode != '\0')	/* We run in cluster mode */
		init_cluster_listener();
#endif

/* Initialyze the lines */
#ifdef VMS
/*  This must be the last one, since it allocates non-paged pool.
    Check whether we have a DMF at all. */
	for(i = 0; i < MAX_LINES; i++) {
		if((IoLines[i].state == ACTIVE) &&
		   (IoLines[i].type == DMF)) {
/*			DMF_routine_address = LOAD_DMF();	*/
/*************************** TEmp ***************************************/
			if((AddressFd = fopen(ADDRESS_FILE, "r")) == NULL) {
				logger(1, "Can't open DMF address file.\n");
				exit(1);
			}
			if((fscanf(AddressFd, "%x", &DMF_routine_address)) != 1) {
				logger(1, "Can't Fscanf address.\n"); exit(1);
			}
			fclose(AddressFd);
			logger(1, "DMF routine address: %x\n", DMF_routine_address);
/**********************************************************************/
			break;	/* Loaded only once */
		}
	}
#endif
/* If we got up to here, almost all initialization was ok; we must call it
   before initializing the lines, as from there we start a mess, and this
   SEND_OPCOM seems to colide with AST routines when calling Logger().
*/
	sprintf(line, "HUyNJE, Version-%s(%s) started on node %s, logfile=%s\r\n",
		VERSION, SERIAL_NUMBER, LOCAL_NAME, LOG_FILE);
	strcat(line, "  Copyright (1988,1989,1990) - The Hebrew University of Jerusalem\r\n");
	strcat(line, LICENSED_TO);
	send_opcom(line);	/* Inform operator */

	init_communication_lines();	/* Setup the lines (open channels, etc)
			and fire reads when needed. */


/* Queue the auto-restart function */
	queue_timer(T_AUTO_RESTART_INTERVAL, -1, T_AUTO_RESTART);

/* Start the statistics timer if we are in debug mode */
#ifdef DEBUG
	queue_timer(T_STATS_INTERVAL, -1, (T_STATS));
#endif
#ifdef UNIX
	for(;;) {
		poll_sockets();
		timer_ast();
		if(MustShutDown > 0) break;	/* We have to shut down */
	}
#endif
#ifdef VMS
	while(MustShutDown <= 0) {
		sys$hiber(); 		/* Sleep. All work will be done in AST mode */
		if(MustShutDown <= 0)
			logger(1, "MAIN, False wakeup\n");
	}
#endif

/* If we got here - Shutdown the daemon (Close permanently opened files). */
	close_command_mailbox();
	close_route_file();
	send_opcom("HUJI-NJE, normal shutdown");
	sleep(1);		/* Wait a bit */
}


/*
 | Called after a line has signedoff. Checks whether this is a result of a
 | request to shutdown after all lines has signed off (MustShutDown = -1).
 | If so, scan all lines. If none is active we can shut: change MustShutDown
 | to 1 (Imediate shut) and wakeup the main routine to do the real shutdown.
 */
can_shut_down()
{
	register int	i;

	if(MustShutDown == 0) return;	/* No need to shut down */

	for(i = 0; i < MAX_LINES; i++)
		if(IoLines[i].state == ACTIVE)	/* Some line is still active */
			return;		/* Can't shutdown yet */

/* All lines are closed. Signal it */
	MustShutDown = 1;	/* Immediate shutdown */
#ifdef VMS
	sys$wake(0,0);		/* Wakeup the main routine */
#endif
}


/*======================== INIT-LINES-DB ===============================*/
/*
 | Clear the line database. Init the initial values.
 */
init_lines_data_base()
{
	int	i, j;

	for(i = 0; i < MAX_LINES; i++) {
		IoLines[i].flags = 0;
		IoLines[i].QueueStart =    IoLines[i].QueueEnd =     NULL;
		IoLines[i].MessageQstart = IoLines[i].MessageQend =  NULL;
		IoLines[i].TotalErrors =   IoLines[i].errors =       0;
		IoLines[i].state = INACTIVE;	/* They are all dead */
		IoLines[i].InBCB =         IoLines[i].OutBCB =       0;
		IoLines[i].TimeOut = 3;		/* 3 seconds */
		(IoLines[i].HostName)[0] = '\0';
		IoLines[i].MaxXmitSize = IoLines[i].PMaxXmitSize = MAX_BUF_SIZE;
		IoLines[i].XmitSize = 0;
		IoLines[i].QueuedFiles = 0;	/* No files queued */
		IoLines[i].FirstXmitEntry =
		IoLines[i].LastXmitEntry = 0;
		IoLines[i].ActiveStreams = IoLines[i].CurrentStream = 0;
		IoLines[i].FreeStreams = 1;	/* Default of 1 stream */
		for(j = 0; j < MAX_STREAMS; j++) {
			IoLines[i].InStreamState[j] =
			IoLines[i].OutStreamState[j] = S_INACTIVE;
			IoLines[i].SizeSavedJobHeader[j] =
			IoLines[i].SizeSavedDatasetHeader[j] = 0;
		}
		IoLines[i].TcpState = IoLines[i].TcpXmitSize = 0;
		IoLines[i].stats.TotalIn =   IoLines[i].stats.TotalOut =     0;
		IoLines[i].stats.WaitIn  =   IoLines[i].stats.WaitOut  =     0;
		IoLines[i].stats.AckIn   =   IoLines[i].stats.AckOut   =     0;
		IoLines[i].stats.RetriesIn = IoLines[i].stats.RetriesOut =   0;
		IoLines[i].stats.MessagesIn = IoLines[i].stats.MessagesOut = 0;
	}
}


/*============================ LOGGER =================================*/
/*
 | Write a logging line in our logfile. If the loglevel is 1, close the file
 | after writing, so we can look in it at any time.
 */
logger(lvl, fmt, A,B,C,D,E,F,G,H)
char	*fmt;
{
	char	*local_time();
	static char	line[LINESIZE];

/* Do we have to log it at all ? */
	if(lvl > LogLevel) return;

/* Open the log file */
	if(LogFd == 0) {		/* Not opened before */
		if((LogFd = fopen(LOG_FILE, "a")) == NULL) {
			LogFd = 0;
			return;
		}
	}
	sprintf(line, "%s, ", local_time());
	sprintf(&line[strlen(line)], fmt, A,B,C,D,E,F,G,H);
	fprintf(LogFd, "%s", line);
	if(LogLevel == 1) {	/* Normal run - close file after loging */
		fclose(LogFd);
		LogFd = 0;
	}
}


/*
 | Return the time in a printable format; to be used by Bug-Check and Logger.
 */
char *
local_time()
{
	static	char	TimeBuff[80];
	struct	tm	*tm, *localtime();
	long	clock;

	time(&clock);		/* Get the current time */
	tm = localtime(&clock);
	sprintf(TimeBuff, "%02d/%02d/%02d %02d:%02d:%02d",
		tm->tm_mday, (tm->tm_mon + 1), tm->tm_year,
		tm->tm_hour, tm->tm_min, tm->tm_sec);
	return TimeBuff;
}


/*
 | Log the string, and then abort.
 */
bug_check(string)
char	*string;
{
	logger(1, "Bug check: %s\n", string);
	send_opcom("HUJI-NJE: Aborting due to bug-check.");
	close_command_mailbox();
	close_route_file();
	exit(0);
}


/*
 | Write a hex dump of the buffer passed. Do it only if the level associated
 | with it is lower or equal to the current one. After the dump write the
 | text of the message in ASCII. WE always try to convert from EBCDIC to
 | ASCII as most traces are done at the network level.
 */
#define ADD_TEXT {	/* Add the printable text */ \
	NextLinePosition = &line[count * 3]; \
	*NextLinePosition++ = ' '; \
	*NextLinePosition++ = '|'; \
	*NextLinePosition++ = ' '; \
	while(q < p) { \
		c = EBCDIC_ASCII[*q++]; \
		if((c >= 32) && (c <= 126))	/* Printable */ \
			*NextLinePosition++ = c; \
		else \
			*NextLinePosition++ = '.'; \
	} \
	*NextLinePosition = '\0'; \
}

trace(p, n, lvl)
unsigned char *p;
int	n, lvl;
{
	register int	count;
	char	line[LINESIZE], *NextLinePosition;
	unsigned char	c, *q;	/* Point to the beginning of this buffer */

	if(lvl > LogLevel) return;
	logger(lvl, "Trace called with data size=%d\n", n);

	count = 0;  q = p;	/* save beginning of buffer */
	while(n-- > 0) {
		if(count++ > 13) {
			count--;	/* To get the real end of line */
			ADD_TEXT;
			q = p;	/* Equate them just to be sure... */
			logger(lvl, "%s\n", line);
			count = 1;
		}
		sprintf(&line[(count - 1) * 3], "%02x ", *p++);
	}

	ADD_TEXT;
	logger(lvl, "%s\n", line);
}


/*=========================== INIT ==============================*/
/*
 | Init some commonly used variables.
 */
init_variables()
{
	int	TempVar;	/* For the macros */

/* Create our name in EBCDIC */
	E_BITnet_name_length = strlen(LOCAL_NAME);
	ASCII_TO_EBCDIC(LOCAL_NAME, E_BITnet_name, E_BITnet_name_length);
	PAD_BLANKS(E_BITnet_name, E_BITnet_name_length, 8);
	E_BITnet_name_length = 8;

/* Init static job header fields */
	memcpy(NetworkJobHeader.NJHGACCT, EightSpaces, 8); /* Blank ACCT */
	memcpy(NetworkJobHeader.NJHGPASS, EightSpaces, 8);
	memcpy(NetworkJobHeader.NJHGNPAS, EightSpaces, 8);
	memcpy(NetworkJobHeader.NJHGXEQU, EightSpaces, 8);
	memcpy(NetworkJobHeader.NJHGFORM, EightSpaces, 8);
	PAD_BLANKS(NetworkJobHeader.NJHGPRGN, 0, 20);
	memcpy(NetworkJobHeader.NJHGROOM, EightSpaces, 8);
	memcpy(NetworkJobHeader.NJHGDEPT, EightSpaces, 8);
	memcpy(NetworkJobHeader.NJHGBLDG, EightSpaces, 8);
/* Same for dataset header */
	memcpy(NetworkDatasetHeader.NDH.NDHGDD, EightSpaces, 8);
	memcpy(NetworkDatasetHeader.NDH.NDHGFORM, EightSpaces, 8);
	memcpy(NetworkDatasetHeader.NDH.NDHGFCB, EightSpaces, 8);
	memcpy(NetworkDatasetHeader.NDH.NDHGUCS, EightSpaces, 8);
	memcpy(NetworkDatasetHeader.NDH.NDHGXWTR, EightSpaces, 8);
	memcpy(NetworkDatasetHeader.NDH.NDHGPMDE, EightSpaces, 8);
	memcpy(NetworkDatasetHeader.RSCS.NDHVDIST, EightSpaces, 8);
}
