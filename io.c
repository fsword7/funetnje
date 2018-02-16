/* IO.C    V3.3-mea
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
 |
 | Sections: SHOW-LINES:   Send to users the lines status.
 |           INIT-IO:      Initialize the I/O lines.
 |           QUEUE-IO:     Queue an I/O request.
 |           STATS:        Write statistics.
 |           COMMAND-PARSER Parses the opareator's commands.
 */
#include "consts.h"
#include "headers.h"
#include "prototypes.h"

static int   add_VMnet_block __(( const int Index, const int flag, const void *buffer, const int size, void *NewLine, const int BCBcount ));
static void  debug_dump_buffers __(( const char *UserName ));

EXTERNAL struct	ENQUIRE	Enquire;

EXTERNAL int	MustShutDown;
EXTERNAL int	PassiveSocketChannel;
EXTERNAL int	LogLevel;

#define	VMNET_INITIAL_TIMEOUT	15	/* 15 seconds */

/*============================ SHOW-LINES ==============================*/
/*
 | Send the lines status to the user.
 */
void show_lines_status(to,infoprint)
const char	*to;	/* User@Node */
int infoprint;
{
	int	i, j;
	char	line[LINESIZE],
		from[SHORTLINE];	/* The message's sender address */
	struct	LINE	*temp;

	time_t now;

	time(&now);

	/* Create the sender's address.
	   It is the daemon on the local machine */

	sprintf(from, "@%s", LOCAL_NAME);	/* No username */

	sprintf(line, "FUNET-NJE version %s(%s)/%s, Lines status:",
		VERSION, SERIAL_NUMBER, version_time);
	send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);

	for (i = 0; i < MAX_LINES; i++) {
	  temp = &(IoLines[i]);
	  if (*(temp->HostName) == 0) continue; /* No host defined for this
						   line, ignore             */
	  sprintf(line, "Line.%d %-8s %3d (Q=%04d)  ", i,
		  temp->HostName, temp->TotalErrors,
		  temp->QueuedFiles);

	  switch (temp->state) {
	    case INACTIVE:
	        strcat(line, "INACTIVE  ");
		break;
	    case SIGNOFF:
		strcat(line, "SIGNEDOFF ");
		break;
	    case DRAIN:
		strcat(line, "DRAINED   ");
		break;
	    case ACTIVE:
		strcat(line, "ACTIVE    ");
		break;
	    case F_SIGNON_SENT:
	    case I_SIGNON_SENT:
		strcat(line, "SGN-Sent  ");
		break;
	    case LISTEN:
		strcat(line, "LISTEN    ");
		break;
	    case RETRYING:
		strcat(line, "Retry     ");
		break;
	    case TCP_SYNC:
		if (temp->socketpending >= 0)
		  strcat(line, "TCP-pend  ");
		else
		  strcat(line, "TCP-sync  ");
		break;
	    default:
		strcat(line, "******    ");
		break;
	  }
	  switch (temp->type) {
	    case DMF:
		sprintf(&line[strlen(line)], "  DMF (%s)",
			temp->device);
		break;
	    case DMB:
		sprintf(&line[strlen(line)], "  DMB (%s)",
			temp->device);
		break;
	    case DSV:
		sprintf(&line[strlen(line)], "  DSV (%s)",
			temp->device); 
		break;
	    case UNIX_TCP:
		strcat(line, "  TCP      ");
		break;
	    case EXOS_TCP:
		strcat(line, "  TCP(Exos)");
		break;
	    case MNET_TCP:
		strcat(line, "  TCP(Mnet)");
		break;
	    case DEC__TCP:
		strcat(line, "  TCP(DEC) ");
		break;
	    case DECNET:
		sprintf(&line[strlen(line)], "  DECNET (%s)",
			temp->device);
		break;
	    case ASYNC:
		sprintf(&line[strlen(line)], "  ASYNC (%s)",
			       temp->device);
		break;
	    default:
		strcat(line, "   ***");
		break;
	  }
	  send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);

	  if (infoprint) {
	    int InAge   = temp->InAge   ? now - temp->InAge   : 99999;
	    int XmitAge = temp->XmitAge ? now - temp->XmitAge : 99999;
	    sprintf(line," Bufinfo: InAge=%ds, RecvSize=%d, XmitAge=%ds, XmitSize=%d",
		    InAge, temp->RecvSize, XmitAge, temp->XmitSize);
	    send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
	  }

	  if (temp->state != ACTIVE) /* Not active - don't display */
	    continue;		/* streams status */

	  for (j = 0; j < temp->MaxStreams; j++) {
	    sprintf(line, "Rcv-%d  ", j);
	    if (temp->InStreamState[j] != S_INACTIVE) {
	      /* Don't show inactive ones */
	      switch (temp->InStreamState[j]) {
		case S_INACTIVE:
		    strcat(line, "Inactive");
		    break;
		case S_REQUEST_SENT:
		    strcat(line, "REQUEST ");
		    break;
		case S_NJH_SENT:
		    strcat(line, "NJH-RECV");
		    break;
		case S_NDH_SENT:
		    strcat(line, "NDH-RECV");
		    break;
		case S_NJT_SENT:
		    strcat(line, "NJT-RECV");
		    break;
		case S_SENDING_FILE:
		    strcat(line, "RECEIVNG");
		    break;
		case S_EOF_SENT:
		    strcat(line, "EOF-RECV");
		    break;
		case S_REFUSED:
		    strcat(line, "REFUSED ");
		    break;
		case S_WAIT_A_BIT:
		    strcat(line, "WAITABIT");
		    break;
		default:
		    strcat(line, "******   ");
		    break;
	      }
	      if ((temp->InStreamState[j] != S_INACTIVE) &&
		  (temp->InStreamState[j] != S_REFUSED) )
		sprintf(&line[strlen(line)], " (%s) (%s => %s) %dkB",
			temp->InFileParams[j].JobName,
			temp->InFileParams[j].From,
			temp->InFileParams[j].To,
			(int)(temp->OutFilePos[j]/1024));
	      send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
	    }
	    sprintf(line, "Snd-%d  ", j);
	    if (temp->OutStreamState[j] != S_INACTIVE) {
	      /* Don't show inactive ones */
	      switch(temp->OutStreamState[j]) {
		case S_INACTIVE:
		    strcat(line, "Inactive");
		    break;
		case S_REQUEST_SENT:
		    strcat(line, "REQUEST ");
		    break;
		case S_NJH_SENT:
		    strcat(line, "NJH-SENT");
		    break;
		case S_NDH_SENT:
		    strcat(line, "NDH-SENT");
		    break;
		case S_SENDING_FILE:
		    strcat(line, "SENDING ");
		    break;
		case S_NJT_SENT:
		    strcat(line, "NJT-SENT");
		    break;
		case S_EOF_SENT:
		    strcat(line, "EOF-SENT");
		    break;
		case S_REFUSED:
		    strcat(line, "REFUSED ");
		    break;
		case S_WAIT_A_BIT:
		    strcat(line, "WAITABIT");
		    break;
		default:
		    strcat(line, "******  ");
		    break;
	      }
	      if ((temp->OutStreamState[j] != S_INACTIVE)  &&
		  (temp->OutStreamState[j] != S_REFUSED) )
		sprintf(&line[strlen(line)], " (%s) (%s => %s) %dkB",
			temp->OutFileParams[j].JobName,
			temp->OutFileParams[j].From,
			temp->OutFileParams[j].To,
			(int)(temp->InFilePos[j]/1024));
	      send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
	    }
	  }
	  sprintf(line," %d streams in service, inactive ones not shown",
		  temp->MaxStreams);
	  send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
