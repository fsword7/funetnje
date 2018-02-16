/* VMS_TCP.C (formerly VMS_EXOS)	V3.1
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
 | NOTE: Although this module supports both MultiNet and EXOS, only one
 |       package is supported at a time (if the machine runs both of them,
 |       the NJE can use only one).
 | Works according to VMnet specs (version 2) except the following:
 |    1. We accept FAST OPEN bu do not generate it.
 |
 | For future work: When writing to EXOS, we don't start a timer. We should,
 | in case of a very slow line.
 | When a connection is accepted, we try to read the control block in one
 | read. Should check the log for errors with this. If there are too much
 | times that a partial block is read, we should change it.
 | The handling of errors here is simple. Usually an error will result by
 | the other side (or our kernel) closing the channel, so we rely on it.
 | When a link is broken, we try to restart it.
 |
 | In order to not change the upper layers too much, we queue a timeout on
 | the channel (done by the timer routine in module VMS.C). When it expires,
 | we simulate an ack if the link is idle, and
 | requeue the timeout. When a write to exos is done, we also simulate an
 | ack (only if the line is in ACTIVE state) to send the next buffer.
 |  The buffers are kept from overflowing by using the F_WAIT_V_A_BIT flag.
 | It is used much like the Wait-a-Bit flag, but we don't have to change the
 | BCB in order to set our private flag.
 |
 | When we need to close a line, we set its state to INACTIVE and call
 | Retstart-Channel; it'll requeue files in transit and call CLOSE_EXOS_CHAN
 | (because of the INACTIVE state). This last routine will deactivate the line
 | and place it into LISTEN or RETRYING state (depending on the Secondary or
 | Primary state). A timer routine will try to restart the Retry lines once
 | every 5 minutes.
 |
 | V1.1 - Convert to VMNET version 2.
 | V1.2 - Block multiple blocks into one VMNET transmission block.
 | V1.3 - Move the VMNET outgoing-xmission protocol handler to IO.C
 | V1.4 - Correct an error during close_exos_chan. If we receive an incorrect
 |        vmnet record, close the static channel, not one of a specific line.
 | V1.5 - Add support for multinet. Change close-exos-chan function name
 |        to close-tcp-chan.
 | V1.6 - Add a query to MultiNet name server.
 | V1.7 - Correct a bug in accept_vmnet_control routine. It used by mistake the
 |        line's index as a flag to point to the TcpIp package used. Inserted
 |        a global flag which receives its value in Init-Passive_TCP-end.
 | V1.8 - Change the buffer management in sending. Instead of using DONT_ACK
 |        flag, we use the WAIT-a-BIT flag. After all buffers were sent, we
 |        clear it.
 | V1.9 - Do a conditional compliation according to EXOS ot MULTINET definition.
 | V2.0 - When looping over the received buffer, check that the size in TTR
 |        is not longer than the remaining buffer length.
 | V2.1 - Add support for DEC's TcpIp (UCX). To compile with it, you should
 |        define the symbol DEC_TCP. SInce it is quite close to the MultiNet
 |        behaviour, most of the Multinet routines are used for the DEC TcpIp
 |        also.
 | V2.2 - Correct a bug handling VMnet initiating record. If a line was defined
 |        as primary but received a connection it went into a loop of Restart-chan.
 | V2.3 - Change the way we handle auto retsarts with TCP lines. Instead of
 |        the closing routine queueing a retstart, there is a scan every 5 minutes
 |        to detect such dead lines and restart them if the AUTO-RESTART flag
 |        is on.
 | V2.4 - 10/2/90 - Correct a bug in Accept-Vmnet_record; when the line was
 |        in the wrong state, we did restart channel and continued as the
 |        line was in the correct state. We now go to the error-handler
 |        (Retry Connection).
 | V2.5 - 14/2/90 - Change SWAP_xxx to htonns and others. Also, if there is
 |        a value in Device field, use it to get the IP address instead of the
 |        hostname.
 | V2.6 - 19/2/90 - 1. When completing a connection creation, do not send ENQ
 |        but send SYN-NAK. This is because CTC behaves so.
 |        Accept_Vmnet_control_record - Return a reasonable reason code when
 |        sending NAK.
 | V2.7 - 20/2/90 - When sending VMnet's ACK record put zero in the reason code
 |        instead of 1. This is in order to bypass a UREP implementation bug.
 | V2.8 - 21/2/90 - Replace calls to BCOPY with calls to memcpy().
 | V2.9 - 22/3/90 - Correct a bug in Accept_vmnet_record(). If there was an
 |        error during accept, we deassigned by mistake PassiveChannel instead
 |        of ManetAcceptChannel
 | V3.0 - 11/2/91 - Minor fixes - Check write status and fix minor UCX problems.
 | V3.1 - 14/6/91 - Replace some Logger(1) with Logger(2) to make the program
 |        less verbose.
 */
#include <stdio.h>
#include <errno.h>	/* For Free and Malloc error codes */
#include "consts.h"
#include "headers.h"
#ifdef EXOS
#include "exos.h"	/* TcpIp definitions */
#endif
#ifdef MULTINET
#include "multinet.h"
#include <iodef.h>
#endif
#ifdef DEC_TCP
#include <ucx$inetdef.h>
#include <iodef.h>
#endif

EXTERNAL struct LINE	IoLines[MAX_LINES];
EXTERNAL struct	ENQUIRE	Enquire;

/* For the passive general accept() command */
#ifdef EXOS
static	struct	SOioctl	PassiveLocalSocket, PassiveRemoteSocket;	/* For EXOS */
#endif
#ifdef MULTINET
static	struct	sockaddr PassiveSocket;	/* For MultiNet */
#endif
#ifdef DEC_TCP
static	struct	sockaddr PassiveLocalSocket, PassiveRemoteSocket;
struct itm_list {
	int	length;
	struct	sockaddr	*hst;
	};
struct itm_list_1 {
	int	length;
	struct	sockaddr	*hst;
	long	*rtn;
	};
#endif

static	short	PassiveChannel, MnetAcceptChannel;
static	short	PassiveIosb[4], MnetAcceptIosb[4];
static	struct	VMctl	ControlBlock;
static	short	TcpIpType;	/* What package is used for the passive TcpIp side */

#define	VMNET_IP_PORT	175
#define	ETIMEDOUT	60	/* Connection request timedout */

#define	EXPLICIT_ACK	0


/*
 | OPen a channel to the TCP package (EXOS or MultiNet or UCX). If we can't
 | create the connection the line is palced in RETRYING state for later retrial.
 | If we can't find the hosts name, we place the line in LISTEN mode, which means
 | it'll only accept connections but we won't try active open for it.
 */
