/* IO.C    V3.3
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
 | Do the OS independent IO parts.
 | Note: The way of saving the last tramitted buffer should be improved. It
 |       currently saves all data if it is not a NAK. This should be modified
 |       according to the status, and also the routine that handles the NAK
 |       should be corrected accordingly.
 |
 | When a buffer is sent, we send the saved buffer in IoLines structure. This
 | is because the buffer we usually get is a dynamic memory location. Thus,
 | the XmitBuffer in IoLines structure is the only one which is static.
 |
 | V1.1 - Start converting from VMnet version 1 to VMnet version 2.
 | V1.2 - If send_data() is called when a line is in transmit state, the buffer
 |        is queued if the line supports it.
 | V1.3 - Do not add the last TTR in VMNET blocks. It'll be added later by the
 |        SEND_EXOS routine.
 |        In case of EXOS_TCP & UNIX_TCP, do not copy send buffer over
 |        XmitBuffer in IoLines, since SEND_EXOS and SEND_TCP uses this buffer
 |        to block more than one block in transmission.
 | V1.4 - Save xmitted data in IoLines->XmitBuffer even when it is a NAK block.
 | V1.5 - Add support for MultiNet TcpIp package. When using passive ends, they
 |        all must be of the same package (all MultiNet or all EXOS).
 | V1.6 - When queueing a buffer for later transmission (since the line is
 |        already transmitting) on reliable links, set the F_WAIT_V_A_BIT.
 | V1.7 - Do a conditional compilation of EXOS and MULTINET parts.
 | V1.8 - Add support for DEC's TcpIp.
 | V1.9 - If the line type is DMB, add obly BCB and other stuff, but not CRC.
 | V2.0 - Add debug-dump-buffers and Debug-rescan-queue routines to aid
 |        debugging. They are here to be common for both VMS and Unix.
 | V2.1 - Add 5 to the size of memory allocated when queing a buffer for deffered
 |        transmission. This is the BCB + FCS + other overhead per block.
 | V2.2 - When creating a VMNET connection use a lrger timeout during the initial
 |        phase (the value is VMNET_INITIAL_TIMEOUT) in order to prevent
 |        slow signon on slow links.
 | V2.3 - Ignore the SECONDARY keyword for all lines. We now initiate both active
 |        and passive connections.
 | V2.4 - Replace SWAP_xxx with htons() and compatible functions.
 | V2.5 - 21/2/90 - Replace BCOPY with memcpy().
 | V2.6 - 22/2/90 - Make various changes to Send_data() routine. remove unnecessary
 |        code (code which is never used), and make use of the F_RELIABLE new
 |        flag instead of checking for TCP/DECNET line type.
 |        When writing TTB/TTRs, do not do word/longwords writes; only byte
 |        writes, as SUN-4 can't write words on odd addresses.
 |        Change call to Queue_timer with index=0 (where it should be a general
 |        routine and not for line #0) to index=-1, so Delete_line_timeouts()
 |        for line 0 will not deleet it.
 | V2.8 - 1/4/90 - The command parser of the operator's command has been moved
 |        from UNIX.C and VMS.C to here.
 | V2.9 - 6/5/90 - When parsing an address to send a message (not command) to it,
 |        append our local nodename if the address does not conatins @, so it'll
 |        be sent to a user on the local machine.
 | V3.0 - 31/5/90 - In Parse_operator_command() add the cases of ADD_GONE_UCP
 |        and DEL_GONE_UCP; here they are treated the same. However, VMS.C
 |        accepts ADD_GONE_USER and DEL_GONE_USER from anyone, while accepting
 |        ADD_GONE_USER_UCP and DEL_GONE_USER_UCP only from INFORM users. This
 |        allows us to leave UCP world-executable without leting every user
 |        play with it.
 | V3.1 - 8/10/90 - Add multi-stream support in SHOW LINE.
 | V3.2 - 7/3/91 - Add DSV support. It is much like the DMB.
 | V3.3 - 11/3/91 - Add Q STATS command support.
 |
 | Sections: SHOW-LINES:   Send to users the lines status.
 |           INIT-IO:      Initialize the I/O lines.
 |           QUEUE-IO:     Queue an I/O request.
 |           STATS:        Write statistics.
 |           COMMAND-PARSER Parses the opareator's commands.
 */
#include "consts.h"
#include "headers.h"
#include <errno.h>	/* FOr the MALLOC error messages */

EXTERNAL struct	LINE	IoLines[MAX_LINES];
EXTERNAL struct	ENQUIRE	Enquire;
EXTERNAL int	MustShutDown;
EXTERNAL int	LogLevel;

#define	VMNET_INITIAL_TIMEOUT	15	/* 15 seconds */

char	*strchr();
/*============================ SHOW-LINES ==============================*/
/*
 | Send the lines status to the user.
 */
