/* VMS.C    V4.2
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
 | Do some specific VMS work - mailboxes, sending messages, timers.
 | Notes:
 | 1. The communication mailbox is assigned to the logical name HUJI_NJE_CMD.
 |    It is used only to pass information TO the emulator. The format of the
 |    messages is described bellow.
 | 2. The maximum number of timer entries is defined to be 10. Increase to more
 |    if needed.  If there is no room in the timer queue, the program aborts.
 | 3. The timer holds the numbr of seconds to expiration. 1 is added to it,
 |    because the next expiration could be quite soon after queing. Thus,
 |    a timeout of n seconds is really between n to n+1 seconds.
 | 4. The File I/O parts should be modified to handle multiple streams.
 | 5. The received file is placed in BITNET_QUEUE named NJE_R_TEMP.TMP; It
 |    is renamed when the file is closed, to show it disposition.
 | 6. When a shutdown is requested - it sends signoff on active lines.
 | 7. The command mailbox is opened for commands from anyone. However, all
 |    privileged commands can be issued only by users whose username is in
 |    the INFORM command.
 |
 | Mailbox message format: One byte of command and the other is parameters of
 | the command. The commands are:
 | CMD_SHUTDOWN:  Shutdown this daemon gracefully.
 | CMD_SHOW_LINES: Show lines status
 | CMD_QUEUE_FILE: Queue a file to send.
 | CMD_SEND_MESSAGE/COMMAND - Send an interactive message or command.
 | CMD_START_LINE - Start an inactive line.
 |
 | Sections in this file:
 | BROADCAST - Inform operator and user for various events.
 | MAILBOX   - Mailbox communication with command mbx,
 | TIMER     - Timer queue, deque and timeout.
 | FILES     - Files I/O and directory searches.
 |
 | V1.1 - Change the temporary file name from TEMP_R_TMP.TMP to
 |        TEMP_R_TMP_#.TMP where # is the line number.
 | V1.2 - Change the DECnet timoeout routine to do nothing.
 | V1.3 - Change DECnet timeout back to call simulate ACK.
 | V1.4 - The close-file function returns the size of the file (in blocks).
 |        (After various tests - it can't do it..., so we call get_file_size
 |         for it). This costs us more file accesses.
 | V1.5 - Add function get_file_size which returns the file's size in blocks.
 | V1.6 - Add IBM-TIME() which returns the current time in IBM format
 |        (the time is aproximate).
 | V1.7 - Change the code T_EXOS_SELECT to T_TCP_TIMEOUT, so the same routine
 |        will be useable for EXOS and MULTINET.
 | V1.8 - When a command/message comes from the user with SEND/xxx, it does not
 |        contains the local nodename (as in the past), so add it.
 | V1.9 - Call Handle-Ack always on reliable links (except from when the line
 |        in in initialization steps).
 | V2.0 - Send messages to OPCOM (up to now we only logged them).
 | V2.1 - Add the commands FORCE LINE.
 | V2.2 - Add the T_DMF_RESTART in the timer-ast routine.
 | V2.3 - Add the DEBUG RESCAN and DEBUG DUMP commands. Dispatch to routines in
 |        IO.C
 | V2.4 - Add CMD_LOGLEVEL to change the log level during run. Before changing,
 |        we call the log function while setting the loglevel to 1 in order to
 |        close the log file. This guarantees writing all logs up to this
 |        point.
 | V2.5 - Change the protection of the command mailbox so everyone can issue
 |        commands. However, when it is a privileged command, we check the
 |        username against the INFORM list.
 | V2.6 - Change Authorized-User procedure to enable SYSTEM as authorized user
 |        even if it doesn't appear in INFORM list. This allows mailer to queue
 |        files when it is started from SYSTARTUP (thus it runs under SYSTEM)
 |        without the need to define SYSTEM in INFORM list.
 | V2.7 - Change the handling of auto restarts. Instead of each routine queueing
 |        the autorestart, we simply loop every 5 minutes and check whether there
 |        are links needing restarts.
 | V2.8 - 7/2/90 - Add the routine Delete_line_timeouts which is called by
 |        Restart Channel.
 | V2.9 - 11/2/90 - Change inform-mailer; previously it woke-up the mailer.
 |        Now, it connects to its command mailbox and queue the file there.
 | V3.0 - 3/3/90 - When queueing the auto-restart timer entry change the index
 |        number to -1 so it won't be deleted when line 0 is inactivated.
 |        Also simplify auto_restart_line() routine.
 | V3.1 - Correct a bug when computing the PID to pass to Authorized_user() function.
 |        The left part was moved 8 bits instead of 16.
 | V3.2 - 7/3/90 - Add CMD_CHANGE_ROUTE to enable operator to update database
 |        online.
 | V3.3 - 19.3.90 - If we are in shutdown process, ignore auto restarts.
 | V3.4 - 22/3/90 - Add CMD_GONE_ADD and CMD_GONE_DEL commands to maintain
 |        the gone list.
 |        Change the Send_user to call Send_Gone() is no user has been found.
 |        When shutting down call shut_gone_users() to inform them.
 | V3.5 - 28.3.90 - When deleting a file, if its name doesn't contain ; already
 |        then append ;0 to it.
 | V3.6 - 1/4/90 - Move the pasrsing of the command accepted from the mailbox
 |        command channel into IO.C
 | V3.7 - 4/4/90 - Do not create permanent mailbox any more. Use a temporary
 |        one and place its name in the system's table. This will clear the
 |        mailbox upon NJE's crash and will not hang the mailer.
 | V3.8 - 19/4/90 - add BRK$M_CLUSTER flag when broadcasting messages to users.
 | V3.9 - 15/6/90 - When renaming a received file search for the last @ in the
 |        address and not for the first one. This is because there are sometimes
 |        addresses with more than one @ in them.
 | V4.0 - 7/10/90 - Open_recv_file(), Delete_file(), Rename_file(), Close_file()
 |        uwrite()
 |         - Accept also the stream number.
 | V4.1 - 11/3/91 - Add multistream support on send.
 | V4.2 - 26/12/91 - Clear file's flags in OPen_Recv_file.
 */