init_active_tcp_connection(Index)
int	Index;
{
#ifdef EXOS
	struct	SOioctl *LocalSocket, *RemoteSocket;	/* EXOS */
#endif
	struct	sockaddr *MNsocket;	/* MultiNet */
#ifdef DEC_TCP
	struct	sockaddr *LocalSocket, *RemoteSocket;	/* DEC tcp */
	struct	itm_list	local_host_list, remote_host_list;
	short	sock_option[2];
#endif
	struct	LINE	*temp;
	char	DevName[16],	/* EXA0: or INET0: */
		HostName[64];	/* For temporary usage */
	struct	{			/* Descriptor */
		short	length;
		short	filler;
		char	*address;
		} Device;
	long	i, status;
	int	tcp_connect_ast();
	short	IOctlBuffer;

	temp = &(IoLines[Index]);
	temp->state = INACTIVE;	/* No connection yet */
	temp->RecvSize = 0;	/* Empty buffer */
	temp->TcpState = 0;

/* Assign a channel to device */
	if(temp->type == EXOS_TCP)
		strcpy(DevName, "EXA0:");
	else
	if(temp->type == MNET_TCP)
		strcpy(DevName, "INET0:");
	else
	if(temp->type == DEC__TCP)
		strcpy(DevName, "BG0:");
	else
		strcpy(DevName, "***");	/* Just put there garbage so $ASSIGN will fail */

	Device.address = DevName; Device.length = strlen(DevName);
	if(((status = sys$assign(&Device, &(temp->channel),
		(long)(0), (long)(0))) & 0x1) == 0) {
		logger(1,
			"VMS_TCP, Can't assign channel to %s: $ASSIGn status=%d\n",
			DevName, status);
		temp->state = INACTIVE;	/* Disable the line */
		return;
	}

	if(temp->type == EXOS_TCP) {
#ifdef EXOS
	/* Get some memory space for the sockets structures */
		LocalSocket = &(temp->LocalSocket);
		RemoteSocket = &(temp->RemoteSocket);

	/* get the local socket: */
		LocalSocket->hassa = 0;		/* No local port preferation */
		LocalSocket->options = (SO_LARGE);
		LocalSocket->hassp = 1;		/* Define protocol */
		(LocalSocket->sp).sp_family = AF_INET;
		(LocalSocket->sp).sp_protocol = IPPROTO_TCP;
		LocalSocket->type = SOCK_STREAM;

	/* Remote Socket, fill the connection data: */
		RemoteSocket->hassa = 1;		/* We give the remote address */
		(RemoteSocket->sa).sin.sin_family = AF_INET;
		(RemoteSocket->sa).sin.sin_port = htons(temp->IpPort);
		if(*temp->device != '\0')
			strcpy(HostName, temp->device);
		else	strcpy(HostName, temp->HostName);
		i = get_e_host_ip_address(HostName);
		(RemoteSocket->sa).sin.sin_addr.S_un.S_addr = htonl(i);
		RemoteSocket->hassp = 0;		/* Not needed here */
		RemoteSocket->type = 0;
		RemoteSocket->options = SO_LARGE;

/* Check whether we got the address correctly */
		if((RemoteSocket->sa).sin.sin_addr.S_un.S_addr == -1) {
			logger(2, "VMS_TCP, Deactivating line #%d\n", Index);
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = LISTEN;	/* So the passive open will succeed */
			return;
		}

/* Now, request a local socket */
		status = sys$qiow((long)(0), temp->channel, (short)(EX__SOCKET),
				  temp->iosb, (long)(0), (long)(0),
				  (long)(0), (long)(0), LocalSocket, (long)(0),
				  (long)(0), (long)(0));
		if(((status & 0x1) == 0) || (((temp->iosb)[0] & 0x1) == 0)) {
			logger(1,
				"VMS_TCP, Requesting local socket error: line=%d, $QIOW\
status=d^%d, iosb.status=d^%d, iosb.exos_stat=d^%d\n",
				Index, status, (temp->iosb)[0], ((temp->iosb)[2] >> 8));
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = INACTIVE;	/* Disable the line */
			return 0;
		}

/* Now, connect to the remote socket */
		temp->state = TCP_SYNC;	/* we posted a connect but not accepted  yet */
		status = sys$qio((long)(0), temp->channel, (short)(EX__CONNECT),
			  temp->iosb, tcp_connect_ast, Index,
			  (long)(0), (long)(0), RemoteSocket,
			  (long)(0), (long)(0), (long)(0));
		if((status & 0x1) == 0) {
			logger((int)(2), "VMS_TCP, Requesting remote socket\
 error: line=%d, address(%s), $QIO status=d^%d\n",
				Index, temp->HostName, status);
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = INACTIVE;	/* Disable the line */
			return 0;
		}
		return;
#endif
	}

	else		/* MultiNet */
	if(temp->type == MNET_TCP) {
#ifdef MULTINET
		/* Request a local socket */
		status = sys$qiow((long)(0), temp->channel, (short)(IO$_SOCKET),
				  temp->iosb, (long)(0), (long)(0),
				  (int)(AF_INET), (int)(SOCK_STREAM), (long)(0),
				  (long)(0), (long)(0), (long)(0));
		if(((status & 0x1) == 0) || (((temp->iosb)[0] & 0x1) == 0)) {
			logger(1,
				"VMS_TCP, Requesting local socket error: line=%d, $QIOW\
status=d^%d, errno=d^%d\n",
				Index, status, (temp->iosb)[0],
					(((temp->iosb)[0] & 0x7fff) / 8));
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = INACTIVE;	/* Disable the line */
			return 0;
		}

		/* Remote socket */
		MNsocket = &(temp->MNsocket);
		MNsocket->sa_family = AF_INET;
		MNsocket->sa_port = htons(temp->IpPort);
		if(*temp->device != '\0')
			strcpy(HostName, temp->device);
		else	strcpy(HostName, temp->HostName);
		i = get_m_host_ip_address(HostName);
		MNsocket->sa_addr = htonl(i);

/* Check whether we got the address correctly */
		if(MNsocket->sa_addr == -1) {
			logger(2, "VMS_TCP, Deactivating line #%d\n", Index);
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = LISTEN;	/* For the passive open */
			return;
		}

		temp->state = TCP_SYNC;	/* we posted a connect but not accepted  yet */
		status = sys$qio((long)(0), temp->channel, (short)(IO$_CONNECT),
			  temp->iosb, tcp_connect_ast, Index,
			  MNsocket, sizeof(struct sockaddr), (long)(0),
			  (long)(0), (long)(0), (long)(0));
		if((status & 0x1) == 0) {
			logger((int)(2), "VMS_TCP, Requesting remote socket\
 error: line=%d, address(%s), $QIO status=d^%d\n",
				Index, temp->HostName, status);
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = INACTIVE;	/* Disable the line */
			return 0;
		}
		return;
#endif
	}
	else
	if(temp->type == DEC__TCP) {
#ifdef DEC_TCP
/* Get a socket */
		RemoteSocket = &(temp->RemoteSocket);
		LocalSocket = &(temp->LocalSocket);
		sock_option[0] = INET$C_TCP;
		sock_option[1] = INET_PROTYP$C_STREAM;

		LocalSocket->inet_family = INET$C_AF_INET;
		LocalSocket->inet_port = 0;	/* Get any free port */
		LocalSocket->inet_adrs = 0;	/* Local address */
		local_host_list.length = sizeof(struct sockaddr);
		local_host_list.hst = LocalSocket;

		status = sys$qiow(0,temp->channel,IO$_SETMODE,temp->iosb,0,0,
			sock_option, 0, &local_host_list,
			0,0,0);
		if(((status & 0x1) == 0) || ((temp->iosb[0] & 0x1) == 0)) {
			logger(1,
				"VMS_TCP, Requesting local socket error: \
line=%d, $QIOW status=d^%d, iosb=%d\n",
				Index, status, (temp->iosb)[0]);
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = INACTIVE;	/* Disable the line */
			return 0;
		}

/* Remote Socket: */
		RemoteSocket->inet_family = INET$C_AF_INET;
		RemoteSocket->inet_port = htons(temp->IpPort);
		if(*temp->device != '\0')
			strcpy(HostName, temp->device);
		else	strcpy(HostName, temp->HostName);
		i = get_m_host_ip_address(HostName);
		RemoteSocket->inet_adrs = htonl(i);
/* Check whether we got the address correctly */
		if(RemoteSocket->inet_adrs == -1) {
			logger(2, "VMS_TCP, Deactivating line #%d\n", Index);
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = LISTEN;	/* For the passive open to succeed */
			return;
		}


		remote_host_list.length = sizeof(struct sockaddr);
		remote_host_list.hst = RemoteSocket;
/* Now, connect to the remote socket */
		temp->state = TCP_SYNC;	/* we posted a connect but not accepted  yet */
		status = sys$qio(0,temp->channel,IO$_ACCESS,
			  temp->iosb, tcp_connect_ast, Index,
			  0,0, &remote_host_list, 0,0,0);
		if((status & 0x1) == 0) {
			logger((int)(2), "VMS_TCP, Requesting remote socket\
 error: line=%d, address(%s), $QIO status=d^%d, iosb=%d\n",
				Index, temp->HostName, status, temp->iosb[0]);
			sys$dassgn(temp->channel);	/* Deassign channel */
			temp->state = INACTIVE;	/* Disable the line */
			return 0;
		}
		return;
#endif
	}
	else {
		logger(1, "VMS_TCP, Undefined line type #%d\n", temp->type);
		bug_check("HUJI-NJE, Fatal error in TCP module. Aborting...\n");
	}
}


