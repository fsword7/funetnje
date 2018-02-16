/* SENDCMDMSG.C	V1.2
 | Send a command or message to the NJE emulator to send it over to the
 | correct line.
 | The call to this module is either by SEND/COMMAND or SEND/MESSAGE. If the
 | user gives the message's text, then we send it and exit. If not, we
 | inquire it interactively.
 | This command is define in the DCL table by the SENDFILE command definition.
 | One line usage is: SEND/COMMAND Node-name command
 |                or: SEND/MESSAGE User@Node Message-text
 |
 | V1.1 - In the past, we've generated the sender address as User@local-node.
 |        Now the address is only the username, and the daemon adds the site
 |        name, thus we don't have to define the local site name in CONSTS.H
 | V1.2 - 7.3.90 - Scan the user's text and remove all controls, so users
 |        can't send screen controls to other terminals.
 */
#define	MAIN
#include "consts.h"
#include <stdio.h>
#include <iodef.h>

/* CLI parameters */
char	ADDRESS_PARAMETER[] = "P1",
	MESSAGE_QUALIFIER[] = "MESSAGE",
	COMMAND_QUALIFIER[] = "COMMAND";

main()
{
	struct	DESC	cli_desc, cli_answer;
	char	CommandLine[LINESIZE], address[LINESIZE], line[LINESIZE], *p;
	int	type,	/* Message or command */
		mode;		/* Interactive or not */
	short	i;		/* Must be short, because the CLI routine expects it */

/* Get the command line and command parameters from the CLI parser */
	cli_desc.type = 0;
	cli_desc.address = ADDRESS_PARAMETER;
	cli_desc.length = strlen(ADDRESS_PARAMETER);
	if((cli$present(&cli_desc) & 0x1) != 0) {
		cli_answer.type = 0;
		cli_answer.address = CommandLine;
		cli_answer.length = LINESIZE;
		cli$get_value(&cli_desc, &cli_answer, &i);
		CommandLine[i] = '\0';
	}
	cli_desc.type = 0;
	cli_desc.address = MESSAGE_QUALIFIER;
	cli_desc.length = strlen(MESSAGE_QUALIFIER);
	if((cli$present(&cli_desc) & 0x1) != 0)
		type = CMD_MSG;
	cli_desc.type = 0;
	cli_desc.address = COMMAND_QUALIFIER;
	cli_desc.length = strlen(COMMAND_QUALIFIER);
	if((cli$present(&cli_desc) & 0x1) != 0)
		type = CMD_CMD;

/* Parse the line. Get the address first */
	mode = 0;	/* "Batch" mode - send one text unit and exit. */
	if((p = strchr(CommandLine, ' ')) != NULL) {
		*p++ = '\0';
	}
	else	mode++;		/* Quiery user for text, repeatedly */

	strcpy(address, CommandLine);

	if(mode == 0) {		/* Batch mode */
		send_nje(type, address, p);	/* Send it to the daemon */
		exit(1);
	}

/* We have to read it interactively. Loop untill blank line */
	printf("Type your message/command. End with empty line\n");
	for(;;) {
		printf("%s: ", address);
		if(fgets(line, sizeof line, stdin) == NULL)
			break;	/* Null line */
		if((p = strchr(line, '\n')) != NULL) *p = '\0';
		if(*line == '\0') break;	/* Another sign of empty line */
		send_nje(type, address, line);
	}
	exit(1);
}


send_nje(type, address, text)
int	type;
char	*address, *text;
{
	char	line[LINESIZE], from[LINESIZE], *p;
	int	status, size;
	short	iosb[4], chan;
	struct	DESC	MailBoxDesc;

/* Remove all controls from the message's text */
	for(p = text; *p != NULL; *p++)
		if((*p < ' ') || (*p > '\176')) *p = ' ';

/* Create the sender's address */
	cuserid(from);

	if(type == CMD_CMD) {
/* Uppercase the message's text */
		for(p = text; *p != '\0'; p++)
			if((*p >= 'a') && (*p <= 'z')) *p -= ' ';
		*line = CMD_SEND_COMMAND;
	}
	else
		*line = CMD_SEND_MESSAGE;

	if(type == CMD_CMD)	/* Add @ before site name */
		sprintf(&line[2], "%s @%s ", from, address);
	else			/* Address already contains @ */
		sprintf(&line[2], "%s %s ", from, address);

	line[1] = strlen(&line[2]) + 2;	/* Where the text begins */
	strcat(&line[2], text);

	size = strlen(&line[1]) + 1;	/* 1 for the command code */

	MailBoxDesc.address = MAILBOX_NAME;
	MailBoxDesc.length = strlen(MAILBOX_NAME);
	MailBoxDesc.type = 0;
	status = sys$assign(&MailBoxDesc, &chan, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		printf("Control program is not running\n");
		exit(status);
	}

	status = sys$qiow((long)(0), chan,
		(short)(IO$_WRITEVBLK), iosb,
		(long)(0), (long)(0),
		line, size, (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		printf("Control program is not responding\n");
			exit(status);
	}
	sys$dassgn(chan);
}
