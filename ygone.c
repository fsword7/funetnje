/* YGONE.C	V1.3
 | It has two parts which are different in functionality:
 | YGONE/CONTINUE: Will trap all messages sent to the terminal into a file. This
 |    a general mechanism and the NJE emulator is not concerned. The messages are
 |    appended to SYS$LOGIN:YGONE_MESSAGES.TXT, and the terminal is left logged
 |    in but unuseable for any other thing, untill this program is aborted.
 |    When aborting the terminal is set to Broadcast, disregarding its previous
 |    setup.
 | YGONE - Inform the NJE emulator that you want to be added to the Gone list.
 | YGONE/DISABLE - Remove yourself from the gone list.
 |
 | V1.1 - 25/3/90 - Add support for Unix.
 | V1.2 - 28/3/90 - Acount for long lines, thus not overflowing one screen when long
 |        lines are displayed.
 | V1.3 - 15/10/90 - When called with /DISABLE call Mail_check() to inform
 |        about new mails.
 */
#include <stdio.h>
#ifdef VMS
#include <msgdef.h>	/* For the mailbox messages definition */
#include <lnmdef.h>
#include <iodef.h>	/* For setting the terminal */
#include <ttdef.h>
#include <tt2def.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pwd.h>
#endif
#include <signal.h>	/* For the ^C abort function */
#include <time.h>	/* To get the time */
#define	MAIN -1
#include "consts.h"

/* Hold the terminal characteristics in order to change and retore them: */
struct	terminal_char {	/* Terminal characteristics */
		unsigned char	class, type;
		unsigned short	PageWidth;
		long	BasicChars;
		long	ExtChars;
	} TerChar;
short	TerminalMailboxChan, TerminalChan;
int	MessagesCount = 0;	/* So we can inform user how many new messages */

/* The file descriptor to write messages on */
FILE	*MessageFd;

/*
 | If there are no parameters - inform NJE and logout. If there is a parameter
 | named /CONTINUE - only trap terminal messages and do not logout; this is
 | general and have no special connection to the NJE emulator.
 */
main(cc, vv)
char	**vv;
{
	if(cc == 1) {	/* No parameters */
		add_gone();	/* Inform emulator about it. */
		printf("Your messages will be recorded while you are away.\n");
		logout_process();	/* Send him home */
		exit(1);
	}
	if(strncmp(vv[1], "/disa", 5) == 0) {	/* YGONE/DISABLE */
		mail_check();	/* Inform about new mails */
		remove_gone();
		display_file();
		exit(1);
	}
#ifdef VMS
	if(strncmp(vv[1], "/cont", 5) == 0) {	/* YGONE/CONTINUE */
		trap_terminal_messages();
		exit(1);
	}
#endif
	printf("Illegal option. Aborting\n");
}


/*********************** NJE-GONE section *****************************/
/*
 | Get the user's login directory, create a command line which contains it and
 | the username and starts with the command code CMD_GONE_ADD, and then send
 | it to the NJE emulator.
 */
add_gone()
{
#ifdef VMS
	struct DESC	logname, tabname;	/* Descriptor to pass the names */
	struct {
		short	length;
		short	code;
		long	address;
		long	rtn;
	} list[2];		/* Item list for $TRNLNM */
	char	JOB_TABLE[] = "LNM$JOB";	/* The logical names table. */
	char	LOGNAME[] = "SYS$LOGIN";	/* We look for the home directory */
	static int	NameLength;
	char	LoginDirectory[128];
#else
	char	*LoginDirectory;
#endif
	char	UserName[128], CommandLine[128];
	struct	passwd	*UserEntry, *getpwnam();
	int	status;

#ifdef VMS
/* Get the logical name translation of SYS$LOGIN */
	tabname.address = JOB_TABLE; tabname.length = strlen(JOB_TABLE);
	tabname.type = 0;
	logname.address = LOGNAME; logname.length = strlen(LOGNAME);
	logname.type = 0;

	list[0].length = sizeof LoginDirectory;
	list[0].code = LNM$_STRING;	/* Get the equivalence string */
	list[0].address = LoginDirectory;
	list[0].rtn = &NameLength;	/* The length of returned string */
	list[1].length = list[1].code = list[1].address = 0;

	if(((status = sys$trnlnm(0,&tabname, &logname,0,list)) & 0x1) == 0) {
		printf("Can't get translation of SYS$LOGIN\n");
		exit(status);
	}

	LoginDirectory[NameLength] = '\0';
#else
	cuserid(UserName);
	if((UserEntry = getpwnam(UserName)) == NULL) {
		perror("Getpwname"); exit(1);
	}
	LoginDirectory = UserEntry->pw_dir;
#endif

	*CommandLine = CMD_GONE_ADD;
	cuserid(&CommandLine[1]);	/* The username */
	strcat(CommandLine, " ");	/* A space to separate them */
	strcat(CommandLine, LoginDirectory);	/* And the login directory */
#ifdef UNIX
	strcat(CommandLine, "/");
#endif
	send_nje(CommandLine, strlen(&CommandLine[1]) + 1);	/* And send it to emulator */
}

