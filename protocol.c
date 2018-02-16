/* PROTOCOL.C   V4.5
 | Copyright (c) 1988,1989,1990,1991,1992 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use or misuse of this software.
 |
 | Do the protocol handling.
 | The IoLines.SubState Should change in the future to per-stream instead of 
 | per-channel.
 | restart_channel() should also be modified for multi streams.
 | FCS bits are not checked. Only the wait-a-bit is checked. When we get such
 | thing, a log at level 1 is produced. Change this log level to be 2 after this
 | module is debugged.
 | NOTE: Ascii_to_Ebcdic, Pad-Blanks & BCOPY are macros. Hence, no auto indexing
 |       should be used when calling them; they are using the variable TempVar.
 | The function that deblocks incoming records assumes for some types that they
 | are a single-record block.
 |
 | MORE things to do:
 | 3. Statistics computation: Count Null blocks as ACKS also.
 | 4. Send-njh: ADD maximal record length.
 | 5. Handle prepare mode on idle line.
 | 6. MOve the Open_Recv_file into RECV_FILE so it can open it with Fortran
 |    cariage control if needed.
 |
 | V1.1 - Set the value of NDHVIDEV when sending a file.
 | V1.2 - Send multiple blocks in one VMNET buffer, if the line is of type
 |        TCP.
 | V1.3 - Temporary: When RETSART_CHANNEL is called - close line.
 | V1.4 - Modify DECnet handling to not send ACKs and to use full-duplex.
 | V1.5 - When requeing a file in RESTART_CHANNEL send its size also.
 | V1.6 - Inform registered users when a link change state (in Rstart-Channel
 |        and in SignOn).
 | V1.7 - Insert the form code QUIET into NDHGFORM, unless the flag F_NOQUIET
 |        is sepcified (this flag is set if the user use /NOQUIET).
 | V1.8 - Add time in NJH records.
 | V1.9 - Correct the QUIET handling. It was incorrect.
 | V2.0 - Input-arrived: VMnet tends to preceed some records with SYN... Remove it.
 | V2.1 - Add support for MultiNet TcpIp package.
 | V2.2 - Change the behaviour of TcpIp buffer management - use WAIT_V_A_BIT
 |        instead of DONT_ACK bit.
 | V2.3 - When restarting a link, clear all the flags that are not permanent.
 | V2.4 - Add support for DEC's TcpIp package.
 | V2.5 - If the line type is DMB (so we use BISYNC on the DMB firmware), then
 |        we don't have to check the CRC on received messgaes. Instead, we
 |        have to remove only double DLEs. Hence, instead of calling to CKECK-CRC
 |        we call REMOVE-DLES.
 | V2.6 - Add support for binary mode. In places where we check for the message's
 |        format (ASCII or EBCDIC) we added also check for BINARY.
 | V2.7 - When a line is of type TcpIp and restart_channel is called, it changes
 |        its state to INACTIVE always, thus causing the TCP link to be disconnected
 |        and the line returns to LISTEN or RETRY. This is because restarts are
 |        good on BiSync lines but not on reliable ones.
 | V2.8 - Change the messages sent to INFORM users. Add the previous line's state.
 |        Also correct the Restart-channel for TCP links - do not close the
 |        channel in states where it is already closed.
 | V2.9 - Change casting and SWAP_xxx routines.
 | V3.0 - When a singonff is received we drain the line. Now we change it to
 |        INACTIVE in case of TCP channels, so the link will be disconnected.
 | V3.1 - 21/2/90 - Correction to 3.0 - we change it to TCP_SYNC, so it'll
 |        also call close-line.
 |        Replace BCOPY macro calls with calls to memcpy().
 | V3.2 - 22/2/90 - 1. Replace all checks to TCP/DECnet links with the checks
 |        for F_RELIABLE flag.
 |        2. Replace all htons() and htonl() calls with byte-division to prevent
 |        word/longword writes over odd addresses.
 |        3. Change all calls to Queue_timer from short to int.
 | V3.3 - 6/3/90 - When a line is restarted we queue the file back. Up to now
 |        we queued it back directly to the line on which it was sent. Now, we
 |        call Queue_file() to queue it back to the link or to the alternate
 |        route.
 | V3.4 - 19/3/90 - Clear the fast open flag. As we are not yet receiving files
 |        with this flag we didn't notice this error.
 | V3.5 - 19/3/90 - Restart_Channel() - If a line has changed state to INACTIVE
 |        and we are during shutdown process, change its state to SIGNOFF in
 |        order to prevent further actions on it.
 | V3.6 - 27/3/90 - Make some long functions more modular = split them.
 | V3.7 - 9/6/90 - Send_Njh-Dsh: Some information (addresses, file name, etc)
 |        was saved in a static area between the preparation of NJH header and
 |        DSH header in order to save processing time. However, if more than
 |        one link were sending NJH and DSH fields at the same time, addresses
 |        could "swap" between the two files. This is fixed now by re-computing
 |        these fields.
 | V3.8 - 11/6/90 - Upcase the addresses and nodenames when creating the
 |        network-level headers (IBM don't like lower case...).
 | V3.9 - 7/10/90 - 1. When logging error print the hostname instead of line #.
 |        2. After closing a line due to shutdown force it to be in SIGNOFF state
 |        as Close_line() tends to place VMnet links in RETRY.
 |        3. OPen_recv_file() is passed also the stream number.
 |        4. Income_file_request() changed to handle multi streams.
 |        5. Wherever InStreamXxxx appears, or Delete_file() and Rename_file()
 |           for output are called, the stream number was added.
 | V4.0 - 7/3/91 - Add DSV-11 support (same as DMB in this context).
 | V4.1 - 11/3/91 - Add multi-stream support on transmit also.
 | V4.2 - 7/5/91 - When generating the NDH, for Print files set NDHGRFCM to
 |        80 for punch, 44 for PRINT+ASA, 40 for PRINT. Raise NDHGLREC by one
 |        for PRINT with ASA (the carriage control adds this one).
 | V4.3 - 14/6/91 - Replaced some Logger(1) with Logger(2).
 | V4.4 - 29/7/92 - When signon is sent clear all streams to INACTIVE. In this
 |        version its timing was modified.
 | V4.5 - 30/7/92 - If INCLUDE_TAG is defined send the dataset header using
 |        fragments. This option is supported only if all your links are of
 |        VMnet type.
 */
#include "consts.h"
#include "headers.h"
#include <errno.h>

EXTERNAL struct	LINE	IoLines[MAX_LINES];

EXTERNAL struct NEGATIVE_ACK	NegativeAck;
EXTERNAL struct POSITIVE_ACK	PositiveAck;
EXTERNAL struct ENQUIRE		Enquire;
EXTERNAL struct SIGNON		InitialSignon, ResponseSignon;
EXTERNAL struct SIGN_OFF		SignOff;
EXTERNAL struct JOB_HEADER	NetworkJobHeader;
EXTERNAL struct DATASET_HEADER	NetworkDatasetHeader;
EXTERNAL struct JOB_TRAILER	NetworkJobTrailer;
EXTERNAL struct EOF_BLOCK		EOFblock;
EXTERNAL struct PERMIT_FILE	PermitFile;
EXTERNAL struct REJECT_FILE	RejectFile;

EXTERNAL int	MustShutDown;

#define	SS_ABORT	44		/* VMS Abort code */
#define	SS_TIMEOUT	556

#define	INITIAL_SIGNON	1		/* For send_singon routine */
#define	FINAL_SIGNON	2

#define	EXPLICIT_ACK	0		/* ACK was received */
#define	IMPLICIT_ACK	1		/* DLE-STX block also acks */
#define	INPLICIT_ACK	2		/* Idle line - delayed ack */

#define	NJH		0		/* Send NJH record */
#define	DSH		1		/* Send DSH record */

/* A macro to send either DEL+ACK0 or an empty block as ACK */
unsigned char	NullBuffer = 0;

/* Send an ACK or Null-buffer, depending on the line's state. If the line is
   of TCP type - don't send ACks of any type, since this is a reliable link,
   and no ACKS are expected. DECnet expects ACKs, since it has problems working
   full duplex.
*/
#define	SEND_ACK() { \
	if(((temp->flags & F_RELIABLE) == 0) ||	/* These have to get ACK always */ \
	   (temp->state != ACTIVE)) { \
		if(((temp->flags & F_ACK_NULLS) == 0) ||	/* Use normal ACK */ \
		    (temp->state != ACTIVE)) \
			send_data(Index, &PositiveAck, \
				(sizeof(struct POSITIVE_ACK)), \
				SEND_AS_IS); \
		else \
			send_data(Index, &NullBuffer, 1, \
				ADD_BCB_CRC); \
	} \
}

