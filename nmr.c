/* NMR.C    V2.7
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
 | Handle NMR records (commands and messages).
 | The function Handle_NMR is called when an NMR is received via an NJE line.
 | If it is intended to the local node, it is broadcasted. If not, it is forwarded
 | (in EBCDIC, as-is) to SEND_NMR to forward it to the correct line. We do not
 | Send-NMR checks first whether this is a local address. If so, it is
 | broadcasted localy. If not, it is queued on the correct line. If the message
 | is a forwarded one, we take it as-is and save us formatting work.
 |   Commands are assumed to be in upper case (either received from the other
 | side, ot translated so by the local user's interface).
 |   Messages sent to MAILER are discarded always, since this account gets a
 | lot of messages and never logged-in.
 |
 | V1.1 - Add the conditional of DONT_SEND_TO_ME which do not send NMR messages
 |        to some pre-defined addresses to keep us from problems.
 | V1.2 - Log messages sent to null username (they are sent to the node).
 | V1.3 - Add support for command CPQ LOG, CPU, IND, CPLEVEL, Time.
 | V1.4 - If a message is unsent to user, and the message text starts with *,
 |        then do not send a message back that the user is not logged in (* is
 |        usually in GONE messages).
 | V1.5 - If an NMR message is originated locally, the sender is not null, and
 |        it cannot be sent, then return a message to the sender. We asume
 |        that ASCII messages are locally originated.
 | V1.6 - Change castings and variables definitions to compile smoothly under
 |        most systems.
 | V1.7 -  There are implementations which do not like redundant bytes after
 |         the end of the message's text. Hence, when we call Compress_SCB,
 |         give the real size and not the whole NMR_MESSAGE structure size.
 | V1.8 - 21/2/90 - Move the definition of the macro DONT_SEND_TO_ME to
 |        SITE_CONSTS.H
 |        Replace BCOPY calls with memcpy().
 | V1.9 - 6/3/90 - Add alternate routes support for NMR messages. We first look
 |        for the link name in the list of links. If not found, we look in the
 |        routing table for a different entry. If found, and that line is in
 |        connect state, we use it. If not, we conclude that the node is not
 |        defined.
 | V2.0 - 7/3/90 - Add Q NODE command response.
 | V2.1 - 7/3/90 - Before sending an ASCII message to the user, remove all
 |        codes whose ASCII is less than 32 or greater than 128. This will
 |        disable user's ability to play with the screen...
 | V2.2 - 22/3/90 - If Send_user returns a negative value, then the user is not
 |        logged-in and his message was recorded. Send an appropriate reply
 |        back.
 | V2.3 - 1/4/90 - Get_line_index() - Upcase the nodename before searching.
 | V2.4 - 6/5/90 - When treating an address to send message to, remove trailing
 |        .BITNET, .EARN, etc.
 | V2.5 - 28/10/90 - Find_line_index() - Return also the character set read from
 |        the routing table. It is used for the local nodes. If the link is
 |        directly connected force it to EBCDIC.
 | V2.6 - 5/2/91 - Change Find_line_index() to be recursive, thus having
 |        alternate route for hosts routed via inactive node XXX and not only
 |        for node XXX itself.
 | V2.7 - 11/3/91 - Add Q STAT command.
 */
#include <stdio.h>
#include <errno.h>
#include "consts.h"
#include "headers.h"
#include <time.h>
#ifdef VMS
#include <jpidef.h>
#include <lnmdef.h>
#include <syidef.h>
#define	SYI$_HW_NAME	4362	/* Does not appear there... */
#endif

EXTERNAL struct	LINE IoLines[MAX_LINES];

char	*strchr(), *strrchr();

/* The help text. Must end with a null string: */
char	HelpText[][80] = {
	"Commands available for HUJI-NJE emulator:",
	"   Query SYStem - Show lines status, num queued files and active files",
        "   Query STATs - Show lines statictics.",
	"   Query Nodename - Show the route entry to that node.",
	"   Query linkname F/A - Available via Query SYStem. Q command is not available",
#ifdef VMS
	"   CPQuery Names - List all users logged-in",
	"   CPQuery User username - Look for a specific username",
	"   CPQuery LOG - Send the WELCOME message.",
	"   CPQuery CPU, CPLEVEL, IND, T - Machine type and time.",
#endif
	"" };


/*
 | The main entry point. All NMR's are received here and dispatched from
 | Here.
 */
