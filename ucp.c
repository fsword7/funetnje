/* UCP.C (formerly CP.C)     V1.9
 | Copyright (c) 1988,1989,1990 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use or misuse of this software.
 |
 | Control program for the RSCS daemon. It assigns a channel to its mailbox
 | and send a command message to it.
 |
 | V1.1 - Add FORCE LINE command.
 | V1.2 - Merge the UNIX and VMS versions to one source file.
 | V1.3 - Add the DEBUG RESCAN and DEBUG DUMP commands.
 | V1.4 - Add LOGLEVEL command.
 | V1.5 - Replace SWAP_xxx with htons().
 | V1.6 - 7/3/90 - Add ROUTE command to change routes on the fly.
 | V1.7 - 22/3/90 - Add GONE and UNGONE commands.
 | V1.8 - 4/4/90 - UCP can now be called with the command line as arguments.
 | V1.9 - 31/5/90 - Replace CMD_GONE_DEL/ADD with CMD_GONE_ADD/DEL_UCP. See
 |        1.7.4 release notes for explanation.
 */
#include <stdio.h>
#ifdef VMS
#include <iodef.h>
#endif
#define MAIN
#include "consts.h"
#ifdef UNIX
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

unsigned long	LOCAL_ADDRESS;
#endif

#define	Exit	1
#define	Show	2
#define	Shut	3
#define	Start	4
#define	Help	5
#define	Queue	6
#define	Stop	7
#define	Force	8
#define	Debug	9
#define	Loglevel 10
#define	Route	11
#define	Gone	12
#define	Ungone	13

struct Commands {
	char	*command;
	int	code;
	} commands[] = { "EXIT", Exit, "SHOW", Show, "SHUT", Shut,
		"START", Start, "HELP", Help, "QUEUE", Queue,
		"STOP", Stop, "FORCE", Force, "DEBUG", Debug,
		"LOGLEVEL", Loglevel, "ROUTE", Route,
		"GONE", Gone, "UNGONE", Ungone,
		"\0", 0};

char	*strchr();


main(cc, vv)
int	cc;
char	**vv;
{
	char	line[LINESIZE];
	int	i, NumParams, cmd;
#ifdef UNIX
	struct	hostent	*HostEntry;

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
#endif
	if(cc == 1) {
		while(!feof(stdin) && !(ferror(stdin))) {
			printf("NJE-CP> ");
			if(fgets(line, sizeof line, stdin) == NULL)
				exit(0);
			process_cmd(line);
		}
	} else {
		*line = '\0';
		for(i = 1; i < cc; i++) {
			strcat(line, vv[i]); strcat(line, " ");
		}
		process_cmd(line);
	}
}


process_cmd(line)
char	*line;
{
	char	*p, KeyWord[80], param1[80], param2[80], param3[80];
	int	i, NumParams, cmd;

	if((p = strchr(line, '\n')) != NULL) *p = '\0';
	if(*line == '\0') return;	/* Null command */
	NumParams = sscanf(line, "%s %s %s %s", KeyWord, param1,
		param2, param3);
	switch((cmd = parse_command(KeyWord))) {
	case Help:	show_help(); break;
#ifdef UNIX
	case Exit:	exit(0);	/* Exit program successfully */
#else
	case Exit:	exit(1);	/* Exit program */
#endif
	case Show:	if(NumParams != 2) {
				printf("Illegal SHOW command\n");
				break;
			}
			if(compare_p(param1, "LINE", 4) == 0)
				command(CMD_SHOW_LINES);
			else
			if(compare_p(param1, "QUEUE", 5) == 0)
				command(CMD_SHOW_QUEUE);
			else
				printf("Illegal SHOW command\n");
			break;
	case Start:	if(NumParams != 3) {
				printf("Illegal START command\n");
				break;
			}
			sscanf(param2, "%d", &i);
			if(compare_p(param1, "LINE", 4) == 0)
				command(CMD_START_LINE, i);
			else
				printf("Illegal START command\n");
			break;
	case Force:
	case Stop:	if(NumParams != 3) {
				printf("Illegal STOP/FORCE command\n");
				break;
			}
			if(compare_p(param1, "LINE", 4) != 0) {
				printf("Illegal STOP command\n");
				break;
			}
			if(sscanf(param2, "%d", &i) != 1) {
				printf("Illegal line number\n");
				break;
			}
			if(cmd == Force)
				command(CMD_FORCE_LINE, i);
			else
				command(CMD_STOP_LINE, i);
			break;
	case Gone:
	case Ungone:
			if(cmd == Gone) {
				if(NumParams != 3) {
					printf("Illegal GONE command\n");
					break;
				}
				sprintf(line, "%s %s", param1, param2);
				command(CMD_GONE_ADD_UCP, 0, line);
			} else {
				if(NumParams != 2) {
					printf("Illegal GONE command\n");
					break;
				}
				command(CMD_GONE_DEL_UCP, 0, param1);
			}
			break;
	case Queue:	if(NumParams < 2) {
				printf("No filename given\n");
				break;
			}
			if((NumParams == 4) &&
			   (compare_p(param2, "SIZE", 4) == 0))
				sscanf(param3, "%d", &i);
			else
				i = 0;
			command(CMD_QUEUE_FILE, 0, param1, i);
			break;
	case Shut:	if(NumParams == 1)	/* Shutdown normally */
				command(CMD_SHUTDOWN);
			else
			if(compare_p(param1, "ABORT", 5) == 0)
				command(CMD_SHUTDOWN_ABRT);
			else
				printf("Illegal SHUT command\n");
			break;
	case Debug:	if(NumParams != 2) {
				printf("DEBUG must get a parameter\n");
				break;
			}
			if(compare_p(param1, "DUMP", 4) == 0)
				command(CMD_DEBUG_DUMP);
			else
			if(compare_p(param1, "RESCAN", 6) == 0)
				command(CMD_DEBUG_RESCAN);
			else
				printf("Illegal DEBUG command\n");
			break;
	case Loglevel:	if(NumParams != 2) {
				printf("No log level given\n");
				break;
			}
			sscanf(param1, "%d", &i);
			command(CMD_LOGLEVEL, i);
			break;
	case Route:	if(NumParams != 4) {
				printf("Illegal format: Try: ROUTE xx TO yy\n");
				break;
			}
			if(compare_p(param2, "TO", 2) != 0) {
				printf("Illegal format: Try: ROUTE xx TO yy\n");
				break;
			}
			command(CMD_CHANGE_ROUTE, 0, param1, param3);
			break;
	default:	printf("Illegal command '%s'. Type HELP\n", line);
			break;
	}
}