show_lines_status(to)
char	*to;	/* USer@Node */
{
	int	i, j;
	char	line[LINESIZE],
		from[SHORTLINE];		/* The message's sender address */
	struct	LINE	*temp;

/* Create the sender's address. It is the daemon on the local machine */
	sprintf(from, "@%s", LOCAL_NAME);	/* No username */

	sprintf(line, "HUJI-NJE version %s(%s), Lines status:", VERSION,
				SERIAL_NUMBER);
	send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);

	for(i = 0; i < MAX_LINES; i++) {
		temp = &(IoLines[i]);
		if(*(temp->HostName) != '\0')
			sprintf(line, "Line.%d %8s  %4d (Q=%d)  ", i,
				temp->HostName, temp->TotalErrors,
				temp->QueuedFiles);
		else	/* No host defined for this line. */
			sprintf(line, "Line.%d ********  %4d ", i,
				temp->TotalErrors);
		switch(temp->state) {
		case INACTIVE: strcat(line, "INACTIVE  "); break;
		case SIGNOFF:  strcat(line, "SIGNEDOFF "); break;
		case DRAIN:    strcat(line, "DRAINED   "); break;
		case ACTIVE:   strcat(line, "ACTIVE(CNCT)"); break;
		case F_SIGNON_SENT:
		case I_SIGNON_SENT: strcat(line, "SGN-Sent  "); break;
		case LISTEN:   strcat(line, "LISTEN    "); break;
		case RETRYING: strcat(line, "Retry     "); break;
		case TCP_SYNC: strcat(line, "TCP-sync  "); break;
		default:       strcat(line, "******    "); break;
		}
		switch(temp->type) {
		case DMF: sprintf(&line[strlen(line)], "  DMF (%s)",
				temp->device); break;
		case DMB: sprintf(&line[strlen(line)], "  DMB (%s)",
				temp->device); break;
		case DSV: sprintf(&line[strlen(line)], "  DSV (%s)",
				temp->device); break;
		case UNIX_TCP: strcat(line, "  TCP      "); break;
		case EXOS_TCP: strcat(line, "  TCP(Exos)"); break;
		case MNET_TCP: strcat(line, "  TCP(Mnet)"); break;
		case DEC__TCP: strcat(line, "  TCP(DEC) "); break;
		case DECNET:   sprintf(&line[strlen(line)], "  DECNET (%s)",
				temp->device); break;
		case ASYNC:    sprintf(&line[strlen(line)], "  ASYNC (%s)",
				temp->device); break;
		default: strcat(line, "   ***");
		}
		send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
		if(temp->state != ACTIVE)	/* Not active - don't display */
			continue;		/* streams status */

		for(j = 0; j < temp->MaxStreams; j++) {
			sprintf(line, "Recv-%d  ", j);
			switch(temp->InStreamState[j]) {
			case S_INACTIVE: strcat(line, "Inactive"); break;
			case S_REQUEST_SENT: strcat(line, "REQUEST_SNT"); break;
			case S_NJH_SENT: strcat(line, "NJH-SENT"); break;
			case S_NDH_SENT: strcat(line, "NDH-SENT"); break;
			case S_NJT_SENT: strcat(line, "NJT-SENT"); break;
			case S_SENDING_FILE: strcat(line, "SEND-FL"); break;
			case S_EOF_SENT: strcat(line, "EOF-SENT"); break;
			case S_REFUSED:   strcat(line, "REFUSED"); break;
			case S_WAIT_A_BIT: strcat(line, "WAIT-A-BIT"); break;
			default:          strcat(line, "******"); break;
			}
			if((temp->InStreamState[j] != S_INACTIVE) &&
			   (temp->InStreamState[j] != S_REFUSED))
				sprintf(&line[strlen(line)], "  (%s)  (%s => %s)",
					(temp->InFileParams[j]).JobName,
					(temp->InFileParams[j]).From,
					(temp->InFileParams[j]).To);
			send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
		}
		for(j = 0; j < temp->MaxStreams; j++) {
			sprintf(line, "Send-%d  ", j);
			switch(temp->OutStreamState[j]) {
			case S_INACTIVE: strcat(line, "Inactive"); break;
			case S_REQUEST_SENT: strcat(line, "REQUEST_SNT"); break;
			case S_NJH_SENT: strcat(line, "NJH-SENT"); break;
			case S_NDH_SENT: strcat(line, "NDH-SENT"); break;
			case S_NJT_SENT: strcat(line, "NJT-SENT"); break;
			case S_SENDING_FILE: strcat(line, "SEND-FL"); break;
			case S_EOF_SENT: strcat(line, "EOF-SENT"); break;
			case S_REFUSED:   strcat(line, "REFUSED"); break;
			case S_WAIT_A_BIT: strcat(line, "WAIT-A-BIT"); break;
			default:          strcat(line, "******"); break;
			}
			if((temp->OutStreamState[j] != S_INACTIVE) &&
			   (temp->OutStreamState[j] != S_REFUSED))
				sprintf(&line[strlen(line)], "  (%s)  (%s => %s)",
					(temp->OutFileParams[j]).JobName,
					(temp->OutFileParams[j]).From,
					(temp->OutFileParams[j]).To);
			send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
		}
	}

	sprintf(line, "End of Q SYS display");
	send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
}


