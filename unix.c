/* UNIX.C	V3.5
 | Copyright (c) 1988,1989,1990,1991,1992,1993 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use or misuse of this software.
 |
 | Sections: COMMAND - Command mailbox (socket)
 |           COMMUNICATION - Send messages to user and  opcom.
 |           TIMER - The one second timer process.
 |
 | V1.1 - Changes to comply with VMS.C, version 1.3
 | V1.2 - The close-file function returns the file size in 512 bytes blocks.
 | V1.3 - When a user sends message or command, add @Local-name to his address
 |        (Was done up to now by SEND.C.
 | V1.4 - Change the TCP_TIMEOUT handling in the timer AST.
 | V1.5 - Add the calls for routines Debug-dump-buffers and Debug-rescan-queue
 |        to aid debugging. These routines reside in IO.C module.
 | V1.6 - Add IBM-time. Also add LOGLEVEL command in UNIX_CP.C
 |        Before changing the loglevel
 |        we call the log function while setting the loglevel to 1 in order to
 |        close the log file. This guarantees writing all logs up to this
 |        point.
 | V1.7 - Do a quick change to Uread to handle binary files. Have to debug it...
 | V1.8 - Change the handling of auto restarts. Instead of each routine queueing
 |        the autorestart, we simply loop every 5 minutes and check whether there
 |        are links needing restarts.
 | V1.9 - Add the Delete-line_timeouts which is called by Restart_channel.
 | V2.0 - When doing select, add the channels used to accept and read the initial
 |        VMnet records.
 | V2.1 - FCNTL.H include added; (int) removed; SWAP_xxx replaced with XtoXY.
 | V2.2 - 21/2/90 - Change the include file <sys/dir.h> and the relevant
 |        structure type to be defined in SITE_CONSTS, as there is difference
 |        among various Unix systems.
 | V2.3 - 23/2/90 - 1. Change the timer-queue elements to be INT all.
 |        2. When queueing a timer entry for general routine (not related with
 |        some line) use index of -1.
 | V2.4 - 3/3/90 - Simplify auto_restart_lines() routine.
 | V2.5 - 7/3/90 - Add CMD_CHANGE_ROUTE to enable changing the routing database
 |        on-line.
 | V2.6 - 14/3/90 - When calling select(), do not pass the Fd's table width as
 |        a constant. Instead, call getdtablesize() to get it.
 | V2.7 - 15/3/90 - Add F_IN_HEADER flag for each file. This is true while reading
 |        or writing our internal header (which is always lines of ASCII text)
 |        and false after. This will allow us to read the rest of file with
 |        fread() instead of fgets() if the file is binary.
 |        Currently each read/write in binary/EBCDIC mode is done in two calls
 |        (one for the size and one for the string itself). This should be
 |        improved.
 | V2.8 - 22/3/90 - Add CMD_GONE_ADD and CMD_GONE_DEL to maintain the Gone list.
 | V2.9 - 26/3/90 - When calling select add a one second timeout. Remove the
 |        T_POLL timer entry.
 | V3.0 - 4/4/90 - Replace printouts of Errno value with error messages from
 |        sys_errlist[].
 | V3.1 - 8/5/90 - Use FD_SET and the other related functions if it is defined.
 | V3.2 - 7/10/90 - Open_recv_file(), Delete_file(), Rename_file(), Close_file()
 |         - Add stream number to the filename.
 | V3.3 - 31/3/91 - Add multi-stream support on sending.
 | V3.4 - 1/9/93 - Uwrite() - Do not terminate the string with a NULL and use
 |        Fwrite which does not require terminating NULL. This way we do not
 |        ruin the original buffer.
 | V3.5 - 8/9/93 - Add support for AIX.
 */
#include "site_consts.h"
#include "consts.h"
#include "headers.h"
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include DIRENTFILE
#include <sys/stat.h>
#ifdef AIX
#include <sys/select.h>
#endif

#define	TEMP_R_FILE		"NJE_R_TMP_"
#define	TEMP_R_EXT		"TMP"

static int	CommandSocket;	/* The command socket number */
extern int	LogLevel;	/* So we change it with the LOGLEVEL CP command */