handle_NMR(NmrRecord, size)
struct	NMR_MESSAGE	*NmrRecord;
int	size;
{
	char	UserName[20], MessageText[LINESIZE],
		OriginNode[20], DestinationNode[20],
		OriginUser[20], line[LINESIZE];
	char	*p;
	unsigned char	*up, *uq;	/* For EBCDIC fields handling */
	register char	*RealMessageText;
	register int	i, s, TempVar;
	int	send_user();

/* Retrieve the origin site name, the destination sitename and username */
	EBCDIC_TO_ASCII((NmrRecord->NMRFMNOD), OriginNode, 8);
	EBCDIC_TO_ASCII((NmrRecord->NMRTONOD), DestinationNode, 8);
	EBCDIC_TO_ASCII((NmrRecord->NMRUSER), UserName, 8);
	for(p = &OriginNode[7]; p > OriginNode; p--) if(*p != ' ') break;
	*++p = '\0';	/* Remove trailing blanks */
	for(p = &DestinationNode[7]; p > DestinationNode; p--) if(*p != ' ') break;
	*++p = '\0';
	for(p = &UserName[7]; p > UserName; p--) if(*p != ' ') break;
	*++p = '\0';

/* Test whether it is for our node */
#ifdef DEBUG
	logger(3, "NMR: Message from node '%s' to %s@%s\n",
		OriginNode, UserName, DestinationNode);
#endif
	if(compare(DestinationNode, LOCAL_NAME, (p - DestinationNode)) != 0) {
		if((NmrRecord->NMRFLAG & 0x80) == 0)	/* MEssage */
			send_nmr("", DestinationNode, NmrRecord, size,
				EBCDIC, CMD_MSG);
		else {	/* Command */
/* If we can't forward a command we send a rejection message - so create the
   sender's address.
*/
			sprintf(line, "%s@%s", UserName, OriginNode);
			send_nmr(line, DestinationNode, NmrRecord,
				size, EBCDIC, CMD_CMD);
		}
		return;
	}

/* Test whether this is a command or message. */
	*OriginUser = '\0';	/* Assume no username */
	if((NmrRecord->NMRFLAG & 0x80) != 0) {
		logger(3, "NMR: command from '%s@%s' to node %s\n",
			UserName, OriginNode, DestinationNode);
		up = NmrRecord->NMRMSG;
		i = (NmrRecord->NMRML & 0xff);
		EBCDIC_TO_ASCII(up, MessageText, i);
		MessageText[i] = '\0';
		logger(3, "Command is: '%s'\n", MessageText);
		handle_local_command(OriginNode, UserName, MessageText);
		return;		/* It's a message - ignore it */
	}

	up = &((NmrRecord->NMRMSG)[0]); s = (NmrRecord->NMRML & 0xff);
	if((NmrRecord->NMRTYPE & 0x8) != 0) {
		/* There is username (sender) inside the message's text */
		if((NmrRecord->NMRTYPE & 0x4) != 0) {	/* No time stamp */
		/* First 8 characters of message are the sender's username */
			EBCDIC_TO_ASCII((NmrRecord->NMRMSG), OriginUser, 8);
			for(i = 7; i >= 0; i--) if(OriginUser[i] != ' ') break;
			OriginUser[++i] = '\0';
			up += 8; s -= 8;	/* Username removed from message */
		} else {
		/* There is timestamp, username, and then the message's text */
			uq = &((NmrRecord->NMRMSG)[8]);
			EBCDIC_TO_ASCII(uq, OriginUser, 8);
			for(i = 7; i >= 0; i--) if(OriginUser[i] != ' ') break;
			OriginUser[++i] = '\0';
			/* Copy the message's text over the username */
			memcpy(&(up[8]), &(up[16]), s - 16);
			s -= 8;	/* Username removed */
		}
/* Create the message's text */
#ifdef VMS
		sprintf(MessageText, "\r\n\007%s(%s): ",
			OriginUser, OriginNode);
#else
		sprintf(MessageText, "\r\007%s(%s): ",
			OriginUser, OriginNode);
#endif
	}
	else	/* No username - take message as-is */
#ifdef VMS
		sprintf(MessageText, "\r\n\007%s: ", OriginNode);
#else
		sprintf(MessageText, "\r\007%s: ", OriginNode);
#endif

	i = strlen(MessageText);
	/* Save the start of the original text in RealMessageText. See
	   explanations in return-message */
	RealMessageText = &MessageText[i];  i += s;
	EBCDIC_TO_ASCII(up, RealMessageText, s);
/* Remove all codes whose ASCII is less than 32 and greater than 127 */
	RealMessageText[s] = '\0';
	for(p = RealMessageText; *p != '\0'; *p++)
		if((*p < ' ') || (*p > '\176')) *p = ' ';

	MessageText[i++] = '\r';
#ifndef VMS
	MessageText[i++] = '\n';
#endif
	MessageText[i] = '\0';

#ifdef DEBUG
	logger(4, "Sending message to user '%s'\n", UserName);
#endif

/* Send to user and check whether he received it */
	if(*UserName == '\0') {	/* Happens sometimes */
#ifdef DEBUG
		logger(3, "NMR message to null user: '%s'\n",
			MessageText);
#endif
		return;
	}

/* Mailer receives quite a lot of messges, but he never logged-in... */
	if(compare(UserName, "MAILER") == 0)
		return;

	if((i = send_user(UserName, MessageText)) <= 0)
/* If there is a username we can answer. If not - the source was machine.
   Do not answer if the message text starts with *, since these are usually GONE
   messages */
		if((*OriginUser != '\0') && (*RealMessageText != '*')) {
			if(i == 0)	/* Not logged in */
				sprintf(line, "* %s not logged in", UserName);
			else	/* -1 = Not logged-in, but got the Gone */
				sprintf(line, "*yGONE: %s not logged-in, but\
 your message has been recorded", UserName);

/* Create the sender and receiver */
			sprintf(DestinationNode, "%s@%s", OriginUser, OriginNode);
			sprintf(OriginNode, "@%s", LOCAL_NAME);
			send_nmr(OriginNode, DestinationNode, line,
				strlen(line), ASCII, CMD_MSG);
		}
}