/*
 | Create a passive end to listen on  the VMnet port. When some Accept()
 | arrives, we'll look for its line.
 */
init_passive_tcp_connection(TcpFlag)
int	TcpFlag;	/* EXOS or MultiNet */
{
	char	DevName[16];
	struct	{			/* Descriptor */
		short	length;
		short	filler;
		char	*address;
		} Device;
	long	status;
	int	exos_accept_ast(), mnet_passive_accept();
#ifdef DEC_TCP
	struct	itm_list	local_host_list;
	static struct	itm_list_1	remote_host_list;
	static int	RemoteSocketLength;
	short	sock_option[2];
	int	DEC_passive_accept();
#endif

	TcpIpType = TcpFlag;	/* The package used for passive end */
/* Assign a channel to device */
	if(TcpFlag == MNET_TCP)
		strcpy(DevName, "INET0:");
	else
	if(TcpFlag == EXOS_TCP)
		strcpy(DevName, "EXA0:");
	else
	if(TcpFlag == DEC__TCP)
		strcpy(DevName, "BG0:");
	else {
		logger(1, "VMS_TCP, Illegal TCP package code %d\n", TcpFlag);
		bug_check("TcpIp module error");
	}

	Device.address = DevName; Device.length = strlen(DevName);
	Device.filler = 0;
	PassiveChannel = 0;
	if(((status = sys$assign(&Device, &PassiveChannel,
		(long)(0), (long)(0))) & 0x1) == 0) {
		logger(1,
			"VMS_TCP, Can't assign channel to %s: $ASSIGn status=%d\n",
			DevName, status);
		return;
	}

/* get the local socket: */
	if(TcpFlag == EXOS_TCP) {
#ifdef EXOS
		PassiveLocalSocket.hassa = 1;		/* We give the remote address */
		(PassiveLocalSocket.sa).sin.sin_family = AF_INET;
		(PassiveLocalSocket.sa).sin.sin_port = htons(VMNET_IP_PORT);
		(PassiveLocalSocket.sa).sin.sin_addr.S_un.S_addr = 0;
		PassiveLocalSocket.options = (SO_ACCEPTCONN | SO_LARGE);
		PassiveLocalSocket.hassp = 1;		/* Define protocol */
		(PassiveLocalSocket.sp).sp_family = AF_INET;
		(PassiveLocalSocket.sp).sp_protocol = IPPROTO_TCP;
		PassiveLocalSocket.type = SOCK_STREAM;

/* Remote Socket: */
		PassiveRemoteSocket.hassa = 1;	/* We want to get the address of caller */
		(PassiveRemoteSocket.sa).sin.sin_family = 0;
		(PassiveRemoteSocket.sa).sin.sin_port = 0;
		(PassiveRemoteSocket.sa).sin.sin_addr.S_un.S_addr = 0;
		PassiveRemoteSocket.hassp = 0;		/* Not needed here */
		PassiveRemoteSocket.type = 0;
		PassiveRemoteSocket.options = SO_LARGE;

	/* Now, request a local socket */
		status = sys$qiow((long)(0), PassiveChannel, (short)(EX__SOCKET),
				  PassiveIosb, (long)(0), (long)(0),
				  (long)(0), (long)(0), &PassiveLocalSocket, (long)(0),
				  (long)(0), (long)(0));
		if(((status & 0x1) == 0) || (((PassiveIosb)[0] & 0x1) == 0)) {
			logger(1,
				"VMS_TCP, Requesting local socket error: $QIOW\
status=d^%d, iosb.status=d^%d, iosb.exos_stat=d^%d\n",
				status, PassiveIosb[0], (PassiveIosb[2] >> 8));
			sys$dassgn(PassiveIosb);	/* Deassign channel */
			return 0;
		}

/* Post an accept for the call */
		status = sys$qio((long)(0), PassiveChannel, (short)(EX__ACCEPT),
			  PassiveIosb, exos_accept_ast, (int)(0),
			  (long)(0), (long)(0), &PassiveRemoteSocket,
			  (long)(0), (long)(0), (long)(0));
		if((status & 0x1) == 0) {
			logger(1, "VMS_TCP, Requesting accept error:\
 $QIO status=d^%d\n",
				status);
			sys$dassgn(PassiveChannel);	/* Deassign channel */
			return 0;
		}
		return 1;
#endif
	}

	else
	if(TcpFlag == MNET_TCP) {		/* Multinet */
#ifdef MULTINET
		/* Request a local socket */
		status = sys$qiow((long)(0), PassiveChannel, (short)(IO$_SOCKET),
				  PassiveIosb, (long)(0), (long)(0),
				  (int)(AF_INET), (int)(SOCK_STREAM), (long)(0),
				  (long)(0), (long)(0), (long)(0));
		if(((status & 0x1) == 0) || (((PassiveIosb)[0] & 0x1) == 0)) {
			logger(1,
				"VMS_TCP, Requesting local socket error: $QIOW\
status=d^%d, errno=d^%d\n",
				status, (PassiveIosb)[0],
					(((PassiveIosb)[0] & 0x7fff) / 8));
			sys$dassgn(PassiveChannel);	/* Deassign channel */
			return 0;
		}

		/* Bind a name to it */
		PassiveSocket.sa_family = AF_INET;
		PassiveSocket.sa_port = htons(VMNET_IP_PORT);
		PassiveSocket.sa_addr = 0;	/* Local machine */
		status = sys$qiow((long)(0), PassiveChannel, (short)(IO$_BIND),
				  PassiveIosb, (long)(0), (long)(0),
				  &PassiveSocket, sizeof(struct sockaddr),
				(long)(0), (long)(0), (long)(0), (long)(0));
		if(((status & 0x1) == 0) || (((PassiveIosb)[0] & 0x1) == 0)) {
			logger(1,
				"VMS_TCP, Can't bind; $QIOW\
status=d^%d, errno=d^%d\n",
				status, (PassiveIosb)[0],
					(((PassiveIosb)[0] & 0x7fff) / 8));
			sys$dassgn(PassiveChannel);	/* Deassign channel */
			return 0;
		}

		/* Call listen on it */
		status = sys$qiow((long)(0), PassiveChannel, (short)(IO$_LISTEN),
				  0, (long)(0), (long)(0),
				  (long)(1), (long)(0),
				(long)(0), (long)(0), (long)(0), (long)(0));
		/* Queue accept wait */
		status = sys$qio((long)(0), PassiveChannel,
				(short)(IO$_ACCEPT_WAIT),
				  PassiveIosb, mnet_passive_accept, (long)(0),
				  (long)(0), (long)(0),
				(long)(0), (long)(0), (long)(0), (long)(0));
		if((status & 0x1) == 0) {
			logger(1,
				"VMS_TCP, Can't accept-wait; $QIOW status=d^%d\n",
					status);
			sys$dassgn(PassiveChannel);	/* Deassign channel */
			return 0;
		}
#endif
	}
	else
	if(TcpFlag == DEC__TCP) {
#ifdef DEC_TCP
/* Get a socket */
		sock_option[0] = INET$C_TCP;
		sock_option[1] = INET_PROTYP$C_STREAM;

		PassiveLocalSocket.inet_family = INET$C_AF_INET;
		PassiveLocalSocket.inet_port =
			htons(VMNET_IP_PORT);
		PassiveLocalSocket.inet_adrs = 0;	/* Local address */
		local_host_list.length = sizeof(struct sockaddr);
		local_host_list.hst = &PassiveLocalSocket;
		status = sys$qiow(0,PassiveChannel,IO$_SETMODE,
			PassiveIosb,0,0,
			sock_option, (1 << 24 | UCX$M_REUSEADDR),
			&local_host_list, 2,	/* Maximum queue of 2 */
			0,0);
		if(((status & 0x1) == 0) || ((PassiveIosb[0] & 0x1) == 0)) {
			logger(1,
				"VMS_TCP, Requesting local socket error (Listen): \
$QIOW status=d^%d, iosb=%d\n",
				status, PassiveIosb[0]);
			sys$dassgn(PassiveChannel);	/* Deassign channel */
			return 0;
		}

/* We need another channel to another BG: device for accept */
		Device.address = DevName; Device.length = strlen(DevName);
		Device.filler = 0;
		if(((status = sys$assign(&Device, &MnetAcceptChannel,
			(long)(0), (long)(0))) & 0x1) == 0) {
			logger(1,
				"VMS_TCP, Can't assign channel to %s: $ASSIGn status=%d\n",
				DevName, status);
			return;
		}

/* Remote Socket: */
		remote_host_list.length = sizeof(struct sockaddr);
		remote_host_list.hst = &PassiveRemoteSocket;
		remote_host_list.rtn = &RemoteSocketLength;
/* Now, queue an accept */
		status = sys$qio(0,PassiveChannel, (IO$_ACCESS | IO$M_ACCEPT),
			  PassiveIosb, DEC_passive_accept, 0,
			  0,0, &remote_host_list, &MnetAcceptChannel, 0,0);
		if((status & 0x1) == 0) {
			logger(1, "VMS_TCP, Trying accept \
error: $QIO status=d^%d\n",
				status);
			sys$dassgn(MnetAcceptChannel);
			sys$dassgn(PassiveChannel);
			return 0;
		}
#endif
	}
}


