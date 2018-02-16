/*  UNIX_MAIL.C	V1.4
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
 | Call to SendMail via local SMTP connection and send the incoming mail to.
 | In case of errors we try to return the message to the sender. If impossible,
 | it is left in the queue waiting for the manager...
 |
 | Upon successfull or unsuccessfull delivery, we return the caller a text
 | of message to send to the sender using interactive traffic.
 |
 | V1.1 - 4/1/90 - Add BSMTP support to addresses MAILER, BSMTP, SMTP on the
 |        local machine.
 | V1.2 - 15/1/90 - We forgot that a BSMTP message might contain more than
 |        one address... Handle this situation.
 | V1.3 - 4/4/90 - Replace print of errno value with the corresponding error messagfe.
 | V1.4 - 6/5/90 - Add a return value from SMTP_write().
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include "consts.h"
#ifndef	INADDR_LOOPBACK
#define INADDR_LOOPBACK (unsigned long)0x7F000001
#endif

#define	MAX_ADDRESSES	50	/* Maximum number of To addresses in BSMTP envelope */
static long	FilePosition;	/* Save the file position after the NJE headers */
extern int errno;
extern int	sys_nerr;	/* Maximum error number recognised */
extern char	*sys_errlist[];	/* List of error messages */
#define	PRINT_ERRNO	(errno > sys_nerr ? "***" : sys_errlist[errno])
static	char	*ToAddresses[MAX_ADDRESSES];

char	*strchr(), *malloc();

#define	IPPORT_SMTP	25	/* SMTP port */

inform_mailer(OrigFileName, FileName, FileExt, From, To, Class)
char	*OrigFileName, *FileName, *FileExt, *From, *To, Class;
{
	FILE	*fd;
	int	i, BSMTPflag, status;
	char	line[1000], NewFrom[LINESIZE], *p;
	static char	ReturnMessage[LINESIZE];	/* The NMR message to send to originator */

	if((fd = fopen(OrigFileName, "r")) == NULL) {
		logger(1, "UNIX_MAIL, Can't open '%s'\n", OrigFileName);
		return;
	}

	while(fgets(line, sizeof line, fd) != NULL) {
		if((p = strchr(line, '\n')) != NULL) *p = '\0';
		if(strncmp(line, "END:", 4) == 0) break;
	}
	BSMTPflag = 0;	/* No BSMTP envelope */
	FilePosition = ftell(fd);	/* Save the beginning of real message */

/* Test if it is intended for an SMTP parser. If so, try to parse it */
	strcpy(line, To);	/* Get the receipient userID only */
	if((p = strchr(line, '@')) != NULL) *p = '\0';
	if((compare(line, "MAILER") == 0) ||
	   (compare(line, "SMTP") == 0) ||
	   (compare(line, "BSMTP") == 0)) {
		if((i = get_BSMTP_address(fd, NewFrom)) > 0) {	/* Real BSMTP there */
			strcpy(From, NewFrom);
			FilePosition = i;
			BSMTPflag = 1;	/* We have to handle dots... */
		}
	}
/* If it is not BSMTP, copy the single address into our vector to make the
   handling of BSMTP and none-BSMTP the same */
	if(BSMTPflag == 0) {
		if((ToAddresses[0] = malloc(strlen(To) + 1)) == NULL) {
			logger(1, "UNIX_MAIL, Can't malloc, Error: %s\n",PRINT_ERRNO);;
			bug_check("Can't malloc");
		}
		strcpy(ToAddresses[0], To);
		ToAddresses[1] = NULL;	/* End of list */
	}

	for(i = 0; i < MAX_ADDRESSES; i++) {
		if(ToAddresses[i] == NULL) {	/* End of list */
			fclose(fd);
			unlink(OrigFileName);
			return;
		}
		fseek(fd, FilePosition, 0);	/* Get back to beginning of real text */
		if((status = send_smtp(From, ToAddresses[i], fd, FileName,
			FileExt, Class, BSMTPflag)) != 0) {
			if(status == 1)
				sprintf(ReturnMessage, "*HUJI-NJE, Sent SMTP mail to %s",
					ToAddresses[i]);
			else
				sprintf(ReturnMessage, "*HUJI_NJE, Can't send \
mail to %s. Sending return message",
					ToAddresses[i]);
		}
		else
			sprintf(ReturnMessage, "*HUJI-NJE, Can't send mail to %s",
				ToAddresses[i]);
/* Send an NMR to the sender */
		sprintf(line, "@%s", LOCAL_NAME);	/* Sender's addresses */
		send_nmr(line, From, ReturnMessage, strlen(ReturnMessage),
			 ASCII, CMD_MSG);
	}
}


