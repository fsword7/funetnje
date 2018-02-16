/* CLUSTER_CLIENT.C	V1.0
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
 | LINK with READ_CONFIG.C
 |
 |  This module runs on a cluster node and passes requests to Mailer and NJE
 | as they are, via DECnet links. When a DECnet link is broken, it is retried
 | once every 5 minutes.
 |   The DECnet object numbers used are 203 and 204 and should be coordinated
 | with the mailer.
 */
#define	MAIN
#include "site_consts.h"
#include "consts.h"
#include <stdio.h>
#include <time.h>
#include <iodef.h>
#include <lnmdef.h>
#include <dvidef.h>

#define	NJE_CLUSTER_NUMBER	203
#define	NJE_MAILBOX		"HUJI_NJE_CMD"
#define	MAILER_CLUSTER_NUMBER	204
#define	MAILER_MAILBOX		"HUYMAIL_MBOX"

INTERNAL short	MailerChannel, NJEchannel,	/* For DECnet access */
		NJEMailboxChan, MailerMailboxChan;	/* For mailbox access */

INTERNAL int	LogLevel;	/* Refferenced in READ_CONFIG.C also */
FILE	*LogFd = NULL;

INTERNAL struct	LINE	IoLines[MAX_LINES];	/* For the line's database */

int	NJEinited, MAILERinited;	/* Have we succeeded creating the
					   link to them? */

#define	NJE	1	/* Send message to NJE's mailbox */
#define	MAILER	2	/* Mailer's mailbox */

/*
 | Create the mailboxes and communication path. If can't create a communication
 | path, retry once per 5 minutes.
 */
main()
{
	int	i;

	LogFd = NULL;	/* Log not opened yet */
	NJEinited = MAILERinited = 0;
	LogLevel = 1;
	if(read_configuration() == 0)
		exit(1);	/* Can't work without configuration */

	logger(1, "CLSTER_CLIENT: Starting...\n");
/* Try connection every 5 minutes untill success */
	if(create_decnet_link(NJE_CLUSTER_NUMBER, &NJEchannel) != 0)
		NJEinited = 1;
	if(create_decnet_link(MAILER_CLUSTER_NUMBER, &MailerChannel) != 0)
		MAILERinited = 1;

	init_command_mailbox(NJE_MAILBOX, &NJEMailboxChan);
	init_command_mailbox(MAILER_MAILBOX, &MailerMailboxChan);

/* All done; now it'll work in AST mode. However, try every 5 minutes to recreate
   a DECnet connection if we did not succeed before, or if the connections was
   disconnected. */
	for(;;) {
		sleep(300);
		if(NJEinited == 0)
			if(create_decnet_link(NJE_CLUSTER_NUMBER, &NJEchannel) != 0)
				NJEinited = 1;
		if(MAILERinited == 0)
			if(create_decnet_link(MAILER_CLUSTER_NUMBER, &MailerChannel) != 0)
				MAILERinited = 1;
	}
}

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
 | Open a channel to another host. We can't do it asynchronously since the
 | $ASSIGN system service is a synchronous one. Hopefully, this connect will
 | not take too much time.
 */
create_decnet_link(ObjectNumber, DECnetChannel)
int	ObjectNumber;
short	*DECnetChannel;
{
	char	ConnectString[SHORTLINE];
	struct	DESC	ConnectDesc;
	long	status;
	register int	i, TempVar;

/* Create the connection string */
	sprintf(ConnectString, "%s::\042%d=\042", ClusterNode, ObjectNumber);

/* Assign a channel to device */
	ConnectDesc.address = ConnectString; ConnectDesc.type = 0;
	ConnectDesc.length = strlen(ConnectString);
	if(((status = sys$assign(&ConnectDesc, DECnetChannel,
		(long)(0), (long)(0))) & 0x1) == 0) {
		logger(2, "CLUSTER_SERVER, Can't assign DECnet channel to '%s';\
 $ASSIGn status=%d\n",
			ConnectString, status);
		return 0;
	}

/* Connection succeeded - keep it... */
	return 1;
}

/*
 | Write a buffer to the given channel. If unsuccessfull, close the connection
 | and mark this link as inactive.
 */
write_DECnet(buffer, size, DECnetChannel)
char	*buffer;
short	DECnetChannel;
{
	int	status;
	short	iosb[4];

	status = sys$qiow((long)(0), DECnetChannel,
		(short)(IO$_WRITEVBLK), iosb,
		0, 0,
		buffer, size, (long)(0), (long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "CLUSTER_CLIENT, Can't write to DECnet, status=%d, iosb=%d\n",
			status, iosb[0]);
		if(DECnetChannel == NJEchannel)
			NJEinited = 0;
		else	MAILERinited = 0;
		sys$dassgn(DECnetChannel);
	}
}


/*
 | Init the operatore's communication mailbox and queue an AST for it.
 | the mailbxo created is accessible by everyone. The emulator checks who
 | is the sender in order to protect itself.
 | The mailbox is temporary and it's name is defined in the system table. This
 | helps us getting rid of the mailbox when the emulator crash.
 */