/*
 | Send the NMR. Parse the address (which arrives as User@Site), and queue it
 | to the correct line.
 */
send_nmr(Faddress, Taddress, text, size, format, Cflag)
int	size, format,	/* ASCII or EBCDIC */
	Cflag;			/* Command or Message */
char	*text, *Faddress, *Taddress;
{
	char	*p, *q, line[LINESIZE], LineName[20],
		NodeKey[20], TempLine[LINESIZE],
		CharacterSet[16];	/* Not needed but returned by Find_line_index() */
	unsigned char	*up;
	struct	NMR_MESSAGE	NmrRecord;
	struct	MESSAGE	*Message;
	register int	TempVar, MessageSize;
	int	i, AddUserName,	/* Do we include Username when sending a message? */
		get_route_record(), send_user();

	if(size > LINESIZE) {	/* We don't have room for longer message */
		logger(1, "NMR, Message text size(%d) too long for '%s'. Ignored\n",
		size, Taddress);
		return;
	}

/* Copy the message text into the NMR buffer */
	if(format != ASCII) {
		if((p = strchr(Taddress, '@')) != NULL)
			strcpy(NodeKey, ++p);
		else
			strcpy(NodeKey, Taddress);	/* In this case this is only the node name */
		goto Emessage;	/* Ebcdic message - send as-is */
	}

/* Remove traling .EARN, .BITNET, etc. in the Taddress */
	if((p = strchr(Taddress, '.')) != NULL) {
		if((compare(p, ".BITNET") == 0) ||
		   (compare(p, ".EARN") == 0))
			*p = '\0';	/* Drop it. */
	}

/* Test whether this message is for our node. If so, send it localy */
	if((p = strchr(Taddress, '@')) != NULL)
		if(compare(++p, LOCAL_NAME) == 0) {
			strcpy(LineName, Taddress);	/* Don't ruin original */
			if((p = strchr(LineName, '@')) != NULL) *p++ = '\0';	/* Username */
			strcpy(line, Faddress);
			if((p = strchr(line, '@')) != NULL) *p++ = '\0'; else p = line;
/* If it is a command - execute it. If not - broadcast */
			if(Cflag == CMD_CMD) {
				handle_local_command(LOCAL_NAME, line, text);
			}
			else {
/* Remove all codes whose ASCII is less than 32 and greater than 127 */
				for(q = text; *q != '\0'; *q++)
					if((*q < ' ') || (*q > '\176')) *q = ' ';
				if(*line != '\0')	/* There is username */
					sprintf(TempLine, "\r\n%s(%s): %s\r",
						line, p, text);
				else	/* No username */
					sprintf(TempLine, "\r\n%s: %s\r",
						p, text);
				send_user(LineName, TempLine);
			}
			return;
	}

/* The message is in ASCII = no forwarded message. Check whether it is intended
   to some specific addresses that we don't want to send messages to them,
   since they usually can't receive messages.
*/
	if(DONT_SEND_TO_ME(Taddress))
		return;		/* Ignore it */

/* Create the From address */
	strcpy(line, Faddress);
	if((p = strchr(line, '@')) == NULL) {
		logger(1, "NMR, Illegal from address '%s'\n", Faddress);
		return;
	}
	*p++ = '\0';	/* Separate the Node and the Username */
	size = strlen(p);
	ASCII_TO_EBCDIC(p, (NmrRecord.NMRFMNOD), size);
	PAD_BLANKS((NmrRecord.NMRFMNOD), size, 8);
	/* Put the sender username as the first 8 bytes of message text */
	if(Cflag == CMD_CMD) {	/* NMRUSER holds the originator if command */
		size = strlen(line);
		ASCII_TO_EBCDIC(line, (NmrRecord.NMRUSER), size);
		PAD_BLANKS((NmrRecord.NMRUSER), size, 8);
	}
	else {	/* Message - Write the username as first message's field */
		if(*line != '\0') {	/* There is username there */
			size = strlen(line);
			ASCII_TO_EBCDIC(line, (NmrRecord.NMRMSG), size);
			PAD_BLANKS((NmrRecord.NMRMSG), size, 8);
			AddUserName = 1;
		}
		else
			AddUserName = 0;
	}
	if(Cflag == CMD_MSG) {
		/* Message - If we put the username before, then we have to skip
		   now over it (it occipies the first 8 characters of the
		   message's text) */
		if(AddUserName == 0) {	/* No username is there */
			size = strlen(text);
			ASCII_TO_EBCDIC(text, (NmrRecord.NMRMSG), size);
		}
		else {	/* The username is there */
			up = &(NmrRecord.NMRMSG[8]); size = strlen(text);
			ASCII_TO_EBCDIC(text, up, size);
			size += 8;	/* Add 8 for the username that was before */
		}
	} else {	/* Command - do not start message with username */
		up = &(NmrRecord.NMRMSG[0]); size = strlen(text);
		ASCII_TO_EBCDIC(text, up, size);
	}

	NmrRecord.NMRML = (size & 0xff);
	MessageSize = size;	/* The text size */
	NmrRecord.NMRFLAG = 0x20;	/* We send the username */
	if(Cflag == CMD_CMD)
		NmrRecord.NMRFLAG |= 0x80;	/* The command flag */
	NmrRecord.NMRLEVEL = 0x77;	/* RSCS does it... */
	if(Cflag == CMD_MSG) {
		if(AddUserName != 0)
			NmrRecord.NMRTYPE = 0xc; /* Username, no timestamp... */
		else
			NmrRecord.NMRTYPE = 0x4; /* no Username, no timestamp... */
	} else
		NmrRecord.NMRTYPE = 0;
	memcpy(NmrRecord.NMRFMNOD, E_BITnet_name, 8);

/* Create the destination address */
	strcpy(line, Taddress);
	if((p = strchr(line, '@')) == NULL) {
		logger(1, "NMR, Illegal address '%s' to send message to.\n",
			Taddress);
/* Send a reply back to the user only if the message if of type ASCII (thus
   locally originated) and the "from" is a real from */
		if(format == ASCII) {
			strcpy(TempLine, Faddress);
			if((p = strchr(TempLine, '@')) == NULL) return;
			else	*p++ = '\0';
			if((compare(p, LOCAL_NAME) != 0) ||/* Sender not on our node */
			   (*TempLine == '\0'))	/* Not a real username */
				return;
			sprintf(line,
				"\r\n\007HUJI-NJE: Can't send message to %s\r",
				Taddress);
			send_user(TempLine, line);	/* Don't bother to check
							whether he received it */
		}
		return;
	}
	*p++ = '\0';	/* Separate the Node and the Username */
	strcpy(NodeKey, p);
	size = strlen(p);
	ASCII_TO_EBCDIC(p, (NmrRecord.NMRTONOD), size);
	PAD_BLANKS((NmrRecord.NMRTONOD), size, 8);
	if(Cflag == CMD_MSG) {	/* NMRUSER holds the sender if message */
		size = strlen(line);
		ASCII_TO_EBCDIC(line, (NmrRecord.NMRUSER), size);
		PAD_BLANKS((NmrRecord.NMRUSER), size, 8);
	}
	NmrRecord.NMRFMQUL = NmrRecord.NMRTOQUL = 0;

/* Create the RCB, etc.: */
Emessage:
	if(format == ASCII) {	/* Create the whole NMR record */
		size = 0;
		line[size++] = 0x9a;	/* RCB */
		line[size++] = 0x80;	/* SRCB */
/* There are implementations which do not like redundant bytes after the end
   of the message's text. Hence, when we call Compress_SCB, give the real size.
   This is the text's size and 30 for all the controls before it.*/
		size += compress_scb(&NmrRecord, &line[2], MessageSize + 30);
	}
	else {	/* It's EBCDIC - Have to add only the fisrt RCB+SRCB. Last RCB
		   is not added since the sending routine might block a few
                   messages in one transmission block. Size contains now the
		   size of the received NMR */
		line[0] = 0x9a;
		line[1] = 0x80;
		size = 2 +
			compress_scb(text, &line[2], size);
	}
	/* Check whether we do not have a buffer overflow */
	if(size >= LINESIZE)
		bug_check("NMR: NMR record size overflow (after compress)");

/* Convert the nodename to uppercase */
	for(i = 0; i < strlen(NodeKey); i++)
		if((NodeKey[i] >= 'a') && (NodeKey[i] <= 'z'))
			NodeKey[i] -= ' ';

/* look for the line number we need - it returns the link to send the NMR on
   (the direct link or alternate route): */
	switch((i = find_line_index(NodeKey, LineName, CharacterSet))) {
	case NO_SUCH_NODE:	/* Does not exist at all */
		logger(2, "NMR, Node '%s' unknown\n", NodeKey);
		if(Cflag == CMD_CMD) { /* If it was command - Return a message to the sender */
			sprintf(line, "Node %s not recognised.", NodeKey);
			sprintf(TempLine, "@%s", LOCAL_NAME);	/* Sender */
			send_nmr(TempLine, Faddress, line, strlen(line),
				ASCII, CMD_MSG);
		}
		return;
	case ROUTE_VIA_LOCAL_NODE:	/* Not connected via NJE */
		if(Cflag == CMD_CMD) {
			sprintf(line, "Node %s can't receive commands", NodeKey);
			sprintf(TempLine, "@%s", LOCAL_NAME);	/* Sender */
			send_nmr(TempLine, Faddress, line, strlen(line),
				ASCII, CMD_MSG);
		}
		return;
	case LINK_INACTIVE:	/* Both link and alternate routes inactives */
		if(Cflag == CMD_CMD) {
			sprintf(line, "Link %s to node %s inactive", LineName,
					NodeKey);
			sprintf(TempLine, "@%s", LOCAL_NAME);	/* Sender */
			send_nmr(TempLine, Faddress, line, strlen(line),
				ASCII, CMD_MSG);
		}
		return;
	default:	/* All other values are index values of line to queue on */
		if((i < 0) || (i >= MAX_LINES)) {
			logger(1, "NMR, Illegal line number #%d returned by\
 find_file_index() for node %s\n", i, NodeKey);
			return;
		}
		/* OK - get memory and queue for line */
		if(IoLines[i].MessageQstart == NULL) {
		/* Init the list */
			IoLines[i].MessageQstart =
			  IoLines[i].MessageQend =
			   Message = (struct MESSAGE*)malloc(sizeof(struct MESSAGE));
		}
		else {
			IoLines[i].MessageQend->next =
			 Message =
				(struct MESSAGE*)malloc(sizeof(struct MESSAGE));
			IoLines[i].MessageQend = Message;
		}
		if(Message == NULL) {
#ifdef VMS
			logger(1, "NMR, Can't malloc. Errno=%d, VmsErrno=%d\n",
				errno, vaxc$errno);
#else
			logger(1, "NMR, Can't malloc. Errno=%d\n",
				errno);
#endif
			bug_check("NMR, Can't Malloc for message queue.");
		}
		Message->next = NULL;		/* Last item in list */
		Message->length = size;
/* For debugging and safety checks */
		if(size > sizeof(struct MESSAGE))
			logger(1, "NMR, Too long message. Malloc will overflow!\n");
		memcpy(Message->text, line, size);
#ifdef DEBUG
		logger(3, "NMR, queued message to address %s, size=%d.\n",
			Taddress, size);
#endif
		return;
	}
}