/*
 | Send the lines statistics to the user.
 */
show_lines_stats(to)
char	*to;	/* USer@Node */
{
	int	i, j;
	char	line[LINESIZE],
		from[SHORTLINE];		/* The message's sender address */
	struct	LINE	*temp;

/* Create the sender's address. It is the daemon on the local machine */
	sprintf(from, "@%s", LOCAL_NAME);	/* No username */

	sprintf(line, "HUJI-NJE version %s(%s), Lines statistics:", VERSION,
				SERIAL_NUMBER);
	send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);

	for(i = 0; i < MAX_LINES; i++) {
		temp = &(IoLines[i]);
		if(*(temp->HostName) != '\0') {
			sprintf(line, "Line.%d %8s: Blocks send/recv: %d/%d,\
 Wait recvd: %d, Acks-only sent/recv: %d/%d",
				i, temp->HostName, (temp->stats).TotalOut,
				(temp->stats).TotalIn, (temp->stats).WaitIn,
				(temp->stats).AckOut, (temp->stats).AckIn);
			send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
			sprintf(line, "     NMRs sent/recv: %d/%d, NAKs send/\
recvd: %d/%d",
				(temp->stats).MessagesOut, (temp->stats).MessagesIn,
				(temp->stats).RetriesOut, (temp->stats).RetriesIn);
			send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
		}
	}

	sprintf(line, "End of Q STAT display");
	send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
}


/*======================= INIT-IO ==================================*/
/*
 | Initialize all the communication lines. Initialize DMFs, TCP and
 | DECnet connections, each one only if there is a link of that type.
 | If we have at least one TCP passive end, we don't queue an accept for
 | the specific line. We queue only one accept, regarding of the number of
 | passive ends. When a connection will be received, the VMnet control record
 | will be used to locate the correct line.
 */
init_communication_lines()
{
	int	i, InitTcpPassive, InitDECnetPassive, TcpType;

	InitDECnetPassive = InitTcpPassive = 0;
	for(i = 0; i < MAX_LINES; i++) {
		   if(IoLines[i].state == ACTIVE) {
			switch(IoLines[i].type) {
#ifdef VMS
			case DMB:
			case DSV:
			case DMF: init_dmf_connection(i);
				  queue_receive(i); break;
			case DEC__TCP:
			case MNET_TCP:
			case EXOS_TCP:
/* Create an active side and also passive side */
				init_active_tcp_connection(i);
				InitTcpPassive++;
				TcpType = IoLines[i].type;
				break;
			case ASYNC:
				init_async_connection(i);
				queue_receive(i); break;
			case DECNET:
				init_active_DECnet_connection(i);
				InitDECnetPassive++;
				break;
#endif
#ifdef UNIX
			case UNIX_TCP:
				init_active_tcp_connection(i);
				InitTcpPassive++;
				break;
#endif
			case X_25:
			default:
				logger(1, "IO: No protocol for line #%d\n",
						i); break;
			}
		}
	}

/* Check whether we have to queue a passive accept for TCP & DECnet lines */
	if(InitTcpPassive != 0)
		init_passive_tcp_connection(TcpType);
#ifdef VMS
	if(InitDECnetPassive != 0)
		init_DECnet_passive_channel();
#endif
}


/*
 | Restart a line that is not active.
 */
restart_line(Index)
int	Index;
{
/* First check the line is in correct status */
	if((Index < 0) || (Index >= MAX_LINES)) {
		logger(1, "IO, Restart line: line #%d out of range\n",
			Index);
		return;
	}

	IoLines[Index].flags &= ~F_SHUT_PENDING; /* Remove the shutdown
						    pending flag. */

	switch(IoLines[Index].state) {
	case INACTIVE:
	case SIGNOFF:
	case RETRYING:
			break;	/* OK - go ahead and start it */
	case DRAIN:
	case F_SIGNON_SENT:
	case I_SIGNON_SENT:
		logger(1, "IO, Trying to start line %d in state %d. Illegal\n",
			Index, IoLines[Index].state);
		return;
	default:
		logger(1, "IO, Line %d in illegal state for start op.\n",
			Index);
		return;
	}

	logger(2, "IO, Restarting line #%d\n", Index);

/* Init the line according to its type */
	IoLines[Index].state = DRAIN;	/* Will be set to INACTIVE in case of error
					   during initialization. */
	switch(IoLines[Index].type) {
#ifdef VMS
	case DMB:
	case DSV:
	case DMF: init_dmf_connection(Index); queue_receive(Index); break;
	case DEC__TCP:
	case MNET_TCP:
	case EXOS_TCP: init_active_tcp_connection(Index);
			break;
	case DECNET: init_active_DECnet_connection(Index);
			break;
	case ASYNC: init_async_connection(Index); queue_receive(Index); break;
#endif
#ifdef UNIX
	case UNIX_TCP:
		init_active_tcp_connection(Index); break;
#endif
	default:;
	}
}