/*
 | Inform the NJE emulator to remove the user from the gone list.
 */
remove_gone()
{
	char	CommandLine[128];

	*CommandLine = CMD_GONE_DEL;
	cuserid(&CommandLine[1]);	/* The username */
	send_nje(CommandLine, strlen(&CommandLine[1]) + 1);	/* And send it to emulator */
}


/*
 | Send the given line to the NJE emulator.
 */
send_nje(CommandLine, size)
char	*CommandLine;
{
#ifdef VMS
	struct	DESC	MailBoxDesc;
	int	status;
	short	chan, iosb[4];
#else
	int	i, LOCAL_ADDRESS, Socket;
	struct	sockaddr_in	SocketName;
	struct	hostent	*HostEntry;
#endif

#ifdef VMS
	MailBoxDesc.address = MAILBOX_NAME;
	MailBoxDesc.length = strlen(MAILBOX_NAME);
	MailBoxDesc.type = 0;
	status = sys$assign(&MailBoxDesc, &chan, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
/*		printf("Can't connect to command mailbox, status=%d\n", status); */
		return;
	}

	status = sys$qiow((long)(0), chan,
		(short)(IO$_WRITEVBLK), iosb,
		(long)(0), (long)(0),
		CommandLine, size, (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
/*		printf("CP, Can't write mailbox, status=%d, iosb=%d\n",
			status, iosb[0]); */
	}
	sys$dassgn(chan);
#else
	if((HostEntry = gethostbyname("localhost")) == NULL) {
		perror("GetHostByName");
		exit(1);
	}

	if(HostEntry->h_length != 4) {	/* Illegal */
		printf("Illegal address length=%d\n", HostEntry->h_length);
		exit(1);
	}

	LOCAL_ADDRESS =
		((HostEntry->h_addr)[0] << 24) +
		((HostEntry->h_addr)[1] << 16) +
		((HostEntry->h_addr)[2] << 8) +
		((HostEntry->h_addr)[3]);

/* Create a local socket */
	if((Socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Can't create command socket");
		return;
	}

/* Create the connect request. We connect to our local machine */
	SocketName.sin_family = (AF_INET);
	SocketName.sin_port = htons(COMMAND_MAILBOX);
	SocketName.sin_addr.s_addr = htonl(LOCAL_ADDRESS);
	for(i = 0; i < 8; i++)
		(SocketName.sin_zero)[i] = 0;
	if(sendto(Socket, CommandLine, size, 0, &SocketName, sizeof(SocketName)) == -1) {
		perror("Can't send command");
	}
	close(Socket);
#endif
}

#ifdef VMS
/*
 | Logout the process as it wants to receive messages while not logged-in.
 */
logout_process()
{
	struct	DESC	CommandDesc;
	char	Command[] = "LOGOUT";

	CommandDesc.address = Command;
	CommandDesc.length = strlen(Command);
	CommandDesc.type = 0;

	lib$do_command(&CommandDesc);
}
#else
logout_process()
{
	printf("Logout now!!!!\n");
}
#endif

#ifdef VMS
/*************************** Terminal pseudo-gone section ***************/
/*
 | Open the message's file, initialize the mailbox and associate it with the
 | terminal. Then set the terminal to NOBROADCAST and enable mailbox trapping
 | for broadcasts. Enable ^C trap, fire the AST on mailbox input, and then go
 | to sleep forever (or untill ^C...).
 */
trap_terminal_messages()
{
	int	status, mailbox_ast(), ctrlcabort();
	char	*p, line[128], MailBoxName[128];
	struct	DESC	mail_box, Device;

	printf("yGONE, v1.0;  The Hebrew University of Jerusalem.\n");
/* Open the file to log messages in */
	if((MessageFd = fopen("SYS$LOGIN:YGONE_MESSAGES.TXT", "a")) == NULL) {
		perror("Can't open message's file\n");
		exit(1);
	}

/* Create a unique mailbox name for each terminal */
	cuserid(line);	/* Get the username */
	sprintf(MailBoxName, "TERM_%s", line);

	mail_box.length = strlen(MailBoxName);
	mail_box.type = 0;
	mail_box.address = MailBoxName;

/* Create a temporary mailbox. */
	if(((status = sys$crembx((char)(0),	/* temporary mailbox */
			&TerminalMailboxChan,
			(long)(0), (long)(0), (long)(0), (long)(3),
			&mail_box)) & 0x1) == 0) {
		printf("Can't create mailbox for terminal line\n");
		exit(status);
	}

/* Assign a channel to terminal, and associate the mailbox with it. */
	Device.address = "TT:"; Device.length = 3;
	Device.type = 0;

	if(((status = sys$assign(&Device, &TerminalChan,
			(long)(3), &mail_box)) & 0x1) == 0) {
		printf("Can't assign channel to '%s'\n", Device.address);
		sys$dassgn(TerminalMailboxChan);
		exit(status);
	}

/* Set up the terminal to be NOBROADCAST and BROADCAST-MAILBOX */
/* Read the old ones: */
	status = sys$qiow((long)(0), TerminalChan, (short)(IO$_SENSEMODE),
		(long)(0), (long)(0), (long)(0),
		&TerChar, (int)(sizeof TerChar),
		(int)(0), (int)(0), (int)(0), (int)(0));
	if((status & 0x1) == 0) {
		printf("Can't read terminal setup\n");
		sys$dassgn(TerminalMailboxChan);
		sys$dassgn(TerminalChan);
		exit(status);
	}

/* Setup the needed ones: */
	TerChar.BasicChars |= TT$M_NOBRDCST;
	TerChar.ExtChars |= TT2$M_BRDCSTMBX;
	status = sys$qiow((long)(0), TerminalChan, (short)(IO$_SETMODE),
		(long)(0), (long)(0), (long)(0),
		&TerChar, (int)(sizeof TerChar),
		(int)(0), (int)(0), (int)(0), (int)(0));
	if((status & 0x1) == 0) {
		printf("Can't set terminal to NOBROADCAST\n");
		sys$dassgn(TerminalMailboxChan);
		sys$dassgn(TerminalChan);
		exit(status);
	}

/* Setup a ^C trap */
	signal(SIGINT, ctrlcabort);

/* Queue the AST for mailbox messages, and then go to sleep untill ^C */
	queue_mailbox_ast(TerminalMailboxChan);

	printf("Trapping all messages to attached terminal. Hit ^C to abort.\n\n");
	sys$hiber();
}


/*
 | Queue a write-attention AST for the mailbox that is associated with the
 | terminal.
 */
int
queue_mailbox_ast(TerminalMailboxChan)
int	TerminalMailboxChan;
{
	long	status, mailbox_ast();

/* Enable/Re-enable AST delivery for the mailbox */
	status = sys$qiow((long)(0),
			TerminalMailboxChan,
			(short)(IO$_SETMODE|IO$M_WRTATTN),
			(long)(0), (long)(0), (long)(0),
			mailbox_ast, TerminalMailboxChan,
			(long)(3),	/* Access mode */
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if((status & 0x1) == 0) {
		printf("Can't declare AST for terminal mailbox\n");
		exit(status);
	}
}


/*
 | There is something in the mailbox. REad it, and dispatch according to the
 | message code (the first word in the mailbox's contents).
 */
mailbox_ast(TerminalMailboxChan)
{
	register long	i, status, size;
	unsigned char	buffer[256];	/* For reading mailbox */

/* Read the message from mailbox. */
	status = sys$qiow((int)(0), TerminalMailboxChan,
			(short)(IO$_READVBLK),
			(long)(0), (long)(0), (long)(0),
			buffer, (int)(sizeof buffer),
			(long)(0), (long)(0), (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		printf("Can't read mailbox for broadcast message\n");
		exit(status);
	}

	status = (buffer[1] << 8) + buffer[0];	/* Message type */
	switch(status) {
	case MSG$_TRMUNSOLIC:	/* There is input on the terminal line */
			printf("*** Only ^C to abort!\n"); break;
	case MSG$_TRMHANGUP:	/* Terminal hangup - Drain line */
			printf("Terminal hangup. exiting\n");
			exit(1);
	case MSG$_TRMBRDCST: process_broadcast(buffer);
			break;
	default:	printf("Unrecognised mailbox type d^%d\n", status);
			break;
	}

/* Queue the AST again */
	queue_mailbox_ast(TerminalMailboxChan);
}

/*
 | Process the mailbox contents. According to the I/O book, terminal's chapter,
 | the real message is a counted string whose length is at offset 20+21 (one
 | word) and the string comes after it.
 */
process_broadcast(buffer)
unsigned char	*buffer;
{
	unsigned char	*p, *message;
	char	*local_time();
	int	size;

/* Jump over control section and sender's unit: */
	message = &buffer[20]; p = message++;
	size = (*p & 0xff) | ((*message++ & 0xff) << 8);
	message[size] = '\0';

/* Loop over the message and remove all control characters from it */
	for(p = message; *p != '\0'; *p++)
		if(*p < ' ') *p = ' ';

	if(fprintf(MessageFd, "%s: %s\n", local_time(), message) == EOF) {
		perror("Writing to message's file\n");
		ctrlcabort();	/* Will clear everything and exit */
	}
	MessagesCount++;
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
 | ^C was hitted. Set the terminal back to Broadcast, disable mailbox broadcast,
 | close all channels and files, and exit...
 */
ctrlcabort()
{
	int	status;

	printf("%d new messages received. Exiting...\n", MessagesCount);
/* Set up the terminal to be back /BROADCAST and remove the mailbox broadcast */
/* Clear the needed ones: */
	TerChar.BasicChars &= ~TT$M_NOBRDCST;
	TerChar.ExtChars &= ~TT2$M_BRDCSTMBX;
	status = sys$qiow((long)(0), TerminalChan, (short)(IO$_SETMODE),
		(long)(0), (long)(0), (long)(0),
		&TerChar, (int)(sizeof TerChar),
		(int)(0), (int)(0), (int)(0), (int)(0));
	if((status & 0x1) == 0)
		printf("Can't reset terminal to BROADCAST\n");

	sys$dassgn(TerminalMailboxChan);
	sys$dassgn(TerminalChan);
	fclose(MessageFd);
	display_file();
	exit(status);
}
#endif


/*
 | Display the file on the screen, one screen at a time. Delete it after done.
 */
display_file()
{
	int	counter;
	char	line[256];
#ifdef UNIX
	char	FileName[128], UserName[128], *LoginDirectory;
	struct	passwd	*UserEntry, *getpwnam();
#endif
	FILE	*fd;

#ifdef UNIX
	cuserid(UserName);
	if((UserEntry = getpwnam(UserName)) == NULL) {
		perror("Getpwname"); exit(1);
	}
	LoginDirectory = UserEntry->pw_dir;
	sprintf(FileName, "%s/%s", LoginDirectory, "YGONE_MESSAGES.TXT");
	if((fd = fopen(FileName, "r")) == NULL)
		return;		/* New new messages or user not subscribed */
#else
	if((fd = fopen("SYS$LOGIN:YGONE_MESSAGES.TXT", "r")) == NULL)
		return;		/* New new messages or user not subscribed */
#endif
	counter = 0;
	if(fgets(line, sizeof line, fd) == NULL) {	/* Empty file */
		printf("No new messages have arrived!\n");
		goto close_file;
	}

	printf("New messages have arrived:\n");
	printf("-------------------------\n");

	for(;;) {
		printf("%s", line);
		counter += (strlen(line) / 80);	/* Add for lines longer than 80 */
		if(counter++ > 20) {
			printf("<CR> to continue, Anything else to abort: ");
			fgets(line, sizeof line, stdin);
			if((*line != '\n') && (*line != '\0')) break;
			counter = 0;
			printf("\033[2J\033[H");	/* Clear screen */
		}
		if(fgets(line, sizeof line, fd) == NULL) break;	/* End of file */
	}

close_file:
	fclose(fd);
#ifdef VMS
	delete("SYS$LOGIN:YGONE_MESSAGES.TXT;0");
#else
	unlink(FileName);
#endif
}