#include "consts.h"
#include "headers.h"
#include <iodef.h>
#include <jpidef.h>
#include <ssdef.h>
#include "brkdef.h"
#include <lnmdef.h>
#include <dvidef.h>

#define	TEMP_R_FILE	"NJE_R_TMP_"	/* Temporary filename */
#define	TEMP_R_EXT	"TMP"		/* It's extension */
#define	MAILER_PROCESS	"BITNET_MAILER" /* The process name of the mailer */

static	short	MailBoxChan;		/* The channel for mailbox I/O */
static	long	DeltaTime[2];		/* Binary time of 1 second */
EXTERNAL	int	MustShutDown;		/* We have to shutdown */
EXTERNAL	int	LogLevel;		/* So we can change it */

EXTERNAL struct	LINE	IoLines[MAX_LINES];

#define	MAX_QUEUE_ENTRIES	12	/* Maximum entries in the timer queue */
struct	TIMER {				/* The timer queue */
		short	expire;		/* Number of seconds untill expiration.
					   if currently 0, unused entry. */
		int	index;		/* The index in IoLines of the line
					   on which this timeout is issued */
		short	action;		/* What to do when time expires? */
	} TimerQueue[MAX_QUEUE_ENTRIES];

#define	EXPLICIT_ACK	0
#define	DELAYED_ACK	2	/* Must be in accordance with PROTOCOL.C */

#define	IBM_TIME_ORIGIN	15020	/* Number of days since VMS start time and 1/1/00 */
#define	SECONDS_IN_DAY	86400	/* Number of seconds in one day */
#define	BIT_32_SEC	1.048565 /* The number of seconds the 32 bit is */

/*====================== BROADCAST section =========================*/

/*
 | Send messages to operator. Log it before sending it.
 */
send_opcom(string)
char	*string;
{
	struct DESC	command_desc;
	char	line[512];
	long	status;

	logger(2, "VMS, Opcom message: %s\n", string);

/* Create the OPCOM buffer. Very ugly, but we'll change it in the future. */
	sprintf(line, "%c%c%c%c0000%s\r\n", 3, 0xff, 0xff, 0xff, string);
	command_desc.address = line; command_desc.length = strlen(line);
	command_desc.type = 0;
	line[4] = line[5] = line[6] = 0; line[7] = 1;	/* Request #1 */
	sys$sndopr(&command_desc, 0);
}