send_smtp(from, to, fd, FileName, FileExt, Class, BSMTPflag)
char	*from, *to, *FileName, *FileExt, Class;
FILE	*fd;
{
	long	ReturnStatus,	/* Status to return to calling program */
		state,		/* In which state are we */
		ReplyStatus,	/* The status we read from other party */
		TCPfd;
	char	line[1005];
	struct	sockaddr_in	Socket;

	if((TCPfd = open_SMTP_chan(&Socket)) <= 0) {
		return 0;			/* Can't open a channel */
	}

#ifdef DEBUG
	logger(4, "outgoing SMTP opened: %s > %s\n",
		from, to);
#endif
	ReturnStatus = 0;			/* Assume error */
	state = 0;				/* Nothing yet */
	while(SMTP_read(TCPfd, line, sizeof line) >= 0) {
		if(sscanf(line, "%d", &ReplyStatus) == 0)
			ReplyStatus = 400;	/* Artificial error code */
		if(ReplyStatus >= 400) {	/* Some error */
			logger(1, "SMTP send error (state=%d): '%s'\n", state, line);
			if(ReplyStatus != 421) {	/* We initiate the close */
				logger(4, "Starting hi log level for MultiNet\n");
				SMTP_write(TCPfd, "RSET", 4);	/* Reset message */
				ReturnStatus = 0;	/* Signal error */
				if(state >= 3) {	/* Addressee error */
/* If the From contains @ then return the message. When we send the error
   message.   We give address without @ to prevent loops */
					if(strchr(from, '@') != NULL) {
						send_postmaster(to, line,
								fd, from);
						ReturnStatus = 2;	/* Delete original message */
					}
					else	/* Already processing reply - abort */
						ReturnStatus = 0;
				}
				state = 6;		/* So we know to quit now */
				continue;		/* Read the reply */
			}
			else {				/* Other party has closed
							   the channel */
				close(TCPfd);
				logger(4, "Ending SMTP session (1)\n");
				return 0;		/* Some error */
			}
		}

		switch(state) {
		case 0:			/* Send the HELO message */
			sprintf(line, "HELO %s", BITNET_HOST);
			SMTP_write(TCPfd, line, strlen(line));
			state++;
			continue;
		case 1:			/* Send the MAIL FROM */
			sprintf(line, "MAIL FROM:<%s>", from);
			SMTP_write(TCPfd, line, strlen(line));
			state++;
			continue;
		case 2:			/* Send the RCPT TO: */
			sprintf(line, "RCPT TO:<%s>", to);
			SMTP_write(TCPfd, line, strlen(line));
			state++;
			continue;
		case 3:			/* Send the DATA */
			sprintf(line, "DATA");
			SMTP_write(TCPfd, line, strlen(line));
			state++; continue;
		case 4:			/* Send the data itself */
			if(Class != 'M') {
				/* It's a file - create simple header */
				sprintf(line, "From:   %s", from);
				SMTP_write(TCPfd, line, strlen(line));
				sprintf(line, "To:     %s", to);
				SMTP_write(TCPfd, line, strlen(line));
				sprintf(line, "Subject: BITnet file (%s.%s)\r\n",
					FileName, FileExt);
				SMTP_write(TCPfd, line, strlen(line));
				/* Previous write inserts also blank line */
			}
			if(cp_mnet_message(fd, TCPfd, BSMTPflag) == 0) {	/* Copy the message */
				logger(1, "UNIX_MAIL: Error during message copy \n");
				close(TCPfd);
				return 0;
			}
			sprintf(line, ".");	/* Signal end of message */
			SMTP_write(TCPfd, line, strlen(line));
/* Don't forget to write the mail header */
			state++; continue;
		case 5:			/* End of message - quit */
			ReturnStatus = 1;	/* We sent all the message and
						   got OK for it */
			sprintf(line, "QUIT");
			SMTP_write(TCPfd, line, strlen(line));
			state++;
			continue;
		case 6:			/* After the quit command */
			if(ReplyStatus == 221)	/* Other party is closing */
				goto Out;
			sprintf(line, "QUIT");	/* Other party hasn't closed - Close it */
			SMTP_write(TCPfd, line, strlen(line));
		}
	}
Out:	close(TCPfd);
	logger(4, "Ending SMTP session (2)\n");
	return ReturnStatus;
}