show_help()
{
	printf("   HELP  - Show this message\n");
	printf("   SHOW LINE/QUEUE - Show lines or queue status\n");
	printf("   START LINE n - Start a closed line\n");
	printf(" * START STREAM n LINE m - Start specific stream in active line\n");
	printf("   SHUT [ABORT] - Shutdown or abort the whole program\n");
	printf("   STOP LINE - Stop a line\n");
	printf("   FORCE LINE - Stop a line immediately\n");
	printf(" * STOP STREAM n LINE m - Stop a single stream in active line\n");
	printf(" * FORCE STREAM n LINE m - Stop immediately\n");
	printf("   QUEUE file-name [SIZE size] - To queue a file to send\n");
	printf("   DEBUG DUMP - Dump all lines buffers to logfile\n");
	printf("   DEBUG RESCAN - Rescan queue and reque files.\n");
	printf("   LOGLEVEL n - Set the loglevel to N\n");
	printf("   ROUTE xxx TO yyy - Chnage the routing table.\n");
	printf("   GONE username LoginDirectory - Add username to gone list\n");
	printf("   UNGONE username - Remove a user from the Gone list.\n");
	printf("* - Not yet implemented\n");
}


/*
 | Send the command. The command is in CMD, a numeric value (if needed) is in
 | VALUE, and a string value (if needed) is in STRING.
 */
command(cmd, value, string, param)
int	cmd, value, param;
char	*string;
{
#ifdef VMS
	struct	DESC	MailBoxDesc;
#endif
	char	line[LINESIZE];
	long	i, size, status;
	short	chan, iosb[4];
#ifdef UNIX
	int	Socket;
	struct	sockaddr_in	SocketName;
#endif

	*line = cmd;
	switch(cmd) {
	case CMD_QUEUE_FILE :
			/* File size: */
		line[1] = (unsigned char)((param & 0xff00) >> 8);
		line[2] = (unsigned char)(param & 0xff);
		strcpy(&line[3], string);
		size = strlen(&line[3]) + 3;
		break;
	case CMD_CHANGE_ROUTE:
		sprintf(&line[1], "%s %s", string, param);
		size = strlen(&line[1]) + 1;
		break;
	case CMD_START_LINE:
	case CMD_STOP_LINE:
	case CMD_FORCE_LINE:
	case CMD_LOGLEVEL:
		line[1] = (value & 0xff); line[2] = '\0';
		size = 2;
		break;
	case CMD_GONE_ADD_UCP:
	case CMD_GONE_DEL_UCP:
		strcpy(&line[1], string);
		size = strlen(string) + 1;
		break;
	default:
		cuserid(&line[1]);	/* Add the username of sender */
		size = strlen(&line[1]) + 1;	/* 1 for the command code */
	}

#ifdef VMS
	MailBoxDesc.address = MAILBOX_NAME;
	MailBoxDesc.length = strlen(MAILBOX_NAME);
	MailBoxDesc.type = 0;
	status = sys$assign(&MailBoxDesc, &chan, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		printf("Can't connect to command mailbox, status=%d\n", status);
		return;
	}

	status = sys$qiow((long)(0), chan,
		(short)(IO$_WRITEVBLK), iosb,
		(long)(0), (long)(0),
		line, size, (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		printf("CP, Can't write mailbox, status=%d, iosb=%d\n",
			status, iosb[0]);
	}
	sys$dassgn(chan);
#endif
#ifdef UNIX
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
	if(sendto(Socket, line, size, 0, &SocketName, sizeof(SocketName)) == -1) {
		perror("Can't send command");
	}
	close(Socket);
#endif
}



parse_command(line)
char	*line;
{
	int	i;

	for(i = 0; ; i++) {		/* Look for the command */
		if(commands[i].code == 0)	/* End of valid commands */
			return 0;	/* Signal error */
		if(compare_p(commands[i].command, line,
		   strlen(line)) == 0)
			return commands[i].code;
	}
}


/*
 | Case insensitive comparison of size bytes.
 */
#define	TO_UPPER(c)	(((c >= 'a') && (c <= 'z')) ? (c - ' ') : c)

compare_p(a, b, size)
char	*a, *b;
{
	char	*p, *q;

	p = a; q = b;

	for(; TO_UPPER(*p) == TO_UPPER(*q); p++,q++)
		if((*p == '\0') || (*q == '\0')) break;

	if((p - a) >= size)
		return 0;	/* The SIZE part is equal */

/* Not equal */
	return(TO_UPPER(*p) - TO_UPPER(*q));
}