/*
 | Loop over links and find the requested line. If it is in ACTIVE/CONNECT
 | mode - good; return it. If not (no such line or link inactive), try looking
 | in the routing table. If the destination there is different, then call this
 | routine again in order to try finding an alternate link using the same
 | algorithm. This allows us to "chain" alternative routes.
 |   The character set for directly connected line is forced to be EBCDIC.
 */
find_line_index(NodeName, LineName, CharacterSet)
char	*NodeName,	/* The nodename we are looking for */
	*LineName,	/* The actual line to route to */
	*CharacterSet;	/* ASCII or EBCDIC. */
{
	int	i;
	char	NodeKey[16], TempLine[LINESIZE];

/* Upcase nodename */
	strcpy(NodeKey, NodeName);
	for(i = 0; i < strlen(NodeKey); i++)
		if((NodeKey[i] >= 'a') && (NodeKey[i] <= 'z')) NodeKey[i] -= ' ';
/* Append blanks because Get_Route_record wants it to be 8 characters long */
	for(i = strlen(NodeKey); i < 8; i++) NodeKey[i] = ' ';	/* Pad with blanks */
	NodeKey[8] = '\0';

/* First - search for the line and see whether it is in Connect state */
	for(i = 0; i < MAX_LINES; i++) {
		if((IoLines[i].HostName)[0] != '\0') {	/* This line is defined */
			if(compare(IoLines[i].HostName, NodeName) == 0) {
				if(IoLines[i].state == ACTIVE) {
/* OK - found a directly connected line in connect mode - tell caller that
   the linename to queue is the same and return the line index */
					strcpy(LineName, NodeName);
					strcpy(CharacterSet, "EBCDIC");
					return i;
				}
				else
					break;
			}
		}
	}

/* Not found such a line - Look in the table for an entry for it. */
	if(get_route_record(NodeKey, TempLine, sizeof TempLine) == 0)
		return NO_SUCH_NODE;	/* Not defined in tables at all */

	sscanf(TempLine, "%s %s %s", NodeKey, LineName, CharacterSet);

/* If this link is LOCAL, discard the message. It is intended to sites that
   get mail services from us but are not connected with NJE protocol.
*/
	if(compare(LineName, "LOCAL") == 0)
		return ROUTE_VIA_LOCAL_NODE;
/* If no alternate route given then retire... */
	if(compare(LineName, NodeName) == 0)	/* No alternate route */
		return LINK_INACTIVE;

	strcpy(TempLine, LineName);	/* This is the new node we for look now */
	return find_line_index(TempLine, LineName, CharacterSet);
}


