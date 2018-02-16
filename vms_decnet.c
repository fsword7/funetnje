/* VMS_DECNET.C		V2.1
 | Copyright (c) 1988,1989,1990,1991 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use of misuse of this software.
 |
 | DO DECnet connection. The protocol used is like VMnet, but the fields in the
 | control blocks are in ASCII. Another deviation from VMnet is the
 | tranmission of ACKS.
 |
 | The handling of errors here is simpler. Usually an error will result by
 | the other side (or our kernel) closing the channel, so we rely on it.
 | When a link is broken, we try to restart it.
 |
 | We send ACKS, since we must keep synchronised. DECnet has problems with
 | full duplex, so we must work half-duplex.
 |
 | The DECnet host name is taken from the Device field in the IoLines structure.
 | If there are problems with I/O on a line we place it in INACTIVE state and
 | call Retsart-Channel. This procedure will change the state from IACTIVE
 | to the right state (LISTEN or RETRYING).
 |   The control fields in the VMnet control blocks are in ASCII here...
 |
 | V1.1 - Since IO.C has been changed, SEND_DATA no more adds the last TTR.
 |        Thus, SEND_DECNET now adds the last TTR.
 | V1.2 - Modify SEND_DECNET to use a different IOSB then the receiving one,
 |        to allow a really full-duplex operation.
 | V1.3 - Change SEND_DMF_AST to work in full duplex.
 | V1.4 - Move the VMNET headers creation into IO.C
 | V1.5 - Send-DECnet-AST - Clear the flag WAIT_V_A_BIT before calling Handle-Ack.
 |        (Useable for DMB's also), and if the type is DMB use BISYNC protocol.
 | V1.6 - Change the way we handle auto retsarts with DMF/DMB lines. Instead of
 |        the closing routine queueing a retstart, there is a scan every 5 minutes
 |        to detect such dead lines and restart them if the AUTO-RESTART flag
 |        is on.
 | V1.7 - 21/2/90 - Replace BCOPY calls with memcpy() calls.
 | V1.8 - 7/3/90 - Add CRC checks when sending over DECnet.
 | V1.9 - 22/3/90 - Prefix PassiveChannel and PassiveIosb with DECnet to prevent
 |        conflicts with the same variables in VMS_TCP.
 | V2.0 - 20/10/90 - When accepting DECnet connection we restart the link if it
 |        is not in LISTEN state. Now we do not do it when it is also in RETRY
 |        state. God knows how it worked up to now...
 | V2.1 - 14/6/91 - Replace some Logger(1) with Logger(2) so we won't log
 |        non-fatal errors.
 */
#include <stdio.h>
#include <iodef.h>
#include <msgdef.h>	/* For MSG$_xxx definition */
#include "consts.h"
#include "headers.h"

#define	NJE_OBJECT_NUMBER	202	/* The DECnet object number */
#define	EXPLICIT_ACK	0

#define	DECNET_MAILBOX	"NJE_DECNET_MBX"
INTERNAL short	NETACPchan,	/* Channel to NETACP */
	NETACPmailboxChan,	/* The mailbox on which we receive intrupt
				   when NETACP writes something to our channel */
	DECnetPassiveIosb[4],		/* For the IO of the passive channel */
	DECnetPassivechannel = 0;	/* To receive connection during its sync phase */
static struct VMctl	DECnetPassiveControlBlock;	/* Where to receive control block */

#define	NFB$C_DECLNAME	21	/* NETACP declare object name */
#define	NFB$C_DECLOBJ	22	/* NETACP declare object number */


EXTERNAL struct LINE	IoLines[MAX_LINES];
EXTERNAL struct	ENQUIRE	Enquire;

/* For the CRC calculations: */
static long	CrcTable[16];	/* Inited by LIB$CRC_TABLE and used by LIB$CRC */

/*
 | Called by the mailer's startup to init the CRC table. We use the CRC-16
 | polynom.
 */