/*
 | This AST routine is posted when the cnnection is either accepted or the
 | request fail. Check what happened - If the connection was accepted, start it.
 | if rejected - close the channel and requeue it for later retrial.
 */
tcp_connect_ast(Index)
int	Index;
{
	struct	LINE	*temp;
	register int	i, TempVar, status;

	temp = &(IoLines[Index]);

	if((((temp->iosb)[0]) & 0x1) != 0) {	/* Connection accepted */
#ifdef DEBUG
		logger((int)(3), "VMS_TCP: Connect succeeded on line #%d\n", Index);
#endif
		temp->state = TCP_SYNC;	/* Expecting reply for the control block */
		queue_receive(Index);	/* Queue receive on it */
/* Send the first control block */
		ASCII_TO_EBCDIC("OPEN", ControlBlock.type, 4);
		PAD_BLANKS(ControlBlock.type, 4, 8);
		memcpy(ControlBlock.Rhost, E_BITnet_name, E_BITnet_name_length);
		i = strlen(temp->HostName);
		ASCII_TO_EBCDIC((temp->HostName), ControlBlock.Ohost, i);
		PAD_BLANKS(ControlBlock.Ohost, i, 8);
		if(temp->type == EXOS_TCP) {
#ifdef EXOS
			i = get_e_host_ip_address(IP_ADDRESS);
			ControlBlock.Rip = htonl(i);
			i = get_e_host_ip_address(temp->HostName);
			ControlBlock.Oip = htonl(i);
#endif
		} else {
#ifdef MULTINET_or_DEC
			i = get_m_host_ip_address(IP_ADDRESS);
			ControlBlock.Rip = htonl(i);
			i = get_m_host_ip_address(temp->HostName);
			ControlBlock.Oip = htonl(i);
#endif
		}
		ControlBlock.R = 0;
		if(temp->type == EXOS_TCP) {
#ifdef EXOS
			status = sys$qiow((long)(0), temp->channel,
				(short)(EX__WRITE), temp->iosb,
				(long)(0), (long)(0),
				&ControlBlock, (int)(sizeof(struct VMctl)),
				(long)(0), (long)(0), (long)(0), (long)(0));
#endif
		} else {
#ifdef MULTINET_or_DEC
			status = sys$qiow((long)(0), temp->channel,
				(short)(IO$_WRITEVBLK), temp->iosb,
				(long)(0), (long)(0),
				&ControlBlock, (int)(sizeof(struct VMctl)),
				(long)(0), (long)(0), (long)(0), (long)(0));
#endif
		}
		if((status & 0x1) == 0) {
			logger(2, "VMS_TCP, Control block write error; line=%d,\
 status=%d, iosb=%d\n", Index, status, temp->iosb[0]);
			temp->state = INACTIVE;
			close_tcp_chan(Index);
		}
		return;
	}

/* Some error - log it and queue a retry */
	if(temp->type == EXOS_TCP)
		status = ((temp->iosb)[2] >> 8);
	else
	if(temp->type == MNET_TCP)
		status = (((temp->iosb)[0] & 0x7fff) / 8);
	else	/* Probably DEC */
		status = (temp->iosb)[1];

	if(status == ETIMEDOUT) {	/* Happens too much */
		logger((int)(2), "VMS_TCP, Requesting Remote socket error:\
 line=%d, iosb.stat=%d, errno=%d\n",
			Index, (temp->iosb)[0], status);
		}
		else {	/* Log it always */
			logger((int)(2), "VMS_TCP, Requesting remote socket error:\
 line=#%d, iosb.stat=d^%d, errno=d^%d\n",
			Index, (temp->iosb)[0], status);
		}
/* Deassign the channel, deactivate the line, and queue a retry
   to be in 5 minutes later.
*/
	temp->state = INACTIVE;
	close_tcp_chan(Index);		/* Close channel and queue retry */
}


/*
 | A connection has been accepted. 
 | Queue a receive to read the OPEN control block.
 */
#ifdef EXOS
exos_accept_ast()
{
	long	status, tcp_receive_ast();
	short	iosb[4];

	if((PassiveIosb[0] & 0x1) == 0) {	/* Error - abandon it */
		logger(2, "VMS_TCP, Accept error: iosb.stat=d^%d, \
iosb.EXOS-stat=d^%d\n",
			PassiveIosb[0], (PassiveIosb[2] >> 8));
		sys$dassgn(PassiveChannel);	/* Deassign channel */
		return;
	}

/* It seems ok - queue a receive for it (Index < 0 tells that we are
   receiving for the Passive end)
*/
#ifdef DEBUG
	logger((int)(3), "VMS_TCP, Accepting some connection\n");
#endif
	status = sys$qiow((long)(0), PassiveChannel, (short)(EX__SELECT),
		iosb, (long)(0), (long)(0),
		tcp_receive_ast, (int)(-EXOS_TCP),
		(int)(0),	/* Notify when something to read */
		(long)(0), (long)(0), (long)(0));	/* Close the socket */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS_TCP, can't Select, status=d^%d, iosb.stat=d^%d,\
 iosb.ex_stat=d^%d\n",
			status, iosb[0], (iosb[2] >> 8));
	}
}
#endif


/*
 | A connection has been accepted. 
 | Queue a receive to read the OPEN control block.
 */
#ifdef MULTINET
mnet_passive_accept()
{
	long	status, tcp_receive_ast();
	short	iosb[4];
	struct	sockaddr *MNsocket;	/* MultiNet */
	char	DevName[16];	/* EXA0: or INET0: */
	struct	{			/* Descriptor */
		short	length;
		short	filler;
		char	*address;
		} Device;

	if((PassiveIosb[0] & 0x1) == 0) {	/* Error - abandon it */
		logger(2, "VMS_TCP, Accept error: iosb.stat=d^%d, errno=%d\n",
			PassiveIosb[0], ((PassiveIosb[0] & 0x7fff) >> 3));
		goto ReAccept;
/*		sys$dassgn(PassiveChannel);	/* Deassign channel */
		return;
	}

/* Accept wait seems to be ok. Now, we have to assign another channel to
   MultiNet, accept the connection, and then queue the read request.
   Start with accept:
*/
	strcpy(DevName, "INET0:");
	Device.address = DevName; Device.length = strlen(DevName);
	if(((status = sys$assign(&Device, &MnetAcceptChannel,
		(long)(0), (long)(0))) & 0x1) == 0) {
		logger(1,
			"VMS_TCP, Can't assign channel to %s: $ASSIGn status=%d\n",
			Device, status);
		return;
	}
	status = sys$qiow((long)(0), MnetAcceptChannel, (short)(IO$_ACCEPT),
		iosb, (long)(0), (long)(0),
		(long)(0), (long)(0), (long)(PassiveChannel),
		(long)(0), (long)(0), (long)(0));	/* Close the socket */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS_TCP, can't ACCEPT, status=d^%d, iosb.stat=d^%d,\
 errno=d^%d\n",
			status, iosb[0], ((iosb[0] & 0x7fff) >> 3));
		sys$dassgn(MnetAcceptChannel);
		return;
	}