char	*strchr();

/*
 | Try recovery from error. If there are only few errors on the line, try
 | sending NAK. If there are too much errors - restart the line.
 | Two error counters are kept: TotalError which counts all errors on the
 | line since program startup, and Errors which counts the number of errors
 | since the last seccessfull operation on the line.
 */
error_retry(Index, temp)
int	Index;
struct LINE	*temp;
{
	temp->TotalErrors++;	/* Count total errors on line */
	if((temp->errors++) < MAX_ERRORS) {	/* Not too much errors */
		send_data(Index, &NegativeAck,
			(sizeof(struct NEGATIVE_ACK)),
			SEND_AS_IS);
		return;
	} 
	else {	/* Too much errors. Retstart the line */
		logger(2, "PROTOCOL: Too much error on line %s, restarting.\n",
			temp->HostName);
		restart_channel(Index);
		return;
	}
}


/*
 | Restart a channel - I.e.: close files, delete output files. In the future,
 | it should scan all streams, not only the first one.
 | If there are interactive messages/commands waiting on the line, clear
 | them, and free their memory.
 | If there is a file in transit, re-queue it.
 */
restart_channel(Index)
int	Index;
{
	struct	LINE	*temp;
	struct	MESSAGE	*MessageEntry;
	register long	i, PreviousState;	/* To know whether to inform state change */

	temp = &(IoLines[Index]);
	logger(2, "Rstart_channel. Line=%s, type=%d\n", temp->HostName,
			temp->type);

/* Close active file, and delete of output file. */
	for(i = 0; i < temp->MaxStreams; i++) {
		if((temp->OutStreamState[i] != S_INACTIVE) &&
		   (temp->OutStreamState[i] != S_REFUSED)) {	/* File active */
			close_file(Index, F_INPUT_FILE, i);
			queue_file((temp->OutFileParams[i]).OrigFileName,
				temp->OutFileParams[i].FileSize);	/* Requeue file */
		}
	}
	for(i = 0; i < temp->MaxStreams; i++) {
		if((temp->InStreamState[i] != S_INACTIVE) &&
		   (temp->InStreamState[i] != S_REFUSED)) {	/* File active */
			delete_file(Index, F_OUTPUT_FILE, i);
		}
	}

/* Dequeue all messages and commands waiting on it */
	while(temp->MessageQstart != 0) {
		MessageEntry = temp->MessageQstart;
		if(MessageEntry->next == NULL)	/* End of list */
			temp->MessageQstart = temp->MessageQend = NULL;
		else
			temp->MessageQstart = MessageEntry->next;
		free(MessageEntry);
	}

/* Clear all queued transmit buffers */
	while(temp->FirstXmitEntry != temp->LastXmitEntry) {
		free((temp->XmitQueue)[temp->FirstXmitEntry]);
		temp->FirstXmitEntry = ++(temp->FirstXmitEntry) % MAX_XMIT_QUEUE;
	}

/* After all files closed, restart the line */
	temp->errors = 0;	/* We start again... */
	temp->InBCB = temp->OutBCB = 0;
	temp->flags &= (~F_RESET_BCB & ~F_WAIT_A_BIT & ~F_WAIT_V_A_BIT &
			~F_WAIT_V_A_BIT & ~F_SENDING & ~F_CALL_ACK &
			~F_XMIT_CAN_WAIT);
	temp->CurrentStream = temp->ActiveStreams = 0;
	temp->FreeStreams = temp->MaxStreams;
	for(i = 0; i < temp->MaxStreams; i++)
		temp->InStreamState[i] = temp->OutStreamState[i] = S_INACTIVE;
	temp->MessageQstart = temp->MessageQend = 0;
	temp->RecvSize = temp->XmitSize = 0;
	temp->FirstXmitEntry = temp->LastXmitEntry = 0;

/* Dequeue all waiting timeouts for this line */
	delete_line_timeouts(Index);

/* Send the ENQ block again, only if the line was before in some active state.
   If it wasn't, then this call is because we just want to close the files.
   If the line is of type TCP, we close it in any case (put it in INACTIVE state).
   We do not do it only during the initial signon since NAKs are exchanged there.
*/
	PreviousState = temp->state;
	if(((temp->flags & F_RELIABLE) != 0) && (temp->state != DRAIN))
		temp->state = TCP_SYNC;	/* This will cause it to call close_line */

	switch(temp->state) {
	case DRAIN:
	case I_SIGNON_SENT:
	case F_SIGNON_SENT:
	case ACTIVE:		/* Restart it and put in DRAIN mode */
		temp->state = DRAIN;
		send_data(Index, &Enquire, (sizeof(struct ENQUIRE)),
			SEND_AS_IS);
/* Inform registered users about line being deisabled */
		if(InformUsersCount > 0) {
			if(temp->state != PreviousState) {
				inform_users_about_line(Index, PreviousState);
			}
		}
		break;
	case TCP_SYNC:	/* Line inactive */
	case SIGNOFF:
		temp->state = INACTIVE;
		close_line(Index);	/* Line is disabled, so close it */
	case INACTIVE:	/* On all the other types - the channel is closed */
	case LISTEN:
	case RETRYING:
/* Inform registered users about line being deisabled. If we were called in this
   state, then it is sure that the previous state was different. */
		temp->state = RETRYING;
		if(InformUsersCount > 0) {
			inform_users_about_line(Index, PreviousState);
		}
		break;
	default:
		logger(1, "PROTOCOL, Illegal line state=%d in Restart_chan\n",
			temp->state);
	}

/* Check whether we are in shutdown process. If so, and the line has changed
   state to INACTIVE, change its state to SIGNEDOFF */
	if(MustShutDown == -1)
		if((temp->state == INACTIVE) ||
		   (temp->state == RETRYING) ||
		   (temp->state == LISTEN))
			temp->state = SIGNOFF;
}


/*
 | Some input has been arrived from some line.
 */
input_arrived(Index, status, buffer, size)
int	Index,		/* Index in IoLines */
	status,		/* VMS I/O status of read */
	size;
unsigned char	*buffer;
{
	struct	LINE	*temp;
	unsigned char	*p, *q;

	temp = &(IoLines[Index]);

	logger(3, "PROTOCOL: Input from line %s, status=%d, size=%d\n",
		temp->HostName, status, size);

/* First check the status. If error, then continue accordingly */
	if((status & 0x1) == 0)
		return input_read_error(Index, status, temp);

/* No error, something was received - handle it */
	p = buffer;

/* VMnet tends to preceed some buffers with SYN... remove it */
	while((*p == SYN) && (size > 0)) {
		*p++; size--;
	}

	logger(4, "PROTOCOL: line=%s, Data received:\n", temp->HostName);
	trace(p, size, 5);

/* Now, check the code. Check first for the 3 known blocks which has a special
   structure (SOH-ENQ, NAK and ACK) */
	q = p; *q++;	/* q points to the next character */
	((temp->stats).TotalIn)++;
	if((*p == DLE) && (*q == STX))
		return handle_text_block(Index, p, size);
	if(*p == NAK)
		return handle_nak(Index);

	if((*p == SOH) && (*q == ENQ))
		return handle_enq(Index);
	if((*p == DLE) && (*q == ACK0)) {
		handle_ack(Index, EXPLICIT_ACK); return;
	}

/* If we are here, this block has an invalid format */
	logger(1, "PROTOCOL, Illegal block format (line=%s):\n",
		temp->HostName);
	trace(buffer, size, 1);
	error_retry(Index, temp);
}


/*
 | The input routine has returned error status. Try recovering if we are in
 | ACTIVE state and this is not a repititive error.
 | Called from Input_arrived() when the input routine returns error.
 */
input_read_error(Index, status, temp)
int	Index, status;
struct LINE	*temp;
{
	logger(2, "PROTOCOL: Read error, status=%d\n", status);
	temp->TotalErrors++;	/* Increment total errors */
/* We handle here only timeouts which has one of the two codes: */
	if((status != SS_ABORT) && (status != SS_TIMEOUT))
		return;		/* Not it, we can't handle it */
	switch(temp->state) {
	case ACTIVE:	/* There is some activity. Try recovery */
		error_retry(Index, temp); return;
	case DRAIN:	/* try to start line again */
		send_data(Index, &Enquire, (sizeof(struct ENQUIRE)),
			SEND_AS_IS);
		return;		/* Continue to send ENQ */
	case I_SIGNON_SENT:
	case F_SIGNON_SENT:
		restart_channel(Index); return;	/* Restart all files */
	default: return;	/* Line is not active */
	}
}