init_crc_table()
{
	static long	coefficients;

	coefficients = 0120001;	/* Why? Look in the LIB$ book */
	lib$crc_table(&coefficients, CrcTable);
}


/*
 | Open a channel to another host. We can't do it asynchronously since the
 | $ASSIGN system service is a synchronous one. Hopefully, this connect will
 | not take too much time.
 | If we can't connect to other side, we retry it after 10 minutes.
 | If the connection succeeds, we send the VMnet control block (just the
 | relevant parts are filled) to identify ourselves.
 */
init_active_DECnet_connection(Index)
int	Index;
{
	struct	LINE	*temp;
	char	ConnectString[SHORTLINE];
	struct	DESC	ConnectDesc;
	long	status;
	struct	VMctl	ControlBlock;
	register int	i, TempVar;

	temp = &(IoLines[Index]);
	temp->state = INACTIVE;	/* No connection yet */
	temp->RecvSize = 0;	/* Empty buffer */
	temp->TcpState = 0;

/* Create the connection string */
	sprintf(ConnectString, "%s::\042%d=\042", temp->device,
		NJE_OBJECT_NUMBER);

/* Assign a channel to device */
	ConnectDesc.address = ConnectString; ConnectDesc.type = 0;
	ConnectDesc.length = strlen(ConnectString);
	if(((status = sys$assign(&ConnectDesc, &(temp->channel),
		(long)(0), (long)(0))) & 0x1) == 0) {
		logger(2, "VMS_DECNET, Can't assign DECnet channel to '%s';\
 $ASSIGn status=%d\n",
			ConnectString, status);
		temp->state = RETRYING;	/* Retry later */
		return;
	}

/* Connection succeeded - send the control block */
#ifdef DEBUG
	logger(3, "VMS_DECNET: Connect succeeded on line #%d\n", Index);
#endif
	temp->state = TCP_SYNC;	/* Expecting reply for the control block */
	queue_receive(Index);	/* Queue receive on it */
/* Send the first control block */
	strcpy(ControlBlock.type, "OPEN");
	strcpy(ControlBlock.Rhost, LOCAL_NAME);
	strcpy(ControlBlock.Ohost, temp->HostName);
	ControlBlock.Rip = ControlBlock.Oip = 0;	/* No IP here... */
	ControlBlock.R = 0;
	sys$qiow((long)(0), IoLines[Index].channel,
		(short)(IO$_WRITEVBLK), IoLines[Index].iosb,
		(long)(0), (long)(0),
		&ControlBlock, (int)(sizeof(struct VMctl)),
		(long)(0), (long)(0), (long)(0), (long)(0));
	return;
}


/*
 | Deaccess the channel and close it. If it is a primary channel, queue a restart
 | for it after 10 minutes to establish the link again. Don't do it immediately
 | since there is probably a fault at the remote machine.
 | If this line is a secondary, nothing has to be done. We have an accept active
 | always. When the connection will be accepted, the correct line will be
 | located by the accept routine.
 */
close_DECnet_line(Index)
int	Index;
{
	sys$dassgn(IoLines[Index].channel);	/* Deassign channel */

	IoLines[Index].flags &= ~F_SHUT_PENDING;

/* Re-start the channel. */
	IoLines[Index].state = RETRYING;
}


/*
 | Queue an asynchronous receive on the DECnet link. Append the newly received
 | data to the buffer already filled by previous reads.
 */