/*
 | A command was sent to the local node. Parse it and send the reply to
 | the originator.
 */
handle_local_command(OriginNode, UserName, MessageText)
char	*OriginNode, *UserName, *MessageText;
{
	char	Faddress[20], Taddress[20], line[LINESIZE];
	char	KeyWord[SHORTLINE], param1[SHORTLINE], param2[SHORTLINE];
	int	i, NumKeyWords;

/* Create first the addresses of the sender and receiver */
	sprintf(Faddress, "@%s", LOCAL_NAME);
	sprintf(Taddress, "%s@%s", UserName, OriginNode);

/* Get the command keywords */
	*line = *param1 = *param2 = '\0';
	if((NumKeyWords = sscanf(MessageText, "%s %s %s", KeyWord, param1,
		param2)) == 0)
		return;		/* Ignore null commands */
	if(NumKeyWords < 2)
		*param1 = '\0';
	if(NumKeyWords < 3)
		*param2 = '\0';

/* Try parsing messages we recognise */
	if(*KeyWord == 'Q') {	/* Query command */
		if(NumKeyWords < 2)
			sprintf(line, "QUERY command needs parameters");
		else
		if(strncmp(param1, "SYS", 3) == 0) {
			show_lines_status(Taddress);
			return;
		}
		else
		if(strncmp(param1, "STAT", 4) == 0) {
			show_lines_stats(Taddress);
			return;
		}
		else {	/* Probably something for a link */
			switch(*param2) {
			case 'Q': sprintf(line, "QUERY link Q is not allowed via the net");
				  break;
			case 'F': sprintf(line, "Use QUERY SYSTEM to see file's queue");
				  break;
			case 'A': sprintf(line, "Use QUERY SYSTEM to find active file");
				  break;
			case '\0':	/* No parameter - show the route to that node */
				  query_route(Faddress, Taddress, param1);
				  return;
			default: sprintf(line, "Illegal Query command");
				  break;
			}
		}
	}
	else
#ifdef VMS
	if(strncmp(KeyWord, "CPQ", 3) == 0) {
		if(*param1 == 'N') {	/* CPQ Names */
			list_users(Faddress, Taddress, "");	/* List all users */
			return;
		} else
		if((*param1 == 'U') && (*param2 != '\0')) {
			list_users(Faddress, Taddress, param2);
				/* List that user only */
			return;
		} else
		if(compare(param1, "LOG") == 0) {
			send_welcome_message(Faddress, Taddress);
				/* Send our WELCOME text */
			return;
		} else
		if((compare(param1, "CPU") == 0) ||
		   (compare(param1, "CPLEVEL") == 0) ||
		   (compare(param1, "LOG") == 0) ||
		   (compare(param1, "IND") == 0) ||
		   (*param1 == 'T')) {
			send_cpu_time(Faddress, Taddress);
				/* Send our CPU type and time */
			return;
		} else
			sprintf(line, "Illegal CPQuery command syntax. Try HELP");
	}
	else
#endif
	if(*KeyWord == 'H') {
		for(i = 0; *HelpText[i] != '\0'; i++)
		send_nmr(Faddress, Taddress, HelpText[i], strlen(HelpText[i]),
			ASCII, CMD_MSG);
		return;
	}
/* Create a message text saying that we do not recognise this command */
	else
		sprintf(line, " Command '%s' unrecognised. Try HELP", MessageText);

	send_nmr(Faddress, Taddress, line, strlen(line),
			ASCII, CMD_MSG);
}