/*
 | Handle a NACK received. Check error count. If still small, retransmit last
 | buffer sent.
 */
handle_nak(Index)
int	Index;
{
	struct	LINE	*temp;

	temp = &(IoLines[Index]);
	temp->TotalErrors++;	/* Count in total errors for that line */

/* Check in which state are we */
	switch(temp->state) {
	case DRAIN:
	case I_SIGNON_SENT:
	case F_SIGNON_SENT:
		restart_channel(Index); return;
	case ACTIVE:
		/* Try re-sending last buffer */
		temp->TotalErrors++;	/* Count total errors on line */
		if((temp->errors++) < MAX_ERRORS) {
			logger(2, "PROTOCOL: NAK recieved. Re-sending last buffer\n");
			/* Not too much errors, re-send last buffer */
			send_data(Index, temp->XmitBuffer,
				temp->XmitSize, SEND_AS_IS);
			return;
		} 
		else {	/* Too much errors. Retstart the line */
			logger(2, "PROTOCOL: Too much NAKs on line %s, restarting.\n",
				temp->HostName);
			restart_channel(Index); return;
		}
	default:;	/* Ignore in other states */
	}
	/* Other states - simply ignore it */
	return;
}

/*
 | Handle the SOH-ENQ block. If we are in the starting position, ack it. In
 | any other place, restart the channel, since the other party is also
 | starting.
 */
handle_enq(Index)
int	Index;
{
	struct	LINE	*temp;

	temp = &(IoLines[Index]);
	temp->errors = 0;		/* Clear error count. */
	switch(temp->state) {
	case DRAIN:	/* Send the ACK block to start Sign-on */
		send_data(Index, &PositiveAck,
			(sizeof(struct POSITIVE_ACK)),
			SEND_AS_IS);
		return;
	default: restart_channel(Index);	/* Reset the line */
		return;
	}
}

/*
 | Handle an ACK block. If we are in starting position, its time to send
 | the initial signon record. If not, Then it acks something.
 | The flag tells us whether this was an ACK block (EXPLICIT_ACK), or whether
 | we got a text block which is an implicit ack (IMPLICIT_ACK).
 */
handle_ack(Index, flag)
int	Index;
short	flag;	/* Is this an implicit or explicit ACK? */
{
	register struct	LINE	*temp;
	struct	QUEUE	*FileEntry;	/* File queue for the line */
	struct	MESSAGE	*MessageEntry;	/* Messages' queue for the line */
	register long	i, size, position, MaxSize, TempVar;

	temp = &(IoLines[Index]);
	temp->errors = 0;		/* Clear error count. */

/* Should we wait for ACK at all? */
	switch(temp->state) {
	case INACTIVE:
	case SIGNOFF:
	case I_SIGNON_SENT:	/* This is an illegal ACK */
		logger(2, "PROTOCOL: Illegal ACK. state=d^%d, line=%s\n",
			temp->state, temp->HostName);
		restart_channel(Index);
		return;
	case DRAIN:	/* Is is the ACK for the Enquire - initiate singon */
		temp->OutBCB = 0; temp->flags &= ~F_RESET_BCB; /* Reset BCB was sent in the first packet */
		for(i = 0; i < temp->MaxStreams; i++)
			temp->InStreamState[i] = temp->OutStreamState[i] = S_INACTIVE;
			/* Line is starting - streams are all idle */
		send_signon(Index, INITIAL_SIGNON);
		temp->state = I_SIGNON_SENT;
		return;
	case F_SIGNON_SENT:
		temp->state = ACTIVE;	/* Line has finished signon */
		break;
	}

/* Check whether we are in the Wait-a-bit mode. If so - only send Acks. Send
   Ack immediately if something was received from the other side. If not,
   send it after a delay of 1 second as the line is inactive.
   WAIT_A_BIT is from the NJE block header. WAIT_V_A_BIT is from VMnet.
*/
	if(((temp->flags & F_WAIT_A_BIT) != 0) ||
	   ((temp->flags & F_WAIT_V_A_BIT) != 0)) {
		if(flag != EXPLICIT_ACK) {
			 logger(4, "PROTOCOL: Sending ACK\n");
			 SEND_ACK();
			  return;
		}
		/* Nothing was received - delay the ACK */
		/* Queue it only if we'll have to send it */
		if(((temp->flags & F_RELIABLE) == 0) ||
		   (temp->state != ACTIVE))
			queue_timer(1, Index, T_SEND_ACK);
		return;
	}

/* It is ACK for something - test for what. Test only outgoing streams, since
   explicit ACK can't come when we receive a stream...
   Before we check it, check whether there is an interactive message waiting.
   If so, send it and don't handle this ACK farther as it is impossible to mix
   NMRs and other records in the same block. If we have interactive
   messages waiting, then try to block as much as we can in one block.
*/
	if(temp->MessageQstart != NULL)
		return fill_message_buffer(Index, temp);

/* No interactive message - handle the ack */
/* Check whether we have a file waiting for sending and free streams to initiate it */
	if((temp->QueueStart != 0) && (temp->FreeStreams > 0))
		if(request_to_send_file(Index, temp) != 0)
			return;	/* If 0 - we have to send an ACK */

/* Check whether there is another active stream. If so, switch to it. */
	if(temp->MaxStreams > 1) {	/* No need to do it if only one stream */
		for(i = (temp->CurrentStream + 1) % temp->MaxStreams;	/* Don't jump over maximum streams defined for this line */
		    i != temp->CurrentStream; i = (i + 1) % temp->MaxStreams)
			if((temp->ActiveStreams & (1 << i)) != 0)
				break;	/* Found an active stream */
		temp->CurrentStream = i;
	}

	switch(temp->OutStreamState[temp->CurrentStream]) {
/* If there is a file to send, open it ans send a request to initiate a stream.
*/
	case S_REQUEST_SENT:	/* It is ack for the request. ACK it back */
		SEND_ACK(); return;
	case S_INACTIVE:	/* Send another ACK, but after 1 second delay. */
/* Check whether the line has to signoff */
		if((temp->flags & F_SHUT_PENDING) != 0) {
			if((temp->InStreamState[temp->CurrentStream] == S_INACTIVE) ||
			   (temp->InStreamState[temp->CurrentStream] == S_REFUSED)) {	/* Can shut */
				send_data(Index, &SignOff,
					(sizeof(struct SIGN_OFF)),
					ADD_BCB_CRC);
				temp->state = SIGNOFF;
				logger(2, "PROTOCOL, Line %s signedoff due to operator request\n",
					temp->HostName);
				inform_users_about_line(Index, -1);
				close_line(Index);
				temp->state = SIGNOFF;	/* Close_line() change the state
							   to Retry, and we don't want it... */
				can_shut_down();	/* Check whether the daemon need shut */
				return;
			}
	/* Stream not idle - Ack and don;t open a new file */
			SEND_ACK();
			return;
		}
	case S_REFUSED:	/* If refused - just chitchat. Handle also INACTIVE states
			   when there is nothing to send */
		if(flag != EXPLICIT_ACK) {
/* Ack it right away, since it is either an implicit ACK (which came with data)
   so we must ack it immediately, or because this is a delayed ACK, which we
   should now send (the line is idle, so the ack was not sent immediately the
   previous time).
*/
			 logger(4, "PROTOCOL: Sending ACK\n");
			  SEND_ACK();
			  return;
		}
/* Queue it only if we'll have to send it on an idle BiSync line */
		if(((temp->flags & F_RELIABLE) == 0) ||
		   (temp->state != ACTIVE))
			queue_timer(1, Index, T_SEND_ACK);
		return;
	case S_NJH_SENT:
		logger(3, "PROTOCOL: Sending Dataset header\n");
		temp->flags |= F_XMIT_CAN_WAIT;	/* We support TCP lines here */
		send_njh_dsh_record(Index, DSH);
		temp->OutStreamState[temp->CurrentStream] = S_NDH_SENT;
/* If it is VMNET protocol, and more room in transmit buffers, don't return;
   fall to next transmit block */
		temp->flags &= ~F_XMIT_CAN_WAIT;
		if((temp->flags & F_XMIT_MORE) == 0)
			return;
	case S_NDH_SENT:
		logger(3, "PROTOCOL: Starting file transmission\n");
		temp->OutStreamState[temp->CurrentStream] = S_SENDING_FILE;	/* We start sending */
		(temp->OutFileParams[temp->CurrentStream]).RecordsCount = 0;
		temp->flags |= F_XMIT_CAN_WAIT;	/* We support TCP lines here */
		if((((temp->OutFileParams[temp->CurrentStream]).type & F_FILE) != 0) &&
		   ((temp->OutFileParams[temp->CurrentStream]).format != EBCDIC))
			send_netdata_file(Index, 0);
				/* 0 = Send INMR records */
		else
			send_file_buffer(Index);	/* send the file */
/* If it is VMNET protocol, and more room in transmit buffers, don't return;
   fall to next transmit block */
		temp->flags &= ~F_XMIT_CAN_WAIT;
		if((temp->flags & F_XMIT_MORE) == 0)
			return;
	case S_SENDING_FILE:	/* We are in the middle of the file */
SendAgain:
		logger(3, "PROTOCOL: Sending next file's buffer\n");
		temp->flags |= F_XMIT_CAN_WAIT;
		if((((temp->OutFileParams[temp->CurrentStream]).type & F_FILE) != 0) &&
		   ((temp->OutFileParams[temp->CurrentStream]).format != EBCDIC))
			send_netdata_file(Index, 1);
				/* 1 = INMR records sent already */
		else
			send_file_buffer(Index);	/* Send as punch */
/* If it is VMNET protocol, and more room in transmit buffers, don't return;
   fall to next transmit block */
		temp->flags &= ~F_XMIT_CAN_WAIT;
		if((temp->flags & F_XMIT_MORE) == 0)
			return;
		else
			if(temp->OutStreamState[temp->CurrentStream] == S_SENDING_FILE)
				goto SendAgain;
			/* else - fall to send-njt... */
	case S_EOF_FOUND:	/* Send the NJT block */
		logger(3, "PROTOCOL: Sending NJT.\n");
/* If we send EBCDIC files and TCP line, we fall here by mistake. In this case,
   do not send NJT, since it was already sent as part of stored file.
*/
		if(temp->OutStreamState[temp->CurrentStream] != S_NJT_SENT) {
			temp->OutStreamState[temp->CurrentStream] = S_NJT_SENT;
			temp->flags |= F_XMIT_CAN_WAIT;
			send_njt(Index);
			temp->flags &= ~F_XMIT_CAN_WAIT;
/* If it is VMNET protocol, and more room in transmit buffers, don't return;
   fall to next transmit block */
			if((temp->flags & F_XMIT_MORE) == 0)
				return;
		}
	case S_NJT_SENT:
		/* The NJT was sent and ACKED. Send EOF now. */
		EOFblock.RCB = (((temp->CurrentStream + 9) << 4) | 0x9);
		temp->OutStreamState[temp->CurrentStream] = S_EOF_SENT;
/* Since we do not set the flag F_XMIT_CAN_WAIT, this will force the TCP laier
   to send the data, even if the buffer is not full. */
		temp->flags &= ~F_XMIT_CAN_WAIT;
		send_data(Index, &EOFblock,
			(sizeof(struct EOF_BLOCK)),
			ADD_BCB_CRC);
		return;
	case S_EOF_SENT:	/* We are waiting now for the final completion block */
		logger(3, "PROTOCOL: EOF sent and confirmed by ACK. ACKED back.\n");
		SEND_ACK();
		return;
	default: logger(1, "PROTOCOL: Line %s, ACK received when line operation=%d. Illegal.\n",
			temp->HostName, temp->OutStreamState[temp->CurrentStream]);
		restart_channel(Index);
		return;
	}
}