queue_DECnet_receive(Index)
int	Index;
{
	long	size, status, DECnet_receive_ast();
	struct	LINE	*temp;

	temp = &IoLines[Index];
	if((size = (MAX_BUF_SIZE - temp->RecvSize)) <= 0)
		bug_check("VMS_DECNET, DECnet receive buffer is full");

	status = sys$qio((long)(0), IoLines[Index].channel,
		(short)(IO$_READVBLK),
		temp->iosb,
		DECnet_receive_ast, Index,
		&(temp->buffer[temp->RecvSize]),	/* Append to buffer */
		size, (long)(0), (long)(0),(long)(0), (long)(0));

	if((status & 0x1) == 0) {
		logger(1, "VMS_DECNET, can't queue receive, status=d^%d, iosb.stat=d^%d\n",
			status, (int)(temp->iosb[0]));
		temp->state = INACTIVE;
		restart_channel(Index);	/* Will close line and put it into correct state */
		return 0;
	}
	return 1;
}


/*
 | Called when something was received from DECnet.
 | When receiving,  the first 4 bytes are the count field. When we get the
 | count, we continue to receive untill the count exceeded. Then we call the
 | routine that handles the input data.
 */
DECnet_receive_ast(Index)
int	Index;
{
	register long	i, status, size, TempVar;
	struct	LINE	*temp;
	struct	VMctl	*ControlBlock;
	struct	TTR	*ttr;
	struct	DESC	DataDesc;
	int	crc, InitialCrc;
	register unsigned char	*p, *q;

	if(Index == -1) {	/* This is for the passive end which accepted
				   a connection short while ago */
		accept_DECnet_control_record();
		return;
	}

	temp = &(IoLines[Index]);
	dequeue_timer(temp->TimerIndex);	/* Dequeue the timeout */

/* Data was read already. Check the status block for status and length of data
   read.
*/
	if((temp->iosb[0] & 0x1) == 0) {
		logger(2, "VMS_DECNET, Error reading from DECnet: Line=%d,\
 iosb.stat=d^%d, length=%d\n",
			Index, (int)(temp->iosb[0]), (int)(temp->iosb[1]));
		temp->state = INACTIVE;
		restart_channel(Index);	/* Will close line and put it into correct state */
		return;
	}

	size = (unsigned int)((temp->iosb)[1] & 0xffff);	/* Number of characters read this time */
	temp->RecvSize += size;
/* If we are in the TCP_SYNC stage, then this is the reply from other side */
	if(temp->state == TCP_SYNC) {
		if(size < sizeof(struct VMctl)) {	/* Too short block */
			logger(1, "VMS_DECNET, Too small OPen record received\n");
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		}
		ControlBlock = temp->buffer;
		if(strncmp(ControlBlock->type, "ACK", 3) != 0) {	/* Something wrong */
			logger(1, "VMS_DECNET, Illegal control record '%s'\n",
				ControlBlock->type);
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		}
		/* It's ok - set channel into DRAIN and send the first Enquire */
		temp->state = DRAIN;
		send_data(Index, &Enquire, (int)(sizeof(struct ENQUIRE)),
			(int)(SEND_AS_IS));	/* Send an enquiry there */
		temp->TcpState = temp->RecvSize = 0;
		queue_receive(Index);
		return;
	}

/* Loop over the received buffer, and append characters as needed */
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
/* Check the CRC of received data */
		InitialCrc = 0;
		DataDesc.address = temp->buffer; DataDesc.type = 0;
		DataDesc.length = temp->TcpState;
		crc = lib$crc(CrcTable, &InitialCrc, &DataDesc);
		InitialCrc = (temp->buffer[temp->RecvSize - 2] << 8) |
			     (temp->buffer[temp->RecvSize - 1]);
		if(InitialCrc != crc) {
			logger(2, "VMS_DECNET, CRC error. Received %x, expecting %x\n",
				InitialCrc, crc);
			restart_channel(Index);
			return;
		}
/* Loop over it */
		p = &temp->buffer[sizeof(struct TTB)];	/* First TTR */
		i = temp->RecvSize - sizeof(struct TTB);	/* Size of TTB */
		for(;i > 0;) {
			ttr = p;
			if(ttr->LN == 0) break;	/* End of buffer */
			p += sizeof(struct TTR);
			TempVar = ntohs(ttr->LN);
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

/* Jump over the two bytes of CRC: */
		i -= 2; p += 2;

/* Check whether we have another block concatanated to this one: */
		if(i > sizeof(struct TTR)) {	/* rest of buffer is next frame */
			ttr = p;
/* Sometimes there are redundant zeroes at the end of buffer: */
			if(ttr->LN != 0) {
				i -= sizeof(struct TTR);	/* # of chrs from next frame */
				p += sizeof(struct TTR);
				q = temp->buffer;
				memcpy(q, p, i);	/* Re-allign buffer */
				temp->TcpState = 	/* Size of next frame */
					(temp->buffer[2] << 8) +
					temp->buffer[3];
				temp->RecvSize = i;
				goto Loop;
			}
		}
		temp->TcpState = 0;	/* New frame */
		temp->RecvSize = 0;
	}
	queue_receive(Index);	/* Rqueue the read request */
}