/*===================== QUEUE-IO =====================================*/
/*
 | Queue the receive for the given line. Test its type, and then call the
 | appropriate routine. Also queue a timer request for it (our internal
 | timer, not the VMS one).
 */
queue_receive(Index)
int	Index;
{
	struct	LINE	*temp;
	int	queue_timer(), queue_dmf_receive(), queue_async_receive();

	temp = &(IoLines[Index]);

/* Do we have to queue a receive ??? */
	switch(temp->state) {
	case INACTIVE:
	case SIGNOFF:
	case RETRYING:
	case LISTEN:		/* No need to requeue on these states */
			return;
	case ACTIVE:
	case DRAIN:
	case F_SIGNON_SENT:
	case I_SIGNON_SENT:
	case TCP_SYNC:
			break;	/* OK, requeue it */
	default: logger(1, "IO, Illegal line state %d on line %d during queue-Receive\n",
		temp->state, Index);
		temp->state = INACTIVE;
		return;
	}

	switch(temp->type) {
#ifdef VMS
	case DMB:
	case DSV:
	case DMF: if(queue_dmf_receive(Index) != 0)
			/* Queue a timeout for it */
			temp->TimerIndex =
				queue_timer(temp->TimeOut, Index, T_DMF_CLEAN);
		  break;
	case ASYNC: if(queue_async_receive(Index) != 0)
			temp->TimerIndex =
				queue_timer(temp->TimeOut, Index, T_ASYNC_TIMEOUT);
		  break;
	case DECNET: if(queue_DECnet_receive(Index) != 0)
			temp->TimerIndex =
				queue_timer(temp->TimeOut, Index,
				T_DECNET_TIMEOUT);
		  break;
#ifdef EXOS
	case EXOS_TCP: if(queue_exos_tcp_receive(Index) != 0)
			if(temp->state != ACTIVE)
				temp->TimerIndex =
					queue_timer(VMNET_INITIAL_TIMEOUT,
						Index, T_TCP_TIMEOUT);
			else
				temp->TimerIndex =
					queue_timer(temp->TimeOut, Index,
						T_TCP_TIMEOUT);
		  break;
#endif
#ifdef MULTINET_or_DEC
	case DEC__TCP:
	case MNET_TCP: if(queue_mnet_tcp_receive(Index) != 0)
/* If the link was not established yet, use longer timeout (since VMnet is
   running here in a locked-step mode on the VM side). If we use the regular
   small timeout we make the login process difficult (we transmit the next
   DLE-ENQ packet while the other side is acking the previous one. This is
   caused due to slow lines). */
			if(temp->state != ACTIVE)
				temp->TimerIndex =
					queue_timer(VMNET_INITIAL_TIMEOUT,
						Index, T_TCP_TIMEOUT);
			else
				temp->TimerIndex =
					queue_timer(temp->TimeOut, Index,
						T_TCP_TIMEOUT);
		  break;
#endif
#endif
#ifdef UNIX
	case UNIX_TCP:	/* We poll here, so we don;t queue a real receive */
			temp->TimerIndex =
				queue_timer(temp->TimeOut, Index, T_TCP_TIMEOUT);
		  break;
#endif
	default:  logger(1, "IO: No support for device on line #%d\n",
			Index); break;
	}
}


/*
 | Send the data using the correct interface. Before sending, add BCB+FCS and
 | CRC if asked for.
 | If the line is of TCP or DECnet type, we add only the first headers, and do not add
 | the tralier (DLE+ETB_CRCs) and we do not duplicate DLE's.
 | See comments about buffers at the head of this module.
 | If we are called when the link is already transmitting (and haven't finished
 | it), then the buffer is queued for later transmission if the line supports
 | it. The write AST routine will send it.
 */