#ifdef VMS
/*
 | Loop over all users in the system. Look for those that are interactive users.
 | from those either list all of them (if UserName points to Null), or look
 | for a  specific username (if UserName points to some string).
 | If a list of users is requested, we try to block 6 usernames in a single
 | line.
 */
static struct	itmlist {	/* To retrive information from $GETJPI */
		short	length;
		short	code;
		long	address;
		long	rtn;
	} list[3];
list_users(Faddress, Taddress, UserName)
char	*Faddress, *Taddress, *UserName;
{
	char	line[LINESIZE];
	static	char	username[13];
	static	long	mode;
	static	long	status, PID;			/* Use for search */
	register int	i, LinePosition, counter;
	char	*p;

	if(*UserName == '\0') {
		sprintf(line, "Users logged in on %s:", LOCAL_NAME);
		send_nmr(Faddress, Taddress, line, strlen(line),
			ASCII, CMD_MSG);
	}

/* Init item's list */
	list[0].code = JPI$_MODE;
	list[0].address = &mode;  list[0].length = 4;
	list[1].code = JPI$_USERNAME;
	list[1].address = username;  list[1].length = (sizeof username);
	list[2].code = list[2].length = 0;

/* Now - Call JPI */
	PID = -1; counter = LinePosition = 0;
	for(;;) {
		status = sys$getjpiw((long)(0), &PID, (long)(0), list,
			(long)(0), (long)(0), (long)(0));
		if((status & 0x1) == 0)
			break;	/* Abort on any error */
		if(mode != 3)		/* Not interactive */
			continue;
		if((p = strchr(username, ' ')) != NULL) *p = '\0';
		if(*UserName == '\0') {	/* List all users */
			strcpy(&line[LinePosition], username);
			LinePosition += strlen(username);
			/* Pad the username with blanks to 12 characetrs */
			i = LinePosition;
			LinePosition = ((LinePosition + 12) / 12) * 12;
			for(; i < LinePosition; i++) line[i] = ' ';
			if(counter++ >= 5) {
				line[LinePosition] = '\0';
				send_nmr(Faddress, Taddress, line, strlen(line),
					ASCII, CMD_MSG);
				LinePosition = counter = 0;
			}
		}
		else {		/* Check whether this is the username */
			if(compare(username, UserName) == 0) {
				sprintf(line, "%s logged in on %s", username,
					LOCAL_NAME);
				send_nmr(Faddress, Taddress, line, strlen(line),
					ASCII, CMD_MSG);
				return;
			}
		}
	}
/* Check whether there was a request for a specific user. If there was and
   we are here, then the user is not logged-in.
*/
	if(*UserName != '\0') {
		sprintf(line, "%s is not logged in", UserName);
			send_nmr(Faddress, Taddress, line, strlen(line),
				ASCII, CMD_MSG);
	}
	else {
		if(counter > 0) {	/* Incomplete line */
			line[LinePosition] = '\0';
			send_nmr(Faddress, Taddress, line, strlen(line),
				ASCII, CMD_MSG);
		}
		sprintf(line, "End of list");
		send_nmr(Faddress, Taddress, line, strlen(line),
			ASCII, CMD_MSG);
	}
}