/*
 | Write a line to DECnet. An AST will be called when done.
 | Add CRC at the end of the buffer. Assume that caller has left place for it.
 */
send_DECnet(Index, line, size)
int	Index, size;
unsigned char	*line;
{
	long	status, DECnet_write_ast();
	int	InitialCrc, crc;
	register int	i;
	struct	LINE	*temp;
	struct	DESC	DataDesc;

	temp = &IoLines[Index];
	temp->flags |= F_SENDING;

	InitialCrc = 0;
	DataDesc.address = line; DataDesc.type = 0;
	DataDesc.length = size;
	crc = lib$crc(CrcTable, &InitialCrc, &DataDesc);
	line[size++] = (crc >> 8) & 0xff;
	line[size++] = crc & 0xff;

	status = sys$qio((long)(0), temp->channel,
		(short)(IO$_WRITEVBLK), temp->Siosb,
		DECnet_write_ast, Index,
		line, size, (long)(0), (long)(0), (long)(0), (long)(0));

	if((status & 0x1) == 0) {
		logger(2, "VMS_DECNET, Writing to DECnet: WRITEVBLK status=d^%d,\
 line=#%d\n",
			status, Index);
			temp->flags &= ~F_SENDING;
			temp->state = INACTIVE;
			restart_channel(Index);
	}
}


/*
 | Call this routine when write is done. We call the "Handle-ack" routine,
 | since this end of write signals that the write succeeded (and probably the
 | read also).
 */
DECnet_write_ast(Index)
int	Index;
{
	struct	LINE	*temp;
	register int	size, TempVar;
	register unsigned char	*p, *q;

	temp = &IoLines[Index];

	temp->flags &= ~F_SENDING;	/* Send done */
	temp->XmitSize = 0;		/* Buffer sent correctly (hopefully...) */

/* Sometimes the IOSB status is zero - probably due to synchronization problems.
   treat this as ok */
	if(((temp->Siosb[0] & 0x1) == 0) &&
	    (temp->Siosb[0] != 0)) {
		logger(2, "VMS_DECNET, Write to DECnet, IOSB.stat=d^%d\n",
			(long)(temp->Siosb[0]));
		temp->state = INACTIVE;
		restart_channel(Index);	/* Will close line and put it into correct state */
		return;
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
		send_DECnet(Index, temp->XmitBuffer, size);
		return;
	}

	temp->flags &= ~F_WAIT_V_A_BIT;
	if(temp->state == ACTIVE) {
		handle_ack(Index, (short)(EXPLICIT_ACK));
	}
}


/*
 | Create a temporary mailbox and assign a channel (associated with this
 | mailbox) to device _NET:. Then ask to declare us as known object number
 | 202, and ask for an AST to be fired when something is written on that
 | mailbox. When the DECnet control program would like to tell us something,
 | it'll write it into the mailbox, and then the mailbox's AST will fire. We
 | then read the mailbox and parse it.
 */