extern struct	LINE IoLines[MAX_LINES];
extern struct	SIGN_OFF	SignOff;
extern int	MustShutDown;
extern int	PassiveSocketChannel,	/* We called LISTEN on it */
		PassiveReadChannel;	/* We are waiting on it for the initial VMnet record */

#define	MAX_QUEUE_ENTRIES	9	/* Maximum entries in timer's queue */
struct	TIMER	{			/* The timer queue */
		int	expire;		/* Number of seconds untill expiration */
		int	index;		/* Line;s index */
		int	action;		/* What to do when expires */
	} TimerQueue[MAX_QUEUE_ENTRIES];

#define	EXPLICIT_ACK	0
#define	DELAYED_ACK	2		/* In accordance with PROTOOCL */

char	*strchr(), *strrchr();

#define	IBM_TIME_ORIGIN	2105277440	/* Number of seconds between 1900 and 1970 */
#define	BIT_32_SEC	1.048565 /* The number of seconds the 32 bit is */

extern int	sys_nerr;	/* Maximum error number recognised */
extern char	*sys_errlist[];	/* List of error messages */
#define	PRINT_ERRNO	(errno > sys_nerr ? "***" : sys_errlist[errno])

/*===================== COMMAND ============================*/
/*
 | Init the command socket. We use a datagram socket.
 */
init_command_mailbox()
{
	struct	sockaddr_in	SocketName;
	int	i;

/* Create a local socket */
	if((CommandSocket = socket(AF_INET, SOCK_DGRAM,	0)) == -1) {
		logger(1, "UNIX, Can't create command socket, error: %s\n",
			PRINT_ERRNO);
		exit(1);
	}

/* Now, bind a local name for it */
	SocketName.sin_family = (AF_INET);
	SocketName.sin_port = htons(COMMAND_MAILBOX);
	SocketName.sin_addr.s_addr = 0;	/* Local machine */
	for(i = 0; i < 8; i++)
		(SocketName.sin_zero)[i] = 0;
	if(bind(CommandSocket, &SocketName, sizeof(SocketName)) == -1) {
		logger(1, "UNIX, Can't bind command socket, error: %s\n",
			PRINT_ERRNO);
		exit(1);
	}

/* That's all. Select will poll for it */
}

/*
 | CLose the operator communication channel (we are shutting down).
 */
close_command_mailbox()
{
	close(CommandSocket);
}

/*================= COMMUNICATION ========================*/
/*
 | Send a message to console and log it in our logfile.
 */
send_opcom(text)
{
	int	ftty;
	char	line[512];

	logger(1, "OPCOM message: %s\n", text);

	if(strlen(text) > 500) return;	/* Too long to handle */

	sprintf(line, "\r%s\r\n", text);
	if((ftty = open("/dev/console", O_WRONLY)) < 0)
		return;
	write(ftty, line, strlen(line));
	close(ftty);
}


/*================ TIMER ============================*/
/*
 | Init the timer's queue entries (zero them), queue an entry to tick each
 | second to poll the active sockets.
 | The timer's AST is called by the main routine wach second.
 */
init_timer()
{
	long	i, timer_ast();

/* Zero the queue */
	for(i = 0; i < MAX_QUEUE_ENTRIES; i++)
		TimerQueue[i].expire = 0;
}

/*
 | The AST routine that is waked-up once a second.
 */
long
timer_ast()
{
	long	i;
	int	queue_timer();
	struct	LINE	*temp;

/* Loop over all active timeouts and decrement them. If expired, handle
   accordingly.
*/
	for(i = 0; i < MAX_QUEUE_ENTRIES; i++) {
		if(TimerQueue[i].expire != 0) {		/* Active */
			if(--(TimerQueue[i].expire) == 0) {
				switch(TimerQueue[i].action) {
				case T_SEND_ACK:	/* Send ACK */
					handle_ack(TimerQueue[i].index,
						DELAYED_ACK);
						break;
				case T_TCP_TIMEOUT:
				/* Read timeout, Simulate an an ACK if needed */
					temp = &(IoLines[TimerQueue[i].index]);
					if(temp->state == ACTIVE) {
						handle_ack(TimerQueue[i].index,
						 EXPLICIT_ACK);
					}
					/* Requeue it: */
					temp->TimerIndex = queue_timer(temp->TimeOut,
						TimerQueue[i].index,
						T_TCP_TIMEOUT);
					break;
					break;
				case T_AUTO_RESTART:
					auto_restart_lines();
					queue_timer(T_AUTO_RESTART_INTERVAL,
						-1, T_AUTO_RESTART);
					break;
#ifdef DEBUG
				case T_STATS:	/* Compute statistics */
					compute_stats();
					break;
#endif
				default: logger(1, "UNIX, No timer routine, code=d^%d\n",
					TimerQueue[i].action);
				}
			}
		}
	}
}