/* It seems ok - queue a receive for it (Index < 0 tells that we are
   receiving for the Passive end)
*/
#ifdef DEBUG
	logger((int)(3), "VMS_TCP, Accepting some connection\n");
#endif
	status = sys$qio((long)(0), MnetAcceptChannel, (short)(IO$_READVBLK),
		MnetAcceptIosb, tcp_receive_ast, (long)(-MNET_TCP),
		&ControlBlock, sizeof ControlBlock,
		(int)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0))
		logger(2, "VMS_TCP, can't queue read, status=d^%d\n",
			status);

	/* RE-Queue accept wait */
ReAccept:
	status = sys$qio((long)(0), PassiveChannel,
			(short)(IO$_ACCEPT_WAIT),
			  PassiveIosb, mnet_passive_accept, (long)(0),
			  (long)(0), (long)(0),
			(long)(0), (long)(0), (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		logger(1,
			"VMS_TCP, Can't accept-wait; $QIOW status=d^%d\n",
				status);
		sys$dassgn(PassiveChannel);	/* Deassign channel */
		return 0;
	}
}
#endif


/*
 | A connection has been accepted. 
 | Queue a receive to read the OPEN control block.
 */
#ifdef DEC_TCP
DEC_passive_accept()
{
	long	status, tcp_receive_ast();
	short	iosb[4];
	char	DevName[16];
	struct	{			/* Descriptor */
		short	length;
		short	filler;
		char	*address;
		} Device;

	if((PassiveIosb[0] & 0x1) == 0) {	/* Error - abandon it */
		logger(2, "VMS_TCP, Accept error: iosb.stat=d^%d\n",
			PassiveIosb[0]);
		sys$dassgn(MnetAcceptChannel);
		sys$dassgn(PassiveChannel);	/* Deassign channel */
		return;
	}

/* It seems ok - queue a receive for it (Index < 0 tells that we are
   receiving for the Passive end)
*/
#ifdef DEBUG
	logger((int)(3), "VMS_TCP, Accepting some connection\n");
#endif
	status = sys$qio((long)(0), MnetAcceptChannel, (short)(IO$_READVBLK),
		MnetAcceptIosb, tcp_receive_ast, (long)(-DEC__TCP),
		&ControlBlock, sizeof ControlBlock,
		(int)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0))
		logger(1, "VMS_TCP, can't queue read, status=d^%d\n",
			status);
}

/*
 | Reassign a channel to BG0 and port another accept. We can do it only after
 | we've assigned the previous accepted channel to the line or dismissed it.
 */
DEC_reinit_accept()
{
	long	status, DEC_passive_accept();
	short	iosb[4];
	static struct	itm_list_1	remote_host_list;
	static int	ReturnLength;
	char	DevName[16];
	struct	{			/* Descriptor */
		short	length;
		short	filler;
		char	*address;
		} Device;

/* Assign a channel to the BG device: */
	strcpy(DevName, "BG0:");
	Device.address = DevName; Device.length = strlen(DevName);
	if(((status = sys$assign(&Device, &MnetAcceptChannel,
		(long)(0), (long)(0))) & 0x1) == 0) {
		logger(1,
			"VMS_TCP, Can't assign channel to %s: $ASSIGn status=%d\n",
			Device, status);
		return;
	}

	remote_host_list.length = sizeof(struct sockaddr);
	remote_host_list.hst = &PassiveRemoteSocket;
	remote_host_list.rtn = &ReturnLength;
/* Now, connect to the remote socket */
	status = sys$qio(0,PassiveChannel, (IO$_ACCESS | IO$M_ACCEPT),
		  PassiveIosb, DEC_passive_accept, 0,
		  0,0, &remote_host_list, &MnetAcceptChannel, 0,0);
	if((status & 0x1) == 0) {
		logger(1, "VMS_TCP, Trying accept \
 error: $QIO status=d^%d\n",
			status);
		sys$dassgn(MnetAcceptChannel);
		sys$dassgn(PassiveChannel);
		return 0;
	}
}
#endif


/*
 | Some input has been received for the passive end. Try receiving the OPEN
 | block in one read (and hopefully we'll receive it all).
 */