init_DECnet_passive_channel()
{
	long	status, PassiveMailboxAst();
	struct	DESC	MbxDesc, NetDesc,
			ObjDesc;	/* For requesting object from NETACP */
	struct	{
		unsigned char	type;
		long		object;
		} ObjectType;
	char	NET[] = "_NET:";
	short	iosb[4];		/* IOSB for QIO call */

/* Create the logical name descriptor */
	MbxDesc.address = DECNET_MAILBOX;
	MbxDesc.length = strlen(DECNET_MAILBOX);
	MbxDesc.type = 0;

/* Create the temporary mailbox. */
	status = sys$crembx((unsigned char)(0), &NETACPmailboxChan, (long)(0),
		(long)(0), (long)(0),
		(long)(0),&MbxDesc);
	if((status & 0x1) == 0) {
		logger(1, "VMS_DECNET, Can't create mailbox, status=%d.\n",
			status);
		return;		/* Ignore it */
	}

/* Assign a channel to NETACP */
	NetDesc.address = NET; NetDesc.type = 0;
	NetDesc.length = strlen(NET);
	status = sys$assign(&NetDesc, &NETACPchan, (long)(0), &MbxDesc);
	if((status & 0x1) == 0) {
		logger(1, "VMS_DECNET, Can't assign channel to '%s', status=%d\n",
			NET, status);
		sys$dassgn(NETACPmailboxChan);
		return;
	}

/* Queue an attention AST to fire when something is written into the mailbox */
	status = sys$qiow((long)(0), NETACPmailboxChan,
			(short)(IO$_SETMODE|IO$M_WRTATTN), iosb,
			(long)(0), (long)(0),
			PassiveMailboxAst, (long)(0),
			(long)(3),	/* Access mode */
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS_DECNET, Can't declare AST for NETACP\
 mailbox, status=d^%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(NETACPchan); sys$dassgn(NETACPmailboxChan);
		return;
	}

/* Declare the object to NETACP */
	ObjectType.type = NFB$C_DECLOBJ;
	ObjectType.object = NJE_OBJECT_NUMBER;
	ObjDesc.address = &ObjectType; ObjDesc.type = 0;
	ObjDesc.length = sizeof ObjectType;
	status = sys$qiow((long)(0), NETACPchan,
			(short)(IO$_ACPCONTROL), iosb,
			(long)(0), (long)(0),
			&ObjDesc, (int)(0),
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));

	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS_DECNET, Can't declare NETACP object\
 status=d^%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(NETACPchan); sys$dassgn(NETACPmailboxChan);
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
PassiveMailboxAst()
{
	short	iosb[4];
	long	status, MessageType, unit, NCBsize, TempVar;
	unsigned char	buffer[LINESIZE],	/* Where to read the message */
			name[SHORTLINE],	/* Device name */
			ncb[SHORTLINE],	/* Information field */
			*p;

/* Read the mailbox message */
	status = sys$qiow((long)(0), NETACPmailboxChan,
			(short)(IO$_READVBLK), iosb,
			(long)(0), (long)(0),
			buffer, (int)(sizeof buffer),
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));

	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS_DECNET, Can't read DECnet mailbox message,\
 status=d^%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(NETACPchan); sys$dassgn(NETACPmailboxChan);
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
	logger((int)(4), "VMS_DECNET, Received mailbox message: type=%d, unit=%d\
 name=%s\n",
		MessageType, unit, name);
#endif

/* Reqeue the attention AST on the mailbox */
	status = sys$qiow((long)(0), NETACPmailboxChan,
			(short)(IO$_SETMODE|IO$M_WRTATTN), iosb,
			(long)(0), (long)(0),
			PassiveMailboxAst, (long)(0),
			(long)(3),	/* Access mode */
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS_DECNET, Can't declare AST for NETACP\
 mailbox, status=d^%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(NETACPchan); sys$dassgn(NETACPmailboxChan);
		return;
	}