/*
 | Send the WELCOME file to the requestor. Try getting the translation of the
 | logical name SYS$WELCOME. If it doesn't start with @, send it as-is. If it
 | starts, try sending the file.
 */
send_welcome_message(Faddress, Taddress)
char	*Faddress, *Taddress;
{
	long	status;
	static long		NameLength;
	static struct DESC	logname, tabname;
	char	*p, line[LINESIZE];
	FILE	*fd;
	char	SYSTEM_TABLE[] = "LNM$SYSTEM_TABLE";	/* The logical names
							   table. */
	char	LOGNAME[] = "SYS$WELCOME";

/* Get the translation of it */
	tabname.address = SYSTEM_TABLE; tabname.length = strlen(SYSTEM_TABLE);
	logname.address = LOGNAME; logname.length = strlen(LOGNAME);
	logname.type = tabname.type = 0;

	list[0].length = LINESIZE;
	list[0].code = LNM$_STRING;	/* Get the equivalence string */
	list[0].address = line;
	list[0].rtn = &NameLength;	/* The length of returned string */
	list[1].length = list[1].code = list[1].address = 0;

	if(((status = sys$trnlnm(0,&tabname, &logname,0,list)) & 0x1) == 0) {
		logger(1, "NMR, Can't get logical translation of '%s'\n",
			LOGNAME);
		return;
	}
	line[NameLength] = '\0';

/* Check whether it starts with @ */
	if(*line != '@') {	/* Yes, send as-is */
		send_nmr(Faddress, Taddress, line, strlen(line),
			ASCII, CMD_MSG);
		return;
	}
/* Doesn't start with @ - open the file */
	if((fd = fopen(&line[1], "r")) == NULL) {
		logger(1, "NMR, Can't open '%s' for CPQ LOG command\n",
			&line[1]);
		sprintf(line, "LOG file not available");
		send_nmr(Faddress, Taddress, line, strlen(line),
			ASCII, CMD_MSG);
		return;
	}
	while(fgets(line, sizeof line, fd) != NULL) {
		if((p = strchr(line, '\n')) != NULL) *p = '\0';
		send_nmr(Faddress, Taddress, line, strlen(line),
			ASCII, CMD_MSG);
	}
	fclose(fd);
}