/*
 | Called from Handle_ack() when there are NMRs to send to other side. Try
 | blocking as much messages as you can and send them.
 */
fill_message_buffer(Index, temp)
int	Index;
struct LINE	*temp;
{
	struct	QUEUE	*FileEntry;	/* File queue for the line */
	struct	MESSAGE	*MessageEntry;	/* Messages' queue for the line */
	unsigned char	buffer[MAX_BUF_SIZE];
	register long	size, position, MaxSize, TempVar;

	MaxSize = temp->MaxXmitSize - 20;	/* The place we have
						   after counting overheads */
	position = 0;
	while(temp->MessageQstart != NULL) {
		size = temp->MessageQstart->length;
		if((position + size) > MaxSize) break;	/* No room for more */
		memcpy(&buffer[position],
			(unsigned char *)(temp->MessageQstart->text),
			size);
		position += size;
		/* Dequeue this entry */
		MessageEntry = temp->MessageQstart;
		if(MessageEntry->next == NULL)	/* End of list */
			temp->MessageQstart = temp->MessageQend = NULL;
		else
			temp->MessageQstart = MessageEntry->next;
		free(MessageEntry);
	}
	/* Send the message */
	buffer[position++] = NULL_RCB;	/* Final RCB */
	send_data(Index, buffer, position, ADD_BCB_CRC);
	return;
}


/*
 | The outgoing stream is inactive and we have a file to send. Open it, and
 | if it is ok, send a request to send to the other side.
 | This function is called by Handle_ack().
 | Returns 0 if not successfull (then a normal ACK should be sent) or 1 if
 | successfull (the request to init a stream will serve as an implicit ack).
 */
request_to_send_file(Index, temp)
int	Index;
struct LINE	*temp;
{
	struct	QUEUE	*FileEntry;
	unsigned char	buffer[16];	/* To send the request block */
	int	i;

/* Dequeue the first file entry and init the various variables of the file */
	temp->QueuedFiles--;	/* Reduce count */
	FileEntry = temp->QueueStart;
	if(temp->QueueStart == temp->QueueEnd) /* Last item in queue */
		temp->QueueStart = temp->QueueEnd = 0;
	else
		temp->QueueStart = temp->QueueStart->next;

/* Find which stream is free */
	for(i = 0; i < temp->MaxStreams; i++)
		if((temp->ActiveStreams & (1 << i)) == 0)
			break;	/* Found an inactive stream */
	temp->CurrentStream = i;

/* Open the file */
	strcpy(((temp->OutFileParams)[temp->CurrentStream]).OrigFileName,
		FileEntry->FileName);	/* Save VMS file name */
	free(FileEntry);
	if(open_xmit_file(Index, temp->CurrentStream,
		((temp->OutFileParams)[temp->CurrentStream]).OrigFileName) == 0) {
		temp->OutStreamState[temp->CurrentStream] = S_INACTIVE;
		/* Some error in file, fall to send ACK */
		return 0;
	} else {
		/* Create a request block and send it */
		buffer[0] = REQUEST_RCB;	/* RCB */
		buffer[1] = (((temp->CurrentStream + 9) << 4) | 0x9);
		buffer[2] = NULL_RCB;	/* Null string */
		buffer[3] = NULL_RCB;	/* End of block */
		temp->OutStreamState[temp->CurrentStream] = S_REQUEST_SENT;
		temp->XmitSavedSize[temp->CurrentStream] = 0;	/* Init it */
		temp->FreeStreams--;		/* Made one stream active */
		temp->ActiveStreams |= (1 << temp->CurrentStream);	/* Mark the specific stream */
		send_data(Index, buffer, 4,
			ADD_BCB_CRC);
		logger(3, "Sent request for transmission.\n");
		return 1;	/* No need to send ACK */
	}
}


/*
 | A text block was received. Look what text block it is, and process
 | accordingly.
 */