/*
 | Queue an entry. The line index and the timeout us expected.  Since we don't
 | use a 1-second clock, but a SLEPP call, we do not add 1 to the timeout value
 | as is done on the VMS.
 | A constant which decalres the routine to call on timeout is also expected.
 */
int
queue_timer(expiration, Index, WhatToDo)
int	expiration, WhatToDo;
int	Index;			/* Index in IoLines array */
{
	int	i;

	for(i = 0; i < MAX_QUEUE_ENTRIES; i++) {
		if(TimerQueue[i].expire == 0) {	/* Free */
			TimerQueue[i].expire = expiration;
			TimerQueue[i].index = Index;
			TimerQueue[i].action = WhatToDo;
			return i;
		}
	}
/* Not found an entry - bug check */
	logger(1, "UNIX, Can't queue timer, timer queue dump:\n");
	for(i = 0; i < MAX_QUEUE_ENTRIES; i++)
		logger(1, "  #%d, Expire=%d, index=%d, action=%d\n",
			i, TimerQueue[i].expire,
			TimerQueue[i].index, TimerQueue[i].action);
	bug_check("UNIX, can't queue timer entry.");
	return 0;	/* To make LINT quiet... */
}


/*
 | Dequeue a timer element (I/O completed ok, so we don't need this timeout).
 */
dequeue_timer(TimerIndex)
int	TimerIndex;		/* Index into our timer queue */
{
	if(TimerQueue[TimerIndex].expire == 0) {
		/* Dequeueing a non-existent entry... */
		logger(1, "UNIX, Unexisted timer entry; Index=%d, action=%d\n",
			TimerQueue[TimerIndex].index,
			TimerQueue[TimerIndex].action);
		logger(1, "UNIX, dequeing unexisted timer queue entry.\n");
	}

/* Mark this entry as free */
	TimerQueue[TimerIndex].expire = 0;
}

/*
 | Delete all timer entries associated with a line.
 */
int
delete_line_timeouts(Index)
int	Index;			/* Index in IoLines array */
{
	register int	i;

	for(i = 0; i < MAX_QUEUE_ENTRIES; i++) {
		if(TimerQueue[i].index == Index)	/* For this line - clear it */
			TimerQueue[i].expire = 0;
	}
}


/*
 | Loop over all lines. If some line is in INACTIVE to RETRY state and the
 | AUTO-RESTART flag is set, try starting it.
 */
auto_restart_lines()
{
	int	i;
	struct	LINE	*temp;

	for(i = 0; i < MAX_LINES; i++) {
		temp = &IoLines[i];
		if(*temp->HostName == '\0') continue;	/* Not defined... */
		if((temp->flags & F_AUTO_RESTART) == 0)
			continue;	/* Doesn't need restarts at all */
		switch(temp->state) {	/* Test whether it needs restart */
		case INACTIVE:
		case RETRYING:	restart_line(i);	/* Yes - it needs */
				break;
		default:	break;		/* Other states - no need */
		}
	}
}


/*
 | Use the Select function to poll for sockets status. We distinguish between
 | two types: The command mailbox and the NJE/TCP sockets.
 | If there was some processing to do, we repeat this call, since the processing
 | done might take more than 1 second, and our computations of timeouts might
 | be too long.
 |  If FD_SET is defined, then we run on SparcStation and should use the
 | system's supplied macros and data structure for Select() call.
 */
