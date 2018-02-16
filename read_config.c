/* READ_CONFIG.C	V2.7
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
 | Read our configuration from the configuration file. The file is
 | placed in a well-known location.
 | Each definition in it is one line long. Lines that start with * are comments.
 | Each line starts with  a keyword and parameters. They are separated by
 | white space.
 | The valid definitions are:
 | NAME   our-BITnet-name
 | IPADDRESS   our-IP-address
 | The LINE command is a primary command. The other commands after it are related
 | to the line last used by LINE command.
 | LINE  #  Name       ( Number starts at 0 and up to MAX_LINES-1).
 |    TYPE  DMF / DMB / DSV / EXOS_TCP / MULTINET / DEC_TCP / UNIX_TCP / ASYNC / DECNET
 |    DEVICE  XGA0: / TTA1: / ...
 |    BUFSIZE  Number  (more than 400 and 20 less than MAX_BUF_SIZE)
 |    TOMEOUT  Number  (Timeout in seconds)
 |    IPPORT  Number   (POrt number of IP (in case of IP line).
 |    ACK  NULLS       (Use Null blocks instead of ACK for acks).
 |    DECNETNAME name  (DECnet name of other node in case of DECNET link).
 |    TCP-SIZE size    (MUST for TCP lines - the size of TCP buffer. Not more
 |                      than 8K for EXOS).
 |    DUPLEX FULL/HALF (DMB or DMF lines - half or full duplex; default - full).
 |    AUTO-RESTART     (DMB or DMF lines - if the line enters INACTIVE state,
 |                      try to restart it every 10 minutes).
 |    MAX-STREAMS n    (Maximum number of streams active on this line).
 |
 | QUEUE  File's-queue-directory
 | LOG   log-file-name (fully qualified)
 | LLEVEL - To change the log-level from the default 1.
 | TABLE  full-name-of-routing-table
 | INFORM BITnet-address   (who to infrom when a line change state. Can be
 |                          repeated up to MAX_INFORM -1 times.
 | DMF-FILE  The name of the file holding the DMF address routine.
 | DEFAULT-ROUTE Nodename  - THe name of node to route all unknown traffic.
 | GONE username login-directory - Add this user with this login diurectory
 |                                 to the gone list.
 | CLUSTER Decnet-name  - Defines the DECnet name of the cluster member that
 |                        that is connected to BITnet.
 |
 | Notes for buffer size: Look in MAIN.C notes.
 |
 | V1.1 - Add keyword TCP-SIZE
 | V1.2 - Add keyword INFORM
 | V1,3 - Add field MULTINET in TYPE statement.
 | V1.4 - Add DMF-FILE command.
 | V1.5 - Add the DUPLEX FULL/HALF command.
 | V1.6 - Add the AUTO-RESTART command.
 | V1.7 - Add support for DEC's TcpIp.
 | V1.8 - If the link is of type TCP or DECnet, set the auto-restart flag. This
 |        will cause it to retry failed connection once in a while.
 | V1.9 - Add INTERNETNAME and TCPNAME which are synoyms to DEVICE command.
 |        VMS_TCP and UNIX_TCP will use this field if it contains a value.
 | V2.0 - Remove the XMIT-QUEUE keyword. If the link is of type DECnet or
 |        TCP it sets the reliable bit which means also XMIT-QUEUE.
 | V2.1 - 5/3/90 - Add default route command.
 | V2.2 - 22/3/90 - Add the GONE command.
 | V2.3 - 1/4/90 - Add the CLUSTER command.
 | V2.4 - 7/10/90 - Add the MAX-STREAMS command.
 | V2.5 - 7/3/91 - Add DSV keyword.
 | V2.6 - 11/3/91 - Initialize field FreeStreams in case of MAX-STREAMS keyword.
 | V2.7 - 14/6/91 - REPLACE LOGGER(1) with Logger(2).
 */
#include <stdio.h>
#include "consts.h"

EXTERNAL struct	LINE	IoLines[MAX_LINES];
EXTERNAL int	LogLevel;	/* Defined in MAIN.C */

/*
 | Open the configuration file. Read it and parse it. If there are problems,
 | print them to standard output and return 0 status. Otherwise return status
 | of 1.
 | The followings are flags used to determine whether we read all parameters.
 | They are it masks, and we finaly check that they are all there.
 */
#define	NAME		0x001
#define	IPADDRESS	0x002
#define	LINE		0x004
#define	QUEUE		0x008
#define	LOG		0x010
#define	TABLE		0x020
#define	ALL_FOUND	0x03f	/* The sum of all the above */