handle_text_block(Index, buffer, size)
unsigned char	*buffer;
int	Index, size;
{
	int	SendAck;	/* Shall we treat this message as an
					   implicit ack? */
	struct	LINE	*temp;
	unsigned char	p, *pointer, line[MAX_BUF_SIZE];
	register int	TempVar, i;
	int		SizeConsumed;	/* Size consumed from input buffer by
					   uncompress routine */
	register short	BufSize;
	short	Ishort;

	temp = &IoLines[Index];

/* Check the received CRC. The procedure that does it also discards double
   DLE's.
   However, don't call this procedure if the line is of TCP type, since these
   lines don't send CRC characters.
*/
	if((temp->flags & F_RELIABLE) == 0) {
		if((temp->type == DMB) || (temp->type == DSV)) {	/* Just remove double DLE's */
			remove_dles(buffer, &size);
		}
		else {
			if(check_crc(buffer, &size) == 0) {
				/* CRC error */
				error_retry(Index, temp);
				return;
			}
		}
	}

	temp->errors = 0;		/* Clear error count. */

/* Check the BCB now. If incorrect - Cry... */
	p = buffer[BCB_OFFSET];
	switch(p & 0xf0) {
	case 0x80:	/* Normal block - check sequence */
		if(temp->InBCB == (p & 0x0f)) {	/* OK */
			logger(4, "Received BCB is ok.\n");
			temp->InBCB = (temp->InBCB + 1) % 16; /* Increment it */
			break;
		} else {
/* Check whether this is the preceeding BCB. If so - discard the message */
			if(temp->InBCB == (((p & 0x0f) + 1) % 16)) {
				logger(2, "PROTOCOL: Line %s, Duplicate block discarded.\n",
					temp->HostName);
				handle_ack(Index, IMPLICIT_ACK);
				return;
			}
/* BCB sequence error - probably we missed a block. Restart the line */
			logger(2, "PROTOCOL: Line %s, Incorrect BCB received(%d), expect(%d)\n",
				temp->HostName, (int)(p & 0x0f), (int)(temp->InBCB));
			restart_channel(Index);
			return;
		}
	case 0x90:	/* Bypass BCB count - ignore it and do not increment */
		break;
	case 0xa0:	/* Reset BCB */
		logger(2, "PROTOCOL: Income BCB reset to %d\n", (p & 0x0f));
		temp->InBCB = (p & 0x0f);
		break;
	default:
		logger(1, "PROTOCOL: Line %s, Illegal BCB (%x). Reseting line. Trace:\n",
			temp->HostName, (int)(p & 0x0f));
		trace(buffer, size, 1);
		restart_channel(Index);
		return;
	}

/* Check which type of block it is. Currently ignore the FCS bits.
   First check the ones that occupy a whole block and are not compressed. */
	if(buffer[RCB_OFFSET] == SIGNON_RCB)	/* Signon Control record */
		return income_signon(Index, temp, buffer);

/* Test whether the Wait-a-bit (suspend all streams) is on. If so - mark it,
   so the  routine that sends the reply will handle it properly.
*/
	if((buffer[FCS1_OFFSET] & 0x40) != 0) {
		logger(3, "PROTOCOL, Wait a bit received on line %s\n",
			temp->HostName);
		temp->flags |= F_WAIT_A_BIT;
	}
	else	/* No wait, clear the bit */
		temp->flags &= ~F_WAIT_A_BIT;

/* Loop over the block, in case there are multiple records in it (checked only
   for certain record types).
   Currently there is no check whether all records belongs to the same stream.
*/
	size -= RCB_OFFSET;	/* Update the size of message left to process */
	pointer = &(buffer[RCB_OFFSET]);
	SendAck = 1;	/* Init: Send ack to other side */
	while(size > 0) {
		p = *pointer++;	/* P holds the RCB, *pointer the SRCB */
		switch(p) {
		case NULL_RCB:	/* End of block */
				if(size > 5) {
					logger(1, "PROTOCOL, line=%s, Null RCB received when size > 5\n",
						temp->HostName);
					trace(--pointer, size + 1, 1);
					SendAck = 1;
				}
				size = 0;	/* Closes the block */
				continue;
		case REQUEST_RCB:	/* Other side wants to send a file */
			SendAck = 0;	/* No need for explicit ACK as we comfirm it with a permission block */
			size = 0;	/* Assume signle record block */
			logger(3, "PROTOCOL: Request to initiate a stream.\n");
			income_file_request(Index, temp, *pointer);
					/* Pass the stream number */
			break;
		case PERMIT_RCB:	/* Permission to us to initiate a stream */
			size = 0;	/* Assume signle record block */
			temp->CurrentStream = ((*pointer & 0xf0) >> 4) - 9; /* The stream we are talking on now */
			  if(temp->OutFileParams[temp->CurrentStream].format != EBCDIC) {
				  logger(3, "PROTOCOL: Sending NJH.\n");
				  temp->OutStreamState[temp->CurrentStream] = S_NJH_SENT;
				  send_njh_dsh_record(Index, NJH);
				  SendAck = 0;	/* We ACKed it with the NJH */
			  } else {
				  logger(3, "PROTOCOL: Sending EBCDIC file.\n");
				/* EBCDIC - NJH+NJT will be sent as part of file */
				  temp->OutStreamState[temp->CurrentStream] = S_SENDING_FILE;
				  SendAck = 1;	/* Will cause file sending */
			  }
			  break;
		case CANCEL_RCB:	/* Receiver cancel */
			size = 0;	/* Assume signle record block */
			SendAck = 0;
			logger(2, "PROTOCOL: Received negative permission or cnacel\n");
			temp->CurrentStream = ((*pointer & 0xf0) >> 4) - 9; /* The stream we are talking on now */
			close_file(Index, F_INPUT_FILE, temp->CurrentStream);	/* Close the file */
/* Hold the file as it might have caused the problem */
			rename_file(Index, RN_HOLD_ABORT, F_INPUT_FILE, temp->CurrentStream);
			temp->OutStreamState[temp->CurrentStream] = S_REFUSED;
			logger(1, "PROTOCOL, line=%s, Stream %d, Recievd RECV-CANC for block:\n",
				temp->HostName, temp->CurrentStream);
			trace(temp->XmitBuffer, temp->XmitSize, 1);
			break;
		case COMPLETE_RCB:	/* Transmission complete */
			size = 0;	/* Assume signle record block */
			temp->CurrentStream = ((*pointer & 0xf0) >> 4) - 9; /* The stream we are talking on now */
			logger(3, "PROTOCOL: Ack stream completed.\n");
			delete_file(Index, F_INPUT_FILE, temp->CurrentStream);
			temp->OutStreamState[temp->CurrentStream] = S_INACTIVE;
			SendAck = 1;	/* Ack it... */
			temp->FreeStreams++;
			temp->ActiveStreams &= ~(1 << temp->CurrentStream);	/* Clear its bit */
			break;
		case READY_RCB: logger(1, "Ready to receive a stream. Ignored\n");
			SendAck = 1;
			size = 0;
			break;
		case BCB_ERR_RCB: logger(2, "BCB sequence error\n");
			restart_channel(Index);
			size = 0;	/* Assume signle record block */
			SendAck = 0;	/* No need to ack it */
			break;
		case NMR_RCB: 	/* Nodal message received */
			logger(3, "Incoming NMR RCB\n");
			i = uncompress_scb(++pointer, line, size,
				(sizeof line), &SizeConsumed);
			/* SizeConsumed - number of bytes consumed from
			   input buffer */
			  handle_NMR(line, i);
			size -= (SizeConsumed + 2);	/* +2 for RCB+SRCB */
			pointer += SizeConsumed;
			  SendAck = 1;
			  break;
		case SYSOUT_0:	/* Incoming records for a file */
		case SYSOUT_1:
		case SYSOUT_2:
		case SYSOUT_3:
		case SYSOUT_4:
		case SYSOUT_5:
		case SYSOUT_6:
/* Retrive the records one by one, and call Recv_file each time with one
   record. */
/* Pointer now points to SRCB, Point it back to RCB */
			line[0] = *--pointer;	/* RCB */
			line[1] = *++pointer;	/* SRCB */
			i = uncompress_scb(++pointer, &line[2], size,
				((sizeof line) - 2), &SizeConsumed);
			if(i < 0) {	/* We have an error */
				logger(2, "PROTOCOL, Disabling line %s because UNCOMPRESS error\n",
					temp->HostName);
				temp->state = INACTIVE;
				restart_channel(Index);
				return;
			}
			size -= (SizeConsumed + 2);	/* +2 for RCB_SRCB */
			pointer += SizeConsumed;
			SendAck = receive_file(Index, line, i + 2);
				/* LINE holds RCB + SRCB and the uncompressed line */
			break;
		default:  logger(1, "PROTOCOL, Unrecognized RCB: x^%x\n", p);
			bug_check("Aborting because of illegal RCB received");
		}
	}

/* Text block also acks the last block we sent - so treat it also as an ack.
   However, some functions send some records as replies, so in these case we
   don;t have to send ACK back.
*/
	if(SendAck != 0)
		handle_ack(Index, IMPLICIT_ACK);
}


/*
 | We've received a Signon record (Either Initial, final, or Signoff).
 | These records are uncompressed, thus can be handled as they are.
 | This function is called from Handle_text_block().
 */