poll_sockets()
{
	int	i, j, nfds;
	struct	timeval	timeout;
	static	int FdWidth = 0;
#ifdef AIX
	struct {
		fd_set	fdsmask;
		fd_set	msgsmask;
	} readfds;
#else	/* AIX */
#ifdef FD_SET
	fd_set	readfds;
#else
	long	readfds;	/* On most other machines */
#endif
#endif	/* AIX */

#ifndef FD_SET
/* Get the descriptors table size if first time */
	if(FdWidth == 0) FdWidth = getdtablesize();
#endif

again:
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;	/* 1 second timeout */
#ifdef AIX
	FD_ZERO(&readfds.fdsmask);
	FD_SET(CommandSocket, &readfds.fdsmask);
#else	/* AIX */
#ifdef FD_SET
	FD_ZERO(&readfds);
	FD_SET(CommandSocket, &readfds);	/* Set the command socket */
#else
	readfds = 1 << CommandSocket;
#endif
#endif	/* AIX */

/* Add the sockets used for NJE comminucation */
	for(i = 0; i < MAX_LINES; i++)
		if((IoLines[i].state != INACTIVE) &&
		   (IoLines[i].state != SIGNOFF) &&
		   (IoLines[i].state != RETRYING) &&
		   (IoLines[i].state != LISTEN) &&
		   (IoLines[i].socket != 0)) {
#ifdef AIX
				FD_SET(IoLines[i].socket, &readfds.fdsmask);
#else	/* AIX */
#ifdef FD_SET
				FD_SET(IoLines[i].socket, &readfds);
#else
				readfds |= 1 << IoLines[i].socket;
#endif
#endif	/* AIX */
		}
/* Queue a select for the passive end; do it only if we did succeed binding
   it */
	if(PassiveSocketChannel != 0) {
#ifdef AIX
		FD_SET(PassiveSocketChannel, &readfds.fdsmask);
#else	/* AIX */
#ifdef FD_SET
		FD_SET(PassiveSocketChannel, &readfds);
#else
		readfds |= 1 << PassiveSocketChannel;	/* Expecting connections on it */
#endif
#endif	/* AIX */
		if(PassiveReadChannel != 0) {
#ifdef AIX
			FD_SET(PassiveReadChannel, &readfds.fdsmask);
#else	/* AIX */
#ifdef FD_SET
			FD_SET(PassiveReadChannel, &readfds);
#else
			readfds |= 1 << PassiveReadChannel;	/* The one we read from before deciding which line it is */
#endif
#endif	/* AIX */
		}
	}

#ifdef AIX
	nfds = select(FD_SETSIZE, &readfds, 0, 0, &timeout);
	nfds &= 0xffff;		/* Leave only the low 16 bits. The higher ones are
				   for message queues */
	if(nfds == -1) {
#else	/* AIX */
#ifdef FD_SET
	if((nfds = select(NFDBITS, &readfds, 0, 0, &timeout)) == -1) {
#else
	if((nfds = select(FdWidth, &readfds, 0, 0, &timeout)) == -1) {
#endif
#endif	/* AIX */
		logger(1, "UNIX, Select error, error: %s\n", PRINT_ERRNO);
		bug_check("UNIX, Select error");
	}

	if(nfds == 0) {	/* Nothing is there */
/* We are done with input. When TcpIp sends data, it sets the F_CALL_ACK
   flag, so we need to call handle-Ack from here.
*/
		for(i = 0; i < MAX_LINES; i++) {
			if((IoLines[i].flags & F_CALL_ACK) != 0) {
				IoLines[i].flags &= ~F_CALL_ACK;
				handle_ack(i, EXPLICIT_ACK);
			}
		}
		return;
	}

/* Check which channels have data to read. Differentiate between 2 types of
   channels: the command socket and NJE/TCP channels.
*/
#ifdef AIX
	for(i = 0; i < FD_SETSIZE; i++) {
		if(FD_ISSET(i, &readfds.fdsmask)) {		/* Something is there */
#else	/* AIX */
#ifdef FD_SET
	for(i = 0; i < NFDBITS; i++) {
		if(FD_ISSET(i, &readfds)) {		/* Something is there */
#else
	for(i = 0; i < FdWidth; i++) {
		if((readfds & (1 << i)) != 0) {		/* There is something */
#endif
#endif	/* AIX */
			if(i == CommandSocket)	/* Handle it */
				parse_op_command();
			else {
/* Look for the line that corrsponds to that FD */
				for(j = 0; j < MAX_LINES; j++)
					if((IoLines[j].socket == i) &&
					   (*IoLines[j].HostName != '\0'))
						unix_tcp_receive(j);
			}
		}
	}
/* Check whether there is something to accept connection on */
#ifdef AIX
	if(FD_ISSET(PassiveSocketChannel, &readfds.fdsmask))
#else	/* AIX */
#ifdef FD_SET
	if(FD_ISSET(PassiveSocketChannel, &readfds))
#else
	if((readfds & (1 << PassiveSocketChannel)) != 0)
#endif
#endif	/* AIX */
		accept_tcp_connection();
/* We've accepted some connection; now see whether there is something to read there */
	if(PassiveReadChannel != 0) {
#ifdef AIX
		if(FD_ISSET(PassiveReadChannel, &readfds.fdsmask))
#else	/* AIX */
#ifdef FD_SET
		if(FD_ISSET(PassiveReadChannel, &readfds))
#else
		if((readfds & (1 << PassiveReadChannel)) != 0)
#endif
#endif	/* AIX */
			read_passive_tcp_connection();
	}
	goto again;	/* Retry operation */
}


/*
 | Read from the socket and parse the command.
 */
parse_op_command()
{
	char	*p, line[LINESIZE], Faddress[LINESIZE], Taddress[LINESIZE];
	int	i, size;	

	if((size = recv(CommandSocket, line, sizeof line,
		0)) <= 0) {
		perror("Recv");
		return;
	}
	line[size] = '\0';
	if((p = strchr(line, '\n')) != NULL) *p = '\0';

/* Parse the command. In most commands, the parameter is the username to
   broadcast the reply to. */
	parse_operator_command(line);
}


/*========================== FILES section =========================*/
/*
 | Find the next file matching the given mask.
 | Note: We assume that the file mask has the format of: /dir1/.../FN*.Extension
 | The FileMask tells us whether this is afirst call (it has a file name),
 | or whether it is not a first call (*FileName = Null). In this case, we
 | use the values saved internally.
 | Input:  FileMask - The mask to search.
 |         context  - Should be passed by ref. Must be zero before search, and
 |                    shouldn't be modified during the search.
 | Output: find_file() - 0 = No more files;  1 = Matching file found.
 |         FileName - The name of the found file.
 */
find_file(FileMask, FileName, context)
char	*FileMask, *FileName;
DIR	**context;
{
	DIRENTTYPE	*dirp;
	static char	Directory[LINESIZE], File_Name[LINESIZE], Extension[LINESIZE];
	char	*p;

/* From the file mask, get the directory name, the file name, and the
   extenstion.
*/
	if(*FileMask != 0) {	/* First time */
		strcpy(Directory, FileMask);
		if((p = strrchr(Directory, '/')) == NULL) {
			logger(1, "UNIX, Illegal file mask='%s'\n", FileMask);
			return 0;
		}
		else	*p++ ='\0';
		strcpy(File_Name, p);
		if((p = strchr(File_Name, '*')) == NULL) {
			logger(1, "UNIX, Illegal file mask='%s'\n", FileMask);
			return 0;
		}
		*p++ = '\0';
		if((p = strchr(p, '.')) == NULL) *Extension = '\0';
		else strcpy(Extension, p);
/* Open the directory */
		if((*context = opendir(Directory)) == NULL) {
			logger(1, "UNIX, Can't open dir. error: %s\n", PRINT_ERRNO);
			return 0;
		}
	}

/* Look for the next file name */
	for(dirp = readdir(*context); dirp != NULL; dirp = readdir(*context)) {
		if((dirp->d_namlen > 0) &&
		   ((memcmp(dirp->d_name, File_Name, strlen(File_Name)) == 0) || (*File_Name == '\0'))) {
			if((p = strchr(dirp->d_name, '.')) != NULL) {
				if(strncmp(p, Extension, strlen(Extension)) == 0) {
					(dirp->d_name)[dirp->d_namlen] = '\0';
					sprintf(FileName, "%s/%s",
						Directory, dirp->d_name);
					return 1;
				}
			}
		}
	}
	closedir(*context);
	return 0;
}


/*
 | Open the file which will be transmitted. Save its FD in the IoLine
 | structure. It also calls the routine that parses our envelope in the file.
 | Currently handles only one output stream.
 | Set the flags to hold the F_IN_HEADER flag so we know to start reading our
 | header in normal ASCII mode.
*/
open_xmit_file(Index, DecimalStreamNumber, FileName)
int	DecimalStreamNumber, Index;		/* Index into IoLine structure */
char	*FileName;
{
	struct	LINE	*temp;
	FILE	*fd;
	struct	stat	Stat;

	temp = &(IoLines[Index]);

	/* Open the file */
	if((fd = fopen(FileName, "r")) == NULL) {	/* Open file. */
		logger(1, "UNIX, name='%s', error: %s\n", FileName, PRINT_ERRNO);
		return 0;
	}

	((temp->InFds)[DecimalStreamNumber]) = fd;
	(temp->OutFileParams)[DecimalStreamNumber].flags = F_IN_HEADER;
	parse_envelope(Index, DecimalStreamNumber);	/* get the file's data from our envelope */
	(temp->OutFileParams)[DecimalStreamNumber].flags &= ~F_IN_HEADER;

/* Get the file size in bytes */
	if(stat(FileName, &Stat) == -1) {
		logger(1, "UNIX, Can't stat file '%s'. error: %s\n", FileName, PRINT_ERRNO);
		((temp->OutFileParams)[DecimalStreamNumber]).FileSize = 0;
	}
	else
		((temp->OutFileParams)[DecimalStreamNumber]).FileSize = Stat.st_size;

	return 1;
}


/*
 | Open the file to be received. The FD is stored in the IoLines
 | structure. The file is created in BITNET_QUEUE directory, and is called
 | RECEIVE_TEMP.TMP; It'll be renamed later to a more sensible name.
 |  Caller must make sure that the stream number is within range.
*/
open_recv_file(Index, DecimalStreamNumber)
int	Index,		/* Index into IoLine structure */
	DecimalStreamNumber;	/* In the range 0-7 */
{
	FILE	*fd;
	char	FileName[LINESIZE];
	struct	LINE	*temp;

	temp = &(IoLines[Index]);

/* Create the filename in the queue */
	sprintf(FileName, "%s/%s%d_%d.%s", BITNET_QUEUE, TEMP_R_FILE,
		Index, DecimalStreamNumber, TEMP_R_EXT);
	strcpy((temp->InFileParams[DecimalStreamNumber]).OrigFileName, FileName);
	(temp->InFileParams[DecimalStreamNumber]).NetData = 0;
	(temp->InFileParams[DecimalStreamNumber]).RecordsCount = 0;
	((temp->InFileParams[DecimalStreamNumber]).JobName)[0] = '\0';
	((temp->InFileParams[DecimalStreamNumber]).FileName)[0] =
		((temp->InFileParams[DecimalStreamNumber]).FileExt)[0] =
		((temp->InFileParams[DecimalStreamNumber]).JobName)[0] = '\0';

	/* Open the file */
	if((fd = fopen(FileName, "w")) == NULL) {	/* Open file. */
		logger(1, "UNIX, name='%s', error: %s\n", FileName, PRINT_ERRNO);
		return 0;
	}

	((temp->OutFds)[DecimalStreamNumber]) = fd;
	((temp->InFileParams)[DecimalStreamNumber]).flags = F_IN_HEADER;
	return 1;
}


/*
 |  Write the given string into the file.
*/
uwrite(Index, DecimalStreamNumber, string, size)
unsigned char	*string;	/* Line descption */
int	Index, DecimalStreamNumber, size;
{
	FILE	*fd;

	fd = ((IoLines[Index].OutFds)[DecimalStreamNumber]);

	if((((IoLines[Index].InFileParams[DecimalStreamNumber]).flags & F_IN_HEADER) != 0) ||
	   (IoLines[Index].InFileParams[DecimalStreamNumber].format == ASCII)) {
		if(compare(string, "END:") == 0) {
			IoLines[Index].InFileParams[DecimalStreamNumber].flags &= ~F_IN_HEADER;
		}
/* Used Fwrite since it doesn't need terminating NULL */
		if(fwrite(string, size, 1, fd) == EOF) {
			logger(1, "UNIX: Can't fwrite, error: %s\n", PRINT_ERRNO);
			return 0;
		}
		fwrite("\n", sizeof(char), 1, fd);	/* Write end of line */
	} else {	/* Use Fwrite if the file is binary, Printf otherwise */
		if(fwrite(&size, sizeof(int), 1, fd) == EOF) {
			logger(1, "UNIX: Can't fwrite, error: %s\n", PRINT_ERRNO);
			return 0;
		}
		if(fwrite(string, size, 1, fd) == EOF) {
			logger(1, "UNIX: Can't fwrite, error: %s\n", PRINT_ERRNO);
			return 0;
		}
	}
	return 1;
}

/*
 |  Read from the given file. The function returns the number of characters
 | read, or -1 if error.
 | This function has been modified (ugli...) to handle binary files.
*/
int
uread(Index, DecimalStreamNumber, string, size)
/* Read one record from file */
unsigned char	*string;	/* Line descption */
int	DecimalStreamNumber, Index, size;
{
	char	*p;
	FILE	*fd;
	int	status, NewSize;

	fd = ((IoLines[Index].InFds)[DecimalStreamNumber]);

	if((IoLines[Index].OutFileParams[DecimalStreamNumber].format == ASCII) ||
	   ((IoLines[Index].OutFileParams[DecimalStreamNumber].flags & F_IN_HEADER) != 0)) {
		if(fgets(string, size, fd) != NULL) {
			if((p = strchr(string, '\n')) != NULL) *p = '\0';
			return strlen(string);
		}
		else {
#ifdef DEBUG
			logger(2, "UNIX: Uread errno = %d\n", PRINT_ERRNO);
#endif
			return -1;
		}
	} else {
		if(fread(&NewSize, sizeof(int), 1, fd) != 1)
			return -1;	/* Probably end of file */
		if(NewSize > size) {	/* Can't reduce size, so can't recover */
			logger(1, "Unix, Uread, have to read %d into a buffer of only %d\n",
				NewSize, size);
			bug_check("Uread - buffer too small");
		}
		if(fread(string, NewSize, 1, fd) == 1) {
			return NewSize;
		}
		else {
#ifdef DEBUG
			logger(2, "UNIX: Uread errno = %d\n", PRINT_ERRNO);
#endif
			return -1;
		}
	}
}


/*
 | Close and Delete a file given its index into the Lines database.
 */
delete_file(Index, direction, DecimalStreamNumber)
int	Index, direction, DecimalStreamNumber;
{
	FILE	*fd;
	char	*FileName;

	if(direction == F_INPUT_FILE) {
		fd = ((IoLines[Index].InFds)[DecimalStreamNumber]);
		FileName = IoLines[Index].OutFileParams[DecimalStreamNumber].OrigFileName;
	}
	else {
		fd = ((IoLines[Index].OutFds)[DecimalStreamNumber]);
		FileName = IoLines[Index].InFileParams[DecimalStreamNumber].OrigFileName;
	}

	if(fclose(fd) == -1)
		logger(1, "UNIX: Can't close file, error: %s\n", PRINT_ERRNO);

	if(unlink(FileName) == -1) {
		logger(1, "UNIX: Can't delete file '%s', error: %s\n",
			FileName, PRINT_ERRNO);
	}
}

/*
 | Close a file given its index into the Lines database.
 | The file size in blocks (512 bytes) is returned.
 */
close_file(Index, direction, DecimalStreamNumber)
int	Index, direction, DecimalStreamNumber;
{
	FILE	*fd;
	long	FileSize;

	if(direction == F_INPUT_FILE)
		fd = ((IoLines[Index].InFds)[DecimalStreamNumber]);
	else
		fd = ((IoLines[Index].OutFds)[DecimalStreamNumber]);

	FileSize = ftell(fd) / 512;

	if(fclose(fd) == -1)
		logger(1, "UNIX: Can't close file, error: %s\n", PRINT_ERRNO);

	return FileSize;
}


/*
 | Rename the received file name to a name created from the filename parameters
 | passed. The received file is received into a file named NJE_R_TMP.TMP and
 | renamed to its final name after closing it.
 | The flag defines whether the file will renamed according to the queue name
 | (RN_NORMAL) or will be put on hold = extenstion = .HOLD (RN_HOLD), or on
 | abort = .HOLD$ABORT (RN_HOLD$ABORT).
 | The name of the new file is returned as the function's value.
 | If Direction is OUTPUT_FILE, then we have to abort the sending file.
 | Note: Structures for reived files starts with IN (Incoming file) except
 |       from the FABS and RABS which start with OUT (VMS output file).
 */
char *
rename_file(Index, flag, direction, DecimalStreamNumber)
int	Index, flag, direction, DecimalStreamNumber;
{
	struct	FILE_PARAMS	*FileParams;
	char	InputFile[LINESIZE], ToNode[LINESIZE], *p;
	static char	line[LINESIZE];	/* Will return here the new file name */
	static	int	FileCounter = 0;	/* Just for the name... */

	if(direction == F_OUTPUT_FILE) {	/* The file just received */
		FileParams = &((IoLines[Index].InFileParams)[DecimalStreamNumber]);
		sprintf(InputFile, "%s/%s%d_%d.%s", BITNET_QUEUE, TEMP_R_FILE,
			Index, DecimalStreamNumber, TEMP_R_EXT);
	}
	else {	/* The sending file - change the flag to ABORT */
		FileParams = &((IoLines[Index].OutFileParams)[DecimalStreamNumber]);
		flag = RN_HOLD_ABORT;
		sprintf(InputFile, "%s", FileParams->OrigFileName);
	}

/* File is of format ASCII or EBCDIC ? */
	if((p = strchr(FileParams->To, '@')) != NULL)
		strcpy(ToNode, ++p);
	else	*ToNode = '\0';
	if(FileParams->format == ASCII)
		sprintf(line, "%s/ASC_%s_%04d", BITNET_QUEUE, ToNode, FileCounter++);
	else
		sprintf(line, "%s/EBC_%s_%04d", BITNET_QUEUE, ToNode, FileCounter++);
	FileCounter %= 999;	/* Make it modulo 1000 */
/* What line will it go to ??? */
	strcat(line, ".");
	if(flag == RN_NORMAL)
		strcat(line, FileParams->line);
	else
	if(flag == RN_HOLD)
		strcat(line, "HOLD");
	else
		strcat(line, "HOLD$ABORT");

	if(rename(InputFile, line) == -1)
		logger(1, "UNIX: Can't rename '%s' to '%s'. Error: %s\n",
			InputFile, line, PRINT_ERRNO);
	return line;	/* Return the new file name */
}


/*
 | Return the file size in 512 bytes blocks (to be compatible with VMS).
 */
get_file_size(FileName)
char	*FileName;
{
	struct	stat	Stat;

	if(stat(FileName, &Stat) == -1) {
		logger(1, "UNIX, Can't stat file '%s'. error: %s\n",
			FileName, PRINT_ERRNO);
		return 0;
	}
	return (int)((Stat.st_size / 512) + 1);
}


/*
 | Return the date in IBM's format. IBM's date starts at 1/1/1900, and UNIX starts
 | at 1/1/1970; IBM use 64 bits, where counting the microseconds at
 | the 50 or 51 bit (thus counting to 1.05... seconds of the lower bit of the
 | higher word). In order to compute the IBM's time, we compute the number of
 | days passed from Unix start time, add the number of days between 1970 and
 | 1900, thus getting the number of days since 1/1/1900. Then we multiply by the
 | number of seconds in a day, add the number of seconds since midnight and write
 | in IBM's quadword format - We count in 1.05.. seconds interval. The lower
 | longword is zeroed.
 */
ibm_time(QuadWord)
unsigned long	*QuadWord;
{
	QuadWord[1] = (long)(((float)(time(0)) / (float)(BIT_32_SEC)) +
			IBM_TIME_ORIGIN);
	QuadWord[0] = 0;
}

