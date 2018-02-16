/* CLUSTER_SERVER.C	V1.0
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
 |  This module creates a DECnet known task and listens for connections on it.
 | When a connection arrives, a virtual circuit is created... When there is
 | input from it, it is passed to the NJE emulator like any other command from
 | the command's mailbox. However, only non-privileged commands are passed as
 | we can't verify the other side identity.
 |   The default DECnet object number used is 203.
 */
#include <iodef.h>
#include <msgdef.h>	/* For MSG$_xxx definition */
#include <nfbdef.h>	/* For DECnet message codes */
#include "site_consts.h"
#include "consts.h"

#define	CLUSTER_OBJECT_NUMBER	203	/* The DECnet object number */
#define CLUSTER_MAILBOX	"NJE_CLUSTER_MBX"

#define	MAX_SERVERS	10		/* Maximum 10 remote nodes to server */

/* Hold the channel and buffer for each active link: */
globaldef short	ClusterChannels[MAX_SERVERS],
		ClusterIosb[MAX_SERVERS][4];
globaldef char	*ClusterBuffers[MAX_SERVERS];

globaldef short	NETACPclusterChan,	/* To talk with NetACP */
	ClusterMailboxChan,	/* The mailbox on which we receive intrupt
				   when NETACP writes something to our channel */
	ClusterPassiveIosb[4];		/* For the IO of the passive channel */

/*
 | Create a temporary mailbox and assign a channel (associated with this
 | mailbox) to device _NET:. Then ask to declare us as known object number
 | 203, and ask for an AST to be fired when something is written on that
 | mailbox. When the DECnet control program would like to tell us something,
 | it'll write it into the mailbox, and then the mailbox's AST will fire. We
 | then read the mailbox and parse it.
 */
init_cluster_listener()
{
	long	status, i, cluster_connection_accept();
	struct	DESC	MbxDesc, NetDesc,
			ObjDesc;	/* For requesting object from NETACP */
	struct	{
		unsigned char	type;
		long		object;
		} ObjectType;
	char	NET[] = "_NET:";
	short	iosb[4];		/* IOSB for QIO call */

/* Initialize the strucutres holding the static data */
	for(i = 0; i < MAX_SERVERS; i++)
		ClusterChannels[i] = 0;	/* Mark as free */

/* Create the logical name descriptor */
	MbxDesc.address = CLUSTER_MAILBOX;
	MbxDesc.length = strlen(CLUSTER_MAILBOX);
	MbxDesc.type = 0;

/* Create the temporary mailbox. */
	status = sys$crembx((unsigned char)(0), &ClusterMailboxChan, (long)(0),
		(long)(0), (long)(0),
		(long)(0),&MbxDesc);
	if((status & 0x1) == 0) {
		logger(1, "Cluster_Server: Can't create mailbox, status=%d.\n",
			status);
		return;		/* Ignore it */
	}

/* Assign a channel to NETACP */
	NetDesc.address = NET; NetDesc.type = 0;
	NetDesc.length = strlen(NET);
	status = sys$assign(&NetDesc, &NETACPclusterChan, (long)(0), &MbxDesc);
	if((status & 0x1) == 0) {
		logger(1, "CLUTER_SERVER, Can't assign channel to '%s', status=%d\n",
			NET, status);
		sys$dassgn(NETACPclusterChan);
		return;
	}

/* Queue an attention AST to fire when something is written into the mailbox */
	status = sys$qiow((long)(0), ClusterMailboxChan,
			(short)(IO$_SETMODE|IO$M_WRTATTN), iosb,
			(long)(0), (long)(0),
			cluster_connection_accept, (long)(0),
			(long)(3),	/* Access mode */
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "CLUSTER_SERVER: Can't declare AST for NETACP\
 mailbox, status=d^%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(ClusterMailboxChan); sys$dassgn(NETACPclusterChan);
		return;
	}

/* Declare the object to NETACP */
	ObjectType.type = NFB$C_DECLOBJ;
	ObjectType.object = CLUSTER_OBJECT_NUMBER;
	ObjDesc.address = &ObjectType; ObjDesc.type = 0;
	ObjDesc.length = sizeof ObjectType;
	status = sys$qiow((long)(0), NETACPclusterChan,
			(short)(IO$_ACPCONTROL), iosb,
			(long)(0), (long)(0),
			&ObjDesc, (int)(0),
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));

	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "CLUSTER_SERVER, Can't declare NETACP object\
 status=d^%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(ClusterMailboxChan); sys$dassgn(NETACPclusterChan);
		return;
	}
}


/*
 | We are called when there is something written in our mailbox. Read the
 | mailbox, declare immediately another AST for it (since this AST is one
 | time only), and parse the data read from the mailbox.
 | The format of the mailbox is: MSGtype(short), Unit(short), Count(Byte),
 | Name, Count(Byte), Info.
 */
cluster_connection_accept()
{
	short	iosb[4];
	long	status, MessageType, unit, NCBsize, TempVar;
	unsigned char	buffer[LINESIZE],	/* Where to read the message */
			name[LINESIZE],	/* Device name */
			ncb[LINESIZE],	/* Information field */
			*p;

/* Read the mailbox message */
	status = sys$qiow((long)(0), ClusterMailboxChan,
			(short)(IO$_READVBLK), iosb,
			(long)(0), (long)(0),
			buffer, (int)(sizeof buffer),
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));

	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "CLUSTER_SERVER, Can't read DECnet mailbox message,\
 status=d^%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(ClusterMailboxChan); sys$dassgn(NETACPclusterChan);
		return;
	}