income_signon(Index, temp, buffer)
int	Index;
struct LINE	*temp;
unsigned char	*buffer;
{
	unsigned char	p, *pointer;
	char		Aline[16];
	register int	TempVar, i;
	register short	BufSize;
	register struct SIGNON	*SignOnPointer;
	short	Ishort;

	SignOnPointer = (struct SIGNON *)(&buffer[RCB_OFFSET]);
	switch(buffer[SRCB_OFFSET]) {
	case E_I:	/* Initial signon */
		if(temp->state != DRAIN) {
			logger(1, "PROTOCOL: Line %s, Initial signon received when in state %d\n",
				temp->HostName, temp->state);
			temp->state = INACTIVE;	/* Disable the line */
			restart_channel(Index);	/* Requeue files */
			return;
		}
/* Check that the correct node is at the other side */
		EBCDIC_TO_ASCII(SignOnPointer->NCCINODE, Aline, 8);
		for(i = 7; i >= 0; i--) /* remove trailing blanks */
			if(Aline[i] != ' ') break;
		Aline[++i] = '\0';
		if(compare(Aline, temp->HostName) != 0) {
			logger(1, "Line #%d, Host '%s' tries to\
 logon instead of authorized host '%s' for this line\n",
				Index, Aline, temp->HostName);
			temp->state = INACTIVE;	/* Deactivate line */
			return;
		}
/* Check the buffer size. If smaller than our, use the received one */
		memcpy(&Ishort, &(SignOnPointer->NCCIBFSZ),
			sizeof(SignOnPointer->NCCIBFSZ));
		BufSize = ntohs(Ishort);
		if(BufSize < temp->PMaxXmitSize)
			temp->MaxXmitSize = BufSize;
		else	
			temp->MaxXmitSize = temp->PMaxXmitSize;
		temp->OutBCB = 0;
		temp->flags &= ~F_RESET_BCB;	/* Reste flag = Send reset BCB */
		for(i = 0; i < temp->MaxStreams; i++)
			temp->InStreamState[i] = temp->OutStreamState[i] = S_INACTIVE;
			/* Line starting - streams are all idle */
		send_signon(Index, FINAL_SIGNON);
		temp->state = F_SIGNON_SENT;
		logger(2, "PROTOCOL: Line %d, host %s, Signing on with bufsize=%d\n",
			Index, Aline, temp->MaxXmitSize);
		inform_users_about_line(Index, -1);
		return;
	case E_J:	/* REsponse Signon */
		if(temp->state != I_SIGNON_SENT) {
			logger(1, "PROTOCOL: Line %s, Response signon received when in state %d\n",
				temp->HostName, temp->state);
			temp->state = INACTIVE;	/* Disable the line */
			restart_channel(Index);
			return;
		}
/* Check that the correct node is at the other side */
		EBCDIC_TO_ASCII(SignOnPointer->NCCINODE, Aline, 8);
		for(i = 7; i >= 0; i--)
			if(Aline[i] != ' ') break;
		Aline[++i] = '\0';
		if(compare(Aline, temp->HostName) != 0) {
			logger(1, "Line #%d, Host '%s' tries to\
 logon instead of authorized host '%s' for this line\n",
				Index, Aline, temp->HostName);
			temp->state = INACTIVE;	/* Deactivate line */
			return;
		}
/* If the received buffer size is smaller than ours, decrease our one */
		memcpy(&Ishort, &(SignOnPointer->NCCIBFSZ),
			sizeof(SignOnPointer->NCCIBFSZ));
		BufSize = ntohs(Ishort);
		if(BufSize < temp->MaxXmitSize)
			temp->MaxXmitSize = BufSize;
		logger(2, "PROTOCOL: Line %d, host %s, Signed on with bufsize=%d\n",
			Index, Aline, temp->MaxXmitSize);
		inform_users_about_line(Index, -1);
		/* Ack it */
		temp->OutBCB = 0;
		SEND_ACK();
		temp->state = ACTIVE;
		return;
	case E_B:	/* Signoff */
		logger(2, "PROTOCOL: line=%s, Signoff received.\n",
				temp->HostName);
		temp->state = DRAIN;	/* Drain the channel for re-connection */
		if((temp->flags & F_RELIABLE) != 0)
			temp->state = INACTIVE;	/* Clear the connection */
		restart_channel(Index);
		return;
	default: logger(1,
			"PROTOCOL: Line %s, Illegal control record. SRCB=x^%x\n",
			temp->HostName, (int)(buffer[SRCB_OFFSET]));
		return;
	}
}


/*
 | A request to start a stream was received. Check that it is in range,
 | check that this stream is inactive, try openning
 | an output file, and if ok, confirm it. In all other cases, reject it.
 | Called by Handle_text_block().
 */
income_file_request(Index, temp, StreamNumber)
int	Index;
struct	LINE	*temp;
unsigned char	StreamNumber;
{
	int	DecimalStreamNumber;	/* the stream number in the range 0-7 */

/* Convert the RSCS's stream number to a decimal one */
	DecimalStreamNumber = ((StreamNumber & 0xf0) >> 4) - 9;

/* Check whether we have to shut this line. If so, refuse the request */
	if((temp->flags & F_SHUT_PENDING) != 0) {
		RejectFile.SRCB = StreamNumber;	/* Stream # */
		temp->flags &= ~F_XMIT_CAN_WAIT;
		send_data(Index, &RejectFile,
			(sizeof(struct REJECT_FILE)),
			ADD_BCB_CRC);
		return;
	}

/* Check that it is stream 0 and inactive */
	if((DecimalStreamNumber >= temp->MaxStreams) ||	/* Out of range */
	   ((temp->InStreamState)[DecimalStreamNumber] != S_INACTIVE)) {
/* Stream must be in in INACTIVE state to start a new connection. */
		logger(2, "PROTOCOL: Rejecting request for stream #x^%x, line=%s\n",
			StreamNumber, temp->HostName);
		if(DecimalStreamNumber >= temp->MaxStreams)
			logger(2, "PROTOCOL, Stream is out of range for this line\n");
		else
			logger(2, "PROTOCOL, Stream state is %d\n",
				temp->InStreamState[DecimalStreamNumber]);
		RejectFile.SRCB = StreamNumber;	/* Stream # */
		temp->flags &= ~F_XMIT_CAN_WAIT;
		send_data(Index, &RejectFile,
			(sizeof(struct REJECT_FILE)),
			ADD_BCB_CRC);
		return;
	}

/* It's stream 0, so we can handle it. Try openning output file. */
	if(open_recv_file(Index, DecimalStreamNumber) == 0) {
		/* Can't open file for some reason - reject request */
		temp->InStreamState[DecimalStreamNumber] = S_INACTIVE;
		RejectFile.SRCB = StreamNumber;
		temp->flags &= ~F_XMIT_CAN_WAIT;
		send_data(Index, &RejectFile,
			(sizeof(struct REJECT_FILE)),
			ADD_BCB_CRC);
		return;
	} else {
/* The file is opened and we can confirm it. However, if the link is of TCP
   type, the other side might have already sent locally this permission to
   enhance line performance. In this case, simply do not send the ack */
		temp->InStreamState[DecimalStreamNumber] = S_REQUEST_SENT;
		if(((temp->flags & F_RELIABLE) == 0) ||
		   ((temp->flags & F_FAST_OPEN) == 0)) {
			temp->flags &= ~F_XMIT_CAN_WAIT;
			PermitFile.SRCB = StreamNumber;
			send_data(Index, &PermitFile,
				(sizeof(struct PERMIT_FILE)),
				ADD_BCB_CRC);
		/* Other side requested transmission */
		}
		temp->flags &= ~F_FAST_OPEN;	/* Clear it */
	}
}


/*
 | Send the signon record. The flag tells whether this is the initial or
 | a response signon.
 */
send_signon(Index, flag)
int	Index, flag;
{
	register int	TempVar;
	int	i;
	short	Ishort;

	if(flag == INITIAL_SIGNON) {
		logger(3, "PROTOCOL: Sending initial signon record.\n");
/* Take the local name from a pre-defined string in EBCDIC */
		memcpy(InitialSignon.NCCINODE, E_BITnet_name, E_BITnet_name_length);
		Ishort = htons(IoLines[Index].PMaxXmitSize);	/* Put it in IBM order */
		memcpy(&(InitialSignon.NCCIBFSZ), &Ishort,
			sizeof(InitialSignon.NCCIBFSZ));
/* Create an empty password (all blanks) */
		memcpy(InitialSignon.NCCILPAS, EightSpaces, (long)(8));
		memcpy(InitialSignon.NCCINPAS, EightSpaces, (long)(8));
		send_data(Index, &InitialSignon, (sizeof(struct SIGNON)),
			ADD_BCB_CRC);
	}
	else {	/* Response singon */
		logger(3, "PROTOCOL: Sending response signon record.\n");
		memcpy(ResponseSignon.NCCINODE, E_BITnet_name, E_BITnet_name_length);
		if(IoLines[Index].MaxXmitSize > IoLines[Index].PMaxXmitSize)
			IoLines[Index].MaxXmitSize = IoLines[Index].PMaxXmitSize;
		Ishort = htons(IoLines[Index].MaxXmitSize);	/* Put it in IBM order */
		memcpy(&(ResponseSignon.NCCIBFSZ), &Ishort,
			sizeof(ResponseSignon.NCCIBFSZ));
/* Create an empty password (all blanks) */
		memcpy(ResponseSignon.NCCILPAS, EightSpaces, (long)(8));
		memcpy(ResponseSignon.NCCINPAS, EightSpaces, (long)(8));
		logger(3, "PROTOCOL: Line %s, Sending response signon with bufsize=%d\n",
				IoLines[Index].HostName, IoLines[Index].MaxXmitSize);
		send_data(Index, &ResponseSignon, (sizeof(struct SIGNON)),
			ADD_BCB_CRC);
	}
}