/*if (temp->XmitSize != 0 || temp->RecvSize != 0)  {
  sprintf(line," Bufinfo: InAge=%ds, RecvSize=%d, XmitAge=%ds, XmitSize=%d",
	  now - temp->InAge,temp->RecvSize, now - temp->XmitAge,temp->XmitSize);
  send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
} */
	}

	sprintf(line, "End of Q SYS display");
	send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
}


/*
 | Send the lines statistics to the user.
 */
void
show_lines_stats(to)
const char	*to;	/* User@Node */
{
	int	i;
	char	line[LINESIZE],
		from[SHORTLINE];	/* The message's sender address */
	struct	LINE	*temp;

/* Create the sender's address. It is the daemon on the local machine */
	sprintf(from, "@%s", LOCAL_NAME);	/* No username */

	sprintf(line, "FUNET-NJE version %s(%s)/%s, Lines statistics:",
		VERSION, SERIAL_NUMBER, version_time);
	send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);

	for (i = 0; i < MAX_LINES; i++) {
	  temp = &(IoLines[i]);
	  if (*(temp->HostName) != '\0') {
	    sprintf(line, "Line.%d %8s: Blocks send/recv: %d/%d, Wait recvd: %d,",
		    i, temp->HostName,
		    temp->sumstats.TotalOut+temp->stats.TotalOut,
		    temp->sumstats.TotalIn+temp->stats.TotalIn,
		    temp->sumstats.WaitIn+temp->stats.WaitIn	    );
	    send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
	    sprintf(line, "     NMRs sent/recv: %d/%d, NAKs send/recvd: %d/%d,  Acks-only sent/recv: %d/%d",
		    temp->sumstats.MessagesOut+temp->stats.MessagesOut,
		    temp->sumstats.MessagesIn+temp->stats.MessagesIn,
		    temp->sumstats.RetriesOut+temp->stats.RetriesOut,
		    temp->sumstats.RetriesIn+temp->stats.RetriesIn,
		    temp->sumstats.AckOut+temp->stats.AckOut,
		    temp->sumstats.AckIn+temp->stats.AckIn);
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
void
init_communication_lines()
{
	int	i, InitTcpPassive, InitDECnetPassive = 0, TcpType = 0;

	InitDECnetPassive = InitTcpPassive = 0;
	for (i = 0; i < MAX_LINES; i++) {
	  if (IoLines[i].HostName[0] == 0) continue; /* no line */
	  if (IoLines[i].state == ACTIVE) {
	    switch (IoLines[i].type) {
#ifdef VMS
	      case DMB:
	      case DSV:
	      case DMF:
	          init_dmf_connection(i);
		  queue_receive(i);
		  break;
	      case DEC__TCP:
	      case MNET_TCP:
	      case EXOS_TCP:
		  /* Create an active side and also passive side */
		  init_active_tcp_connection(i,0);
		  InitTcpPassive++;
		  TcpType = IoLines[i].type;
		  break;
	      case ASYNC:
		  init_async_connection(i);
		  queue_receive(i);
		  break;
	      case DECNET:
		  init_active_DECnet_connection(i);
		  InitDECnetPassive++;
		  break;
#endif
#ifdef UNIX
	      case UNIX_TCP:
		  init_active_tcp_connection(i,0);
		  InitTcpPassive++;
		  break;
#endif
	      case X_25:
	      default:
		  logger(1, "IO: No protocol for line #%d\n",
			 i);
		  break;
		}
	  }
	}

/* Check whether we have to queue a passive accept for TCP & DECnet lines */
	if(InitTcpPassive != 0)
	  init_passive_tcp_connection(TcpType);
#ifdef VMS
	if (InitDECnetPassive != 0)
	  init_DECnet_passive_channel();
#endif
}


/*
 | Reset various link state variables, make sure all open files
 | get closed, queue states get reset, etc..
 */
void
init_link_state(Index)
const int Index;
{
	struct	LINE	*temp;
	struct	MESSAGE	*MessageEntry;
	register int	i;	/* Stream index */
	int	oldstate;

	temp = &(IoLines[Index]);
	if (Index > MAX_LINES || temp->HostName[0] == 0) {
	  logger(1,"IO: init_link_state(%d) - Bad line number!\n",Index);
	  return;
	}

	oldstate = temp->state;
	temp->state = INACTIVE;

	logger(2, "IO: init_link_state(%s/%d) type=%d\n",
	       temp->HostName,  Index,  temp->type);
	
	/* Close active file, and delete of output file. */
	for (i = 0; i < temp->MaxStreams; i++) {
	  temp->CurrentStream = i;
	  if ((temp->OutStreamState[i] != S_INACTIVE) &&
	      (temp->OutStreamState[i] != S_REFUSED)) { /* File active */
	    close_file(Index, F_INPUT_FILE, i);
	    requeue_file_entry(Index,temp);
	  }
	  if ((temp->InStreamState[i] != S_INACTIVE) &&
	      (temp->InStreamState[i] != S_REFUSED)) { /* File active */
	    delete_file(Index, F_OUTPUT_FILE, i);
	  }
	}

	i = requeue_file_queue(temp);
	if (i != 0)
	  logger(1,"IO: init_link_state() calling requeue_file_queue() got an activecount of %d!  Should be 0!\n",i); 
	temp->QueuedFiles = i;
	temp->QueuedFilesWaiting = 0;

	temp->state = oldstate;

	/* Dequeue all messages and commands waiting on it */
	MessageEntry = temp->MessageQstart;
	while (MessageEntry != NULL) {
	  struct MESSAGE *NextEntry = MessageEntry->next;
	  free(MessageEntry);
	  MessageEntry = NextEntry;
	}

#ifdef	USE_XMIT_QUEUE
	/* Clear all queued transmit buffers */
	while (temp->FirstXmitEntry != temp->LastXmitEntry) {
	  if (temp->XmitQueue[temp->FirstXmitEntry])
	    free(temp->XmitQueue[temp->FirstXmitEntry]);
	  temp->XmitQueue[temp->FirstXmitEntry] = NULL;
	  temp->FirstXmitEntry = (temp->FirstXmitEntry +1) % MAX_XMIT_QUEUE;
	}
	temp->FirstXmitEntry = 0;
	temp->LastXmitEntry  = 0;
#endif

	/* After all files closed, restart the line */
	temp->errors = 0;	/* We start again... */
	temp->InBCB  = 0;
	temp->OutBCB = 0;
	temp->flags &= ~(F_RESET_BCB    | F_WAIT_A_BIT | F_WAIT_V_A_BIT |
			 F_WAIT_V_A_BIT | F_SENDING    | F_CALL_ACK     |
			 F_XMIT_CAN_WAIT| F_SLOW_INTERLEAVE);
	temp->CurrentStream = 0;
	temp->ActiveStreams = 0;
	temp->FreeStreams   = temp->MaxStreams;
	for (i = 0; i < temp->MaxStreams; i++) {
	  temp->InStreamState[i] = S_INACTIVE;
	  temp->OutStreamState[i] = S_INACTIVE;
	}
	temp->MessageQstart = NULL;
	temp->MessageQend   = NULL;
	temp->RecvSize = 0;
	temp->XmitSize = 0;

	/* Dequeue all waiting timeouts for this line */
	delete_line_timeouts(Index);
}

/*
 | Restart a line that is not active.
 */
void
restart_line(Index)
const int Index;
{
	struct LINE *temp = &IoLines[Index];

	/* First check the line is in correct status */
	if ((Index < 0) || (Index >= MAX_LINES)) {
	  logger(1, "IO, Restart line: line #%d out of range\n",
		 Index);
	  return;
	}

	temp->flags &= ~F_SHUT_PENDING; /* Remove the shutdown
						    pending flag. */

	switch (temp->state) {
	  case INACTIVE:
	  case SIGNOFF:
	  case RETRYING:
	      break;	/* OK - go ahead and start it */
	  case DRAIN:
	  case F_SIGNON_SENT:
	  case I_SIGNON_SENT:
	      logger(1, "IO, Trying to start line %s (#%d) in state %d. Illegal\n",
		     temp->HostName, Index, temp->state);
	      return;
	  default:
	      logger(1, "IO, Line %s (#%d) in illegal state (#%d) for start op.\n",
		     temp->HostName,Index,temp->state);
	      return;
	}

	logger(2, "IO, Restarting line #%d (%s)\n", Index, temp->HostName);

	/* Init the line according to its type */
	temp->state = DRAIN;	/* Will be set to INACTIVE in case
				   of error during initialization. */

	/* Programmable backoff.. */
	if (temp->RetryIndex < MAX_RETRIES-1 &&
	    temp->RetryPeriods[temp->RetryIndex+1] > 0)
	  temp->RetryPeriod = temp->RetryPeriods[temp->RetryIndex++];
	else
	  temp->RetryPeriod = temp->RetryPeriods[temp->RetryIndex];

	init_link_state(Index);

	switch (temp->type) {
#ifdef VMS
	  case DMB:
	  case DSV:
	  case DMF:
	      init_dmf_connection(Index);
	      queue_receive(Index);
	      break;
	  case DEC__TCP:
	  case MNET_TCP:
	  case EXOS_TCP:
	      init_active_tcp_connection(Index,0);
	      break;
	  case DECNET:
	      init_active_DECnet_connection(Index);
	      break;
	  case ASYNC:
	      init_async_connection(Index);
	      queue_receive(Index);
	      break;
#endif
#ifdef UNIX
	  case UNIX_TCP:
	      init_active_tcp_connection(Index,0);
	      break;
#endif
	  default:
	      break;
	}
}



/*===================== QUEUE-IO =====================================*/
/*
 | Queue the receive for the given line. Test its type, and then call the
 | appropriate routine. Also queue a timer request for it (our internal
 | timer, not the VMS one).
 */
void
queue_receive(Index)
const int	Index;
{
	struct	LINE	*temp;

	temp = &(IoLines[Index]);

/* Do we have to queue a receive ??? */
	switch (temp->state) {
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
	  default:
	      logger(1, "IO, Illegal line state %d on line %d during queue-Receive\n",
		     temp->state, Index);
	      temp->state = INACTIVE;
	      return;
	}

	switch (temp->type) {
#ifdef VMS
	  case DMB:
	  case DSV:
	  case DMF:
	      if (queue_dmf_receive(Index) != 0)
		/* Queue a timeout for it */
		temp->TimerIndex =
		  queue_timer(temp->TimeOut, Index, T_DMF_CLEAN);
	      break;
	  case ASYNC:
	      if (queue_async_receive(Index) != 0)
		temp->TimerIndex =
		  queue_timer(temp->TimeOut, Index, T_ASYNC_TIMEOUT);
	      break;
	  case DECNET:
	      if (queue_DECnet_receive(Index) != 0)
		temp->TimerIndex =
		  queue_timer(temp->TimeOut, Index,
			      T_DECNET_TIMEOUT);
		  break;
#ifdef EXOS
	  case EXOS_TCP:
	      if (queue_exos_tcp_receive(Index) != 0) {
		if (temp->state != ACTIVE)
		  temp->TimerIndex =
		    queue_timer(VMNET_INITIAL_TIMEOUT,
				Index, T_TCP_TIMEOUT);
		else
		  temp->TimerIndex =
		    queue_timer(temp->TimeOut, Index,
				T_TCP_TIMEOUT);
	      }
	      break;
#endif
#ifdef MULTINET_or_DEC
	  case DEC__TCP:
	  case MNET_TCP:
	      if (queue_mnet_tcp_receive(Index) != 0)

		/* If the link was not established yet, use longer timeout
		   (since the VMnet is running here in a locked-step mode
		   on the VM side). If we use the regular small timeout we
		   make the login process difficult (we transmit the next
		   DLE-ENQ packet while the other side is acking the previous
		   one. This is  caused due to slow lines). */

		if (temp->state != ACTIVE)
		  temp->TimerIndex =
		    queue_timer(VMNET_INITIAL_TIMEOUT,
				Index, T_TCP_TIMEOUT);
		else
		  temp->TimerIndex =
		    queue_timer(temp->TimeOut, Index,
				T_TCP_TIMEOUT);
	      break;
#endif
#endif /* VMS */
#ifdef UNIX
	  case UNIX_TCP: /* We poll here, so we don't queue a real receive */
	      temp->TimerIndex =
		queue_timer(temp->TimeOut, Index, T_TCP_TIMEOUT);
	      break;
#endif /* UNIX */
	  default:
	      logger(1, "IO: No support for device on line #%d\n", Index);
	      break;
	}
}


/*
 | Send the data using the correct interface. Before sending, add BCB+FCS and
 | CRC if asked for.
 | If the line is of TCP or DECnet type, we add only the first headers, and
 | do not add the tralier (DLE+ETB_CRCs) and we do not duplicate DLE's.
 | See comments about buffers at the head of this module.
 | If we are called when the link is already transmitting (and haven't finished
 | it), then the buffer is queued for later transmission if the line supports
 | it. The write AST routine will send it.
 */
void
send_data(Index, buffer, size, AddEnvelope)
const int	Index, size, AddEnvelope;	/* Add the BCB+...? */
const void	*buffer;
{
	struct	LINE	*temp;
	int	NewSize;
	const	unsigned char *SendBuffer;

	register int	i, flag;
	/* In the followings, ttr/b is used to point inside the buffer, while
	   Ttr/b is used to construct the buffer (RISC alignment..). */
	char		*ttb, *ttr;
	struct	TTB	Ttb;
	struct	TTR	Ttr;
#ifdef	USE_XMIT_QUEUE
	unsigned char *p;
	register int NextEntry;
#endif

	temp = &(IoLines[Index]);
	temp->flags &= ~F_XMIT_MORE;	/* Default - do not block */

	/* Collects stats */
	if (*(unsigned char *)buffer != NAK)
	  temp->stats.TotalOut++;
	else
	  temp->stats.RetriesOut++;

	SendBuffer = buffer;
	NewSize    = size;
	i = temp->OutBCB;
	flag = (temp->flags & F_RESET_BCB);

#ifdef	USE_XMIT_QUEUE  /* Obsolete ? */
	/* Test whether the link is already transmitting.
	   If so, queue the message only if the link supports so.
	   If not, ignore this transmission.			  */

	if ((temp->flags & F_SENDING) != 0) { /* Yes - it is occupied */
	  if ((temp->flags & F_RELIABLE) == 0) {
	    logger(1, "IO, Line %s doesn't support queueing\n",
		   temp->HostName);
	    return;		/* Ignore it */
	  }
	  temp->flags |= F_WAIT_V_A_BIT; /* Signal wait-a-bit so sender
					    will not transmit more */

	  /* Calculate where shall we put it in the queue (Cyclic queue) */
	  NextEntry = (temp->LastXmitEntry + 1) % MAX_XMIT_QUEUE;

	  /* If the new last is the same as the first one,
	     then we have no place... */
	  if (NextEntry == temp->FirstXmitEntry) {
	    logger(1, "IO, No place to queue Xmit on line %s\n",
		   temp->HostName);
	    return;
	  }

	  /* There is a place - queue it */
	  if ((p = (unsigned char*)malloc(size + TTB_SIZE + 5 + 2 +
					  /* 5 for BCB+FCS overhead,
					     2 for DECnet CRC */
					  2 * TTR_SIZE)) == NULL) {
#ifdef VMS
	    logger(1, "IO, Can't malloc. Errno=%d, VaxErrno=%d\n",
		   errno, vaxc$errno);
#else
	    logger(1, "IO, Can't malloc. Errno=%d\n", errno);
#endif
	    bug_check("IO, Can't malloc() memory\n");
	  }

	  NewSize = add_VMnet_block(Index, AddEnvelope,
				    buffer, size, &p[TTB_SIZE], i);
	  SendBuffer = p;
	  if (AddEnvelope == ADD_BCB_CRC)
	    if (flag != 0)	/* If we had to reset BCB, don't increment */
	      temp->OutBCB = (i + 1) % 16;

	  /* <TTB>(LN=length_of_VMnet+TTR) <VMnet_block> <TTR>(LN=0) */

	  Ttr.F = Ttr.U = 0;
	  Ttr.LN = 0;		/* Last TTR */
	  ttr = (void *)(p + NewSize + TTB_SIZE);
	  memcpy(ttr, &Ttr, TTR_SIZE);

	  NewSize += (TTB_SIZE + TTR_SIZE);
	  Ttb.F = 0;		/* No flags */
	  Ttb.U = 0;
	  Ttb.LN = htons(NewSize);
	  Ttb.UnUsed = 0;
	  ttb = (void *)(p + 0);
	  memcpy(ttb, &Ttb, TTB_SIZE);

	  temp->XmitQueue[temp->LastXmitEntry] = (char *)p;
	  temp->XmitQueueSize[temp->LastXmitEntry] = NewSize;
	  temp->LastXmitEntry = NextEntry;
	  return;
	}
#endif

	/* No queueing - format buffer into output buffer.
	   If the line is TCP - block more records if can.
	   Other types - don't try to block.			*/

	if ((temp->flags & F_RELIABLE) != 0) {
	  /* There are  DECNET, or TCP/IP links, which get TTB + TTRs */

	  if (temp->XmitSize == 0)
	    temp->XmitSize = TTB_SIZE;
	  /* First block - leave space for TTB */
	  NewSize = add_VMnet_block(Index, AddEnvelope,
				    buffer, size,
				    &temp->XmitBuffer[temp->XmitSize], i);
	  temp->XmitSize += NewSize;
	  if (AddEnvelope == ADD_BCB_CRC)
	    if (flag != 0)	/* If we had to reset BCB, don't increment */
	      temp->OutBCB = (i + 1) % 16;

	} else {		/* Normal block */

	  if (AddEnvelope == ADD_BCB_CRC) {
	    if ((temp->type == DMB) || (temp->type == DSV))
	      temp->XmitSize =
		NewSize = add_bcb(Index, buffer,
				  size, temp->XmitBuffer, i);
	    else
	      temp->XmitSize =
		NewSize = add_bcb_crc(Index, buffer,
				      size, temp->XmitBuffer, i);
	    if (flag != 0)	/* If we had to reset BCB, don't increment */
	      temp->OutBCB = (i + 1) % 16;

	  } else {
	    memcpy(temp->XmitBuffer, buffer, size);
	    temp->XmitSize = size;
	  }
	  SendBuffer = temp->XmitBuffer;
	}

	/* Check whether we've overflowed some buffer. If so - bugcheck... */

	if (temp->XmitSize > MAX_BUF_SIZE) {
	  logger(1, "IO: Xmit buffer overflow in line #%d\n", Index);
	  bug_check("Xmit buffer overflow\n");
	}

	/* If TcpIp line and there is room in buffer and the sender
	   allows us to defer sending - return. */

	if ((temp->flags & F_RELIABLE) != 0) {
	  if ((temp->flags & F_XMIT_CAN_WAIT) != 0)
	    if ((temp->XmitSize + TTB_SIZE +
		 2 * TTR_SIZE + 5 + 2 +
		 /* +5 for BCB + FCS overhead, +2 for DECnet;s CRC */
		 temp->MaxXmitSize) < temp->TcpXmitSize) { /* There is room */
	      temp->flags |= F_XMIT_MORE;
	      return;
	    }
	}

	/* Ok - we have to transmit buffer. If DECnet or TcpIp - insert
	   the TTB and add TTR at end */

	if ((temp->flags & F_RELIABLE) != 0) {
	  NewSize = temp->XmitSize;
	  ttb = (void *)temp->XmitBuffer;
	  ttr = (void *)(&(temp->XmitBuffer[NewSize]));
	  Ttr.F = Ttr.U = 0;
	  Ttr.LN = 0;		/* Last TTR */
	  memcpy(ttr, &Ttr, TTR_SIZE);
	  temp->XmitSize = NewSize = NewSize + TTR_SIZE;
	  Ttb.F = 0;		/* No flags */
	  Ttb.U = 0;
	  Ttb.LN = htons(NewSize);
	  Ttb.UnUsed = 0;
	  memcpy(ttb, &Ttb, TTB_SIZE);
	  SendBuffer = temp->XmitBuffer;

	  /* Check whether we've overflowed some buffer. If so - bugcheck... */
	  if (temp->XmitSize > MAX_BUF_SIZE) {
	    logger(1, "IO, TCP Xmit buffer overflow in line #%d\n", Index);
	    bug_check("Xmit buffer overflow\n");
	  }
	}

	temp = &(IoLines[Index]);
#ifdef DEBUG
	logger(3, "IO: Sending: line=%s, size=%d, sequence=%d:\n",
	       temp->HostName, NewSize, i);
	trace(SendBuffer, NewSize, 5);
#endif
	switch(temp->type) {
#ifdef VMS
	  case ASYNC:
	      send_async(Index, SendBuffer, NewSize);
	      return;
	  case DMB:
	  case DSV:
	  case DMF:
	      send_dmf(Index, SendBuffer, NewSize);
	      return;
	  case DECNET:
	      send_DECnet(Index, SendBuffer, NewSize);
	      return;
#ifdef EXOS
	  case EXOS_TCP:
	      send_exos_tcp(Index, SendBuffer, NewSize);
	      return;
#endif
#ifdef MULTINET_or_DEC
	  case DEC__TCP:
	  case MNET_TCP:
	      send_mnet_tcp(Index, SendBuffer, NewSize);
	      return;
#endif
#endif
#ifdef UNIX
	  case UNIX_TCP:
	      send_unix_tcp(Index, SendBuffer, NewSize);
	      return;
#endif
	  default:
	      logger(1, "IO: No support for device on line #%d\n", Index);
	      break;
	}
}


/*
 | TCP lines - add the initial TCP header, and copy the buffer. Don't add the
 | final TTR, to allow blocking of more records. The caller routine will add
 | it.
 */
static int
add_VMnet_block(Index, flag, buffer, size, NewLine, BCBcount)
const int	Index, flag, size, BCBcount;
const void	*buffer;
void		*NewLine;
{
	struct	TTR	Ttr;
	char		*ttr;
	register int	NewSize;
	register unsigned char	*p;

	p = NewLine;
	NewSize = size + TTR_SIZE;
	if (flag == ADD_BCB_CRC)	/* Have to add BCB + FCS */
	  NewSize += 5;			/* DLE + STX + BCB + FCS + FCS */

	ttr = (void *)p;
	Ttr.F = 0;	/* No fast open */
	Ttr.U = 0;
	if (flag == ADD_BCB_CRC)
	  Ttr.LN = htons((size + 5));	/* The BCB header */
	else
	  Ttr.LN = htons(size);
	/* Copy the control blocks */
	memcpy(ttr, &Ttr, TTR_SIZE);
	p += TTR_SIZE;

	/* Put the DLE, STX, BCB and FCS.
	   If BCB is zero, send a "reset" BCB.	*/
	if (flag == ADD_BCB_CRC) {
	  *p++ = DLE; *p++ = STX;
	  if ((BCBcount == 0) && ((IoLines[Index].flags & F_RESET_BCB) == 0)) {
	    IoLines[Index].flags |= F_RESET_BCB;
	    *p++ = 0xa0;	/* Reset BCB count to zero */
	  } else
	    *p++ = 0x80 + (BCBcount & 0xf); /* Normal block */
	  *p++ = 0x8f; *p++ = 0xcf; /* FCS - all streams are enabled */
	}
	memcpy(p, buffer, size);
	return NewSize;
}


/*
 | Close the communication channel.
 */
void
close_line(Index)
const int	Index;
{
	IoLines[Index].flags &= ~F_SHUT_PENDING;	/* Clear the flag */

	switch(IoLines[Index].type) {
#ifdef VMS
	  case DMB:
	  case DSV:
	  case DMF:
	      close_dmf_line(Index);
	      break;
	  case ASYNC:
	      close_async_line(Index);
	      break;
	  case DECNET:
	      close_DECnet_line(Index);
	      break;
	  case DEC__TCP:
	  case MNET_TCP:
	  case EXOS_TCP:
	      close_tcp_chan(Index);
	      break;
#endif
#ifdef UNIX
	  case UNIX_TCP:
	      close_unix_tcp_channel(Index);
	      break;
#endif
	  default:
	      logger(1, "IO, Close-Line: line=%d, no support for device #%d\n",
		     Index, IoLines[Index].type);
	      break;
	}
}


/*================================= STATS ===========================*/
/*
 | Write the statistics, clear the counts and requeue the timer.
 */
void
compute_stats()
{
	int	i;
	struct	LINE	*temp;
	struct	STATS	*stats, *sum;

	for (i = 0; i < MAX_LINES; i++) {
	  temp = &(IoLines[i]);
	  if (*(temp->HostName) == 0) continue; /* no line */
	  stats = &temp->stats;
	  sum   = &temp->sumstats;

	  logger(2, "Stats for line #%d, name=%s, state=%d\n",
		 i, temp->HostName, temp->state);
	  logger(2, "Out: Total=%d, Wait-a-Bit=%d, Acks=%d, Messages=%d, NAKS=%d\n",
		 stats->TotalOut, stats->WaitOut, stats->AckOut,
		 stats->MessagesOut, stats->RetriesOut);
	  if (stats->TotalOut > 0)
	    logger(2, "     Util=%d%%, Messages bandwidth=%d%%\n",
		   (100 - 
		    (100*(stats->WaitOut+stats->AckOut+stats->MessagesOut)) /
		    stats->TotalOut),
		   (100 * stats->MessagesOut) / stats->TotalOut);
	  logger(2, "In:  Total=%d, Wait-a-Bit=%d, Acks=%d, Messages=%d, NAKS=%d\n",
		 stats->TotalIn, stats->WaitIn, stats->AckIn,
		 stats->MessagesIn, stats->RetriesIn);
	  if (stats->TotalIn > 0)
	    logger(2, "     Util=%d%%, Messages bandwidth=%d%%\n",
		   (100 -
		    (100*(stats->WaitIn + stats->AckIn + stats->MessagesIn)) /
		    stats->TotalIn),
		   (100 * stats->MessagesIn) / stats->TotalIn);

	  /* Reset the statistics */
	  
	  sum->TotalIn     += stats->TotalIn;     stats->TotalIn     = 0;
	  sum->TotalOut    += stats->TotalOut;    stats->TotalOut    = 0;
	  sum->WaitIn      += stats->WaitIn;      stats->WaitIn      = 0;
	  sum->WaitOut     += stats->WaitOut;     stats->WaitOut     = 0;
	  sum->AckIn       += stats->AckIn;       stats->AckIn       = 0;
	  sum->AckOut      += stats->AckOut;      stats->AckOut      = 0;
	  sum->RetriesIn   += stats->RetriesIn ;  stats->RetriesIn   = 0;
	  sum->RetriesOut  += stats->RetriesOut;  stats->RetriesOut  = 0;
	  sum->MessagesIn  += stats->MessagesIn;  stats->MessagesIn  = 0;
	  sum->MessagesOut += stats->MessagesOut; stats->MessagesOut = 0;
	}

	/* Use -1 so Delete_lines_timeout for line 0 will not clear us */

	queue_timer(T_STATS_INTERVAL, -1, T_STATS);
}


/**************** DEBUG section *********************************/
/*
 | Loop over all lines. For each line, if the xmit or receive buffer size is
 | non-zero, dump it.
 */
static void
debug_dump_buffers(UserName)
const char	*UserName;
{
	register int	i;
	register struct LINE	*temp;

	logger(1, "** IO, Debug-dump-buffers called by user %s\n", UserName);

	for (i = 0; i < MAX_LINES; i++) {
	  temp = &(IoLines[i]);
	  if (*(temp->HostName) == '\0')	/* Line not defined */
	    continue;
	  if (temp->XmitSize > 0) {
	    logger(1, "Line=%d, node=%s, xmit:\n",
		   i, temp->HostName);
	    trace(temp->XmitBuffer, temp->XmitSize, 1);
	  }
	  if (temp->RecvSize > 0) {
	    logger(1, "Line=%d, node=%s, recv:\n",
		   i, temp->HostName);
	    trace(temp->InBuffer, temp->RecvSize, 1);
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
/*static*/ void
debug_rescan_queue(UserName,opt)
const char	*UserName, opt;
{
	register int	i;
	register struct LINE	*temp;
	int activecnt = 0;

	logger(1, "** IO, Debug-rescan-queue called by user %s, opt: `%c'\n", UserName,opt);

	for (i = 0; i < MAX_LINES; i++) {
	  temp = &(IoLines[i]);
	  activecnt = 0;
	  if (temp->HostName[0] != 0  &&  temp->QueueStart != NULL) {
	    activecnt = delete_file_queue(temp);
	  }
	  temp->QueuedFiles = activecnt;
	  temp->QueuedFilesWaiting = 0;
	}

	if (opt != '-') {
	  logger(1, "** IO, Memory freed, starting queue scan\n");
	  init_files_queue();
	  logger(1, "** IO, Queue scan done.\n");
	} else {
	  logger(1, "** IO: no queue rescan executed!\n");
	}
}


/*================== PARSE-COMMAND =================================*/
/*
 | Parse the command line received via the command mailbox/socket.
 */
void
parse_operator_command(line,length)
unsigned char	*line;
const int length;
{
	unsigned char	Faddress[SHORTLINE],	/* Sender for NMR messages */
			Taddress[SHORTLINE];	/* Receiver for NMR */
	EXTERNAL struct SIGN_OFF SignOff;
	int	i = 0;

	switch (*line) {	/* The first byte is the command code. */
	  case CMD_SHUTDOWN_ABRT:	/* Shutdown now */
	      send_opcom("FUNET-NJE: Immediate shutdown by operator request.");
	      if (PassiveSocketChannel >= 0)
		close(PassiveSocketChannel);
	      PassiveSocketChannel = -1;
	      MustShutDown = 1;
	      for (i = 0; i < MAX_LINES; i++)
		if (IoLines[i].HostName[0] != 0 &&
		    IoLines[i].state == ACTIVE)
		  send_data(i, &SignOff,
			    sizeof(struct SIGN_OFF),
			    ADD_BCB_CRC);
	      shut_gone_users();	/* Inform them */
#ifdef VMS
	      sys$wake(0,0);	/* Wakeup the main module */
#endif
	      break;
	  case CMD_SHUTDOWN:	/* Normal shudown */
	      logger(1, "Normal shutdown requested by %s\n", &line[1]);
	      MustShutDown = -1;	/* Signal it */
	      if (PassiveSocketChannel >= 0)
		close(PassiveSocketChannel);
	      PassiveSocketChannel = -1;
	      /* Mark all lines as needing shutdown */
	      for (i = 0; i < MAX_LINES; i++)
		IoLines[i].flags |= F_SHUT_PENDING;
	      shut_gone_users();	/* Inform them */
	      can_shut_down();	/* Maybe all lines are
				   already inactive? */
	      break;
	  case CMD_SHOW_LINES:
	      /* LINE contains username only.
		 The routine we call expect full address, so
		 add our local node name */
	      strcat(&line[1], "@");
	      strcat(&line[1], LOCAL_NAME);
	      show_lines_status(&line[1],0);
	      break;
	  case CMD_SHOW_QUEUE:
	      /* LINE contains username only.
		 The routine we call expect full address, so
		 add our local node name */
	      strcat(&line[1], "@");
	      strcat(&line[1], LOCAL_NAME);
	      show_files_queue(&line[1],"");
	      break;
	  case CMD_QUEUE_FILE: 
	      /* Compute the file's size in bytes: */
	      i = ((line[1] << 8) + (line[2])) * 512;
	      queue_file(&line[3], i);
	      break;
	  case CMD_SEND_MESSAGE:	/* Get the parameters */
	  case CMD_SEND_COMMAND:
	      if (sscanf(&line[2], "%s %s", Faddress, Taddress) != 2) {
		logger(1, "Illegal SEND_MESSAGE line: '%s'\n", &line[2]);
		break;	/* Illegal line */
	      }
	      /* The sender includes only the username.
		 Add @LOCAL-NAME to it. */
	      i = strlen(Faddress);
	      if (Faddress[0] != '@')
		Faddress[i++] = '@';
	      strcpy(&Faddress[i], LOCAL_NAME);
	      if (*line == CMD_SEND_MESSAGE) {

		/* If there is no @ in the Taddress,
		   then append our local nodename, so it'll
		   be sent to a local user */

		if (strchr(Taddress, '@') == NULL) {
		  i = strlen(Taddress);
		  Taddress[i++] = '@';
		  strcpy(&Taddress[i], LOCAL_NAME);
		}
		send_nmr(Faddress, Taddress,
			 &line[line[1]], strlen(&line[line[1]]),
			 ASCII, CMD_MSG);
	      }
	      else
		send_nmr(Faddress, Taddress,
			 &line[line[1]], strlen(&line[line[1]]),
			 ASCII, CMD_CMD);
	      break;
	  case CMD_START_LINE:
	      if ((i >= 0) && (i < MAX_LINES) &&
		 IoLines[i].HostName[0] != 0)
		restart_line(line[1] & 0xff);
	      else
		logger(1, "IO: Operator START LINE #%d on nonconfigured line\n",
		       line[1]&0xff);
	      break;
	  case CMD_STOP_LINE:	/* Stop a line after the last file */
	      i = (line[1] & 0xff);
	      if ((i >= 0) && (i < MAX_LINES) &&
		  IoLines[i].HostName[0] != 0) {
		IoLines[i].flags |= F_SHUT_PENDING;
		logger(1, "IO: Operator requested SHUT for line #%d\n", i);
	      }
	      else
		logger(1, "IO: Illegal line numbr(%d) for STOP LINE\n", i);
	      break;
	  case CMD_FORCE_LINE:	/* Stop a line now */
	      i = (line[1] & 0xff);
	      if ((i >= 0) && (i < MAX_LINES) &&
		  IoLines[i].HostName[0] != 0) {
		logger(1, "IO: Line #%d forced by operator request\n", i);
		if (IoLines[i].state == ACTIVE)
		  send_data(i, &SignOff, sizeof(struct SIGN_OFF),
			    ADD_BCB_CRC);
		IoLines[i].state = SIGNOFF;
		/* Requeue all files back: */
		restart_channel(i);
	      }
	      else
		logger(1, "IO: Illegal line numbr(%d) for FORCE LINE\n", i);
	      break;
	  case CMD_DEBUG_DUMP:	/* Dump lines buffers */
	      debug_dump_buffers(&line[1]); /* Pass the username */
	      break;
	  case CMD_DEBUG_RESCAN:	/* Rescan queue */
	      debug_rescan_queue(&line[1],line[11]);
	      break;
	  case CMD_EXIT_RESCAN:
	      handle_sighup(0); /* Do it with the SIGHUP code.. */
	      break;
	  case CMD_ROUTE_RESCAN:
	      close_route_file();
	      open_route_file();
	      break;
	  case CMD_LOGLEVEL:	/* Set new loglevel */
	      i = LogLevel;
	      LogLevel = 1;	/* To close the file;
				   guaranteed log checkpoint */
	      logger(1, "IO: Log level changed from %d to %d\n",
		     i, line[1] & 0xff);
	      LogLevel = line[1] & 0xff;
	      break;
	  case CMD_CHANGE_ROUTE:	/* Change the route in our database */
	      sscanf(&line[1], "%s %s", Faddress, Taddress);
	      logger(1, "IO: Route %s changed to %s by operator\n",
		     Faddress, Taddress);
	      change_route(Faddress, Taddress);
	      break;
	  case CMD_GONE_ADD_UCP:
	  case CMD_GONE_ADD:
	      add_gone_user(&line[1]);
	      break;
	  case CMD_GONE_DEL_UCP:
	  case CMD_GONE_DEL:
	      del_gone_user(&line[1]);
	      break;
	  case CMD_MSG_REGISTER:
	      msg_register(line+1,length-1);
	      break;
	  default:
	      logger(1, "IO: Illegal command = %d\n", *line);
	      break;
	}
}