init_command_mailbox(MailboxName, MailboxChan)
char	*MailboxName;
short	*MailboxChan;
{
	long	status, mailbox_ast(),		/* The AST function */
		CrelnmFlags = LNM$M_TERMINAL;	/* FLags for logical name creation */
	static	long	Length;
	short	iosb[4];		/* IOSB for QIO call */
	char	DeviceName[64],		/* To get the name of mailbox device */
		NameTable[] = "LNM$SYSTEM";
	struct	DESC	MbxDesc,
			NameTableDesc = { (sizeof NameTable) - 1,
					  0, NameTable };
	struct	{
		short	length, code;
		char	*address,
			*ReturnLength;
		} GetDviList[] = {
				sizeof DeviceName, DVI$_DEVNAM, DeviceName, &Length,
				0, 0, NULL, NULL },
		  LogicalName[] = {
				sizeof(CrelnmFlags), LNM$_ATTRIBUTES, &CrelnmFlags, NULL,
				0, LNM$_STRING, DeviceName, NULL,
				0, 0, NULL, NULL };

/* Create the logical name descriptor */
	MbxDesc.address = MailboxName;  MbxDesc.length = strlen(MailboxName);
	MbxDesc.type = 0;

/* Create the mailbox as a temporary one */
	status = sys$crembx((unsigned char)(0), MailboxChan, (long)(0),
		(long)(0), (long)(0),	/* Allow access to all */
		(long)(3),	/* Access mode is user */
		&MbxDesc);
	if((status & 0x1) == 0) {
		logger((int)(1),
			"CLUSTER_CLIENT, Can't create command mailbox, status=%d.\n",
			status);
		exit(1);
	}

/* Get the MBAnnn: device name of it */
	status = sys$getdviw(0, *MailboxChan, 0, GetDviList, 0, 0, 0, 0);
	if((status & 0x1) == 0) {
		logger(1, "CLUSTER_CLIENT, Can't get device name of mailbox, status=%d\n",
			status);
		exit(status);
	}
	DeviceName[Length] = '\0';
	LogicalName[1].length = Length;

/* Define the logical name poiting to the MBAnnn: device in system table. */
	status = sys$crelnm(0, &NameTableDesc, &MbxDesc, 0, &LogicalName);
	if((status & 0x1) == 0) {
		logger(1, "CLUSTER_CLIENT, Can't decalare mailboxes's name in System's table.status=%d\n",
			status);
		exit(status);
	}

/* Set the write attention AST */
	status = sys$qiow((long)(0), *MailboxChan,
		(short)(IO$_SETMODE | IO$M_WRTATTN), iosb,
		(long)(0), (long)(0),	/* This is NOT the AST we want */
		mailbox_ast, *MailboxChan,	/* Will serve also to differentiate
						   between mailer and NJE */
		(long)(0), (long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger((int)(1),
			"CLUSTER_CLIENT, Can't queue AST for command mailbox, status=%d, iosb=%d\n",
			status, iosb[0]);
		exit(1);
	}
}


/*
 | This routine is called in AST mode when there is something to read from the
 | mailbox. It reads it, and re-queue the attention AST.
 */
long
mailbox_ast(MailboxChan)
int	MailboxChan;
{
	long	i, status;
	static unsigned char	line[LINESIZE]; 	/* Buffer for reading */
	static unsigned short	iosb[4];		/* IOSB for QIO */

/* Read the message from mailbox */
	status = sys$qiow((long)(0), MailboxChan,
		(short)(IO$_READVBLK), iosb,
		(long)(0), (long)(0),
		line, (long)(sizeof line), (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger((int)(1), "CLUSTER_CLIENT, Can't read mailbox, status=%d, iosb=%d\n",
			status, (int)(iosb[0]));
	}	/* We ignore this un-readable message */

	else {
/* Send the command to the correct interface. If the channel is down, try now
   to create a connection again. */
		if(MailboxChan == NJEMailboxChan) {
			if(NJEinited == 0) {	/* Have to retry */
				if(create_decnet_link(NJE_CLUSTER_NUMBER, &NJEchannel) != 0) {
					NJEinited = 1;
					write_DECnet(line, iosb[1], NJEchannel);
				}
			}
			else
				write_DECnet(line, iosb[1], NJEchannel);
		} else {
			if(MAILERinited == 0) {
				if(create_decnet_link(MAILER_CLUSTER_NUMBER, &MailerChannel) != 0) {
					MAILERinited = 1;
					write_DECnet(line, iosb[1], MailerChannel);
				}
			}
			else
				write_DECnet(line, iosb[1], MailerChannel);
		}
	}

/* Re-Enable the write attention AST */
	status = sys$qiow((long)(0), MailboxChan,
		(short)(IO$_SETMODE | IO$M_WRTATTN), iosb,
		(long)(0), (long)(0),	/* This is NOT the AST we want */
		mailbox_ast, MailboxChan, (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger((int)(1),
			"CLUSTER_CLIENT, Can't queue AST for command mailbox, status=%d, iosb=%d\n",
			status, iosb[0]);
		exit(1);
	}
}


/*
 |  Case insensitive strings comparisons. Return 0 only if they have the same
 |  length.
*/
#define	TO_UPPER(c)	(((c >= 'a') && (c <= 'z')) ? (c - ' ') : c)
compare(a, b)
char	*a, *b;
{
	register char	*p, *q;

	p = a; q = b;

	for(; TO_UPPER(*p) == TO_UPPER(*q); p++,q++)
		if((*p == '\0') || (*q == '\0')) break;

	if((*p == '\0') && (*q == '\0'))	/* Both strings done = Equal */
		return 0;

/* Not equal */
	return(TO_UPPER(*p) - TO_UPPER(*q));
}


/*
 | Dummy routine.
 */
add_gone_user()
{
}