accept_VMnet_control_record()
{
	register int	i, size, Index, status, TempVar;
	short		ReasonCode;	/* For the NAK reason code */
	char	HostName[10], Exchange[10], *p;
	struct	SOioctl	*CopyFrom, *CopyTo;	/* For BCOPY */

	ReasonCode = 0;
/* Receive the OPEN block */
	if(TcpIpType == EXOS_TCP) {
#ifdef EXOS
		status = sys$qiow((long)(0), PassiveChannel, (short)(EX__READ),
				PassiveIosb, (long)(0), (long)(0),
				&ControlBlock, (long)(sizeof(struct VMctl)),
				(long)(0), (long)(0), (long)(0), (long)(0));
		if(((status & 0x1) == 0) || ((PassiveIosb[0] & 0x1) == 0)) {
			logger(2, "VMS_TCP, Error reading from EXOS when \
accepting connection: status=d^%d, iosb.stat=d^%d, iosb.EXOS-stat=d^%d\n",
				status, PassiveIosb[0], (PassiveIosb[2] >> 8));
			sys$dassgn(PassiveChannel);	/* Forget it */
			return;
		}
		size = PassiveIosb[1];
#endif
	}
	else {	/* MultiNet or DEC's UCX */
#ifdef MULTINET_or_DEC
		if(((MnetAcceptIosb[0] & 0x1) == 0)) {
			logger(2, "VMS_TCP, Error reading from Mnet when \
accepting connection: status=d^%d, iosb.status=%d, errno=d^%d\n",
				status, MnetAcceptIosb[0],
				((MnetAcceptIosb[0] & 0x7fff) >> 3));
			sys$dassgn(MnetAcceptChannel);	/* Forget it */
			return;
		}
		size = MnetAcceptIosb[1];
	}

/* Check that we've received enough information */
	if(size < sizeof(struct VMctl)) {
		logger(1, "VMS_TCP, Received too small control record\n");
		goto RetryConnection;
#endif
	}

/* Check first that this is an OPEN block. If not - reset connection */
	EBCDIC_TO_ASCII((ControlBlock.type), Exchange, 8); Exchange[8] = '\0';
	if(strncmp(Exchange, "OPEN", 4) != 0) {
		logger(1, "VMS_TCP, Illegal control block '%s' received\n",
			Exchange);
		goto RetryConnection;
	}

/* OK, assume we've received all the information - get the hosts names from the
   control block and check the names.
*/
	EBCDIC_TO_ASCII((ControlBlock.Rhost), HostName, 8);	/* His name */
	for(p = &HostName[7]; p > HostName; *p--) if(*p != ' ') break;
	*++p = '\0';
	EBCDIC_TO_ASCII((ControlBlock.Ohost), Exchange, 8);	/* Our name (as he thinks */
	for(p = &Exchange[7]; p > Exchange; *p--) if(*p != ' ') break;
	*++p = '\0';

/* Verify that he wants to call us */
	if(strncmp(Exchange, LOCAL_NAME, strlen(LOCAL_NAME)) != 0) {
		logger((int)(2), "VMS_TCP, Host %s incorrectly connected to us (%s)\n",
			HostName, Exchange);
		goto RetryConnection;
	}
/* Look for its line */
	for(Index = 0; Index < MAX_LINES; Index++) {
		if(compare(IoLines[Index].HostName, HostName) == 0) {
			/* Found - now do some checks */
			if((IoLines[Index].type != EXOS_TCP) &&
			   (IoLines[Index].type != MNET_TCP) &&
			   (IoLines[Index].type != DEC__TCP)) {	/* Illegal */
				goto RetryConnection;
			}
			if((IoLines[Index].state != LISTEN) &&
			   (IoLines[Index].state != RETRYING)) { /* Break its previous connection */
				if(IoLines[Index].state == ACTIVE) /* ALready connected */
					ReasonCode = 2;
				else	ReasonCode = 3;
				IoLines[Index].state = INACTIVE;
				restart_channel(Index);
				/* Will close line and put it into correct state */
				goto RetryConnection;	/* To close the passive  end */
			}
/* Copy the parameters from the Accept block, so we can post a new one */
			ASCII_TO_EBCDIC("ACK", (ControlBlock.type), (int)(3));
			ControlBlock.R = 0;
			PAD_BLANKS((ControlBlock.type), 3, 8);
#ifdef EXOS
			CopyFrom = &PassiveLocalSocket;
			CopyTo = &(IoLines[Index].LocalSocket);
			memcpy(CopyTo, CopyFrom, sizeof(struct SOioctl));
			CopyFrom = &PassiveRemoteSocket;
			CopyTo = &(IoLines[Index].RemoteSocket);
			memcpy(CopyFrom, CopyTo, sizeof(struct SOioctl));
#endif
			if(TcpIpType == EXOS_TCP) {
#ifdef EXOS
				IoLines[Index].channel = PassiveChannel;
#endif
			} else {
#ifdef MULTINET_or_DEC
				IoLines[Index].channel = MnetAcceptChannel;
#ifdef DEC_TCP
				if(TcpIpType == DEC__TCP)
					DEC_reinit_accept();	/* Freed the channel */
#endif
#endif
			}
			IoLines[Index].state = DRAIN;
			IoLines[Index].RecvSize =
				IoLines[Index].TcpState = 0;
/* Send and ACK block - transpose tyhe fields */
			memcpy(Exchange, (ControlBlock.Rhost), 8);
			memcpy((ControlBlock.Rhost), (ControlBlock.Ohost), 8);
			memcpy((ControlBlock.Ohost), Exchange, 8);
			i = ControlBlock.Oip;
			ControlBlock.Oip = ControlBlock.Rip;
			ControlBlock.Rip = i;
			queue_receive(Index);	/* Queue a receive for it */
			if(IoLines[Index].type == EXOS_TCP) {
#ifdef EXOS
				sys$qiow((long)(0), IoLines[Index].channel,
					(short)(EX__WRITE), IoLines[Index].iosb,
					(long)(0), (long)(0),
					&ControlBlock, (int)(sizeof(struct VMctl)),
					(long)(0), (long)(0), (long)(0), (long)(0));
				init_passive_tcp_connection();	/* Create a new socket */
#endif
			}
			else {
#ifdef MULTINET_or_DEC
				sys$qiow((long)(0), IoLines[Index].channel,
					(short)(IO$_WRITEVBLK),
					IoLines[Index].iosb,
					(long)(0), (long)(0),
					&ControlBlock, (int)(sizeof(struct VMctl)),
					(long)(0), (long)(0), (long)(0), (long)(0));
			/* NO need to re-init passive end on MultiNet */
#endif
			}
			return;
		}
	}

/* Line not found - log it, and dismiss the connection */
	logger((int)(2), "VMS_TCP, Can't find line for host '%s'\n", HostName);
/* Send a reject to other side and re-queue the read */
RetryConnection:
	if(ReasonCode == 0)	/* No value given */
		ReasonCode = 1;	/* Link not found */
	ASCII_TO_EBCDIC("NAK", (ControlBlock.type), (int)(3));
	PAD_BLANKS((ControlBlock.type), 3, 8);
	memcpy((ControlBlock.Rhost), E_BITnet_name, E_BITnet_name_length);
	ControlBlock.R = ReasonCode;	/* Reason code */
	if(TcpIpType == EXOS_TCP) {
#ifdef EXOS
		sys$qiow((long)(0), PassiveChannel,
			(short)(EX__WRITE), IoLines[Index].iosb,
			(long)(0), (long)(0),
			&ControlBlock, (int)(sizeof(struct VMctl)),
			(long)(0), (long)(0), (long)(0), (long)(0));
		sys$dassgn(PassiveChannel);
		init_passive_tcp_connection();	/* Create a new socket */
#endif
	}
	else {
#ifdef MULTINET_or_DEC
		sys$qiow((long)(0), MnetAcceptChannel,
			(short)(IO$_WRITEVBLK),
			IoLines[Index].iosb,
			(long)(0), (long)(0),
			&ControlBlock, (int)(sizeof(struct VMctl)),
			(long)(0), (long)(0), (long)(0), (long)(0));
		sys$dassgn(MnetAcceptChannel);
#ifdef DEC_TCP
		if(TcpIpType == DEC__TCP)
			DEC_reinit_accept();	/* Freed the channel */
#endif
#endif
	}
}


/*
 | Deaccess the channel and close it. If it is a primary channel, queue a restart
 | for it after 5 minutes to establish the link again. Don't do it immediately
 | since there is probably a fault at the remote machine.
 | If this line is a secondary, nothing has to be done. We have an accept active
 | always. When the connection will be accepted, the correct line will be
 | located by the accept routine.
 */
close_tcp_chan(Index)
int	Index;
{
	short	chan;

	if(Index >= 0) {
		if(IoLines[Index].type == EXOS_TCP) {
#ifdef EXOS
			sys$qiow((long)(0), IoLines[Index].channel,
				(short)(EX__CLOSE), (long)(0), (long)(0),
				(long)(0), (long)(0), (long)(0),
				IoLines[Index].RemoteSocket,
				(long)(0), (long)(0), (long)(0));	/* Close the socket */
#endif
		} else {
			sys$cancel(IoLines[Index].channel);
			sleep(1);	/* Wait a little bit for the cancel */
		}
		sys$dassgn(IoLines[Index].channel);	/* Deassign channel */

		IoLines[Index].flags &= ~F_SHUT_PENDING;

/* Re-start the channel. */
		IoLines[Index].state = RETRYING;
	}

/* It was during VMNET control record acceptance, and a line was not found */
	else {
		sys$dassgn(PassiveChannel);	/* Deassign channel */
	}
}

/*
 | Call the SELECT function to inform us when there is input from the EXOS.
 */
#ifdef EXOS
queue_exos_tcp_receive(Index)
int	Index;
{
	long	status, tcp_receive_ast();
	short	iosb[4];

	status = sys$qiow((long)(0), IoLines[Index].channel, (short)(EX__SELECT),
		iosb, (long)(0), (long)(0),
		tcp_receive_ast, Index,
		(int)(0),	/* Notify when something to read */
		(long)(0), (long)(0), (long)(0));	/* Close the socket */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS_TCP, can't Select, status=d^%d, iosb.stat=d^%d,\
 iosb.ex_stat=d^%d\n",
			status, iosb[0], (iosb[2] >> 8));
		IoLines[Index].state = INACTIVE;
		restart_channel(Index);	/* Will close line and put it into correct state */
		return 0;
	}
	return 1;
}
#endif

/*
 | Queue a read to the MultiNet. Give the buffer place from the current place.
 */