send_data(Index, buffer, size, AddEnvelope)
int	Index, size, AddEnvelope;	/* Add the BCB+...? */
unsigned char	*buffer;
{
	struct	LINE	*temp;
	int	NewSize, add_bcb_crc(), add_bcb(), create_VMnet_block();
	unsigned char	*p, *SendBuffer;
	register int	i, flag, TempVar, NextEntry;
	/* In the followings, ttr/b is used to point inside the buffer, while
	   Ttr/b is used to construct the buffer (Sun-4 oddities...). */
	struct	TTB	*ttb, Ttb;
	struct	TTR	*ttr, Ttr;

	temp = &(IoLines[Index]);
	temp->flags &= ~F_XMIT_MORE;	/* Default - do not block */
/* Collects stats */
#ifdef DEBUG
	if(*buffer != NAK)
		((temp->stats).TotalOut)++;
	else
		((temp->stats).RetriesOut)++;
#endif

	SendBuffer = buffer; NewSize = size;
	i = temp->OutBCB;  flag = (temp->flags & F_RESET_BCB);

/* Test whether the link is already transmitting. If so, queue the message
   only if the link supports so. If not, ignore this transmission.
*/
	if((temp->flags & F_SENDING) != 0) {	/* Yes - it is occiupied */
		if((temp->flags & F_RELIABLE) == 0) {
			logger(1, "IO, Line #%d doesn't support queueing\n",
				Index);
			return;	/* Ignore it */
		}
		temp->flags |= F_WAIT_V_A_BIT;	/* Signal wait-a-bit so sender
						   will not transmit more */

/* Calculate where shall we put it in the queue (Cyclic queue) */
		NextEntry = (temp->LastXmitEntry + 1) % MAX_XMIT_QUEUE;
/* If the new last is the same as the first one, then we have no place... */
		if(NextEntry == temp->FirstXmitEntry) {
			logger(1, "IO, No place to queue Xmit on line #%d\n",
				Index);
			return;
		}
/* There is a place - queue it */
		if((p = (unsigned char*)malloc(size + sizeof(struct TTB) + 5 + 2 +
				/* 5 for BCB+FCS overhead, 2 for DECnet CRC */
		    2 * sizeof(struct TTR))) == NULL) {
#ifdef VMS
			logger(1, "IO, Can't malloc. Errno=%d, VaxErrno=%d\n",
					 errno, vaxc$errno);
#else
			logger(1, "IO, Can't malloc. Errno=%d\n", errno);
#endif
			bug_check("IO, Can't malloc() memory\n");
		}
		NewSize = add_VMnet_block(Index, AddEnvelope,
			buffer, size, &p[sizeof(struct TTB)], i);
		SendBuffer = p;
		if(AddEnvelope == ADD_BCB_CRC)
			if(flag != 0)	/* If we had to reset BCB, don't increment */
				temp->OutBCB = (i + 1) % 16;
		ttb = (struct TTB *)(p);
		NewSize += sizeof(struct TTB);
		ttr = (struct TTR *)(&(p[NewSize]));
		Ttr.F = Ttr.U = 0;
		Ttr.LN = 0;		/* Last TTR */
		memcpy(ttr, &Ttr, sizeof(struct TTR));
		NewSize += sizeof(struct TTR);
		Ttb.F = 0;	/* No flags */
		Ttb.U = 0;
		Ttb.LN = htons(NewSize);
		Ttb.UnUsed = 0;
		memcpy(ttb, &Ttb, sizeof(struct TTB));

		temp->XmitQueue[temp->LastXmitEntry] = (char *)p;
		temp->XmitQueueSize[temp->LastXmitEntry] = NewSize;
		temp->LastXmitEntry = NextEntry;
		return;
	}

/* No queueing - format buffer into output buffer. If the line is TCP - block
   more records if can. Other types - don't try to block.
*/
	if((temp->flags & F_RELIABLE) != 0) {
		if(temp->XmitSize == 0)
			temp->XmitSize = sizeof(struct TTB);
					/* First block - leave space for TTB */
		NewSize = add_VMnet_block(Index, AddEnvelope,
			buffer, size,
			&temp->XmitBuffer[temp->XmitSize], i);
		temp->XmitSize += NewSize;
		if(AddEnvelope == ADD_BCB_CRC)
			if(flag != 0)	/* If we had to reset BCB, don't increment */
				temp->OutBCB = (i + 1) % 16;
	}
	else {	/* Normal block */
		if(AddEnvelope == ADD_BCB_CRC) {
			if((temp->type == DMB) || (temp->type == DSV))
				temp->XmitSize =
					NewSize = add_bcb(Index, buffer,
						size, temp->XmitBuffer, i);
			else
				temp->XmitSize =
					NewSize = add_bcb_crc(Index, buffer,
						size, temp->XmitBuffer, i);
			if(flag != 0)	/* If we had to reset BCB, don't increment */
				temp->OutBCB = (i + 1) % 16;
		}
		else {
			memcpy(temp->XmitBuffer, buffer, size);
			temp->XmitSize = size;
		}
		SendBuffer = temp->XmitBuffer;
	}

/* Check whether we've overflowed some buffer. If so - bugcheck... */
	if(temp->XmitSize > MAX_BUF_SIZE) {
		logger(1, "IO, Xmit buffer overflow in line #%d\n", Index);
		bug_check("Xmit buffer overflow\n");
	}

/* If TcpIp line and there is room in buffer and the sender allows us to
   defer sending - return. */
	if((temp->flags & F_RELIABLE) != 0) {
		if((temp->flags & F_XMIT_CAN_WAIT) != 0)
			if((temp->XmitSize + sizeof(struct TTB) +
			      2 * sizeof(struct TTR) + 5 + 2 +
				/* +5 for BCB + FCS overhead, +2 for DECnet;s CRC */
			    temp->MaxXmitSize) < temp->TcpXmitSize) { /* There is room */
				temp->flags |= F_XMIT_MORE;
				return;
			}
	}

/* Ok - we have to transmit buffer. If DECnet or TcpIp - insert the TTB and add
   TTR at end */
	if((temp->flags & F_RELIABLE) != 0) {
		NewSize = temp->XmitSize;
		ttb = (struct TTB *)temp->XmitBuffer;
		ttr = (struct TTR *)(&(temp->XmitBuffer[NewSize]));
		Ttr.F = Ttr.U = 0;
		Ttr.LN = 0;		/* Last TTR */
		memcpy(ttr, &Ttr, sizeof(struct TTR));
		temp->XmitSize = NewSize = NewSize + sizeof(struct TTR);
		Ttb.F = 0;	/* No flags */
		Ttb.U = 0;
		Ttb.LN = htons(NewSize);
		Ttb.UnUsed = 0;
		memcpy(ttb, &Ttb, sizeof(struct TTB));
		SendBuffer = temp->XmitBuffer;

/* Check whether we've overflowed some buffer. If so - bugcheck... */
		if(temp->XmitSize > MAX_BUF_SIZE) {
			logger(1, "IO, TCP Xmit buffer overflow in line #%d\n", Index);
			bug_check("Xmit buffer overflow\n");
		}
	}

	temp = &(IoLines[Index]);
#ifdef DEBUG
	logger(3, "IO: Sending: line=%d, size=%d, sequence=%d:\n",
		Index, NewSize, i);
	trace(SendBuffer, NewSize, 5);
#endif
	switch(temp->type) {
#ifdef VMS
	case ASYNC: send_async(Index, SendBuffer, NewSize);
			return;
	case DMB:
	case DSV:
	case DMF: send_dmf(Index, SendBuffer, NewSize);
		  return;
	case DECNET: send_DECnet(Index, SendBuffer, NewSize);
		 return;
#ifdef EXOS
	case EXOS_TCP: send_exos_tcp(Index, SendBuffer, NewSize);
		  return;
#endif
#ifdef MULTINET_or_DEC
	case DEC__TCP:
	case MNET_TCP: send_mnet_tcp(Index, SendBuffer, NewSize);
		  return;
#endif
#endif
#ifdef UNIX
	case UNIX_TCP: send_unix_tcp(Index, SendBuffer, NewSize);
		  return;
#endif
	default:  logger(1, "IO: No support for device on line #%d\n",
			Index); break;
	}
}