/* Parse the message */
	switch(MessageType) {
	case MSG$_NETSHUT:	/* Network shutting down - close our channel */
		logger(1, "VMS_DECNET, DECnet shutting down message.\n");
		sys$dassgn(NETACPchan); sys$dassgn(NETACPmailboxChan);
		return;
	case MSG$_CONNECT:	/* Inbound connection */
		DECnet_accept(name, unit, ncb, NCBsize);
		return;
	default:		/* Log it and continue */
		logger(1, "VMS_DECNET, Unrecgnised message type %d received from NETACP\n",
			MessageType);
		return;
	}
}

/*
 | Accept a reuqest from DECnet: Open a channel for it, tell NETACP that we
 | accept it, and queue a receive for it.
 */
DECnet_accept(DeviceName, DeviceNumber, ncb, NCBsize)
char	*DeviceName, *ncb;
int	DeviceNumber, NCBsize;
{
	int	status, DECnet_receive_ast();
	char	device[] = "_NET:";
	struct DESC	DeviceDesc, NCBdesc;
	short	channel, iosb[4];

/* If DECnetPassivechannel is not zero, then we are in the middle of accepting another
   connection. If so - reject this one.
*/
	if(DECnetPassivechannel != 0) {
		NCBdesc.address = ncb; NCBdesc.type = 0;
		NCBdesc.length = NCBsize;
		status = sys$qiow((long)(0), NETACPchan,
				(short)(IO$_ACCESS|IO$M_ABORT), iosb,
				(long)(0), (long)(0),
				(long)(0), &NCBdesc,
				(long)(0),
				(long)(0), (long)(0) ,(long)(0));
			/* Notify when message written to mailbox */
		if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
			logger(1, "VMS_DECNET, Can't reject connection, status=%d, iosb=d^%d\n",
				status, (int)(iosb[0]));
			sys$dassgn(NETACPchan);
		}
		return;
	}

/* Create another channel to _NET: and accept the connection on it */
	DeviceDesc.address = device; DeviceDesc.type = 0;
	DeviceDesc.length = strlen(device);
	status = sys$assign(&DeviceDesc, &channel, (int)(0), (int)(0));
	if((status & 0x1) == 0) {
		logger(1, "VMS_DECNET, Can't assign channel to %s, status=%d\n",
			device, status);
		return;
	}

	NCBdesc.address = ncb; NCBdesc.type = 0;
	NCBdesc.length = NCBsize;
/* Queue a receive on it and wait for the initial control block from other side */
	status = sys$qiow((long)(0), channel,
			(short)(IO$_ACCESS), iosb,
			(long)(0), (long)(0),
			(long)(0), &NCBdesc,
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger(1, "VMS_DECNET, Can't accept connection, status=%d, iosb=d^%d\n",
			status, (int)(iosb[0]));
		sys$dassgn(channel);
		return;
	}

/* Queue a receive on this line */
	DECnetPassivechannel = channel;
	status = sys$qio((long)(0), channel,
			(short)(IO$_READVBLK), DECnetPassiveIosb,
			DECnet_receive_ast, (int)(-1),	/* Index = -1 */
			&DECnetPassiveControlBlock, (long)(sizeof(struct VMctl)),
			(long)(0),
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if((status & 0x1) == 0) {
		logger(1, "VMS_DECNET, Can't queue read for %s, status=%d\n",
			device, status);
		sys$dassgn(channel);
		DECnetPassivechannel = 0;
		return;
	}
/* The rest of the work will be done by Receive_ast */
}

/*
 | The other side sent us the OPEN control record. Read it, find the correct line
 | and start it. Also zero DECnetPassivechannel so we can accept another new connection.
 */