/*
 | Send the Network Job headers record or the Dataset header.
 | Some commonly used data (username, filename, etc) is initialised when
 | preparing the NJH, and used when DSH is sent (the data is kept in static
 | variables).
 | If the message is of type FILE, the class in DSH is set to A; if it is
 | MAIL, that class is set to M. However, if the user explicitly specified
 | another class, we use that class.
 | The class fields in NJH are set to A.
 | If a job number was assigned by the user's agent - use it. If not - assign
 | one of ourself.
 | The maximum record length stored in NDH should be read from FAB.
 */
#define	TO_UPPER(c)	(((c >= 'a') && (c <= 'z')) ? (c - ' ') : c)
#define CREATE_NJH_FIELD(FIELD, USER, NODE) {\
	strcpy(TempLine, FIELD); \
	for(p = TempLine; *p != '\0'; p++) *p = TO_UPPER(*p); \
	if((p = strchr(TempLine, '@')) != NULL) *p++ = '\0'; \
		/* Separate the sender and receiver */ \
	else p = TempLine; \
	p[8] = '\0';	/* Delimit to 8 characters */ \
	i = strlen(p); \
	ASCII_TO_EBCDIC(p, NODE, i); \
	PAD_BLANKS(NODE, i, 8); \
	TempLine[8] = '\0'; \
	if(compare(TempLine, "SYSTEM") == 0) \
		*TempLine = '\0';	/* IBM forbids SYSTEm */ \
	i = strlen(TempLine); \
	ASCII_TO_EBCDIC(TempLine, (USER), i); \
	PAD_BLANKS((USER), i, 8); }