#ifdef MULTINET_or_DEC
queue_mnet_tcp_receive(Index)
int	Index;
{
	long	status, size,tcp_receive_ast();
	struct	LINE	*temp;

	temp = &IoLines[Index];
	if((size = (MAX_BUF_SIZE - temp->RecvSize)) <= 0)
		bug_check("VMS_TCP, TCP receive buffer is full");

	status = sys$qio((long)(0), temp->channel, (short)(IO$_READVBLK),
			temp->iosb, tcp_receive_ast, Index,
			&((temp->buffer)[temp->RecvSize]), size,
			(long)(0), (long)(0), (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		if((temp->state != ACTIVE) && (temp->state != DRAIN))
			logger((int)(2), "VMS_TCP, can't queue read for MultiNet, status=%d\n",
				status);
		else
			logger(1, "VMS_TCP, can't queue read for MultiNet, status=%d\n",
				status);
		IoLines[Index].state = INACTIVE;
		restart_channel(Index);	/* Will close line and put it into correct state */
		return 0;
	}
	return 1;
}
#endif

/*
 | Called when something was received from TCP. Receive the data and call the
 | appropriate routine to handle it.
 | When receiving,  the first 4 bytes are the count field. When we get the
 | count, we continue to receive untill the count exceeded. Then we call the
 | routine that handles the input data.
 */
tcp_receive_ast(Index)
int	Index;
{
	register long	i, status, size, TempVar;
	short	iosb[4];
	struct	LINE	*temp;
	char	Type[10];	/* Control block type */
	struct	VMctl	*ControlBlock;
	struct	TTR	*ttr;
	register unsigned char	*p, *q;
	struct SYN_NAK {	/* To start the link we send SYN NAK */
		unsigned char	Syn, Nak;
		} SynNak = { SYN, NAK};

	if(Index < 0) {	/* This is for the passive end which accepted
				   a connection short while ago */
		accept_VMnet_control_record();
		return;
	}

	temp = &(IoLines[Index]);

	if(temp->type == EXOS_TCP) {	/* Have to do the read here */
#ifdef EXOS
	/* Append the data to our buffer */
		if((size = (MAX_BUF_SIZE - temp->RecvSize)) <= 0)
			bug_check("VMS_TCP, TCP receive buffer is full");
		status = sys$qiow((long)(0), temp->channel, (short)(EX__READ),
				iosb, (long)(0), (long)(0),
				&((temp->buffer)[temp->RecvSize]), size,
				(long)(0), (long)(0), (long)(0), (long)(0));
		if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
			logger(2, "VMS_TCP, Error reading from EXOS: \
line = %d, status=d^%d, iosb.stat=d^%d, iosb.EXOS-stat=d^%d\n",
				Index, status, iosb[0], (iosb[2] >> 8));
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		}
		size = (int)(iosb[1] & 0xffff);	/* Number of characters read */
#endif
	} else {	/* MultiNet - just check the IOSB */
		if(((temp->iosb[0] & 0x1) == 0)) {
			if((temp->state != ACTIVE) && (temp->state != DRAIN))
				logger((int)(2), "VMS_TCP, Error reading from Mnet:\
 line=%d, iosb.stat=d^%d, errno=d^%d\n",
					Index, temp->iosb[0], ((temp->iosb[0] & 0x7fff) / 8));
			else
				logger(2, "VMS_TCP, Error reading from Mnet:\
 line=%d, iosb.stat=d^%d, errno=d^%d\n",
					Index, temp->iosb[0], ((temp->iosb[0] & 0x7fff) / 8));
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		}
		size = (int)(temp->iosb[1] & 0xffff);	/* Number of characters read */
	}

	dequeue_timer(temp->TimerIndex);	/* Dequeue the timeout */

/* If we read 0 characters, it usually signals that other side closed connection */
	if(size == 0) {
		logger((int)(2), "VMS_TCP, Zero characters read. Disabling line.\n");
		temp->state = INACTIVE;
		restart_channel(Index);	/* Will close line and put it into correct state */
		return;
	}

/* If we are in the TCP_SYNC stage, then this is the reply from other side */
	if(temp->state == TCP_SYNC) {
		if(size < sizeof(struct VMctl)) {	/* Too short block */
			logger(1, "VMS_TCP, Too small OPen record received\n");
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		}
		ControlBlock = temp->buffer;
		EBCDIC_TO_ASCII((ControlBlock->type), Type, 8); Type[8] = '\0';
		if(strncmp(Type, "ACK", 3) != 0) {	/* Something wrong */
			logger((int)(2), "VMS_TCP, Illegal control record '%s'\n",
				Type);
			/* Print error code */
			switch(ControlBlock->R) {
			case 0x1: logger((int)(2), "     Link could not be found\n");
					break;
			case 0x2: logger((int)(2), "     Link is in active state at other host\n");
					break;
			case 0x3: logger((int)(2), "     Other side is attempting active open\n");
					break;
			}
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		}
		/* It's ok - set channel into DRAIN and send the first Enquire */
		temp->state = DRAIN;
/* We must emulate CTC channel, so we should send SYN+NAK instead of ENQ as the
   first block */
		send_data(Index, &SynNak, (int)(sizeof(struct SYN_NAK)),
			(int)(SEND_AS_IS));	/* Send an enquiry there */
		temp->TcpState = temp->RecvSize = 0;
		queue_receive(Index);
		return;
	}

/* Loop over the received buffer, and append characters as needed */
	temp->RecvSize += size;
	if(temp->TcpState == 0) {	/* New buffer */
		if(temp->RecvSize >= 4) {	/* We can get the size */
			temp->TcpState = 	/* Accumulate it... */
				(temp->buffer[2] << 8) +
				temp->buffer[3];
		}
		else {	/* We need at least 2 bytes */
			queue_receive(Index);	/* Rqueue the read request */
			return;
		}
	}
Loop:	if(temp->RecvSize >= temp->TcpState) {	/* Frame completed */
/* Loop over it */
		p = &temp->buffer[sizeof(struct TTB)];	/* First TTR */
		i = temp->RecvSize - sizeof(struct TTB);	/* Size of TTB */
		for(;i > 0;) {
			ttr = p;
			if(ttr->LN == 0) break;	/* End of buffer */
			p += sizeof(struct TTR);
			TempVar = htons(ttr->LN);
/* Check whether the size in TTR is less than the remained size. If not - bug */
			if(TempVar >= i) {
				logger(1, "VMS_TCP, Line %d, Size in \
TTR(%d) longer than input left(%d)\n",
					Index, TempVar, i);
				logger(1, "Recv size=%d, TcpState=%d, trace follows:\n",
					temp->RecvSize, temp->TcpState);
trace(temp->buffer, temp->RecvSize, 1);
				i = 0;	/* To ignore this deffective buffer */
				break;
			}
/* Check whether FAST-OPEN flag is on. If so - set our flag also */
			if((ttr->F & 0x80) != 0)	/* Yes */
				temp->flags |= F_FAST_OPEN;
			else		/* No - clear the flag */
				temp->flags &= ~F_FAST_OPEN;
			input_arrived(Index, (long)(1),	/* Success status */
				p, TempVar);
			p += TempVar;
			i -= (TempVar + sizeof(struct TTR));
		}
		if(i > sizeof(struct TTR)) {	/* rest of buffer is next frame */
			i -= sizeof(struct TTR);	/* # of chrs from next frame */
			p += sizeof(struct TTR);
			q = temp->buffer;
			memcpy(q, p, i);	/* Re-allign buffer */
			temp->RecvSize = i;
			if(i >= 4) {	/* Enough for getting the length */
				temp->TcpState = 	/* Size of next frame */
					(temp->buffer[2] << 8) +
					temp->buffer[3];
				goto Loop;
			}
			else
				temp->TcpState = 0;	/* Next read will get enough data to complete count */
		}
		else {
			temp->TcpState = 0;	/* New frame */
			temp->RecvSize = 0;
		}
	}
	queue_receive(Index);	/* Rqueue the read request */
}


/*
 | Write a block to EXOS. However, if there is enough room left in the buffer
 | after appending this block, then if F_CAN_XMIT_MORE is on, ask the caller
 | to send more data (turn-on F_XMIT_MORE), else send the buffer.
 */