accept_DECnet_control_record()
{
	register int	i, Index, status, TempVar;
	char	HostName[10], Exchange[10], *p;

/* Check that the received control block is ok: */
	if((DECnetPassiveIosb[0] & 0x1) == 0) {
		logger(2, "VMS_DECNET, Error reading when \
accepting connection: iosb.stat=d^%d\n",
			(int)(DECnetPassiveIosb[0]));
		sys$dassgn(DECnetPassivechannel);	/* Forget it */
		DECnetPassivechannel = 0;
		return;
	}

/* Check that we've received enough information */
	if(DECnetPassiveIosb[1] < sizeof(struct VMctl)) {
		logger(1, "VMS_DECNET, Received too small control record\n");
		goto RetryConnection;
	}
/* Check first that this is an OPEN block. If not - reset connection */
	if(strncmp(DECnetPassiveControlBlock.type, "OPEN", 4) != 0) {
		logger(1, "VMS_DECNET, Illegal control block '%s' received\n",
			DECnetPassiveControlBlock.type);
		goto RetryConnection;
	}

/* OK, assume we've received all the information - get the hosts names from the
   control block and check the names.
*/
	strcpy(HostName, DECnetPassiveControlBlock.Rhost);
	strcpy(Exchange, DECnetPassiveControlBlock.Ohost);

/* Verify that he wants to call us */
	if(strncmp(Exchange, LOCAL_NAME, strlen(LOCAL_NAME)) != 0) {
		logger(2, "VMS_DECNET, Host %s incorrectly connected to us (%s)\n",
			HostName, Exchange);
		goto RetryConnection;
	}
/* Look for its line */
	for(Index = 0; Index < MAX_LINES; Index++) {
		if(compare(IoLines[Index].HostName, HostName) == 0) {
			/* Found - now do some checks */
			if(IoLines[Index].type != DECNET) {	/* Illegal */
				goto RetryConnection;
			}
			if((IoLines[Index].state != LISTEN) &&	/* Break its previous connection */
			   (IoLines[Index].state != RETRYING)) {
				IoLines[Index].state = INACTIVE;
				restart_channel(Index);
				/* Will close line and put it into correct state */
			}
/* Copy the parameters from the Accept block, so we can post a new one */
			strcpy(DECnetPassiveControlBlock.type, "ACK");
			IoLines[Index].channel = DECnetPassivechannel;
			DECnetPassivechannel = 0;	/* Signal that it is free now */
			IoLines[Index].state = DRAIN;
			IoLines[Index].RecvSize =
				IoLines[Index].TcpState = 0;
/* Send and ACK block - transpose tyhe fields */
			strcpy(Exchange, (DECnetPassiveControlBlock.Rhost));
			strcpy((DECnetPassiveControlBlock.Rhost),
				(DECnetPassiveControlBlock.Ohost));
			strcpy((DECnetPassiveControlBlock.Ohost), Exchange);
			queue_receive(Index);	/* Queue a receive for it */
			status = sys$qiow((long)(0), IoLines[Index].channel,
				(short)(IO$_WRITEVBLK), IoLines[Index].iosb,
				(long)(0), (long)(0),
				&DECnetPassiveControlBlock,
				(int)(sizeof(struct VMctl)),
				(long)(0), (long)(0), (long)(0), (long)(0));
			return;
		}
	}

/* Line not found - log it, and dismiss the connection */
	logger(2, "VMS_DECNET, Can't find line for host '%s'\n", HostName);
/* Send a reject to other side and re-queue the read */
RetryConnection:
	logger(2, "VMS_DECNET, Rejecting connection request\n");
	strcpy(DECnetPassiveControlBlock.type, "NAK");
	strcpy(DECnetPassiveControlBlock.Rhost, LOCAL_NAME);
	status = sys$qiow((long)(0), DECnetPassivechannel, (short)(IO$_WRITEVBLK),
		(long)(0), (long)(0), (long)(0), 
		&DECnetPassiveControlBlock, (int)(sizeof(struct VMctl)),
		(long)(0), (long)(0), (long)(0), (long)(0));

	sys$dassgn(DECnetPassivechannel);
	DECnetPassivechannel = 0;
}
