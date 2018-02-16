/*   BMAIL.C	V1.2
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
 | Calling sequence:  BMAIL from to LinkName
 | or, for BSMTP: BMAIL from to LinkName -b GateWay OurName
 |
 | V1.1 - 22/3/90 - Upcase From, To and LinkName as lower case might cause
 |        problems to IBMs.
 | V1.2 - 4/4/90 - Correct the Usage note.
 */

#include	<stdio.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#include	<netdb.h>

#define	MAIN	-1
#include "consts.h"

#define	LSIZE	256			/* Maximum line length */
#define	LOCAL_ADDRESS	0x7f000001

main(cc, vv)
int	cc;
char	**vv;
{
	char	From[LSIZE], To[LSIZE], LinkName[LSIZE], FileName[LSIZE],
		GateWay[LSIZE];
	char	line[1000], *p, *q;
	FILE	*fd;
	int	Bsmtp, FileSize;

	Bsmtp = 0;
	if(cc < 4) {
		fprintf(stderr, "Usage: BMAIL FromUser@FromNode ToUser@ToNode link-name [-b Gateway-address OurBitnetName\n");
		fprintf(stderr, "  File is taken from standard input\n");
		exit(1);
	}

/* Read our name and the BITnet queue */
	read_configuration();

	strcpy(From, *++vv);
	strcpy(To, *++vv);
	strcpy(LinkName, *++vv);
	if(cc == 6) {
		if(strcmp(*++vv, "-b") == 0) {
			Bsmtp = 1;
			strcpy(GateWay, *++vv);
		}
	}

/* Upcase linkname, From and To to prevent from problems with RSCS */
	for(p = LinkName; *p != NULL; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';
	for(p = From; *p != NULL; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';
	for(p = To; *p != NULL; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';

	sprintf(FileName, "%s/ASC_XXXXXX", BITNET_QUEUE);
	mktemp(FileName);
	if(*FileName == '\0') {
		fprintf(stderr, "Can't create unique filename\n");
		exit(1);
	}

/* MKTEMP can't get extension, so we have to add it here... */
	strcat(FileName, "."); strcat(FileName, LinkName);

	if((fd = fopen(FileName, "w")) == NULL) {
		perror(FileName); exit(1);
	}

/* The RSCS envelope for our NJE emulator */
	if(strchr(From, '.') != NULL)	/* Not a BITnet name */
		fprintf(fd, "FRM: MAILER@%s\n",  LOCAL_NAME);
	else
		fprintf(fd, "FRM: %s\n", From);
	fprintf(fd, "TYP: MAIL\nEXT: MAIL\n");
	fprintf(fd, "FNM: %s\n", cuserid(0));	/* The username that sends it */
	fprintf(fd, "FMT: ASCII\n");
	if(Bsmtp == 0)
		fprintf(fd, "TOA: %s\n", To);
	else
		fprintf(fd, "TOA: %s\n", GateWay);
	fprintf(fd, "END:\n");

/* If this is a BSMTP message, add the BSMTP header: */
	if(Bsmtp != 0) {
		fprintf(fd, "HELO %s.BITNET\n", LOCAL_NAME);
		fprintf(fd, "TICK 1\n");
		fprintf(fd, "MAIL FROM:<%s>\n", From);
		fprintf(fd, "RCPT TO:<%s>\n", To);
		fprintf(fd, "DATA\n");
	}

/* The message itself - copy it as-is */
	while(fgets(line, sizeof line, stdin) != NULL) {
/* If it is BSMTP duplicate dots at the beginning of a line */
		if((Bsmtp != 0) && (*line == '.')) {
			p = &line[strlen(line)]; q = p++;
			while(p > line) *p-- = *q--;
		}
		fprintf(fd, "%s", line);
	}

/* If BSMTP - add the .QUIT: */
	if(Bsmtp != 0)
		fprintf(fd, ".\nQUIT\n");

	FileSize = ftell(fd);	/* Get its size in bytes */
	fclose(fd);

/* Queue it to the NJE emulator */
	queue_file(FileName, FileSize);
	exit(0);
}


read_configuration()
{
	FILE	*fd;
	char	line[LINESIZE], KeyWord[80], param1[80],
		param2[80], param3[80];

	if((fd = fopen(CONFIG_FILE, "r")) == NULL) {
		printf("Can't open configuration file named '%s'\n",
			CONFIG_FILE);
		return 0;
	}

	while(fgets(line, sizeof line, fd) != NULL) {
		if(*line == '*') continue;	/* Comment */
		if(*line == '#') continue;	/* Comment */

		sscanf(line, "%s %s %s %s", KeyWord,
			param1, param2, param3);

		if(compare(KeyWord, "QUEUE") == 0) {
			strcpy(BITNET_QUEUE, param1);
		}
		if(compare(KeyWord, "NAME") == 0) {
			strcpy(LOCAL_NAME, param1);
		}
	}

	fclose(fd);
	return 1;	/* All ok */
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

queue_file(FileName, FileSize)
char	*FileName;
int	FileSize;
{
	char	line[LINESIZE];
	long	i, size, status;
	int	Socket;
	struct	sockaddr_in	SocketName;

	*line = CMD_QUEUE_FILE;
/* Send file size */
	line[1] = (unsigned char)((FileSize & 0xff00) >> 8);
	line[2] = (unsigned char)(FileSize & 0xff);
	strcpy(&line[3], FileName);
	size = strlen(&line[3]) + 3;

/* Create a local socket */
	if((Socket = socket( AF_INET, SOCK_DGRAM, 0)) == -1) {
		perror("Can't create command socket");
		return;
	}

/* Create the connect request. We connect to our local machine */
	SocketName.sin_family = (AF_INET);
	SocketName.sin_port = htons(COMMAND_MAILBOX);
	SocketName.sin_addr.s_addr = htonl(LOCAL_ADDRESS);
	/* [mea] .s_addr above was  S_un.S_addr, which is obsolete form */
	for(i = 0; i < 8; i++)
		(SocketName.sin_zero)[i] = 0;
	if(sendto(Socket, line, size, 0, &SocketName, sizeof(SocketName)) == -1) {
		perror("Can't send command");
	}
	close(Socket);
}