#ifdef EXOS
send_exos_tcp(Index, line, size)
int	Index, size;
unsigned char	*line;
{
	long	tcp_write_ast();
	register int	i, TempVar, status;
	register unsigned char	*p, *q;
	struct	LINE	*temp;

	temp = &IoLines[Index];
	temp->flags |= F_SENDING;	/* We queue a send */

	status = sys$qio((long)(0), temp->channel, (short)(EX__WRITE),
		temp->Siosb, tcp_write_ast, Index,
		line, size, (long)(0), (long)(0), (long)(0), (long)(0));

	if((status & 0x1) == 0) {
		logger(2, "VMS_TCP, Writing to EXOS: $PUT status=d^%d,\
 line=#%d\n",
			status, Index);
		temp->flags &= ~F_SENDING;
	}
}
#endif

/*
 | Call this routine when write is done. We call the "Handle-ack" routine,
 | since this end of write signals that the write succeeded (and probably the
 | read also).
 | NOTE: The buffer copy to XmitBuffer is essential! Do not remove it.
 */
tcp_write_ast(Index)
int	Index;
{
	struct	LINE	*temp;
	register int	size, TempVar;
	register unsigned char	*p, *q;

	temp = &IoLines[Index];
	temp->XmitSize = 0;	/* Sent ok */
/* Sometimes the IOSB status is zero - probably due to synchronization problems.
   treat this as ok */
	if(((temp->Siosb)[0] & 0x1) == 0) {
		if(temp->type == EXOS_TCP) {
			logger(2, "VMS_TCP, Write to TCP,\
 IOSB.stat=d^%d, IOSB.EX-stat=d^%d\n",
				temp->Siosb[0],
				(temp->Siosb[2] >> 8));
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		} else {
			logger((int)(2), "VMS_TCP, Write to TCP,\
 IOSB.stat=d^%d, Errno=d^%d\n",
				temp->Siosb[0],
				((temp->Siosb[0] & 0x7fff) / 8));
			if(temp->Siosb[0] != 2096) {	/* not $CANCEL issued */
				temp->state = INACTIVE;
				restart_channel(Index);	/* Will close line and put it into correct state */
			}
			return;
		}
	}

/* Test whether we have a queue of pending transmissions. If so, send them */
	if(temp->FirstXmitEntry != temp->LastXmitEntry) {
		temp->flags &= ~F_XMIT_CAN_WAIT;
		temp->flags &= ~F_XMIT_MORE;
		size = temp->XmitSize =
			(temp->XmitQueueSize)[temp->FirstXmitEntry];
		p = temp->XmitBuffer;
		q = (temp->XmitQueue)[temp->FirstXmitEntry];
		memcpy(p, q, size);
		free(q);	/* Free the memory allocated for it */
		temp->FirstXmitEntry = ++(temp->FirstXmitEntry) % MAX_XMIT_QUEUE;

		if(temp->type == EXOS_TCP) {
#ifdef EXOS
			send_exos_tcp(Index, p, size);
#endif
		} else {
#ifdef MULTINET_or_DEC
			send_mnet_tcp(Index, p, size);
#endif
		}
		return;
	}

	temp->flags &= ~F_SENDING;	/* Send done */
	temp->flags &= ~F_WAIT_V_A_BIT;		/* All buffers were sent. */

/* Ack if we are in active state. Other states are in lock-step mode and no
   ACK should be sent after transmission. */
	if(temp->state == ACTIVE)
		handle_ack(Index, (short)(EXPLICIT_ACK));
}

/*
 | Write a block to multinet. However, if there is enough room left in the buffer
 | after appending this block, then if F_CAN_XMIT_MORE is on, ask the caller
 | to send more data (turn-on F_XMIT_MORE), else send the buffer.
 */
#ifdef MULTINET_or_DEC
send_mnet_tcp(Index, line, size)
int	Index, size;
unsigned char	*line;
{
	long	tcp_write_ast();
	register int	i, TempVar, status;
	register unsigned char	*p, *q;
	struct	LINE	*temp;

	temp = &IoLines[Index];
	temp->flags |= F_SENDING;	/* We queue a send */

	status = sys$qio((long)(0), temp->channel, (short)(IO$_WRITEVBLK),
		temp->Siosb, tcp_write_ast, Index,
		line, size, (long)(0), (long)(0), (long)(0), (long)(0));

	if((status & 0x1) == 0) {
		logger(2, "VMS_TCP, Writing to TCP: $PUT status=d^%d,\
 line=#%d\n",
			status, Index);
		temp->flags &= ~F_SENDING;
	}
}
#endif

/*
 | Try transalting the address as a four dotted number. If not succeeded,
 | look in EXOS's HOSTS file.
 */
#ifdef EXOS
get_e_host_ip_address(HostName)
char	*HostName;	/* Name or 4 dotted numbers */
{
	long	address, i, j;
	char	*p, line[LINESIZE], IPaddress[LINESIZE],
		names[4][LINESIZE];
	FILE	*Ifd;

	if((address = internet_to_integer(HostName)) != 0)
		return address;		/* It was a dotted number */

	if((Ifd = fopen("EXOS_ETC:HOSTS.", "r")) == NULL)
		bug_check("Can't open EXOS hosts file\n");

/* Loop and read lines */
	while(fgets(line, sizeof line, Ifd) != NULL) {
		if((p = strchr(line, '\n')) != NULL) *p = '\0';
		if((*line == '#') || (*line == '\0')) continue;	/* Comment */
		if((j = sscanf(line, "%s %s %s %s %s", IPaddress,
			names[0],names[1],names[2],names[3])) < 2)
				continue;	/* Some error */
		if((address = internet_to_integer(IPaddress)) == 0)
			continue;	/* Some other error */
		for(i = 0; i < (j - 1); i++) {
			if(*names[i] == '#') break;	/* Comment */
			if(compare(names[i], HostName) == 0) {	/* Found */
				fclose(Ifd);
				return address;
			}
		}
	}
	fclose(Ifd);
	logger(1, "VMS_EXOS: Can't find IP address '%s'\n", HostName);
	return -1;	/* Not found */
}
#endif

/*
 | Query the MultiNet's nameserver for the address of a host. Return it in the
 | network format.
 */
#ifdef MULTINET_or_DEC
get_m_host_ip_address(HostName)
char	*HostName;
{
	struct	hostent	*gethostbyname(), *temp;
	int	address;
	char	*p, line[LINESIZE], IPaddress[LINESIZE], filler[LINESIZE],
		name[LINESIZE];
	FILE	*Ifd;


	if((address = internet_to_integer(HostName)) != 0)
		return address;		/* It was a dotted number */

	if((Ifd = fopen("multinet:HOSTS.TXT", "r")) == NULL)
		bug_check("Can't open MultiNet hosts file\n");

/* Loop and read lines */
	while(fgets(line, sizeof line, Ifd) != NULL) {
		if((p = strchr(line, '\n')) != NULL) *p = '\0';
		if((*line == ';') || (*line == '\0')) continue;	/* Comment */
		if(sscanf(line, "%s : %s : %s :", filler, IPaddress,
			name) < 3)
				continue;	/* Some error */
		if((address = internet_to_integer(IPaddress)) == 0)
			continue;	/* Some other error */
		if(compare(name, HostName) == 0) {	/* Found */
			fclose(Ifd);
			return address;
		}
	}
	fclose(Ifd);
	logger(1, "VMS_TCP: Can't find IP address '%s'\n", HostName);
	return -1;	/* Not found */
}
#endif


/*
 | Convert a string of 4 dotted decimal numbers into an integer, reversing
 | the bytes order, as the EXOS requires.
 | If illegal numbers found - return 0.
 */
internet_to_integer(InterNetAddress)
char	*InterNetAddress;
{
	long	A,B,C,D, status;

	status = sscanf(InterNetAddress, "%d.%d.%d.%d", &A, &B, &C, &D);
	if(status < 4) return 0;		/* Some error */
	return((A << 24) + (B << 16) + (C << 8) + D);
}