/* Parse the mailbox message */
	buffer[iosb[1]] = '\0';
	MessageType = (int)((buffer[1] << 8) + buffer[0]);
	unit = (int)((buffer[3] << 8) + buffer[0]);
	strcpy(name, &buffer[5]); name[buffer[4]] = '\0';
	p = buffer + 5 + buffer[4];
	NCBsize = (int)(*p); *p++;
	memcpy(ncb, p, NCBsize); ncb[NCBsize] = '\0';

#ifdef DEBUG
	logger((int)(4), "CLUSTER_SERVER, Received mailbox message: type=%d, unit=%d\
 name=%s\n",
		MessageType, unit, name);
#endif

/* Reqeue the attention AST on the mailbox */
	status = sys$qiow((long)(0), ClusterMailboxChan,
			(short)(IO$_SETMODE|IO$M_WRTATTN), iosb,
			(long)(0), (long)(0),
			cluster_connection_accept, (long)(0),
			(long)(3),	/* Access mode */
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "CLUSTER_SERVER, Can't declare AST for NETACP\
 mailbox, status=d^%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(NETACPclusterChan); sys$dassgn(NETACPclusterChan);
		return;
	}

/* Parse the message */
	switch(MessageType) {
	case MSG$_NETSHUT:	/* Network shutting down - close our channel */
		logger(1, "CLUSTER_SERVER, DECnet shutting down message.\n");
		sys$dassgn(ClusterMailboxChan); sys$dassgn(NETACPclusterChan);
		return;
	case MSG$_CONNECT:	/* Inbound connection */
		cluster_accept(name, unit, ncb, NCBsize);
		return;
	default:		/* Log it and continue */
		logger(1, "CLUSTER_SERVER, Unrecgnised message type %d received from NETACP\n",
			MessageType);
		return;
	}
}

/*
 | Accept a reuqest from DECnet: Open a channel for it, tell NETACP that we
 | accept it, and queue a receive for it.
 */
cluster_accept(DeviceName, DeviceNumber, ncb, NCBsize)
char	*DeviceName, *ncb;
int	DeviceNumber, NCBsize;
{
	int	i, status, cluster_receive_ast();
	char	device[] = "_NET:";
	struct DESC	DeviceDesc, NCBdesc;
	short	channel, iosb[4];

/* First, look for a free entry */
	for(i = 0; i < MAX_SERVERS; i++)
		if(ClusterChannels[i] == 0) break;
	if(i == MAX_SERVERS) {
		logger(1, "CLUSTER_SERVER, No room for more clients.\n");
		return;
	}

	if((ClusterBuffers[i] = malloc(128)) == NULL) {
		logger(1, "CLUSTER_SERVER, Can't malloc...\n");
		bug_check("Can't Malloc()");
	}

/* Create another channel to _NET: and accept the connection on it */
	DeviceDesc.address = device; DeviceDesc.type = 0;
	DeviceDesc.length = strlen(device);
	status = sys$assign(&DeviceDesc, &channel, (int)(0), (int)(0));
	if((status & 0x1) == 0) {
		logger(1, "CLUSER_SERVER, Can't assign channel to %s, status=%d\n",
			device, status);
		return;
	}

	NCBdesc.address = ncb; NCBdesc.type = 0;
	NCBdesc.length = NCBsize;

	status = sys$qiow((long)(0), channel,
			(short)(IO$_ACCESS), iosb,
			(long)(0), (long)(0),
			(long)(0), &NCBdesc,
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "CLUSTER_SERVER, Can't accept connection, status=%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(channel);
		return;
	}

	ClusterChannels[i] = channel;

/* Queue a receive on this line */
	status = sys$qio((long)(0), channel,
			(short)(IO$_READVBLK), ClusterIosb[i],
			cluster_receive_ast, i,
			ClusterBuffers[i], 128,
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if((status & 0x1) == 0) {
		logger(1, "CLUSTER_SERVER, Can't queue read, status=%d\n",
			status);
		sys$dassgn(channel);
		return;
	}
}

/*
 | Receive AST called when there is an input from some client.
 */
cluster_receive_ast(ClusterIndex)
int	ClusterIndex;
{
	short	*iosb;
	char	*buffer;
	int	status;

	iosb = ClusterIosb[ClusterIndex];
	buffer = ClusterBuffers[ClusterIndex];

/* Check that the received control block is ok: */
	if((iosb[0] & 0x1) == 0) {
		logger(1, "CLUSTER_SERVER, Error reading from DECnet, iosb.status=%d\n",
			(int)(iosb[0]));
		if(ClusterChannels[ClusterIndex] != 0) {
			sys$dassgn(ClusterChannels[ClusterIndex]);	/* Forget it */
			free(ClusterBuffers[ClusterIndex]);
			ClusterChannels[ClusterIndex] = 0;
		}
		return;
	}

	buffer[iosb[1]] = '\0';	/* Delimit line */
/* Pass only messages that can be given by everyone */
	switch(*buffer) {
	case CMD_SEND_MESSAGE:
	case CMD_SEND_COMMAND:
	case CMD_GONE_ADD:
	case CMD_GONE_DEL:
		parse_operator_command(buffer);
		break;
	default:	/* Ignore all other types quietly */
		logger(1, "CLUSTER_SERVER, *** Privileged command received from remote client. Ignored\n");
	}

/* ReQueue a receive on this line */
	status = sys$qio((long)(0), ClusterChannels[ClusterIndex],
			(short)(IO$_READVBLK), iosb,
			cluster_receive_ast, ClusterIndex,
			ClusterBuffers[ClusterIndex], 128,
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if((status & 0x1) == 0) {
		logger(1, "CLUSTER_SERVER, Can't queue read, status=%d\n",
			status);
		return;
	}
}