send_njh_dsh_record(Index, flag)
int	Index, flag;
{
	struct	LINE	*temp;
	struct	FILE_PARAMS	*FileParams;
	int	i, TempVar;	/* For CREATE_NJH... macro */
	short	Ishort;
	unsigned long	IbmTime[2];
	int		compress_scb(), *Time;
	char		*p, Afield[10],
			TempLine[MAX_BUF_SIZE];	/* For CREATE_NJH... macro */
	static unsigned char	FromUser[10], FromNode[10],
			ToUser[10], ToNode[10],
			FileName[10], FileExt[10];
	static	int	Jid = 0;	/* Job ID. a running number */
#ifdef INCLUDE_TAG
	int		SerialNumber,	/* We have to divide xmission */
			SizeSent;	/* How much of DSH we already sent */
	unsigned char	SmallTemp[32],	/* For composing small buffers */
			*NetworkDatasetHeaderPointer;	/* For easy manipulations */
#endif

	temp = &(IoLines[Index]);
	FileParams = &(temp->OutFileParams[temp->CurrentStream]);

	if(flag != NJH)
		goto SendDsh;	/* All static data values were computed at
				   a previous call */

	Jid = (++Jid) % 9900;	/* Compute the next one */
	if(Jid == 0) Jid++;

/* Fill the NJH record */
	if(FileParams->FileId != 0) {	/* Assigned by user */
		Ishort = htons((FileParams->FileId));
		memcpy(&(NetworkJobHeader.NJHGJID), &Ishort,
			sizeof(NetworkJobHeader.NJHGJID));
		sprintf(TempLine, "YHVI%04d", FileParams->FileId);
	} else {		/* Not assigned - we assign it */
		Ishort = htons(Jid);
		memcpy(&(NetworkJobHeader.NJHGJID), &Ishort,
			sizeof(NetworkJobHeader.NJHGJID));
		sprintf(TempLine, "YHVI%04d", Jid);	/* Job name */
	}
	i = strlen(TempLine);
	memcpy((FileParams->JobName), TempLine, i);	/* For informational purpose */
	ASCII_TO_EBCDIC(TempLine, NetworkJobHeader.NJHGJNAM, i);

/* Prepare the username and site name in EBCDIC, blanks padded to 8 characters */
	CREATE_NJH_FIELD(FileParams->From, FromUser, FromNode);
	CREATE_NJH_FIELD(FileParams->To, ToUser, ToNode);

	i = strlen(FileParams->FileName);
	ASCII_TO_EBCDIC(FileParams->FileName, FileName, i);
	for(p = FileName; *p != '\0'; p++) *p = TO_UPPER(*p);
	PAD_BLANKS(FileName, i, 8);
	i = strlen(FileParams->FileExt);
	ASCII_TO_EBCDIC(FileParams->FileExt, FileExt, i);
	for(p = FileExt; *p != '\0'; p++) *p = TO_UPPER(*p);
	PAD_BLANKS(FileExt, i, 8);

/* NJH header */
/* Log the transaction */
	logger(3, "=> Sending file/mail %s.%s from %s to %s (line %s)\n",
		FileParams->FileName, FileParams->FileExt,
		FileParams->From, FileParams->To, temp->HostName);
	memcpy(NetworkJobHeader.NJHGUSID, FromUser, 8);
	/* Insert the time in IBM format */
	ibm_time(IbmTime);
	Time = (int *)(NetworkJobHeader.NJHGETS);
	i = htonl(IbmTime[1]);	memcpy(Time++, &i, sizeof(int));
	i = htonl(IbmTime[0]);	memcpy(Time, &i, sizeof(int));
	memcpy(NetworkJobHeader.NJHGORGN, FromNode, 8);
	memcpy(NetworkJobHeader.NJHGORGR, FromUser, 8);
	memcpy(NetworkJobHeader.NJHGXEQN, FromNode, 8);
	memcpy(NetworkJobHeader.NJHGPRTN, ToNode, 8);
	memcpy(NetworkJobHeader.NJHGPRTR, ToUser, 8);
	memcpy(NetworkJobHeader.NJHGPUNN, ToNode, 8);
	memcpy(NetworkJobHeader.NJHGPUNR, ToUser, 8);
	goto SendData;

/* DSH header */
SendDsh:
/* Prepare the username and site name in EBCDIC, blanks padded to 8 characters */
	CREATE_NJH_FIELD(FileParams->From, FromUser, FromNode);
	CREATE_NJH_FIELD(FileParams->To, ToUser, ToNode);

	i = strlen(FileParams->FileName);
	ASCII_TO_EBCDIC(FileParams->FileName, FileName, i);
	for(p = FileName; *p != '\0'; p++) *p = TO_UPPER(*p);
	PAD_BLANKS(FileName, i, 8);
	i = strlen(FileParams->FileExt);
	ASCII_TO_EBCDIC(FileParams->FileExt, FileExt, i);
	for(p = FileExt; *p != '\0'; p++) *p = TO_UPPER(*p);
	PAD_BLANKS(FileExt, i, 8);

	/* Fill the general section */
	memcpy(NetworkDatasetHeader.NDH.NDHGNODE, ToNode, 8);
	memcpy(NetworkDatasetHeader.NDH.NDHGRMT, ToUser, 8);
	memcpy(NetworkDatasetHeader.NDH.NDHGPROC, FileName, 8);
	memcpy(NetworkDatasetHeader.NDH.NDHGSTEP, FileExt, 8);
/* Set the maximum record length */
	if((FileParams->type & F_PRINT) != 0) {	/* Print format = 132 */
		if((FileParams->type & F_ASA) != 0)
			Ishort = htons(0x85);	/* When there is CC the length is 133 */
		else
			Ishort = htons(0x84);	/* No CC - 132 */
		memcpy(&(NetworkDatasetHeader.NDH.NDHGLREC), &Ishort,
			sizeof(NetworkDatasetHeader.NDH.NDHGLREC));
		NetworkDatasetHeader.NDH.NDHGFLG2 = 0x80;	/* Print flag */
		if((FileParams->type & F_ASA) != 0)
			NetworkDatasetHeader.NDH.NDHGRCFM = 0x44;	/* Print file with ASA CC*/
		else
			NetworkDatasetHeader.NDH.NDHGRCFM = 0x40;	/* Print with no CC */
		NetworkDatasetHeader.RSCS.NDHVIDEV = 0x41;	/* One of the
								   IBM printers  */
	} else {	/* All others will be 80 at present */
		Ishort = htons(0x50); /* 80 decimal */
		memcpy(&(NetworkDatasetHeader.NDH.NDHGLREC), &Ishort,
			sizeof(NetworkDatasetHeader.NDH.NDHGLREC));
		NetworkDatasetHeader.NDH.NDHGFLG2 = 0x40;	/* Punch flag */
		NetworkDatasetHeader.NDH.NDHGRCFM = 0x80;	/* Punch file */
		NetworkDatasetHeader.RSCS.NDHVIDEV = 0x82;	/* Punch file */
	}
	NetworkDatasetHeader.NDH.NDHGCLAS = ASCII_EBCDIC[FileParams->JobClass];
/* Set the form code to QUIET if bit F_NOQUIET is not set. */
	memcpy(NetworkDatasetHeader.NDH.NDHGFORM, EightSpaces, 8);
	if((FileParams->type & F_NOQUIET) == 0) {
		strcpy(Afield, "QUIET");
		ASCII_TO_EBCDIC(Afield, NetworkDatasetHeader.NDH.NDHGFORM,
			strlen(Afield));
	}

/* Fill the RSCS section */
	NetworkDatasetHeader.RSCS.NDHVCLAS = ASCII_EBCDIC[FileParams->JobClass];
	memcpy(NetworkDatasetHeader.RSCS.NDHVFNAM, FileName, 8);
	PAD_BLANKS((NetworkDatasetHeader.RSCS.NDHVFNAM), 8, 12);
	memcpy(NetworkDatasetHeader.RSCS.NDHVFTYP, FileExt, 8);
	PAD_BLANKS((NetworkDatasetHeader.RSCS.NDHVFTYP), 8, 12);
#ifdef INCLUDE_TAG
/* Copy the TAG field */
	i = strlen(FileParams->tag);
	ASCII_TO_EBCDIC(FileParams->tag,
		NetworkDatasetHeader.RSCS.NDHVTAGR, i);
	PAD_BLANKS((NetworkDatasetHeader.RSCS.NDHVTAGR), i, 136);
#endif

/* Send the data to the other side */
SendData:
	TempVar = 0;
	if(flag == NJH) {
		TempLine[TempVar++] = (((temp->CurrentStream + 9) << 4) | 0x9);
		TempLine[TempVar++] = NJH_SRCB;
		TempVar += compress_scb(&NetworkJobHeader, &(TempLine[2]),
				(sizeof(struct JOB_HEADER)));
		TempLine[TempVar++] = 0;	/* Closing SCB */
		send_data(Index, TempLine, TempVar, ADD_BCB_CRC);	/* Send  as usual */
		return 1;	/* Sent OK */
	}

/* We process the DSH now */
#ifndef INCLUDE_TAG
/* Dataset header - No INCLUDE_TAG defined - send it in one peice */
	TempLine[TempVar++] = (((temp->CurrentStream + 9) << 4) | 0x9);
	TempLine[TempVar++] = DSH_SRCB;
	TempVar += compress_scb(&NetworkDatasetHeader, &TempLine[2],
			(sizeof(struct DATASET_HEADER)));
	TempLine[TempVar++] = 0;	/* Closing SCB */
	send_data(Index, TempLine, TempVar, ADD_BCB_CRC);	/* Send  as usual */
	return 1;	/* Send OK */

#else	/* INCLUDE_TAG */

/* INCLUDE_TAG is defined - Have to fragment it. Note that it is done very
   ugly, and it depends on a sufficiently large VMnet buffer (it can't be
   used with NJE's with mixed lines types).
*/
	NetworkDatasetHeaderPointer = &NetworkDatasetHeader;

/* Have to fragment the entry */
	NetworkDatasetHeaderPointer += 4;	/* Jump over the first 4 bytes as we rebuild them now */
	SizeSent = 4;	/* 4 - since we re-generate the first 4 bytes */
	SerialNumber = 0;
	while(SizeSent <= (sizeof(struct DATASET_HEADER))) {	/* While there is something to send */
		TempVar = 0;
		TempLine[TempVar++] = (((temp->CurrentStream + 9) << 4) | 0x9);
		TempLine[TempVar++] = DSH_SRCB;
/* If the size if smaller than 252 then this is the last fragment */
		if((i = (sizeof(struct DATASET_HEADER) - SizeSent)) <= 252) {
			Ishort = htons(i + 4);
			memcpy(SmallTemp, &Ishort, 2);
			SmallTemp[2] = 0;
			SmallTemp[3] = SerialNumber;	/* Last one */
			TempVar += compress_scb(SmallTemp,
				&(TempLine[TempVar]), 4);
			TempVar--;	/* Remove the closing SCB */
			TempVar += compress_scb(NetworkDatasetHeaderPointer,
				&(TempLine[TempVar]), i);
			TempLine[TempVar++] = 0;	/* Closing SCB */
		} else {
			Ishort = htons(256);	/* 252 payload + 4 header */
			memcpy(SmallTemp, &Ishort, 2);
			SmallTemp[2] = 0;
			SmallTemp[3] = 0x80 + (SerialNumber++);	/* Last one */
			TempVar += compress_scb(SmallTemp,
				&(TempLine[TempVar]), 4);
			TempVar--;	/* Remove the closing SCB */
			TempVar += compress_scb(NetworkDatasetHeaderPointer,
				&(TempLine[TempVar]), 252);
			TempLine[TempVar++] = 0;	/* Closing SCB */
		}			
		NetworkDatasetHeaderPointer += 252;
		SizeSent += 252;
		temp->flags |= F_XMIT_CAN_WAIT;	/* We support TCP lines here */
		send_data(Index, TempLine, TempVar, ADD_BCB_CRC);	/* Send  as usual */
		temp->flags &= ~F_XMIT_CAN_WAIT;
		if((temp->flags & F_XMIT_MORE) == 0) {
			logger(1, "PROTOCOL: No room in buffer for segmented header\n");
			return 0;
		}
	}
	return 1;
#endif	/* INCLUDE_TAG */
}


/*
 | Send the job trailer. Add the count of lines in the file.
 */
send_njt(Index)
int	Index;
{
	unsigned char	OutputLine[MAX_BUF_SIZE];
	int	TempVar, position;
	struct	LINE	*temp;

	temp = &IoLines[Index];

	/* Since number of lines not written - send anyway */
	position = 0;
	OutputLine[position++] = (((temp->CurrentStream + 9) << 4) | 0x9);	/* RCB */
	OutputLine[position++] = NJT_SRCB;	/* SRCB */
	TempVar = htonl(IoLines[Index].OutFileParams[temp->CurrentStream].RecordsCount);
	memcpy(&(NetworkJobTrailer.NJTGALIN), &TempVar,
		sizeof(NetworkJobTrailer.NJTGALIN));
	position += compress_scb(&NetworkJobTrailer, &OutputLine[position],
			(sizeof(struct JOB_TRAILER)));
	OutputLine[position++] = 0;	/* End of block */
	send_data(Index, OutputLine, position, ADD_BCB_CRC);
}

/*
 | Send a message to users registered in InformUsers with the new line's state.
 | If PreviousState >= 0, then it contains the previous state and we should
 | inform it.
 */
char	states[][20] = { "INACTIVE", "SIGNOFF", "DRAIN", "ACTIVE", "ACTIVE",
			 "ACTIVE", "LISTEN", "RETRYING", "TCP-SYNC" };
inform_users_about_line(Index, PreviousState)
int	Index, PreviousState;
{
	char	From[20], line[LINESIZE];
	int	size, i;
	struct	LINE	*temp;

	if(InformUsersCount == 0)	/* None registered */
		return;

	temp = &IoLines[Index];
/* Create the sender address and the text */
	sprintf(From, "@%s", LOCAL_NAME);

	if(PreviousState >= 0)
		sprintf(line, "NETMON: Line %d, host=%s, changed state from %s to %s",
			Index, temp->HostName, states[PreviousState],
			states[temp->state]);
	else
		sprintf(line, "NETMON: Line %d, host=%s, changed state to %s",
			Index, temp->HostName, states[temp->state]);
	size = strlen(line);

/* Loop over list and send messages */
	for(i = 0; i < InformUsersCount; i++)
		send_nmr(From, InformUsers[i], line, size, ASCII, CMD_MSG);
}