/*
 | TCP lines - add the initial TCP header, and copy the buffer. Don;t add the
 | final TTR, to allow blocking of more records. The caller routine will add
 | it.
 */
int
add_VMnet_block(Index, flag, buffer, size, NewLine, BCBcount)
int	flag, size, BCBcount;
unsigned char	*buffer, *NewLine;
{
	struct	TTR	*ttr, Ttr;
	register int	TempVar, NewSize;
	register unsigned char	*p;

	p = NewLine;
	NewSize = size + sizeof(struct TTR);
	if(flag == ADD_BCB_CRC)	/* Have to add BCB + FCS */
		NewSize += 5;		/* DLE + STX + BCB + FCS + FCS */

	ttr = (struct TTR *)p;
	Ttr.F = 0;	/* No fast open */
	Ttr.U = 0;
	if(flag == ADD_BCB_CRC)
		Ttr.LN = htons((size + 5));	/* The BCB header */
	else
		Ttr.LN = htons(size);
	/* Copy the control blocks */
	memcpy(ttr, &Ttr, sizeof(struct TTR));
	p += sizeof(struct TTR);

/* Put the DLE, STX, BCB and FCS.
   If BCB is zero, send a "reset" BCB.
 */
	if(flag == ADD_BCB_CRC) {
		*p++ = DLE; *p++ = STX;
		if((BCBcount == 0) && ((IoLines[Index].flags & F_RESET_BCB) == 0)) {
			IoLines[Index].flags |= F_RESET_BCB;
			*p++ = 0xa0;	/* Reset BCB count to zero */
		}
		else
			*p++ = 0x80 + (BCBcount & 0xf);	/* Normal block */
		*p++ = 0x8f; *p++ = 0xcf;	/* FCS - all streams are enabled */
	}
	memcpy(p, buffer, size);
	return NewSize;
}


/*
 | Close the communication channel.
 */
close_line(Index)
int	Index;
{
	IoLines[Index].flags &= ~F_SHUT_PENDING;	/* Clear the flag */

	switch(IoLines[Index].type) {
#ifdef VMS
	case DMB:
	case DSV:
	case DMF: close_dmf_line(Index); break;
	case ASYNC: close_async_line(Index); break;
	case DECNET: close_DECnet_line(Index); break;
	case DEC__TCP:
	case MNET_TCP:
	case EXOS_TCP: close_tcp_chan(Index); break;
#endif
#ifdef UNIX
	case UNIX_TCP: close_unix_tcp_channel(Index); break;
#endif
	default: logger(1, "IO, Close-Line: line=%d, no support for device #%d\n",
			Index, IoLines[Index].type);
	}
}


/*================================= STATS ===========================*/
#ifdef DEBUG
/*
 | Write the statistics, clear the counts and requeue the timer.
 */