/*
 | Get the CPU type and VMS version, and send it with the current time.
 */
send_cpu_time(Faddress, Taddress)
char	*Faddress, *Taddress;
{
	long	status;
	static	int	CpuLength;
	static char	CpuName[31], CpuVersion[9];
	char	*p, line[LINESIZE];
	char	*month[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug",
			    "Sep","Oct","Nov","Dec"};
	char	*weekday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	struct	tm	*tm, *localtime();
	long	clock;

	time(&clock);		/* Get the current time */
	tm = localtime(&clock);

	list[0].length = sizeof CpuName;
	list[0].code = SYI$_HW_NAME;
	list[0].address = CpuName;
	list[0].rtn = &CpuLength;
	list[1].length = sizeof CpuVersion;
	list[1].code = SYI$_VERSION;
	list[1].address = CpuVersion;
	list[1].rtn = 0;
	list[2].length = list[2].code = list[2].address = 0;

	if(((status = sys$getsyiw((int)(0), (int)(0), (int)(0), list,
		(int)(0), (int)(0), (int)(0))) & 0x1) == 0) {
		logger(1, "NMR, can't get CPU type and version\n");
		return;
	}

	CpuName[CpuLength] = '\0';
	CpuVersion[8] = '\0'; if((p = strchr(CpuVersion, ' ')) != NULL) *p = '\0';
	sprintf(line, "%s running VMS-%s, using HUJI-NJE, %-3.3s \
%d-%-3.3s-%d %d:%02d %s\n",
		CpuName, CpuVersion,
		weekday[tm->tm_wday], tm->tm_mday, month[tm->tm_mon],
		tm->tm_year, tm->tm_hour, tm->tm_min, GMT_OFFSET);
	send_nmr(Faddress, Taddress, line, strlen(line), ASCII, CMD_MSG);
}


#else
list_users(Faddress, Taddress, UserName)
char	*Faddress, *Taddress, *UserName;
{
	char	line[LINESIZE];

	sprintf(line, "UNIX does not support commands at this stage");
	send_nmr(Faddress, Taddress, line, strlen(line),
		ASCII, CMD_MSG);
}
#endif

/*
 | Show the route of that node. Show both the permannet route (in table) and
 | the active route (if we have alternate route active).
 */
query_route(Faddress, Taddress, NodeName)
char	*Faddress, *Taddress, *NodeName;
{
	char	line[LINESIZE], PermanentRoute[16], ActiveRoute[16],
		CharacterSet[16], *p;
	int	i;

/* Modify nodename to upper case */
	for(p = NodeName; *p != '\0'; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';

/* Get the active route */
	switch(find_line_index(NodeName, ActiveRoute, CharacterSet)) {
	case NO_SUCH_NODE:	/* Does not exist at all */
		sprintf(line, "Node %s not known", NodeName);
		send_nmr(Faddress, Taddress, line, strlen(line),
			ASCII, CMD_MSG);
		return;
	case ROUTE_VIA_LOCAL_NODE:	/* Not connected via NJE */
		sprintf(line, "Node %s is local", NodeName);
		send_nmr(Faddress, Taddress, line, strlen(line),
			ASCII, CMD_MSG);
		return;
	case LINK_INACTIVE:	/* Both link and alternate routes inactives */
		break;
	default: break;
	}

/* OK - The link is defined. If the node to route through is different than the
   original nodename, then look maybe we have a line with that name which is
   inactive and we got an alternate route.
*/
	sprintf(line, "Node %s is routed via %s", NodeName, ActiveRoute);
	if(compare(NodeName, ActiveRoute) != 0) {
		for(i = 0; i < MAX_LINES; i++) {
			if((IoLines[i].HostName)[0] != '\0') {	/* This line is defined */
				if(compare(IoLines[i].HostName, NodeName) == 0) {
					sprintf(line, "Link %s inactive. Routed via %s",
						NodeName, ActiveRoute);
					break;
				}
			}
		}
	}

	send_nmr(Faddress, Taddress, line, strlen(line),
		ASCII, CMD_MSG);
}