read_configuration()
{
	long	i, ThingsRead, NumParams, LineNumber, BufSize, TimeOut;
	FILE	*fd;
	char	line[LINESIZE], KeyWord[80], param1[80], *p,
		param2[80], param3[80], *fgets(), *strchr();

	if((fd = fopen(CONFIG_FILE, "r")) == NULL) {
		printf("Can't open configuration file named '%s'\n",
			CONFIG_FILE);
		return 0;
	}

	ThingsRead = 0;		/* Nothing read */
	LineNumber = -1;	/* No line specified yet */
	*ADDRESS_FILE = '\0';	/* No DMF address file yet */
	*DefaultRoute = '\0';	/* No default route yet. */
	*ClusterNode = '\0';	/* No cluster support */

	while(fgets(line, sizeof line, fd) != NULL) {
		if(*line == '*') continue;	/* Comment */
		if((p = strchr(line, '\n')) != NULL) *p = '\0';
		p = line;
		while((*p == ' ') || (*p == '\t')) *p++;
		if(*p == '\0') continue;	/* Empty line */
		NumParams = sscanf(p, "%s %s %s %s", KeyWord,
			param1, param2, param3);

		if(compare(KeyWord, "NAME") == 0) {
			if(strlen(param1) > 8) {
				printf("BITnet name '%s' too long\n", param1);
				return 0;
			}
			strncpy(LOCAL_NAME, param1, sizeof LOCAL_NAME);
			ThingsRead |= NAME;
		}
		else
		if(compare(KeyWord, "IPADDRESS") == 0) {
			strncpy(IP_ADDRESS, param1, sizeof IP_ADDRESS);
			ThingsRead |= IPADDRESS;
		}
		else
		if(compare(KeyWord, "DMF-FILE") == 0) {
			strncpy(ADDRESS_FILE, param1, sizeof ADDRESS_FILE);
		}
		else
		if(compare(KeyWord, "DEFAULT-ROUTE") == 0) {
			strncpy(DefaultRoute, param1, sizeof DefaultRoute);
		}
		else
		if(compare(KeyWord, "LLEVEL") == 0) {
			switch(*param1) {
			case '1': LogLevel = 1; break;
			case '2': LogLevel = 2; break;
			case '3': LogLevel = 3; break;
			case '4': LogLevel = 4; break;
			case '5': LogLevel = 5; break;
			default: printf("Illegal loglevel=%s\n", param1);
				 return 0;
			}
		}
		else
		if(compare(KeyWord, "INFORM") == 0) {
			if(strlen(param1) >= 20) {	/* Too long... */
				printf("Too long address in: '%s'\n", line);
				return 0;
			}
			if(InformUsersCount >= MAX_INFORM) {
				printf("No space for INFORM. Raise MAX_INFORM\n");
				return 0;
			}
			strncpy(InformUsers[InformUsersCount++], param1,
				sizeof InformUsers[0]);
		}
		else
		if(compare(KeyWord, "GONE") == 0) {
			sprintf(line, "%s %s", param1, param2);
			add_gone_user(line);
		}
		else
		if(compare(KeyWord, "CLUSTER") == 0) {
			strncpy(ClusterNode, param1, sizeof ClusterNode);
		}
		else
		if(compare(KeyWord, "LINE") == 0) {
			if(NumParams < 3) {
				printf("Too less parameters in '%s'\n", line);
				return 0;
			}
			if(sscanf(param1, "%d", &LineNumber) != 1) {
				printf("Illegal line number '%s'\n", param1);
				return 0;
			}
			if((LineNumber < 0) || (LineNumber >= MAX_LINES)) {
				printf("Out of range line number %d\n", LineNumber);
				return 0;
			}
			strncpy(IoLines[LineNumber].HostName, param2,
				sizeof IoLines[0].HostName);
			IoLines[LineNumber].state = ACTIVE;
			*IoLines[LineNumber].device = '\0';	/* For TCP */
			IoLines[LineNumber].flags = 0;
			IoLines[LineNumber].MaxStreams = 1;	/* Default = one stream */
		}
		else
		if(compare(KeyWord, "BUFSIZE") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			sscanf(param1, "%d", &BufSize);
			IoLines[LineNumber].PMaxXmitSize = BufSize;
		}
		else
		if(compare(KeyWord, "TCP-SIZE") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			sscanf(param1, "%d", &BufSize);
			IoLines[LineNumber].TcpXmitSize = BufSize;
		}
		else
		if((compare(KeyWord, "DEVICE") == 0) ||
		   (compare(KeyWord, "TCPNAME") == 0) ||
		   (compare(KeyWord, "DECNETNAME") == 0) ||
		   (compare(KeyWord, "INTERNETNAME") == 0)) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			strncpy(IoLines[LineNumber].device, param1,
				sizeof IoLines[0].device);
		}
		else
		if(compare(KeyWord, "IPPORT") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			if(sscanf(param1, "%d", &i) != 1) {
				printf("Illegal line '%s'\n", line);
				return 0;
			}
			IoLines[LineNumber].IpPort = i;
		}
		else
		if(compare(KeyWord, "TIMEOUT") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			if(sscanf(param1, "%d", &TimeOut) != 1) {
				printf("Illegal line '%s'\n", line);
				return 0;
			}
			IoLines[LineNumber].TimeOut = TimeOut;
		}
		else
		if(compare(KeyWord, "AUTO-RESTART") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			IoLines[LineNumber].flags |= F_AUTO_RESTART;
		}
		else
		if(compare(KeyWord, "DUPLEX") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			if(compare(param1, "FULL") == 0)
				IoLines[LineNumber].flags &= ~F_HALF_DUPLEX;
			else
			if(compare(param1, "HALF") == 0)
				IoLines[LineNumber].flags |= F_HALF_DUPLEX;
			else
				printf("Illegal DUPLEX command\n");
		}
		else
		if(compare(KeyWord, "ACK") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			if(compare(param1, "NULLS") == 0) {
				IoLines[LineNumber].flags |= F_ACK_NULLS;
			}
			else {
				printf("Illegal line '%s'\n", line);
				return 0;
			}
		}
		else
		if(compare(KeyWord, "MAX-STREAMS") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			if(sscanf(param1, "%d", &IoLines[LineNumber].MaxStreams) != 1) {
				printf("Illegal MAX-STREAM command: '%s'\n", line);
			}
			if((IoLines[LineNumber].MaxStreams < 1) ||
			   (IoLines[LineNumber].MaxStreams > 7)) {
				printf("Line %d, MAX-STREAMS out of range\n", LineNumber);
				IoLines[LineNumber].MaxStreams = 1;
			}
			IoLines[LineNumber].FreeStreams = IoLines[LineNumber].MaxStreams;
		}
		else
		if(compare(KeyWord, "TYPE") == 0) {
			if(LineNumber == -1) {
				printf("Misplaced subcommand '%s'\n", KeyWord);
				return 0;
			}
			if(compare(param1, "DMF") == 0)
				IoLines[LineNumber].type = DMF;
			else
			if(compare(param1, "DMB") == 0)
				IoLines[LineNumber].type = DMB;
			else
			if(compare(param1, "DSV") == 0)
				IoLines[LineNumber].type = DSV;
			else
			if(compare(param1, "EXOS_TCP") == 0)
				IoLines[LineNumber].type = EXOS_TCP;
			else
			if(compare(param1, "MULTINET") == 0)
				IoLines[LineNumber].type = MNET_TCP;
			else
			if(compare(param1, "DEC_TCP") == 0)
				IoLines[LineNumber].type = DEC__TCP;
			else
			if(compare(param1, "UNIX_TCP") == 0)
				IoLines[LineNumber].type = UNIX_TCP;
			else
			if(compare(param1, "DECNET") == 0)
				IoLines[LineNumber].type = DECNET;
			else
			if(compare(param1, "ASYNC") == 0)
				IoLines[LineNumber].type = ASYNC;
			else {
				printf("Illegal line type in '%s'\n", line);
				return 0;
			}
			ThingsRead |= LINE;	/* We must read at least one line */
			/* If the line is of relyable type, set the auto-restart flag */
			switch(IoLines[LineNumber].type) {
			case DECNET:
			case EXOS_TCP:
			case MNET_TCP:
			case UNIX_TCP:
			case DEC__TCP:
				IoLines[LineNumber].flags |=
					(F_AUTO_RESTART | F_RELIABLE | F_XMIT_QUEUE);
				break;
			default:;
			}
		}
		else
		if(compare(KeyWord, "QUEUE") == 0) {
			strncpy(BITNET_QUEUE, param1, sizeof BITNET_QUEUE);
			ThingsRead |= QUEUE;
		}
		else
		if(compare(KeyWord, "LOG") == 0) {
			strncpy(LOG_FILE, param1, sizeof LOG_FILE);
			ThingsRead |= LOG;
		}
		else
		if(compare(KeyWord, "TABLE") == 0) {
			strncpy(TABLE_FILE, param1, sizeof TABLE_FILE);
			ThingsRead |= TABLE;
		}
		else {
			printf("Illegal keyword in line '%s'\n", line);
			return 0;
		}
	}
	fclose(fd);

/* Check whether we read them all */
	if(ThingsRead != ALL_FOUND) {
		printf("Not all parameters read, Read mask=x^%x\n", ThingsRead);
		return 0;
	}

/* Now log our parameters in  the file */
	logger(2, "HUJI-NJE initialization parameters:\n");
	logger(2, "    Our name='%s', IP address=%s\n",
			LOCAL_NAME, IP_ADDRESS);
	logger(2, "    BITnet queue is '%s'\n", BITNET_QUEUE);
	logger(2, "    Routing table is '%s'\n",TABLE_FILE);
	if(*DefaultRoute != '\0')
		logger(2, "    Using default route to %s\n", DefaultRoute);
	if(*ClusterNode != '\0')
		logger(2, "CLUSTER software is enabled. Cluster center=%s\n",
				ClusterNode);
	for(i = 0; i < MAX_LINES; i++)
		logger(2,
			"        Line #%d %s Device=%s, bufsize=%d, TimeOut=%d, \
Max streams=%d\n",
			i, IoLines[i].HostName, IoLines[i].device,
			IoLines[i].PMaxXmitSize,
			IoLines[i].TimeOut,
			IoLines[i].MaxStreams);

	return 1;	/* All ok */
}