compute_stats()
{
	int	i;
	struct	LINE	*temp;
	struct	STATS	*stats;

	for(i = 0; i < MAX_LINES; i++) {
		temp = &(IoLines[i]);
		stats = &(temp->stats);
		logger(1, "Stats for line #%d, name=%s, state=%d\n",
			i, temp->HostName, temp->state);
		logger(1, "Out: Total=%d, Wait-a-Bit=%d, Acks=%d, Messages=%d, NAKS=%d\n",
			stats->TotalOut, stats->WaitOut, stats->AckOut,
			stats->MessagesOut, stats->RetriesOut);
		if(stats->TotalOut > 0)
			logger(1, "     Util=%f, Messages bandwidth=%f\n",
				((float)(1) -
				 ((float)(stats->WaitOut + stats->AckOut +
					stats->MessagesOut) / (float)(stats->TotalOut))),
				(float)(stats->MessagesOut) / (float)(stats->TotalOut));
		logger(1, "In:  Total=%d, Wait-a-Bit=%d, Acks=%d, Messages=%d, NAKS=%d\n",
			stats->TotalIn, stats->WaitIn, stats->AckIn,
			stats->MessagesIn, stats->RetriesIn);
		if(stats->TotalIn > 0)
			logger(1, "     Util=%f, Messages bandwidth=%f\n",
				((float)(1) -
				 ((float)(stats->WaitIn + stats->AckIn +
					stats->MessagesIn) / (float)(stats->TotalIn))),
				(float)(stats->MessagesIn) / (float)(stats->TotalIn));

/* Reste the statistics */
		stats->TotalIn = stats->TotalOut =
		stats->WaitIn = stats->WaitOut =
		stats->AckIn = stats->AckOut =
		stats->RetriesIn = stats->RetriesOut =
		stats->MessagesIn = stats->MessagesOut = 0;
	}
/* Use -1 so Delete_lines_timeout for line 0 will not clear us */
	queue_timer(T_STATS_INTERVAL, -1, T_STATS);
}
#endif


/**************** DEBUG section *********************************/
/*
 | Loop over all lines. For each line, if the xmit or receive buffer size is
 | non-zero, dump it.
 */
debug_dump_buffers(UserName)
char	*UserName;
{
	register int	i;
	register struct LINE	*temp;

	logger(1, "** IO, Debug-dump-buffers called by user %s\n", UserName);

	for(i = 0; i < MAX_LINES; i++) {
		temp = &(IoLines[i]);
		if(*(temp->HostName) == '\0')	/* Line not defined */
			continue;
		if(temp->XmitSize > 0) {
			logger(1, "Line=%d, node=%s, xmit:\n",
				i, temp->HostName);
			trace(temp->XmitBuffer, temp->XmitSize, 1);
		}
		if(temp->RecvSize > 0) {
			logger(1, "Line=%d, node=%s, recv:\n",
				i, temp->HostName);
			trace(temp->buffer, temp->RecvSize, 1);
		}
	}
	logger(1, "** IO, End of buffers dump\n");
	dump_gone_list();
}


/*
 | Rescan the queue. Clear all the current queue (free all its memory) and
 | then rescan the queue to queue again all files. This is done after files
 | are manually renamed to be queued to another link.
 */
debug_rescan_queue(UserName)
char	*UserName;
{
	register int	i;
	register struct LINE	*temp;
	register struct QUEUE	*Entry, *NextEntry;

	logger(1, "** IO, Debug-rescan-queue called by user %s\n", UserName);

	for(i = 0; i < MAX_LINES; i++) {
		temp = &(IoLines[i]);
		if((Entry = temp->QueueStart) != NULL) {
			while(Entry != NULL) {
				NextEntry = Entry->next;
				free(Entry);
				Entry = NextEntry;
			}
			temp->QueueStart = temp->QueueEnd = NULL;
		}
		temp->QueuedFiles = 0;
	}

	logger(1, "** IO, Memory freed, starting queue scan\n");
	init_files_queue();
	logger(1, "** IO, Queue scan done.\n");
}


/*================== PARSE-COMMAND =================================*/
/*
 | Parse the command line received via the command mailbox/socket.
 */
