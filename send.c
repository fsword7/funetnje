/* SEND.C	V1.3
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
 | Send a command or message to the NJE emulator to send it over to the
 | correct line.
 | The call to this module is either by SEND/COMMAND or SEND/MESSAGE. If the
 | user gives the message's text, then we send it and exit. If not, we
 | inquire it interactively.
 | Since the command given is parsed by the shell as arguments, we have to
 | collect them back as one text line.
 | One line usage: send /command node-name command
 |            or:  send /message User@Node message...
 | Please note the space between SEND and the qualifier.
 |
 | V1.1 - Do not append any more the local site name to the sender userID.
 |        this is done from now by UNIX.C
 | V1.2 - 7/3/90 - Scan the user's message text and remove all controls. This
 |        will disbale users ability to play with screen controls on others
 |        terminals.
 | V1.3 - 26/3/90 - Add -m, -message, -c, -command as valid flags.
 */
#include "consts.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

unsigned long	LOCAL_ADDRESS;


main(cc, vv)
char	**vv;
int	cc;
{
	struct	hostent	*HostEntry;
	char	text[LINESIZE], address[LINESIZE], *p;
	int	type,	/* Message or command */
		mode;		/* Interactive or not */

	if((HostEntry = gethostbyname("localhost")) == NULL) {
		perror("GetHostByName");
		exit(0);
	}

	if(HostEntry->h_length != 4) {	/* Illegal */
		printf("Illegal address length=%d\n", HostEntry->h_length);
		exit(0);
	}

	LOCAL_ADDRESS =
		((HostEntry->h_addr)[0] << 24) +
		((HostEntry->h_addr)[1] << 16) +
		((HostEntry->h_addr)[2] << 8) +
		((HostEntry->h_addr)[3]);

/* Get the command line and command parameters.*/
	if(cc < 2) {	/* No /Mode - tell user and exit */
		printf("Usage: SEND /COMMAND or SEND /MESSAGE\n");
		exit(0);
	}

/* Get the switch */
	cc -= 2;	/* 1 = Prog name. 2 = this qualifier */
	*vv++;		/* Point to the qualifier */
	if((strcmp(*vv, "/message") == 0) ||
	   (strcmp(*vv, "-message") == 0) ||
	   (strcmp(*vv, "-m") == 0))
		type = CMD_MSG;
	else
	if((strcmp(*vv, "/command") == 0) ||
	   (strcmp(*vv, "-command") == 0) ||
	   (strcmp(*vv, "-c") == 0))
		type = CMD_CMD;
	else {
		printf("Valid qualifiers are /COMMAND or /MESSAGE only\n");
		exit(0);
	}

/* Get the address (if exists) */
	if(cc < 1) {	/* No parameters - prompt for address and enter interactive mode */
		printf("_Address: "); fgets(address, sizeof address, stdin);
		if((p = (char*)strchr(address, '\n')) != NULL) *p = '\0';
		mode = 1;	/* Interactive mode */

	}
	else {		/* Get the available parameters */
		strcpy(address, *++vv);	/* We have at least the address there */
		if(cc == 1) {	/* Nothing more than it */
			mode = 1;	/* Interactive mode */
		}
		else {	/* Reconstruct the parameters as the text */
			*text = '\0';
			while(--cc > 0) {
				strcat(text, *++vv); strcat(text, " ");
			}
			text[strlen(text) - 1] = '\0';	/* Remove the last blank */
			mode = 0;
		}
	}

	if(mode == 0) {		/* Batch mode */
		send_nje(type, address, text);	/* Send it to the daemon */
		exit(0);
	}

/* We have to read it interactively. Loop untill blank line */
	printf("Hit your message/command. End with empty line\n");
	for(;;) {
		printf("%s: ", address);
		if(fgets(text, sizeof text, stdin) == NULL)
			break;	/* Null line */
		if((p = (char*)strchr(text, '\n')) != NULL) *p = '\0';
		if(*text == '\0') break;	/* Another sign of empty line */
		send_nje(type, address, text);
	}
	exit(0);
}


send_nje(type, address, text)
int	type;
char	*address, *text;
{
	char	line[LINESIZE], from[LINESIZE], *p;
	struct	sockaddr_in	SocketName;
	int	Socket, i;
	int	size;

/* Remove all controls */
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

	if(type == CMD_CMD)	/* Add the @ to address */
		sprintf(&line[2], "%s @%s ", from, address);
	else			/* Address already has @ */
		sprintf(&line[2], "%s %s ", from, address);

/* Upper-case the addresses. If this is a command - uppercase it also */
	for(p = &line[2]; *p != '\0'; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';
	if(type == CMD_CMD)
		for(p = text; *p != '\0'; *p++)
			if((*p >= 'a') && (*p <= 'z')) *p -= ' ';

	line[1] = strlen(&line[2]) + 2;	/* Where the text begins */
	strcat(&line[2], text);

	size = strlen(&line[1]) + 1;	/* 1 for the command code */

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
}