/*
 |  Given a user-name, find all terminals he is logged-in and send him
 |  the given message. We use 5 seconds timeout since this is the minimal
 | time allowed by VMS.
 | The function returns the number of terminals successfully notified.
 | If the user is not logged-in, call Send_gone as a last resort if the user
 | is registered in the Gone list.
*/
int
send_user(UserName, string)
char	*UserName, *string;
{
	static struct	DESC	username, message;
	static char	temp[LINESIZE], *p;
	static short	iosb[2];
	static long	sndtyp, reqid;
	long	status;

/* First - modify username to upper case for $BRKTHRU (can't handle lower
   case) */
	strcpy(temp, UserName);
	for(p = temp; *p != '\0'; p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';

	message.length = strlen(string);  message.address = string;	/* Create a descriptor */
	username.length = strlen(temp); username.address = temp;
	sndtyp = BRK$C_USERNAME;	/* Send by username */
	reqid = BRK$C_USER1;		/* Type of message */
	status = sys$brkthruw((long)(0),
		&message, &username, sndtyp,
		iosb, (long)(0), (long)(BRK$M_CLUSTER), reqid,
		(long)(5),	/* 5 seconds timeout */
		(long)(0), (long)(0));
#ifdef DEBUG
	if((status & 0x1) == 0)
		logger(1, "VMS: Can't broadcast message to user '%s'; status=d^%d\n",
			UserName, status);
#endif
/* Return the number of terminals that received the message */
	if(iosb[1] == 0)
		return(send_gone(UserName, string));
	else	/* Found - inform number of terminal informed */
		return (int)(iosb[1]);
}

/*
 | Queue the file to the mailer. Connect to its command mailbox and write the
 | filename there.
 */
#define	MAILER_MAILBOX_NAME	"HUYMAIL_MBOX"
#define	QUEUE_FILE	1	/* Queue file to mailer */
inform_mailer(FileName)
char	*FileName;
{
	struct	DESC	MailBoxDesc;
	char	line[LINESIZE];
	long	i, size, status;
	short	chan, iosb[4];

	*line = QUEUE_FILE;
	strcpy(&line[1], FileName);
	size = strlen(&line[1]) + 2;	/* +1 for the keyword, +1 for th last null */
	MailBoxDesc.address = MAILER_MAILBOX_NAME;
	MailBoxDesc.length = strlen(MAILER_MAILBOX_NAME);
	MailBoxDesc.type = 0;
	status = sys$assign(&MailBoxDesc, &chan, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		logger(2, "VMS, Can't connect to mailer mailbox, status=%d\n", status);
		return;
	}

	status = sys$qiow((long)(0), chan,
		(short)(IO$_WRITEVBLK), iosb,
		(long)(0), (long)(0),
		line, strlen(&line[1]) + 1, (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(2, "VMS, Can't write mailer mailbox, status=%d, iosb=%d\n",
			status, iosb[0]);
	}
	sys$dassgn(chan);
}


/*
 | Loop over the INFORM users list. If found there, return 1. If not, return 0.
 | The function gets a PID, finds its username, and then do the search.
 | UserName SYSTEM is harcoded to be privilged, even if it is not in INFORM list.
 */
authorized_user(pid, UserName)
int	pid;
char	*UserName;
{
	static	long	item_list[4];	/* The items list for GETJPI */
	long	i, status, length;
	char	*p;

/* Init the item list */
	item_list[0] = (JPI$_USERNAME << 16) + 12;	/* Find process name */
	item_list[1] = UserName;		/* Address of buffer */
	item_list[2] = item_list[3] = 0;			/* End of list */

/* Get the username from PID */
	if(((status = sys$getjpiw((int)(0),&pid,(int)(0),item_list,(int)(0),
		(int)(0),(int)(0))) & 0x1) == 0) {
			logger(1, "VMS, Can't get mailbox sender's \
username. Status=%d, pid=%d\n",
				status, pid);
			return 0;	/* No user - not authorized */
	}

/* Remove trailing blanks */
	UserName[12] = '\0';
	for(i = 11; i > 0; i--)
		if(UserName[i] != ' ') break;
	UserName[++i] = '\0';

/* Test whether it is SYSTEM */
	if(compare(UserName, "SYSTEM") == 0)
		return 1;	/* OK - authorized */

/* Search for it in the INFORM users list */
	status = 0;	/* Default - not found */
	length = strlen(UserName);
	for(i = 0; i < InformUsersCount; i++) {
		if(InformUsers[i][length] != '@')
			continue;	/* Length does not match */
		if((strncmp(UserName, InformUsers[i], length) == 0) &&
		   (compare(&InformUsers[i][length + 1], LOCAL_NAME) == 0)) {
			status = 1; break;	/* Found */
		}
	}
	return status;
}


/*======================== MAILBOX section =======================*/
/*
 | Init the operatore's communication mailbox and queue an AST for it.
 | the mailbxo created is accessible by everyone. The emulator checks who
 | is the sender in order to protect itself.
 | The mailbox is temporary and it's name is defined in the system table. This
 | helps us getting rid of the mailbox when the emulator crash.
 */
init_command_mailbox()
{
	long	status, mailbox_ast(),		/* The AST function */
		CrelnmFlags = LNM$M_TERMINAL;	/* FLags for logical name creation */
	static long	Length;
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
	MbxDesc.address = MAILBOX_NAME;  MbxDesc.length = strlen(MAILBOX_NAME);
	MbxDesc.type = 0;

/* Create the mailbox as a temporary one */
	status = sys$crembx((unsigned char)(0), &MailBoxChan, (long)(0),
		(long)(0), (long)(0),	/* Allow access to all */
		(long)(3),	/* Access mode is user */
		&MbxDesc);
	if((status & 0x1) == 0) {
		logger(1,
			"VMS, Can't create command mailbox, status=%d.\n",
			status);
		send_opcom("HUJI-NJE aborted. Can't create command mailbox");
		exit(status);
	}

/* Get the MBAnnn: device name of it */
	status = sys$getdviw(0, MailBoxChan, 0, GetDviList, 0, 0, 0, 0);
	if((status & 0x1) == 0) {
		logger(1, "VMS, Can't get device name of mailbox, status=%d\n",
			status);
		bug_check("GETDVI error");
	}
	DeviceName[Length] = '\0';
	LogicalName[1].length = Length;

/* Define the logical name poiting to the MBAnnn: device in system table. */
	status = sys$crelnm(0, &NameTableDesc, &MbxDesc, 0, &LogicalName);
	if((status & 0x1) == 0) {
		logger(1, "VMS, Can't decalare mailboxes's name in System's table.status=%d\n",
			status);
		bug_check("CRELNM error");
	}

/* Set the write attention AST */
	status = sys$qiow((long)(0), MailBoxChan,
		(short)(IO$_SETMODE | IO$M_WRTATTN), iosb,
		(long)(0), (long)(0),	/* This is NOT the AST we want */
		mailbox_ast, (long)(0), (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1,
			"VMS, Can't queue AST for command mailbox, status=%d, iosb=%d\n",
			status, iosb[0]);
		send_opcom("HUJI-NJE aborted. Can't queue AST for command mailbox");
		exit(status);
	}
}


/*
 | Close the command mailbox and deassign the system;s wide logical name of it.
 */
close_command_mailbox()
{
	long	status, AccessMode;
	char	TableName[] = "LNM$SYSTEM";
	struct	DESC	TableDesc = { sizeof(TableName) - 1, 0, TableName },
			NameDesc = { sizeof(MAILBOX_NAME) - 1, 0, MAILBOX_NAME };

	if(((status = sys$dassgn(MailBoxChan)) & 0x1) == 0) {
		logger(1, "FILE_QUEUE, Can't close mailbox, status=%d\n", status);
		return;
	}
	AccessMode = 3;	/* User mode */
	if(((status = sys$dellnm(&TableDesc, &NameDesc, &AccessMode)) & 0x1) == 0) {
		logger(1, "FILE_QUEUE, Can't delete logical name '%s'\
 from table '%s', status=%d\n", MAILBOX_NAME, TableName, status);
		return;
	}
}


/*
 | This routine is called in AST mode when there is something to read from the
 | mailbox. It reads it, and re-queue the attention AST.
 */
long
mailbox_ast()
{
	long	i, status;
	static unsigned char	line[LINESIZE]; 	/* Buffer for reading */
	unsigned char	Faddress[SHORTLINE],	/* Sender for NMR messages */
			Taddress[SHORTLINE],	/* Receiver for NMR */
			UserName[16],	/* For verifying sender's authority */
			*p;
	static unsigned short	iosb[4];		/* IOSB for QIO */
	EXTERNAL struct SIGN_OFF SignOff;

/* Read the message from mailbox */
	status = sys$qiow((long)(0), MailBoxChan,
		(short)(IO$_READVBLK), iosb,
		(long)(0), (long)(0),
		line, (long)(sizeof line), (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS, Can't read mailbox, status=%d, iosb=%d\n",
			status, (int)(iosb[0]));
	}	/* We ignore this un-readable message */

/* Parse the command. In most commands, the parameter is the username to
   broadcast the reply to. */
	else {
/* Get the username from the PID of message's sender. If he is not authorized
   and the command needs authorization then reject it */
		line[iosb[1]] = '\0';	/* Delimit the username */
		status = authorized_user((unsigned long)((iosb[3] << 16) | iosb[2]), UserName);
		if(status == 0) {	/* Not authorized */
			switch(*line) {
			case CMD_SEND_MESSAGE:
			case CMD_SEND_COMMAND:
			case CMD_GONE_ADD:
			case CMD_GONE_DEL:
				parse_operator_command(line);
				break;	/* These are allowed to all users */
			default:	/* Not authorized  - send him message and reject */
				logger(1, "VMS, User %s issued an unauthorized command\n",
					UserName);
				sprintf(Faddress, "@%s", LOCAL_NAME);
				sprintf(Taddress, "%s@%s", UserName, LOCAL_NAME);
				strcpy(line, "You are not autorized to issue this command");
				send_nmr(Faddress, Taddress,
					line, strlen(line),
					(int)(ASCII), (int)(CMD_MSG));
			}
		}
		else	/* Authorized - process all commands */
			parse_operator_command(line);
	}

/* Re-queue the attention AST */
	status = sys$qiow((long)(0), MailBoxChan,
		(short)(IO$_SETMODE | IO$M_WRTATTN), iosb,
		(long)(0), (long)(0),	/* This is NOT the AST we want */
		mailbox_ast, (long)(0), (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1,
			"VMS, Can't requeue AST for command mailbox, status=%d, iosb=%d\n",
			status, iosb[0]);
	}
}


/*======================== TIMER section =========================*/

/*
 | Init the timer queue. Set all entries to zero, and queue a scheduled wakeup
 | to tick every one second.
 */
init_timer()
{
	long	i, status, timer_ast();
	struct	DESC	TimeDesc;	/* The time descriptor for 1 second */
	char	OneSecond[] = "0 00:00:01.00"; /* One second */

/* Zero all the requests */
	for(i = 0; i < MAX_QUEUE_ENTRIES; i++)
		TimerQueue[i].expire = (short)(0);

/* Convert the time to binary */
	TimeDesc.address = OneSecond; TimeDesc.length = strlen(OneSecond);
	TimeDesc.type = 0;
	status = sys$bintim(&TimeDesc, DeltaTime);
	if((status & 0x1) == 0) {
		logger(1, 
			"VMS, Can't convert time, status=%d\n", status);
		send_opcom("HUJI-NJE: Fatal insitialization error. Aborting.");
		exit(status);
	}

/* Now queue the periodic AST */
	status = sys$setimr((long)(0), DeltaTime, timer_ast, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		logger(1,
			"VMS, Can't schedule timer AST, status=%d\n", status);
		send_opcom("HUJI-NJE: Can't schedule timer AST");
		exit(status);
	}
}


/*
 | The periodic timer routine. It is called once a second (and re-queues
 | itself for the next second). It checks the timer queue, and cancels the
 | IO on a channel if the time expired.
 | A special treament is done for EXOS - since ACKs are not sent there,
 | we simulate it when the timer expires. We simulate it only when we know
 | that no input is expected from the other side.
 */
timer_ast()
{
	register long	i, status;
	struct	LINE	*temp;

/* Decrement the expiration time for each active timeout. If reached zero,
   handle it.
*/
	for(i = 0; i < MAX_QUEUE_ENTRIES; i++) {
		if(TimerQueue[i].expire != 0) {		/* Active */
			if(--(TimerQueue[i].expire) == 0) {
				switch(TimerQueue[i].action) {
				case T_DMF_CLEAN:	/* Abort I/O on DMF */
					  if(dmf_timeout(TimerQueue[i].index) == 1)
						TimerQueue[i].expire = 99;
					/* This entry will be dequeued soon
					   by the DMF's timeout routine */
					  break;
				case T_ASYNC_TIMEOUT:	/* Abort I/O on terminal */
					  async_timeout(TimerQueue[i].index);
					  break;
				case T_SEND_ACK:	/* Send ACK to line */
					  handle_ack(TimerQueue[i].index,
						(short)(DELAYED_ACK));
					  break;
				case T_TCP_TIMEOUT:
				/* Read timeout, Simulate an an ACK if needed */
					temp = &(IoLines[TimerQueue[i].index]);
					if(temp->state == ACTIVE) {
						handle_ack(TimerQueue[i].index,
						 (short)(EXPLICIT_ACK));
					}
						/* Requeue it: */
					if((temp->state != INACTIVE) &&
					   (temp->state != LISTEN) &&
					   (temp->state != RETRYING)) {
						temp->TimerIndex = queue_timer(temp->TimeOut,
							TimerQueue[i].index,
							(short)(T_TCP_TIMEOUT));
					}
					break;
				case T_DECNET_TIMEOUT:
				/* Read timeout, Simulate an an ACK if needed */
					temp = &(IoLines[TimerQueue[i].index]);
					if(temp->state == ACTIVE) {
						handle_ack(TimerQueue[i].index,
						 (short)(EXPLICIT_ACK));
					}
				/* Requeue it */
					temp->TimerIndex = queue_timer(temp->TimeOut,
						TimerQueue[i].index,
						(short)(T_DECNET_TIMEOUT));
					break;
				case T_AUTO_RESTART:
					if(MustShutDown == -1)
						break;	/* We are in shutdown process */
					auto_restart_lines();
					queue_timer((short)(T_AUTO_RESTART_INTERVAL),
						(int)(-1), (short)(T_AUTO_RESTART));
					break;
#ifdef DEBUG
				case T_STATS:	/* Compute statistics */
					compute_stats();
					break;
#endif
				default:  bug_check(
						"VMS: No timeout routine (%d).\n",
							TimerQueue[i].action);
				}
			}
		}
	}

/* Now re-queue the periodic AST */
	status = sys$setimr((long)(0), DeltaTime, timer_ast, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		logger(1,
			"VMS, Can't reschedule timer AST, status=%d\n", status);
		send_opcom("HUJI-NJE: Can't reschedule periodic AST");
		exit(status);
	}
}


/*
 | Queue an entry. The line index and the timeout are expected. Increase it by
 | one to be on the safe side. A constant which decalres the routine to call
 | on timeout is also expected.
 */
int
queue_timer(expiration, Index, WhatToDo)
short	expiration, WhatToDo;
int	Index;			/* Index in IoLines array */
{
	register int	i;

	for(i = 0; i < MAX_QUEUE_ENTRIES; i++) {
		if(TimerQueue[i].expire == 0) {	/* Free */
			TimerQueue[i].expire = expiration + 1;
			TimerQueue[i].index = Index;
			TimerQueue[i].action = WhatToDo;
			return (int)(i);
		}
	}

/* Not found an entry - bug check */
	logger(1, "VMS, Can't queue timer, timer queue dump:\n");
	for(i = 0; i < MAX_QUEUE_ENTRIES; i++)
		logger(1, "  #%d, Expire=%d, index=%d, action=%d\n",
			i, (int)(TimerQueue[i].expire),
			TimerQueue[i].index, (int)(TimerQueue[i].action));
	bug_check("VMS, can't queue timer entry.");
}


/*
 | Dequeue a timer element (I/O completed ok, so we don't need this timeout).
 */
dequeue_timer(TimerIndex)
int	TimerIndex;		/* Index into our timer queue */
{
	if(TimerQueue[TimerIndex].expire == 0) {
		/* Dequeueing a non-existent entry... */
		logger(2, "VMS, Unexisted timer queue, Index=%d, action=%d\n",
			TimerQueue[TimerIndex].index,
			(int)(TimerQueue[TimerIndex].action));
/*		bug_check("VMS, dequeing unexisted timer queue entry."); */
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
		case RETRYING:	restart_line(i);	/* Yes it needs... */
				break;
		default:	break;		/* Other states - no need */
		}
	}
}


/*
 | Return the date in IBM's format. IBM's date starts at 1/1/1900, and VMS starts
 | at 17/11/1858; both times use 64 bits, but IBM's is counting microseconds at
 | the 50 or 51 bit (thus counting to 1.05... seconds of the lower bit of the
 | higher word). In order to compute the IBM's time, we compute the number of
 | days passed from VMS start time, subtract the number of days between 1858 and
 | 1900, thus getting the number of days since 1/1/1900. Then we multiply by the
 | number of seconds in a day, add the number of seconds since midnight and write
 | in IBM's quadword format - We count in 1.05.. seconds interval. The lower
 | longword is zeroed.
 */
ibm_time(QuadWord)
unsigned long	*QuadWord;
{
	unsigned long	NumberOfDays, TimeSinceMidnight;

	lib$day(&NumberOfDays, (int)(0), &TimeSinceMidnight);
	NumberOfDays -= IBM_TIME_ORIGIN;	/* Compute number of days since 1900 */
	NumberOfDays = NumberOfDays * (int)((float)(SECONDS_IN_DAY) / 
		(float)(BIT_32_SEC));
	TimeSinceMidnight = (int)((float)(TimeSinceMidnight) /
		((float)(100) * BIT_32_SEC));
		/* 100 to convert to seconds */
	QuadWord[1] = NumberOfDays + TimeSinceMidnight;
	QuadWord[0] = 0;
}
/*========================== FILES section =========================*/

/*
 | Find the next file matching the given mask.
 | Input:  FileMask - The mask to search.
 |         context  - Should be passed by ref. Must be zero before search, and
 |                    shouldn't be modified during the search.
 | Output: find_file() - 0 = No more files;  1 = Matching file found.
 |         FileName - The name of the found file.
 */
find_file(FileMask, FileName, context)
char	*FileMask, *FileName;
long	*context;
{
	struct	DESC	Mask, Name;
	register long	status;
	register char	*p;

	Mask.length = strlen(FileMask); Mask.type = 0; Mask.address = FileMask;
	Name.address = FileName; Name.length = LINESIZE;
	Name.type = 0;
	if(((status = LIB$FIND_FILE(&Mask, &Name, context)) & 0x1) == 0) {
		LIB$FIND_FILE_END(context);	/* Clear the buffer */
		return 0;
	}
/* Delimit the returned string. It is padded with blanks */
	if((p = strchr(FileName, ' ')) != NULL) *p = '\0';
	return 1;
}

/*
 | Return the file size of the given file name.
 */
get_file_size(FileName)
char	*FileName;
{
	struct	FAB	fab;
	int	FileSize, status;

	fab = cc$rms_fab;	/* Put default values */
	fab.fab$l_fna = FileName;	/* File name to open */
	fab.fab$b_fns = strlen(FileName);
	fab.fab$l_xab = 0;	/* no XAB */

	/* Open the file */
	if(((status = sys$open(&fab)) & 0x1) == 0) {	/* Open file. */
		logger(1, "VMS, name='%s', $OPEN status=%d, fab$l_stv=%d\n",
			FileName, status, fab.fab$l_stv);
		return 0;
	}
	FileSize = fab.fab$l_alq;
	/* Close the file */
	sys$close(&fab);
	return FileSize;
}


/*
 | Open the file which will be transmitted. Save its FAB and RAB in the IoLine
 | structure. It also calls the routine that parses our envelope in the file.
*/
open_xmit_file(Index, DecimalStreamNumber, FileName)
int	Index,		/* Index into IoLine structure */
	DecimalStreamNumber;
char	*FileName;
{
	int	rms_status;
	struct	LINE	*temp;
	struct	FAB	*fab;
	struct	RAB	*rab;

	temp = &(IoLines[Index]);
	fab = &((temp->InFabs)[DecimalStreamNumber]);
	rab = &((temp->InRabs)[DecimalStreamNumber]);

	/* Assign FAB the correct values */
	*fab = cc$rms_fab;	/* Put default values */
	fab->fab$l_fna = FileName;	/* File name to open */
	fab->fab$b_fns = strlen(FileName);
	fab->fab$b_fac = (FAB$M_BRO | FAB$M_GET);
				/* Allow both record and block I/O */
	fab->fab$l_xab = 0;	/* no XAB */

	/* Assign RAB values */
	*rab = cc$rms_rab;	/* Default values */
	rab->rab$l_fab = fab;	/* FAB address */
	rab->rab$b_rac = RAB$C_SEQ;	/* Sequential access */
	rab->rab$l_rop = RAB$M_RAH;	/* Read blocks in advance */

	/* Open the file */
	if(((rms_status = sys$open(fab)) & 0x1) == 0) {	/* Open file. */
		logger(1, "VMS, name='%s', $OPEN status=%d, fab$l_stv=%d\n",
			FileName, rms_status, fab->fab$l_stv);
		return 0;
	}
	if(((rms_status = sys$connect(rab)) & 0x1) == 0) {	/* Connect the RAB */
		sys$close(fab);
		logger(1, "VMS, Can't connect RAB, status=%d\n", rms_status);
		return 0;
	}

	rab->rab$l_ctx = 0;	/* Clear count */
	parse_envelope(Index, DecimalStreamNumber);	/* get the file's data from our envelope */
	((temp->OutFileParams)[DecimalStreamNumber]).FileSize = fab->fab$l_alq * 512;
		/* Aproximate file size in bytes */
	return 1;
}


/*
 | Open the file to be received. The FAB and RAB are stored in the IoLines
 | structure. The file is created in BITNET_QUEUE directory, and is called
 | RECEIVE_TEMP.TMP; It'll be renamed after receive is complete to a more
 | sensible name.
 |   The called MUST make sure that DecimalStreamNumber is in the range of
 | acceptable streams.
*/
open_recv_file(Index, DecimalStreamNumber)
int	Index,		/* Index into IoLine structure */
	DecimalStreamNumber;	/* The stream number in the range 0-7. */
{
	int	rms_status;
	struct	LINE	*temp;
	struct	FAB	*fab;
	struct	RAB	*rab;
	char	FileName[LINESIZE];

	temp = &(IoLines[Index]);
	fab = &((temp->OutFabs)[DecimalStreamNumber]);
	rab = &((temp->OutRabs)[DecimalStreamNumber]);

/* Create a unique filename in the queue */
	sprintf(FileName, "%s%s%d_%d.%s", BITNET_QUEUE, TEMP_R_FILE, Index,
			DecimalStreamNumber, TEMP_R_EXT);
	strcpy((temp->InFileParams[DecimalStreamNumber]).OrigFileName,
		FileName);
	(temp->InFileParams[DecimalStreamNumber]).NetData = 0;
	(temp->InFileParams[DecimalStreamNumber]).RecordsCount = 0;
	((temp->InFileParams[DecimalStreamNumber]).FileName)[0] =
		((temp->InFileParams[DecimalStreamNumber]).FileExt)[0] =
		((temp->InFileParams[DecimalStreamNumber]).JobName)[0] = '\0';

	/* Assign FAB the correct values */
	*fab = cc$rms_fab;	/* Put default values */
	fab->fab$l_fna = FileName;	/* File name to open */
	fab->fab$b_fns = strlen(FileName);
	fab->fab$b_fac = (FAB$M_BRO | FAB$M_PUT);
				/* Allow both record and block I/O */
	fab->fab$l_fop = FAB$M_MXV;	/* Create new version if exists already */
	fab->fab$b_org = FAB$C_SEQ;	/* Sequential file */
	fab->fab$b_rfm = FAB$C_VAR;	/* Variable size records */
	fab->fab$b_rat = FAB$M_CR;	/* So we can TYPE it during debugging */
	fab->fab$l_xab = 0;

	/* Assign RAB values */
	*rab = cc$rms_rab;	/* Default values */
	rab->rab$l_fab = fab;	/* FAB address */
	rab->rab$b_rac = RAB$C_SEQ;
	rab->rab$l_rop = RAB$M_WBH;	/* Write behind to improve performance */

	/* Open the file */
	if(((rms_status = sys$create(fab)) & 0x1) == 0) {	/* Open file. */
		logger(1, "VMS, name='%s', $CREATE status=%d, fab$l_stv=%d\n",
			FileName, rms_status, fab->fab$l_stv);
		return 0;
	}
	if(((rms_status = sys$connect(rab)) & 0x1) == 0) {	/* Connect the RAB */
		sys$close(fab);
		logger(1, "VMS, Can't connect RAB, status=%d\n", rms_status);
		return 0;
	}

	rab->rab$l_ctx = 0;	/* Clear count */
	((temp->InFileParams)[DecimalStreamNumber]).flags = 0;	/* Clear all flags */
	return 1;
}


/*
 |  Write the given string into the file using RMS.
*/
uwrite(Index, DecimalStreamNumber, string, size)
unsigned char	*string;	/* Line descption */
int	Index, DecimalStreamNumber, size;
{
	int	rms_status;
	struct	RAB	*outrab;

	outrab = &((IoLines[Index].OutRabs)[DecimalStreamNumber]);
	outrab->rab$l_rbf = string;	/* Address of buffer */
	outrab->rab$w_rsz = size;	/* Length of buffer */

	rms_status = sys$put(outrab);	/* write in file */
	if((rms_status & 0x1) == 0) {
		logger(1, "VMS: Can't write, $PUT status=%d\n", rms_status);
		return 0;
	}
	else
		return 1;
}

/*
 |  Read from the given file. The function returns the number of characters
 | read, or -1 if EOF. If there is anothre error we put one spave in line,
 | to not blocking the whole transfer.
*/
int
uread(Index, DecimalStreamNumber, string, size)
unsigned char	*string;	/* Line descption */
int	Index, size, DecimalStreamNumber;
{
	int	rms_status;
	struct	RAB	*inrab;

	inrab = &((IoLines[Index].InRabs)[DecimalStreamNumber]);
	inrab->rab$l_ubf = string;	/* Address of buffer */
	inrab->rab$w_usz = size;		/* Length of buffer */

	rms_status = sys$get(inrab);	/* Read from file */
	if(rms_status & 0x1) {
		string[inrab->rab$w_rsz] = '\0';
		return inrab->rab$w_rsz;	/* Return string's length */
	}
	else {
#ifdef DEBUG
		if(rms_status != 98938) {	/* Not EOF */
			string[0] = ' ';	/* Put there something */
			string[1] = '\0';
			logger(1, "VMS: Uread status = %d\n", rms_status);
			return 1;
		}
		logger(2, "VMS: Uread status = %d\n", rms_status);
#endif
		return -1;
	}
}


/*
 | Close and Delete a file given its index into the Lines database.
 */
delete_file(Index, direction, DecimalStreamNumber)
int	Index, direction, DecimalStreamNumber;
{
	struct	FAB	*fab, DelFab;
	unsigned char	*FileName;
	register long	status;

	if(direction == F_INPUT_FILE) {
		fab = &((IoLines[Index].InFabs)[DecimalStreamNumber]);
		FileName = IoLines[Index].OutFileParams[DecimalStreamNumber].OrigFileName;
	}
	else {
		fab = &((IoLines[Index].OutFabs)[DecimalStreamNumber]);
		FileName = IoLines[Index].InFileParams[DecimalStreamNumber].OrigFileName;
	}

	if(((status = sys$close(fab)) & 0x1) == 0)
		logger(1, "VMS: Can't close file, status=%d\n", status);


/* Add ;0 if it is not there already */
	if(strchr(FileName, ';') == NULL) strcat(FileName, ";0");
	DelFab = cc$rms_fab;
	DelFab.fab$l_fna = FileName;
	DelFab.fab$b_fns = strlen(FileName);
	if(((status = sys$erase(&DelFab)) & 0x1) == 0) {
		logger(1, "VMS: Can't delete file '%s', status=%d\n",
			FileName, status);
	}
}

/*
 | Close a file given its index into the Lines database.
 | Return its size as a result.
 */
close_file(Index, direction, DecimalStreamNumber)
int	Index, direction, DecimalStreamNumber;
{
	struct	FAB	*fab;
	char	*FileName;
	long	status, size;

	if(direction == F_INPUT_FILE) {
		fab = &((IoLines[Index].InFabs)[DecimalStreamNumber]);
		size = 0;	/* Don't try to get it, since we don't need it */
		FileName;
	} else {
		fab = &((IoLines[Index].OutFabs)[DecimalStreamNumber]);
		size = -1;	/* Signal to get it later */
		FileName = IoLines[Index].InFileParams[DecimalStreamNumber].OrigFileName;
	}

	if(((status = sys$close(fab)) & 0x1) == 0)
		logger(1, "VMS: Can't close file, status=%d\n", status);

	if(size != 0)
		size = get_file_size(FileName);
	return size;
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
	struct	FAB	OrigFab, FinalFab;
	struct	FILE_PARAMS	*FileParams;
	char	InputFile[LINESIZE], ToNode[SHORTLINE], *p;
	static char	line[LINESIZE];	/* Will return here the new file name */
	long	status;
	static	int	FileCounter = 0;	/* Just for the name... */

	OrigFab = FinalFab = cc$rms_fab;
	if(direction == F_OUTPUT_FILE) {	/* The file just received */
		FileParams = &((IoLines[Index].InFileParams)[DecimalStreamNumber]);
		sprintf(InputFile, "%s%s%d_%d.%s", BITNET_QUEUE, TEMP_R_FILE,
			Index, DecimalStreamNumber, TEMP_R_EXT);
	}
	else {	/* The sending file - change the flag to ABORT */
		FileParams = &((IoLines[Index].OutFileParams)[DecimalStreamNumber]);
		flag = RN_HOLD_ABORT;
		sprintf(InputFile, "%s", FileParams->OrigFileName);
	}

	OrigFab.fab$l_fna = InputFile;
	OrigFab.fab$b_fns = strlen(InputFile);

/* File is of format ASCII or EBCDIC ? */
	if((p = (char *)strrchr(FileParams->To, '@')) != NULL)
		strcpy(ToNode, ++p);
	else	*ToNode = '\0';
	if(FileParams->format == ASCII)
		sprintf(line, "%sASC_%s_%05d", BITNET_QUEUE, ToNode, FileCounter++);
	else
		sprintf(line, "%sEBC_%s_%05d", BITNET_QUEUE, ToNode, FileCounter++);
	FileCounter %= 99999;	/* Make it modulo 100000 */
/* What line will it go to ??? */
	strcat(line, ".");
	if(flag == RN_NORMAL)
		strcat(line, FileParams->line);
	else
	if(flag == RN_HOLD)
		strcat(line, "HOLD");
	else
		strcat(line, "HOLD$ABORT");

	FinalFab.fab$l_fna = line;
	FinalFab.fab$b_fns = strlen(line);
	if(((status = sys$rename(&OrigFab, (int)(0), (int)(0), &FinalFab)) &
	   0x1) == 0)
		logger(1, "VMS: Can't rename '%s' to '%s'. status=d^%d\n",
			InputFile, line, status);
	return line;	/* Return the new file name */
}