parse_operator_command(line)
unsigned char	*line;
{
	unsigned char	Faddress[SHORTLINE],	/* Sender for NMR messages */
			Taddress[SHORTLINE],	/* Receiver for NMR */
			UserName[16],	/* For verifying sender's authority */
			*p;
	EXTERNAL struct SIGN_OFF SignOff;
	int	i;

	switch(*line) {	/* The first byte is the command code. */
	case CMD_SHUTDOWN_ABRT:	/* Shutdown now */
		send_opcom("HUJI-NJE: Immediate shutdown by operator request.");
		for(i = 0; i < MAX_LINES; i++)
			if(IoLines[i].state == ACTIVE)
				   send_data(i, &SignOff,
					(int)(sizeof(struct SIGN_OFF)),
					(int)(ADD_BCB_CRC));
		MustShutDown = 1;
		shut_gone_users();	/* Inform them */
#ifdef VMS
		sys$wake(0,0);	/* Wakeup the main module */
#endif
		break;
	case CMD_SHUTDOWN:	/* Normal shudown */
		logger((int)(1), "Normal shutdown requested by %s\n",
			&line[1]);
		MustShutDown = -1;	/* Signal it */
		/* Mark all lines as needing shutdown */
		for(i = 0; i < MAX_LINES; i++)
			IoLines[i].flags |= F_SHUT_PENDING;
		shut_gone_users();	/* Inform them */
		can_shut_down();	/* Maybe all lines are
					   already inactive? */
		break;
	case CMD_SHOW_LINES:
/* LINE conatains username only. The routine we call expect full address, so
   add our local node name */
		   strcat(&line[1], "@");
		   strcat(&line[1], LOCAL_NAME);
		   show_lines_status(&line[1]);
		   break;
	case CMD_SHOW_QUEUE: show_files_queue(&line[1]); break;
	case CMD_QUEUE_FILE: 
		/* Compute the file's size in blocks: */
		i = ((int)(line[1] << 8) + (int)(line[2]));
		queue_file(&line[3], i);
		break;
	case CMD_SEND_MESSAGE:	/* Get the parameters */
	case CMD_SEND_COMMAND:
		if(sscanf(&line[2], "%s %s", Faddress, Taddress) != 2) {
			logger((int)(1), "Illegal SEND_MESSAGE line: '%s'\n",
				&line[2]);
			break;	/* Illegal line */
		}
/* The sender includes only the username. Add @LOCAL-NAME to it. */
		i = strlen(Faddress);
		Faddress[i++] = '@';
		strcpy(&Faddress[i], LOCAL_NAME);
		if(*line == CMD_SEND_MESSAGE) {
/* If there is no @ in the Taddress, then append our local nodename, so it'll
   be sent to a local user */
			if(strchr(Taddress, '@') == NULL) {
				i = strlen(Taddress);
				Taddress[i++] = '@';
				strcpy(&Taddress[i], LOCAL_NAME);
			}
			send_nmr(Faddress, Taddress,
				&line[line[1]], strlen(&line[line[1]]),
				(int)(ASCII), (int)(CMD_MSG));
		}
		else
			send_nmr(Faddress, Taddress,
				&line[line[1]], strlen(&line[line[1]]),
				(int)(ASCII), (int)(CMD_CMD));
		break;
	case CMD_START_LINE:
		restart_line((int)(line[1] & 0xff));
		break;
	case CMD_STOP_LINE:	/* Stop a line after the last file */
		i = (int)(line[1] & 0xff);
		if((i >= 0) && (i < MAX_LINES)) {
			   IoLines[i].flags |=
				F_SHUT_PENDING;
			logger((int)(1), "VMS, OPerator requested SHUT for line #%d\n",
				i);
		}
		else
			logger((int)(1),
				"VMS, Illegal line numbr(%d) for STOP LINE\n",
					i);
		break;
	case CMD_FORCE_LINE:	/* Stop a line now */
		i = (int)(line[1] & 0xff);
		if((i >= 0) && (i < MAX_LINES)) {
			logger((int)(1), "VMS, Line #%d forced by operator request\n",
				i);
			if(IoLines[i].state == ACTIVE)
			   send_data(i, &SignOff,
				(int)(sizeof(struct SIGN_OFF)),
				(int)(ADD_BCB_CRC));
			IoLines[i].state = SIGNOFF;
			/* Requeue all files back: */
			restart_channel(i);
		}
		else
			logger((int)(1),
				"VMS, Illegal line numbr(%d) for FORCE LINE\n",
					i);
		break;
	case CMD_DEBUG_DUMP:	/* Dump lines buffers */
		debug_dump_buffers(&line[1]); /* Pass the username */
		break;
	case CMD_DEBUG_RESCAN:	/* Rescan queue */
		debug_rescan_queue(&line[1]);
		break;
	case CMD_LOGLEVEL:	/* Set new loglevel */
		i = LogLevel;
		LogLevel = 1;	/* To close the file; guaranteed log checkpoint */
		logger((int)(1), "VMS, Log level changed from %d to %d\n",
			i, line[1] & 0xff);
		LogLevel = line[1] & 0xff;
		break;
	case CMD_CHANGE_ROUTE:	/* Change the route in our database */
		sscanf(&line[1], "%s %s", Faddress, Taddress);
		logger(1, "VMS, Route %s changed to %s by operator\n",
			Faddress, Taddress);
		change_route(Faddress, Taddress);
		break;
	case CMD_GONE_ADD_UCP:
	case CMD_GONE_ADD:
		add_gone_user(&line[1]); break;
	case CMD_GONE_DEL_UCP:
	case CMD_GONE_DEL:
		del_gone_user(&line[1]); break;
	default: logger((int)(1), "VMS, Illegal command = %d\n",
			   (int)(*line)); break;
	}
}