/*
 | Copy the message to the remote SMTP server. Duplicate dots.
 */
cp_mnet_message(fd, TCPfd, BSMTPflag)
FILE	*fd;
{
	char	*p, *q, line[1005];
	long	size, status;

	while(fgets(line, sizeof line, fd) != NULL) {
		if((p = strchr(line, '\n')) != NULL) *p = '\0';
		p = &line[strlen(line) - 1];
/* Remove trailing blanks. */
		while((p >= line) && (*p == ' ')) p--;
		*++p = '\0';
		size = (p - line);
		if(BSMTPflag != 0) {
			if((*line == '.') && (size == 1))	/* Only dot - end of message */
				break;
		} else {
/* Not BSMTP - replicate leading dots if they are there */
			if(*line == '.') {
				q = p++; *p = '\0'; size++;
				while(q > line) *--p = *--q;
			}
		}
		if(SMTP_write(TCPfd, line, size) == 0)
			return 0;	/* Some error */
	}
	return 1;
}



/*
 | Create a local socket.
 | If we are the initiating side, queue a non-blocking receive. If it fails,
 | close the channel, and re-queue a retry for 5 minutes later.
 */
open_SMTP_chan(Socket)
struct	sockaddr_in	*Socket;
{
	int	TCPfd;

/* Create a local socket */
	if((TCPfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		logger(1, "UNIX_MAIL, Can't get local socket. Error: %s\n",
		       PRINT_ERRNO);;
		return 0;
	}

	Socket->sin_family = (AF_INET);
	Socket->sin_port = htons(IPPORT_SMTP);
/* Get the IP adrress */
	Socket->sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* [mea] */

/* Do the connection trial in a subprocess to not block the parent one */
	if(connect(TCPfd, Socket, sizeof(struct sockaddr_in)) == -1) {
		logger(1, "UNIX_MAIL, Can't connect. Error: %s\n",
		       PRINT_ERRNO);;
		return 0;
	}
	return TCPfd;
}

/*
 | Called when something was received from TCP. Receive the data and call the
 | appropriate routine to handle it.
 | When receiving,  the first 4 bytes are the count field. When we get the
 | count, we continue to receive untill the count exceeded. Then we call the
 | routine that handles the input data.
 */
SMTP_read(TCPfd, string, size)
char	*string;
{
	if((size = read(TCPfd, string, size, 0)) == -1) {
		logger(1, "UNIX_MAIL, Error reading, Error: %s\n", PRINT_ERRNO);;
		return 0;
	}

/* If we read 0 characters, it usually signals that other side closed connection */
	if(size == 0) {
		logger(1,"UNIX_MAIL, Zero characters read. Disabling line.\n");
		return 0;
	}

	string[size] = '\0';
#ifdef DEBUG
	logger(4, "UNIX_MAIL, Read: '%s'\n", string);
#endif
	return size;
}


/*
 | Write a block to TCP.
 */
SMTP_write(TCPfd, line, size)
int	size;
unsigned char	*line;
{
	strcpy(&line[size], "\r\n"); size += 2;
#ifdef DEBUG
	logger(4, "UNIX_MAIL, Ending: '%s'\n", line);
#endif
	if(write(TCPfd, line, size) == -1) {
		logger(1, "UNIX_MAIL, Writing to TCP: Error: %s\n", PRINT_ERRNO);;
		return 0;
	}
	return 1;
}



send_postmaster(address, text, Ifd, From)
char	*address, *text, *From;
FILE	*Ifd;
{
	char	file_name[LINESIZE], line[513];
	FILE	*fd;
	long	clock;
	struct	tm	*tm, *localtime();
	char	*month = "JanFebMarAprMayJunJulAugSepOctNovDec"; /* The Months */
	char	*smonth;	/* Point to substring in month */
	char	*weekdays = "SunMonTueWedThuFriSat";
	char	*sweekdays;

		/* Get the machine readable address */
	logger(1, "Sending postmaster: address=%s, text=%s, sender=%s\n",
		address, text, From);
	strcpy(file_name, BITNET_QUEUE); strcat(file_name, "/ErrorReply.DAT");
	if((fd = fopen(file_name, "w")) == NULL)
		return;
	fseek(Ifd, FilePosition, 0);	/* Get back to beginning of real text */
	time(&clock);
	tm = localtime(&clock);
	smonth = month + (tm->tm_mon) * 3;	/* Point to month name */
	sweekdays = weekdays + (tm->tm_wday) * 3;
	fprintf(fd, "Date:     %-3.3s,  %d %-3.3s %d %d:%02d %s\n",
		sweekdays, tm->tm_mday, smonth, tm->tm_year,
		tm->tm_hour, tm->tm_min, GMT_OFFSET);
	fprintf(fd, "From:     Automatic answer system <MAILER@%s>\n",
			LOCAL_NAME);
	fprintf(fd, "To:       %s\n", From);
	fprintf(fd, "Cc:       PostMaster <POSTMASTER@%s>\n", LOCAL_NAME);
	fprintf(fd, "Subject:  Problems delivering a message\n\n");
/* The separating line was printed with the above print command */
	fprintf(fd, "Your message could not be delivered to some or all of\n");
	fprintf(fd, "it's receipients. The problem is:\n");
	fprintf(fd, "%s\n", text);
	fprintf(fd, "The erronous address was: %s\n", address);
	fprintf(fd, "If you have problems locating your addressee, try writing\n");
	fprintf(fd, "to POSTMASTER@%s or INFO@%s\n", LOCAL_NAME, LOCAL_NAME);
	fprintf(fd, "-----------------------------------\n");

/* Copy the original message */
	while(fgets(line, sizeof line, Ifd) != NULL)
		fprintf(fd, "%s", line);

/* Send  the message to the local postmaster and to the sender. */
	fclose(fd);
	fd = fopen(file_name, "r");
	sprintf(line, "POSTMASTER@%s", LOCAL_NAME);
	send_smtp("POSTMASTER",	/* No @Local-Site so SEND_SMTP will not loop on errors */
		line,	/* To whome */
		fd,
		"ERROR", "MAIL", 'M', 0);
	fseek(fd, 0, 0);	/* Rewind file */
	send_smtp("POSTMASTER",
		From,	/* To whome */
		fd,
		"ERROR", "MAIL", 'M', 0);
	fclose(fd);
	unlink(file_name);
}


/*
 |  Case insensitive Partial strings comparisons. Return 0 only if they have the same
 |  length.
*/
#define	TO_UPPER(c)	(((c >= 'a') && (c <= 'z')) ? (c - ' ') : c)
compare_p(a, b, size)
char	*a, *b;
{
	register char	*p, *q;

	p = a; q = b;

	for(; TO_UPPER(*p) == TO_UPPER(*q); p++,q++)
		if((*p == '\0') || (*q == '\0')) break;

	if((*p == '\0') && (*q == '\0'))	/* Both strings done = Equal */
		return 0;

/* Not equal */
	if((p - a) >= size)
		return 0;	/* First part equal */
	else	return(TO_UPPER(*p) - TO_UPPER(*q));
}


/*
 | Decode the BSMTP envelope of an incoming mail. If there is no > at the
 | present line, then we look in the next line for it (mailers wrap long
 | lines).
*/
int
get_BSMTP_address(Fd, from)
FILE	*Fd;
char	*from;
{
	int	counter;
	char	line[LINESIZE], TempLine[LINESIZE], *p;

	counter = 0;	/* No valid addresses yet */

	if(fgets(line, sizeof line, Fd) == NULL)	/* Empty file... */
		return 0;	/* No valid BSMTP found */
	if((p = strchr(line, '\n')) != NULL) *p = '\0';

	for(;;) {
		if(compare_p(line, "DATA", 4) == 0) {	/* Data starts here */
			ToAddresses[counter] = NULL;	/* End of list */
			return ftell(Fd);	/* Return the current position */
		}
		else
		if(compare_p(line, "MAIL FROM:", 10) == 0) {
/* If there is no >, maybe the line is splitted, so look in the next line */
			while((p = strchr(line, '>')) == 0) {
				if(fgets(TempLine, sizeof TempLine, Fd) == NULL)
					break;	/* End of file (strange, eh?) */
				if((p = strchr(line, '\n')) != NULL) *p = '\0';
				if((strlen(line) + strlen(TempLine)) > LINESIZE)
					bug_check(1, "MAILER_BSMTP, BSMTP cont line too long");
				strcat(line, TempLine);
			}
			*p = 0;		/* Cut the line there */
			if((p = strchr(line, '<')) != NULL) *p++;	/* Start of address */
			else	p = line + 10;	/* Where it should start */
			if(*p != '\0')	/* Some mailers put empty string here */
				strcpy(from, p);
		}
		else
		if(compare_p(line, "RCPT TO:", 8) == 0) {
/* If there is no >, maybe the line is splitted, so look in the next line */
			while((p = strchr(line, '>')) == 0) {
				if(fgets(TempLine, sizeof TempLine, Fd) == NULL)
					break;	/* End of file (strange, eh?) */
				if((p = strchr(line, '\n')) != NULL) *p = '\0';
				if((strlen(line) + strlen(TempLine)) > LINESIZE)
					bug_check(1, "MAILER_BSMTP, BSMTP cont line too long");
				strcat(line, TempLine);
			}
			*p = 0;		/* Cut the line there */
			if((p = strchr(line, '<')) != NULL) *p++;	/* Start of address */
			else	p = line + 8;	/* Where it should start */
			if(counter > (MAX_ADDRESSES - 1))
				bug_check("No space for BSMTP addresses");
			if((ToAddresses[counter] = malloc(strlen(p) + 1)) == NULL) {
				logger(1,"UNIX_MAIL, Can't malloc, Error: %s\n",
				       PRINT_ERRNO);;
				bug_check("Can't malloc");
			}
			strcpy(ToAddresses[counter++], p);	/* Copy address */
		}
		else
		if((compare_p(line, "HELO", 4) != 0) &&
		   (compare_p(line, "VERB", 4) != 0) &&
		   (compare_p(line, "TICK", 4) != 0)) {
			logger(2, "Illegal line in income BSMTP: %s", line);
			break;	/* Abort loop - illegal line */
		}
		if(fgets(line, sizeof line, Fd) == NULL) break;	/* End of file (strange, eh?) */
		if((p = strchr(line, '\n')) != NULL) *p = '\0';
		if((strlen(line) == 0) ||
		   (*line == ' '))	break;	/* Null line - quit */
	}
	ToAddresses[counter] = NULL;
	return ftell(Fd);
}
