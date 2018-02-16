/* PROTOCOL.C   V4.5-mea1.1
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
 | thing, a log at level 1 is produced. Change this log level to be 2 after
 | this module is debugged.
 |
 | The function that deblocks incoming records assumes for some types that they
 | are a single-record block.
 |
 | MORE things to do:
 | 3. Statistics computation: Count Null blocks as ACKS also.
 | 5. Handle prepare mode on idle line.

 | mea1.1 - 10-Oct-93 - Removed all conditional compilation of HUJI vs. FUNET
 |			version.  It is now FUNET version only..
 |
 | Document:
 |   Network Job Entry - Formats and Protocols (IBM)
 |	SC23-0070-01
 |	GG22-9373-02 (older version)

 */

#include "consts.h"
#include "headers.h"
#include "prototypes.h"

static void  handle_text_block __(( const int Index, void *buffer, int size ));
static void  input_read_error __(( const int Index, const int status, struct LINE *temp ));
static void  handle_nak __(( const int Index ));
static void  handle_enq __(( const int Index ));
static void  fill_message_buffer __(( const int Index, struct LINE *temp ));
static int   request_to_send_file __(( const int Index, struct LINE *temp ));
static void  income_file_request __(( const int Index, struct LINE *temp, const unsigned char *StreamNumberP ));
static void  send_signon __(( const int Index, const int flag ));
static void  inform_users_about_line __(( const int Index, const int PreviousState ));


EXTERNAL struct NEGATIVE_ACK	NegativeAck;
EXTERNAL struct POSITIVE_ACK	PositiveAck;
EXTERNAL struct ENQUIRE		Enquire;
EXTERNAL struct SIGNON		InitialSignon, ResponseSignon;
EXTERNAL struct SIGN_OFF	SignOff;
EXTERNAL struct EOF_BLOCK	EOFblock;
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

/* A macro to send either DEL+ACK0 or an empty block as ACK */
unsigned char	NullBuffer = 0;

/* Send an ACK or Null-buffer, depending on the line's state. If the line is
   of TCP type - don't send ACks of any type, since this is a reliable link,
   and no ACKS are expected. DECnet expects ACKs, since it has problems working
   full duplex.
*/
#define	SEND_ACK() { \
	if (((temp->flags & F_RELIABLE) == 0) ||	/* These have to get ACK always */ \
	    (temp->state != ACTIVE)) { \
	  if (((temp->flags & F_ACK_NULLS) == 0) ||	/* Use normal ACK */ \
	      (temp->state != ACTIVE)) \
	    send_data(Index, &PositiveAck, sizeof(struct POSITIVE_ACK), \
		      SEND_AS_IS); \
	  else \
	    send_data(Index, &NullBuffer, 1, ADD_BCB_CRC); \
	} \
}

/*
 | Try recovery from error. If there are only few errors on the line, try
 | sending NAK. If there are too much errors - restart the line.
 | Two error counters are kept: TotalError which counts all errors on the
 | line since program startup, and Errors which counts the number of errors
 | since the last seccessfull operation on the line.
 */
static void
error_retry(Index, temp)
const int	Index;
struct LINE	*temp;
{
	temp->TotalErrors++;	/* Count total errors on line */
	temp->stats.RetriesOut += 1;
	if ((temp->errors++) < MAX_ERRORS) {	/* Not too much errors */
	  send_data(Index, &NegativeAck, sizeof(struct NEGATIVE_ACK),
		    SEND_AS_IS);
	  return;
	} 
	else {	/* Too many errors. Restart the line */
	  logger(1, "PROTOCOL: Too many error on line %d, restarting.\n",
		 Index);
	  restart_channel(Index);
	  return;
	}
}


/*
 | Restart a channel - I.e.: close files, delete output files.
 | If there are interactive messages/commands waiting on the line,
 | clear them, and free their memory.
 | If there is a file in transit, change its state to waiting.
 */
void
restart_channel(Index)
const int	Index;
{
	struct	LINE	*temp;
/*	struct	MESSAGE	*MessageEntry; */
	register long	PreviousState;	/* To know whether to inform
					   state change */
/*	register int	i;	 Stream index */

	temp = &(IoLines[Index]);
	if (Index > MAX_LINES || temp->HostName[0] == 0) {
	  logger(1,"PROTOCOL: restart_channel(%d) - Bad line number!\n",Index);
	  return;
	}

	init_link_state(Index);

	/* Send the ENQ block again, only if the line was before in
	   some active state.    If it wasn't, then this call is
	   because we just want to close the files.  If the line is
	   of type TCP, we close it in any case (put it in INACTIVE
	   state).  We do not do it only during the initial signon
	   since NAKs are exchanged there.				*/

	PreviousState = temp->state;
	if ((temp->flags & F_RELIABLE) && (temp->state != DRAIN))
	  temp->state = TCP_SYNC;   /* This will cause it to call close_line */

	switch (temp->state) {
	  case DRAIN:
	  case I_SIGNON_SENT:
	  case F_SIGNON_SENT:
	  case ACTIVE:		/* Restart it and put in DRAIN mode */
	      temp->state = DRAIN;
	      send_data(Index, &Enquire, sizeof(struct ENQUIRE), SEND_AS_IS);
	      /* Inform registered users about line being disabled */
	      if (InformUsersCount > 0) {
		if (temp->state != PreviousState) {
		  inform_users_about_line(Index, PreviousState);
		}
	      }
	      break;
	  case TCP_SYNC:		/* Line inactive */
	  case SIGNOFF:
	      temp->state = INACTIVE;
	      close_line(Index);	/* Line is disabled, so close it */
	  case INACTIVE:		/* On all the other types,
					   the channel is closed */
	  case LISTEN:
	  case RETRYING:
	      /* Inform registered users about line being disabled.
		 If we were called in this state, then it is sure
		 that the previous state was different. */
	      temp->state = RETRYING;
	      temp->RetryPeriod = temp->RetryPeriods[temp->RetryIndex];
	      if (InformUsersCount > 0) {
		inform_users_about_line(Index, PreviousState);
	      }
	      break;
	  default:
	      logger(1, "PROTOCOL, Illegal line state=%d in Restart_chan\n",
		     temp->state);
	      break;
	}

	/* Check whether we are in shutdown process.
	   If so, and the line has changed state to INACTIVE,
	   change its state to SIGNEDOFF */

	if (MustShutDown < 0)
	  if ((temp->state == INACTIVE) ||
	      (temp->state == RETRYING) ||
	      (temp->state == LISTEN))
	    temp->state = SIGNOFF;

}


/*
 | Some input has been arrived from some line.
 */
void
input_arrived(Index, status, buffer, size)
const int	Index,		/* Index in IoLines */
		status;		/* VMS I/O status of read */
      int	size;
void		*buffer;
{
	struct	LINE	*temp;
	const unsigned char	*q;
	unsigned char		*p;

	temp = &(IoLines[Index]);

	logger(3, "PROTOCOL: Input from line %s, status=%d, size=%d\n",
	       temp->HostName, status, size);

	/* First check the status. If error, then continue accordingly */
	if ((status & 0x1) == 0) {
	  input_read_error(Index, status, temp);
	  return;
	}

	/* No error, something was received - handle it */
	p = buffer;

	/* VMnet tends to preceed some buffers with SYN... remove it */
	while ((*p == SYN) && (size > 0)) {
	  ++p; --size;
	}

#ifdef DEBUG
	logger(4, "PROTOCOL: line=%s, Data received:\n", temp->HostName);
	trace(p, size, 5);
#endif

	/* Now, check the code. Check first for the 3 known blocks
	   which has a special structure (SOH-ENQ, NAK and ACK) */

	q = p + 1;	/* q points to the next character */
	temp->stats.TotalIn++;
	if ((*p == DLE) && (*q == STX)) {
	  handle_text_block(Index, p, size);
	  return;
	}
	if (*p == NAK) {
	  temp->stats.RetriesIn += 1;
	  handle_nak(Index);
	  return;
	}
	if ((*p == SOH) && (*q == ENQ)) {
	  handle_enq(Index);
	  return;
	}
	if ((*p == DLE) && (*q == ACK0)) {
	  temp->stats.AckIn += 1;
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
 | ACTIVE state and this is not a repetitive error.
 | Called from Input_arrived() when the input routine returns error.
 */
static void
input_read_error(Index, status, temp)
const int	Index, status;
struct LINE	*temp;
{
	logger(1, "PROTOCOL: Read error, on line %s, status=%d\n", 
	       temp->HostName, status);
	temp->TotalErrors++;	/* Increment total errors */

	/* We handle here only timeouts which has one of the two codes: */

	if ((status != SS_ABORT) && (status != SS_TIMEOUT))
	  return;		/* Not it, we can't handle it */
	switch (temp->state) {
	  case ACTIVE:		/* There is some activity. Try recovery */
	      error_retry(Index, temp);
	      break;
	  case DRAIN:		/* try to start line again */
	      send_data(Index, &Enquire, sizeof(struct ENQUIRE), SEND_AS_IS);
	      break;		/* Continue to send ENQ */
	  case I_SIGNON_SENT:
	  case F_SIGNON_SENT:
	      restart_channel(Index);
	      break;		/* Restart all files */
	  default:
	      break;		/* Line is not active */
	}
}

/*
 | Handle received NAK. Check the error count, if it is still small,
 | retransmit the last sent buffer.
 */
static void
handle_nak(Index)
const int	Index;
{
	struct	LINE	*temp;

	temp = &(IoLines[Index]);
	temp->TotalErrors++;	/* Count in total errors for that line */

	/* Check in which state are we */
	switch (temp->state) {
	  case DRAIN:
	  case I_SIGNON_SENT:
	  case F_SIGNON_SENT:
	      logger(1,"PROTOCOL: NAK received on DRAIN/SIGNON time on line %s\n",temp->HostName);
	      restart_channel(Index);
	      break;
	  case ACTIVE:
	      /* Try re-sending last buffer */
	      temp->TotalErrors++;	/* Count total errors on line */
	      if ((temp->errors++) < MAX_ERRORS &&
		  temp->XmitSize != 0) {
		logger(2, "PROTOCOL: NAK received on line %s. Re-sending last buffer\n",temp->HostName);
		/* Not too many errors, re-send last buffer */
		send_data(Index, temp->XmitBuffer, temp->XmitSize, SEND_AS_IS);

	      } else {	/* Too many errors. Restart the line */
		logger(1, "PROTOCOL: Too many NAKs on line %s, restarting.\n",
		       temp->HostName);
		restart_channel(Index);
	      }
	      break;
	  default:	/* Ignore in other states */
	      break;
	}
	/* Other states - simply ignore it */
	return;
}

/*
 | Handle the SOH-ENQ block. If we are in the starting position, ack it. In
 | any other place, restart the channel, since the other party is also
 | starting.
 */
static void
handle_enq(Index)
const int	Index;
{
	struct	LINE	*temp;

	temp = &(IoLines[Index]);
	temp->errors = 0;		/* Clear error count. */
	switch (temp->state) {
	  case DRAIN:	/* Send the ACK block to start Sign-on */
	      send_data(Index,
			&PositiveAck, sizeof(struct POSITIVE_ACK),
			SEND_AS_IS);
	      break;
	  default:
	      logger(1,"PROTOCOL: Got SOH-ENQ while not in DRAIN state on line %s\n",temp->HostName);
	      restart_channel(Index);	/* Reset the line */
	      break;
	}
}

/*
 | Handle an ACK block. If we are in starting position, its time to send
 | the initial signon record. If not, Then it acks something.
 | The flag tells us whether this was an ACK block (EXPLICIT_ACK), or whether
 | we got a text block which is an implicit ack (IMPLICIT_ACK).
 */
void
handle_ack(Index, flag)
const int	Index;
const short	flag;	/* Is this an implicit or explicit ACK? */
{
	register struct	LINE	*temp;
	register int	i, CurStream, Switches = 7;

	temp = &(IoLines[Index]);
	temp->errors = 0;		/* Clear error count. */

	/* Should we wait for ACK at all??? */

	switch (temp->state) {
	  case SIGNOFF:
	      return; /* Keep old state, it is SIGNOFF! */
	  case INACTIVE:
	  case I_SIGNON_SENT:	/* This is an illegal ACK */
	      logger(1, "PROTOCOL: Illegal ACK. state=%d, line=%s\n",
		     temp->state, temp->HostName);
	      restart_channel(Index);
	      return;
	  case DRAIN:	/* Is is the ACK for the Enquire - initiate signon */
	      /* Reset BCB was sent in the first packet */
	      temp->OutBCB = 0; temp->flags &= ~F_RESET_BCB;
	      for (i = 0; i < temp->MaxStreams; i++)
		temp->InStreamState[i] =
		  temp->OutStreamState[i] = S_INACTIVE;
	      /* Line is starting - streams are all idle */
	      
	      send_signon(Index, INITIAL_SIGNON);
	      temp->state = I_SIGNON_SENT;
	      return;
	  case F_SIGNON_SENT:
	      temp->state = ACTIVE;	/* Line has finished signon */
/* XX: Lets (re)queueing with link-activation time  'debug_rescan_queue()'
       routine..  (Gerald Hanush) */
	      debug_rescan_queue("<signon acked>",'+');
	      break;
	}

	/* Check whether we are in the Wait-a-bit mode.
	   If so - only send Acks. Send Ack immediately
	   if something was received from the other side.
	   If not, send it after a delay.
	   WAIT_A_BIT is from the NJE block header.
	   WAIT_V_A_BIT is from VMnet.			*/

#ifdef	NBSTREAM	/* Do nothing while write is pending.. */
	if (temp->WritePending) {
	  queue_timer(1,Index,T_SEND_ACK);
	  return;
	}
#endif

	if ((temp->flags & F_WAIT_A_BIT) ||
	    (temp->flags & F_WAIT_V_A_BIT)) {
	  if (flag != EXPLICIT_ACK) {
	    logger(4, "PROTOCOL: Sending ACK, line=%d\n",Index);
	    SEND_ACK();
	    return;
	  }
	  /* Nothing was received - delay the ACK */
	  /* Queue it only if we'll have to send it */
	  if (((temp->flags & F_RELIABLE) == 0) ||
	      (temp->state != ACTIVE))
	    queue_timer(1, Index, T_SEND_ACK);
	  return;
	}

	/* It is ACK for something - test for what.
	   Test only outgoing streams, since explicit
	   ACK can't come when we receive a stream...
	   Before we check it, check whether there is
	   an interactive message waiting.
	   If so, send it and don't handle this ACK
	   farther as it is impossible to mix NMRs
	   and other records in the same block.
	   If we have interactive messages waiting,
	   then try to block as much as we can in one block. */

	if (temp->MessageQstart != NULL) {
	  /* logger(2,"PROTOCOL: **DEBUG** About to call fill_message_buffer(%d,..): Line %s, MessageEntry = 0x%x\n",
	     Index,temp->HostName,temp->MessageQstart);  */
	  fill_message_buffer(Index, temp);
	  return;
	}

	/* Check whether we have a file waiting for sending,
	   and free streams to initiate it */

	if ((temp->QueuedFilesWaiting > 0) && (temp->FreeStreams > 0) &&
	    ((temp->flags & F_SHUT_PENDING) == 0))
	  if (request_to_send_file(Index, temp) != 0)
	    return;	/* If 0 - we have to send an ACK */

	/* Check whether there is another active stream.
	   If so, switch to it. */

	Switches = temp->MaxStreams;

    SwitchStream:

	if (temp->MaxStreams > 1) {
	  /* No need to do it if only one stream */
	  for (i = (temp->CurrentStream + 1) % temp->MaxStreams;
	      /* Don't jump over maximum streams defined for this line */
	      i != temp->CurrentStream;
	      i = (i + 1) % temp->MaxStreams)
	    if ((temp->ActiveStreams & (1 << i)) != 0)
	      break;	/* Found an active stream */
	  temp->CurrentStream = i;
	}

	/* No interactive message - handle the ack */

	CurStream = temp->CurrentStream;
	switch (temp->OutStreamState[CurStream]) {

	  /* If there is a file to send, open it and send
	     a request to initiate a stream.			*/

	  case S_REQUEST_SENT:	/* It is ack for the request. ACK it back */
	      SEND_ACK();
	      return;

	  case S_INACTIVE:
	      /* Send another ACK, but after 1 second delay. */
	      /* Check whether the line has to signoff */
	      if ((temp->flags & F_SHUT_PENDING) != 0) {
		for (i = temp->MaxStreams; i >= 0; --i)
		  if ((temp->InStreamState[i] != S_INACTIVE) &&
		      (temp->InStreamState[i] != S_REFUSED))
		    break;
		if (i < 0) {
		  /* Can shut */
		  send_data(Index, &SignOff,
			    sizeof(struct SIGN_OFF),
			    ADD_BCB_CRC);
		  temp->state = SIGNOFF;
		  logger(1, "PROTOCOL, Line  %s signed off due to operator request\n",
			 temp->HostName);
		  inform_users_about_line(Index, -1);
		  close_line(Index);
		  temp->state = SIGNOFF;
		  /* close_line() changes the state
		     to RETRY, and we don't want it */
		  can_shut_down();
		  /* Check whether the daemon need shut */
		  return;
		}
		/* Not all streams idle - Ack and don't open a new file */
		temp->stats.AckOut++;
		SEND_ACK();
		return;
	      }
	      if (flag != EXPLICIT_ACK) {
		/* Ack it right away, since it is either an implicit ACK
		   (which came with data) so we must ack it immediately,
		   or because this is a delayed ACK, which we should now
		   send (the line is idle, so the ack was not sent
		   immediately the previous time).			*/
		logger(4,"PROTOCOL: Sending ACK - after INACTIVE, line=%s:%d\n",
		       temp->HostName,temp->CurrentStream);
		temp->stats.AckOut++;
		SEND_ACK();
		return;
	      }
	      /* Queue it only if we'll have to send it
		 on an idle BiSync line */
	      if (((temp->flags & F_RELIABLE) == 0) ||
		  (temp->state != ACTIVE))
		queue_timer(1, Index, T_SEND_ACK);
	      return;

	  case S_REFUSED:
	      /* If refused - just chitchat. */
	      if ((temp->flags & F_SHUT_PENDING) != 0) {
		/* Can shut ? */
		for (i = temp->MaxStreams; i >= 0; --i)
		  if ((temp->InStreamState[i] != S_INACTIVE) &&
		      (temp->InStreamState[i] != S_REFUSED))
		    break;
		if (i < 0) {
		  /* Can shut ! */
		  send_data(Index, &SignOff, sizeof(SignOff), ADD_BCB_CRC);
		  temp->state = SIGNOFF;
		  logger(1, "PROTOCOL, Line  %s signed off due to operator request\n",
			 temp->HostName);
		  inform_users_about_line(Index, -1);
		  close_line(Index);
		  temp->state = SIGNOFF;
		  /* close_line() changes the state
		     to RETRY, and we don't want it */
		  can_shut_down();
		  /* Check whether the daemon need shut */
		  return;
		}
		/* Stream not idle - Ack and don't open a new file */
		temp->stats.AckOut++;
		SEND_ACK();
		if (--Switches < 0) return;
		goto SwitchStream;
	      }
	      if (flag != EXPLICIT_ACK) {
		/* Ack it right away, since it is either an implicit ACK
		   (which came with data) so we must ack it immediately,
		   or because this is a delayed ACK, which we should now
		   send (the line is idle, so the ack was not sent
		   immediately the previous time).			*/
		logger(4, "PROTOCOL: Sending ACK - after REFUSED, line=%s:d\n",
		       temp->HostName,temp->CurrentStream);
		temp->stats.AckOut++;
		SEND_ACK();
		return;
	      }
	      /* Can it be a link that is shutting down ?
		 If there is some activity aside of S_REFUSED,
		 go in there.. (try anyway) */
	      for (i = temp->MaxStreams; i >= 0; --i)
		if ((temp->OutStreamState[i] != S_INACTIVE) &&
		    (temp->OutStreamState[i] != S_REFUSED))
		  break;
	      if (i >= 0) goto SwitchStream;

	      /* Queue it only if we'll have to send it
		 on an idle BiSync line */
	      if (((temp->flags & F_RELIABLE) == 0) ||
		  (temp->state != ACTIVE))
		queue_timer(1, Index, T_SEND_ACK);
	      return;

	  case S_NJH_SENT:
	      logger(3, "PROTOCOL: Sending Dataset header\n");
	      temp->flags |= F_XMIT_CAN_WAIT;	/* We support TCP lines here */
	      /* Returns 1 if another fragment to send.. */
	      if (send_njh_dsh_record(Index, SEND_DSH,
				      temp->OutFileParams[CurStream].type & F_JOB))
		temp->OutStreamState[CurStream] = S_NDH_SENT;
	      else
		temp->OutStreamState[CurStream] = S_SENDING_FILE;
	      /* If it is VMNET protocol, and more room in transmit
		 buffers, don't return; fall to next transmit block */
	      temp->flags &= ~F_XMIT_CAN_WAIT;
	      if ((temp->flags & F_XMIT_MORE) == 0)
		return;

	  case S_NDH_SENT:
	      /* We fall thru from above.. */
	      if (temp->OutStreamState[CurStream] == S_NDH_SENT) {
		logger(3, "PROTOCOL: Sending Dataset header, fragment 2\n");
		temp->flags |= F_XMIT_CAN_WAIT;	/* We support TCP lines here */
		send_njh_dsh_record(Index, SEND_DSH2,
				    temp->OutFileParams[CurStream].type & F_JOB);
		temp->OutStreamState[CurStream] = S_SENDING_FILE;
		/* If it is VMNET protocol, and more room in transmit
		   buffers, don't return; fall to next transmit block */
		temp->flags &= ~F_XMIT_CAN_WAIT;
		if ((temp->flags & F_XMIT_MORE) == 0)
		  return;
	      }

	  case S_SENDING_FILE:	/* We are in the middle of the file */
	  SendAgain:
	      /* logger(3, "PROTOCOL: Sending next file's buffer\n"); */
	      temp->flags |= F_XMIT_CAN_WAIT;
	      send_file_buffer(Index);	/* pick next record */
	      /* If it is VMNET protocol, and more room in transmit
		 buffers, don't return; fall to next transmit block */
	      temp->flags &= ~F_XMIT_CAN_WAIT;
	      if ((temp->flags & F_XMIT_MORE) == 0)
		return;

#if 0
	      /* If a SLOW-INTERLEAVE is asked for the link, do the
	         dance locally in here, else return and let the upper
		 stages do it.  SLOW-INTERLEAVE might be a bit more
		 efficient at feeding thru large files on the links.. */

	      if (temp->OutStreamState[CurStream] == S_SENDING_FILE)
		if ((temp->flags & F_SLOW_INTERLEAVE) != 0)
		  goto SendAgain;
		else
		  goto SwitchStream;
#else
	      if (temp->OutStreamState[CurStream] == S_SENDING_FILE)
		goto SendAgain;
#endif

	      /* else - fall to send-njt... */
	  case S_EOF_FOUND:	/* Send the NJT block */
	      /* If we send EBCDIC files and TCP line, we fall here
		 by mistake. In this case, do not send NJT, since it
		 was already sent as part of stored file.		*/
	      if (temp->OutStreamState[CurStream] != S_NJT_SENT) {
		logger(3, "PROTOCOL: Sending NJT on line %s:%d\n",
		       temp->HostName,CurStream);
		temp->OutStreamState[CurStream] = S_NJT_SENT;
		temp->flags |= F_XMIT_CAN_WAIT;
		send_njt(Index,
			 temp->OutFileParams[CurStream].type & F_JOB);
		temp->flags &= ~F_XMIT_CAN_WAIT;
		/* If it is VMNET protocol, and more room in transmit
		   buffers, don't return; fall to next transmit block */
		if ((temp->flags & F_XMIT_MORE) == 0)
		  return;
	      }
	  case S_NJT_SENT:
	      /* The NJT was sent and ACKED. Send EOF now. */
	      logger(3,"PROTOCOL: line=%s:%d: NJT sent+ACKed, sending EOF.\n",
		     temp->HostName,CurStream);
	      if (temp->OutFileParams[CurStream].type & F_JOB)
		EOFblock.RCB = (((CurStream + 9) << 4) | 0x8);
	      else
		EOFblock.RCB = (((CurStream + 9) << 4) | 0x9);
	      temp->OutStreamState[CurStream] = S_EOF_SENT;
	      /* Since we do not set the flag F_XMIT_CAN_WAIT,
		 this will force the TCP layer to send the data,
		 even if the buffer is not full. */
	      temp->flags &= ~F_XMIT_CAN_WAIT;
	      send_data(Index, &EOFblock,
			sizeof(struct EOF_BLOCK),
			ADD_BCB_CRC);
	      return;
	  case S_EOF_SENT:	/* We are waiting now for the final
				   completion block */
	      logger(3, "PROTOCOL: EOF sent and confirmed by ACK. ACKED back. Line=%s:%d\n",
		     temp->HostName, CurStream);
	      dequeue_file_entry_ok(Index,temp);
	      delete_file(Index, F_INPUT_FILE, temp->CurrentStream);
	      temp->OutStreamState[CurStream] = S_INACTIVE;
	      temp->FreeStreams += 1;
	      /* Clear its bit */
	      temp->ActiveStreams &= ~(1 << temp->CurrentStream);
	      rscsacct_log(&temp->OutFileParams[CurStream],1);
	      SEND_ACK();
	      return;
	  default:
	      logger(1, "PROTOCOL: Line %s:%d, ACK received when line operation=%d. Illegal.\n",
		     temp->HostName, CurStream,
		     temp->OutStreamState[CurStream]);
	      restart_channel(Index);
	      return;
	}
}


/*
 | Called from Handle_ack() when there are NMRs to send to other side. Try
 | blocking as much messages as you can and send them.
 */
static void
fill_message_buffer(Index, temp)
const int	Index;
struct LINE	*temp;
{
	struct	MESSAGE	*MessageEntry;	/* Messages' queue for the line */
	unsigned char	buffer[MAX_BUF_SIZE];
	register long	size, position, MaxSize;


	temp->stats.MessagesOut += 1;	  /* [mea] Count messages always */

	MaxSize = temp->MaxXmitSize - 20;	/* The space we have after
						   counting the overheads */
	position = 0;
	while (temp->MessageQstart != NULL) {
	  size = temp->MessageQstart->length;
	  if ((position + size) > MaxSize) break; /* No room for more */
	  memcpy(&buffer[position],
		 (unsigned char *)(temp->MessageQstart->text),
		 size);
	  position += size;
	  /* Dequeue this entry */
	  MessageEntry = temp->MessageQstart;
	  /* logger(2,"PROTOCOL: **DEBUG** DEQUEUE THIS MSG: Line %s, MessageEntry = 0x%x, ->next = 0x%x, size=%d\n",
	     temp->HostName,MessageEntry,MessageEntry->next,
	     MessageEntry->length);  */
	  if (MessageEntry->next == NULL) { /* End of list */
	    temp->MessageQstart = NULL;
	    temp->MessageQend   = NULL;
	  } else
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
static int
request_to_send_file(Index, temp)
const int	Index;
struct LINE	*temp;
{
	struct	QUEUE	*FileEntry;
	unsigned char	buffer[4];	/* To send the request block */
	int	i;
	struct FILE_PARAMS *FP;

	/* Dequeue the first file entry and
	   init the various variables of the file */

	FileEntry = pick_file_entry(Index,temp);

	if (FileEntry == NULL) return 0; /* Uhh.. Some error.. */

	/* Find which stream is free */

	for (i = 0; i < temp->MaxStreams; i++)
	  if ((temp->ActiveStreams & (1 << i)) == 0)
	    break;	/* Found an inactive stream */
	if (i >= temp->MaxStreams)
	  bug_check("Aborting because attempted to  request_to_send_file() on link w/o free streams! (ActiveStreams flag corruption?)");

	temp->CurrentStream = i;
	FP = &temp->OutFileParams[temp->CurrentStream];

	/* Open the file */

	FileEntry->state = 1; /* Mark it for sending */
	FP->FileEntry = FileEntry;
	strcpy(FP->SpoolFileName,FileEntry->FileName);	/* Save spool file name */
	if (open_xmit_file(Index, temp->CurrentStream,
			   FP->SpoolFileName) == 0) {
	  temp->OutStreamState[temp->CurrentStream] = S_INACTIVE;
	  /* Some error in file, fall to send ACK */
	  FileEntry->state = -2; /* Place it into HELD status */
	  return 0;
	} else {

	  logger(2,"PROTOCOL: queued file `%s' fn.ft: `%s %s' for sending on stream %d\n",
		 FileEntry->FileName,
		 FP->FileName,FP->FileExt,
		 temp->CurrentStream);

	  /* Create a request block and send it */
	  buffer[0] = REQUEST_RCB; /* RCB */
	  if (FP->type & F_JOB)
	    buffer[1] = (((temp->CurrentStream + 9) << 4) + 0x8);
	  else
	    buffer[1] = (((temp->CurrentStream + 9) << 4) + 0x9);
	  buffer[2] = NULL_RCB;	/* Null string */
	  buffer[3] = NULL_RCB;	/* End of block */
	  temp->OutStreamState[temp->CurrentStream] = S_REQUEST_SENT;

	  /* Made one stream active */
	  temp->FreeStreams -= 1;

	  /* Mark the specific stream */
	  temp->ActiveStreams |= (1 << temp->CurrentStream);

	  /* When we started */
	  time(&FP->XmitStartTime);

	  /* Decrease the counter */
	  temp->QueuedFilesWaiting -= 1;
	  
	  send_data(Index, buffer, 4, ADD_BCB_CRC);

	  logger(3, "Sent request for transmission.\n");
	  return 1;		/* No need to send ACK */
	}
}


/*
 | A text block was received. Look what text block it is, and process
 | accordingly.
 */
static void
handle_text_block(Index, Buffer, size)
const int	Index;
int		size;
void	*Buffer;
{
	int	SendAck;	/* Shall we treat this message as an
					   implicit ack? */
	struct	LINE	*temp;
	unsigned char	BCB, RCB, *pointer, line[MAX_BUF_SIZE];
	register int	i;
	int		SizeConsumed;	/* Size consumed from input buffer
					   by uncompress routine */
	time_t	dt;
	unsigned char *buffer = Buffer;

	temp = &IoLines[Index];

	/* Check the received CRC.
	   The procedure that does it also discards double DLE's.
	   However, don't call this procedure if the line is of
	   TCP type, since these lines don't send CRC characters. */

	if ((temp->flags & F_RELIABLE) == 0) {
	  if ((temp->type == DMB) || (temp->type == DSV)) {
	    /* Just remove double DLE's */
	    remove_dles(buffer, &size);
	  }
	  else {
	    if (check_crc(buffer, &size) == 0) {
	      /* CRC error */
	      error_retry(Index, temp);
	      return;
	    }
	  }
	}

	temp->errors = 0;		/* Clear error count. */

	/* Check the BCB now. If incorrect - Cry... */

	BCB = buffer[BCB_OFFSET];
	switch(BCB & 0xf0) {
	  case 0x80:	/* Normal block - check sequence */
	      if (temp->InBCB == (BCB & 0x0f)) { /* OK */
		logger(4, "Received BCB is ok.\n");
		temp->InBCB = (temp->InBCB + 1) % 16; /* Increment it */
		break;
	      } else {
		/* Check whether this is the preceeding BCB.
		   If so - discard the message */
		if (temp->InBCB == (((BCB & 0x0f) + 1) % 16)) {
		  logger(2, "PROTOCOL: Line %s, Duplicate block discarded.\n",
			 temp->HostName);
		  handle_ack(Index, IMPLICIT_ACK);
		  return;
		}
		/* BCB sequence error - probably we missed a block.
		   Restart the line */
		logger(1, "PROTOCOL: Line %s, Incorrect BCB received(%d), expect(%d)\n",
		       temp->HostName, BCB & 0xf, temp->InBCB);
		trace(buffer,BCB_OFFSET+4,1);
		restart_channel(Index);
		return;
	      }
	  case 0x90:	/* Bypass BCB count - ignore it and do not increment */
	      break;
	  case 0xa0:	/* Reset BCB */
	      logger (2, "PROTOCOL: Line %s, Income BCB reset to %d\n",
		      temp->HostName, BCB & 0xf);
	      temp->InBCB = (BCB & 0xf);
	      break;
	  default:
	      logger(1, "PROTOCOL: Line %s, Illegal BCB (%x). Reseting line. Trace:\n",
		     temp->HostName, BCB);
	      trace(buffer, size, 1);
	      restart_channel(Index);
	      return;
	}

	/* Check which type of block it is.
	   Currently ignore the FCS bits.
	   First check the ones that occupy
	   a whole block and are not compressed. */

	if (buffer[RCB_OFFSET] == SIGNON_RCB) {
	  /* Signon Control record */
	  income_signon(Index, temp, buffer);
	  return;
	}

	/* Test whether the Wait-a-bit (suspend all streams) is on.
	   If so - mark it, so the  routine that sends the reply will
	   handle it properly.						*/

	if ((buffer[FCS1_OFFSET] & 0x40) != 0) {
	  logger(3, "PROTOCOL, Wait a bit received on line %s\n",
		 temp->HostName);
	  temp->stats.WaitIn += 1;
	  temp->flags |= F_WAIT_A_BIT;
	}
	else	/* No wait, clear the bit */
	  temp->flags &= ~F_WAIT_A_BIT;

	/* Loop over the block, in case there are multiple records in it
	   (checked only for certain record types).
	   Currently there is no check whether all records belongs to
	   the same stream.
	   */
	if (buffer[RCB_OFFSET] == NMR_RCB) /* Test it here, because we
					      might have more than one
					      NMR in a block */
	  temp->stats.MessagesIn += 1;

	size -= RCB_OFFSET;	/* Update the size of message left to process*/
	pointer = &(buffer[RCB_OFFSET]);
	SendAck = 1;		/* Init: Send ack to other side */
	while (size > 0) {
	  RCB = *pointer++;	/* P holds the RCB, *pointer the SRCB */
	  switch (RCB) {
	    case NULL_RCB:	/* End of block */
		if (size > 5) {
		  logger(1, "PROTOCOL, line=%s, Null RCB received when size > 5\n",
			 temp->HostName);
		  trace(pointer-1, size + 1, 1);
		  SendAck = 1;
		}
		size = 0;	/* Closes the block */
		break;
	    case REQUEST_RCB:	/* Other side wants to send a file */
		SendAck = 0;	/* No need for explicit ACK as we
				   confirm it with a permission
				   block */
		size = 0;	/* Assume single record block */
		logger(3, "PROTOCOL: Request to initiate a stream on line %s\n",
		       temp->HostName);
		income_file_request(Index, temp, pointer);
		/* Pass the stream number */
		break;
	    case PERMIT_RCB:	/* Permission to us to initiate a stream */
		size = 0;	/* Assume single record block */
		/* The stream we are talking on now */
		temp->CurrentStream = ((*pointer & 0xf0) >> 4) - 9;
		if (temp->CurrentStream < 0 || temp->CurrentStream >= temp->MaxStreams) {
		  logger(1,"PROTOCOL: **BUG** Bad Stream number 0x%02X on PERMIT_RCB on line %s\n",*pointer,temp->HostName);
		  restart_channel(Index);
		  return;
		}
		/* File of own origination: */
		if (temp->OutFileParams[temp->CurrentStream].format != EBCDIC){
		  /* logger(2, "PROTOCOL: Sending NJH.\n"); */
		  temp->OutStreamState[temp->CurrentStream] = S_NJH_SENT;
		  send_njh_dsh_record(Index, SEND_NJH,
				      temp->OutFileParams[temp->CurrentStream].type & F_JOB);
		  SendAck = 0;	/* We ACKed it with the NJH */
		} else {
		  /* Store & Forward */
		  /* logger(2, "PROTOCOL: Sending EBCDIC file.  Line=%s, Stream=%d\n",temp->HostName,temp->CurrentStream); */
		  /* EBCDIC - NJH+NJT will be sent as part of file */
		  temp->OutStreamState[temp->CurrentStream] = S_SENDING_FILE;
		  SendAck = 1;	/* Will cause file sending */
		}
		if (temp->OutFileParams[temp->CurrentStream].RecordsCount > 0)
		  temp->OutFileParams[temp->CurrentStream].RecordsCount = 0;
		logger(2,"PROTOCOL: Startup delay on line %s:%d is %d secs.\n",
		       temp->HostName, temp->CurrentStream,
		       time(0) - (temp->OutFileParams[temp->CurrentStream].XmitStartTime));
		time(&temp->OutFileParams[temp->CurrentStream].XmitStartTime);
		break;
	    case CANCEL_RCB:	/* Receiver cancel */
		size = 0;	/* Assume single record block */
		SendAck = 0;
		/* The stream we are talking on now */
		temp->CurrentStream = ((*pointer & 0xf0) >> 4) - 9;
		if (temp->CurrentStream < 0 || temp->CurrentStream >= temp->MaxStreams) {
		  logger(1,"PROTOCOL: **BUG** Bad Stream number 0x%02X on CANCEL_RCB on line %s\n",*pointer,temp->HostName);
		  restart_channel(Index);
		  return;
		}
		logger(2, "PROTOCOL: Received negative permission or cancel on line %s:%d\n",
		       temp->HostName,temp->CurrentStream);

		/* Close the file */
		close_file(Index, F_INPUT_FILE, temp->CurrentStream);
		/* Hold the file as it might have caused the problem */
		/* DON'T DEQUEUE THIS FILE ENTRY!  Instead mark it HELD..  */
		temp->OutFileParams[temp->CurrentStream].FileEntry->state = -1;
		/* Mark the stream state as REFUSED, DON'T FREE IT! */
		temp->OutStreamState[temp->CurrentStream] = S_REFUSED;
		/* logger(1, "PROTOCOL, line=%s:%d, Received RECV-CANC for block:\n",
		   temp->HostName, temp->CurrentStream);
		   trace(temp->XmitBuffer, temp->XmitSize, 1); */
		break;
	    case COMPLETE_RCB:	/* Transmission complete */
		size = 0;	/* Assume single record block */
		/* The stream we are talking on now */
		temp->CurrentStream = ((*pointer & 0xf0) >> 4) - 9;
		if (temp->CurrentStream < 0 || temp->CurrentStream >= temp->MaxStreams) {
		  logger(1,"PROTOCOL: **BUG** Bad Stream number 0x%02X on COMPLETE_RCB on line %d (%s)\n",*pointer,Index,temp->HostName);
		  trace(pointer-4,8,1);
		  /*temp->state = INACTIVE;*/
		  restart_channel(Index);
		  return;
		}
		dt = time(0)-temp->OutFileParams[temp->CurrentStream].XmitStartTime;
		if (dt < 1) dt = 1;
		logger(2,"PROTOCOL: Transmitted a SYS%s file %s.%s (%s => %s) on line %s:%d,  %d B in %d secs, %d B/sec.\n",
		       temp->OutFileParams[temp->CurrentStream].type & F_JOB ? "IN" : "OUT",
		       temp->OutFileParams[temp->CurrentStream].FileName,
		       temp->OutFileParams[temp->CurrentStream].FileExt,
		       temp->OutFileParams[temp->CurrentStream].From,
		       temp->OutFileParams[temp->CurrentStream].To,
		       temp->HostName, temp->CurrentStream, 
		       temp->InFilePos[temp->CurrentStream], dt,

		       temp->InFilePos[temp->CurrentStream] / dt
		       /*temp->OutFileParams[temp->CurrentStream].FileSize / dt */
		       );
		SendAck = 1;	/* Ack it... */
		/* Rearrange the ACK handling somewhat.. */
	/*	temp->OutStreamState[temp->CurrentStream] = S_INACTIVE; */
#if 0
		temp->FreeStreams += 1;
		/* Clear its bit */
		temp->ActiveStreams &= ~(1 << temp->CurrentStream);
#endif
		break;
	    case READY_RCB:
		logger(1, "Ready to receive a stream. Ignored\n");
		SendAck = 1;
		size = 0;
		break;
	    case BCB_ERR_RCB:
		logger(1, "BCB sequence error\n");
		restart_channel(Index);
		size = 0;	/* Assume single record block */
		SendAck = 0;	/* No need to ack it */
		break;
	    case NMR_RCB: 	/* Nodal message received */
		logger(3, "Incoming NMR RCB\n");
		i = uncompress_scb(++pointer, line, size,
				   (sizeof line), &SizeConsumed);
		/* SizeConsumed - number of bytes consumed from
		   input buffer */
		handle_NMR((struct NMR_MESSAGE *)line, i);
		size -= (SizeConsumed + 2);	/* +2 for RCB_SRCB */
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
		/* Retrive the records one by one, and call   recv_file()
		   each time with one record. */
		/* Pointer now points to SRCB, Point it back to RCB */
		line[0] = *--pointer;	/* RCB */
		line[1] = *++pointer;	/* SRCB */
		i = uncompress_scb(++pointer, &line[2], size,
				   ((sizeof line) - 2), &SizeConsumed);
		if (i < 0) {	/* We have an error */
		  logger(1, "PROTOCOL, Disabling line %s because UNCOMPRESS error\n",
			 temp->HostName);
		  /*temp->state = INACTIVE;*/
		  restart_channel(Index);
		  return;
		}
		size -= (SizeConsumed + 2);	/* +2 for RCB_SRCB */
		pointer += SizeConsumed;
		SendAck = receive_file(Index, line, i + 2);
		/* LINE holds RCB + SRCB and the uncompressed line */
		break;

	    case SYSIN_0:
	    case SYSIN_1:
	    case SYSIN_2:
	    case SYSIN_3:
	    case SYSIN_4:
	    case SYSIN_5:
	    case SYSIN_6:

		/* Retrive the records one by one, and call Recv_file
		   each time with one record. */
		/* Pointer now points to SRCB, Point it back to RCB */
		line[0] = *--pointer;	/* RCB */
		line[1] = *++pointer;	/* SRCB */
		i = uncompress_scb(++pointer, line+2, size,
				   (sizeof line) - 2, &SizeConsumed);
		if (i < 0) { /* We have an error */
		  logger(1, "PROTOCOL, Disabling line %s because UNCOMPRESS error\n",
			 temp->HostName);
		  /*temp->state = INACTIVE;*/
		  restart_channel(Index);
		  return;
		}
		size -= (SizeConsumed + 2);	/* +2 for RCB_SRCB */
		pointer += SizeConsumed;
		
		/* LINE holds RCB + SRCB and the uncompressed line */
		SendAck = receive_file(Index, line, i + 2);
		break;

	    default:
		logger(1, "PROTOCOL, Unrecognized RCB: x^%x on line %s\n",
		       RCB, temp->HostName);
		trace(pointer-2, size + 2, 1);
		
		bug_check("Aborting because of illegal RCB received");

		break;
	  }
	}

	/* Text block also acks the last block we sent - so treat it also
	   as an ack.  However, some functions send some records as replies,
	   so in these case we don't have to send ACK back.		     */
	
	if (SendAck != 0)
		handle_ack(Index, IMPLICIT_ACK);
}


/*
 | We've received a Signon record (Either Initial, final, or Signoff).
 | These records are uncompressed, thus can be handled as they are.
 | This function is called from Handle_text_block().
 */
void
income_signon(Index, temp, buffer)
const int	Index;
struct LINE	*temp;
const unsigned char	*buffer;
{
	char		Aline[16];
	register int	i;
	register short	BufSize;
	struct SIGNON	SignOn;

	memcpy(&SignOn,&buffer[RCB_OFFSET],sizeof(SignOn));

	switch (SignOn.NCCSRCB) {
	  case E_I:	/* Initial signon */
	      if (temp->state != DRAIN) {
		logger(1, "PROTOCOL: Line %s, Initial signon received when in state %d\n",
		       temp->HostName, temp->state);
		temp->state = INACTIVE;	/* Disable the line */
		restart_channel(Index);	/* Requeue files */
		return;
	      }

	      /* Check that the correct node is at the other side */
	      EBCDIC_TO_ASCII(SignOn.NCCINODE, Aline, 8);
	      for (i = 7; i >= 0; i--) /* remove trailing blanks */
		if (Aline[i] != ' ') break;
	      Aline[++i] = '\0';
	      if (strcasecmp(Aline, temp->HostName) != 0) {
		logger(1, "Line #%d:`%s' tries to logon instead of authorized host '%s' for this line\n",
		       Index, Aline, temp->HostName);
		temp->state = INACTIVE;	/* Deactivate line */
		return;
	      }
	      /* Check the buffer size. If smaller than ours,
		 use the received one */
	      BufSize = ntohs(SignOn.NCCIBFSZ);
	      if (BufSize < temp->PMaxXmitSize)
		temp->MaxXmitSize = BufSize;
	      else	
		temp->MaxXmitSize = temp->PMaxXmitSize;
	      temp->OutBCB = 0;
	      temp->flags &= ~F_RESET_BCB;	/* Reset flag = Send reset BCB */
	      for (i = 0; i < temp->MaxStreams; i++)
		temp->InStreamState[i] = temp->OutStreamState[i] = S_INACTIVE;
	      /* Line starting - streams are all idle */
	      send_signon(Index, FINAL_SIGNON);
	      temp->state = F_SIGNON_SENT;
	      logger(1, "PROTOCOL: Line %s (%d), Signing on with bufsize %d\n",
		     Aline, Index, temp->MaxXmitSize);
	      inform_users_about_line(Index, -1);
	      return;
	  case E_J:	/* Response Signon */
	      if (temp->state != I_SIGNON_SENT) {
		logger(1, "PROTOCOL: Line %s, Response signon received when in state %d\n",
		       temp->HostName, temp->state);
		temp->state = INACTIVE;	/* Disable the line */
		restart_channel(Index);
		return;
	      }

	      /* Check that the correct node is at the other side */

	      EBCDIC_TO_ASCII(SignOn.NCCINODE, Aline, 8);
	      for (i = 7; i >= 0; i--)
		if (Aline[i] != ' ') break;
	      Aline[++i] = '\0';
	      if (strcasecmp(Aline, temp->HostName) != 0) {
		logger(1, "Line #%d, Host '%s' tries to\
 logon instead of authorized host '%s' for this line\n",
		       Index, Aline, temp->HostName);
		temp->state = INACTIVE;	/* Deactivate line */
		return;
	      }
	      /* If the received buffer size is smaller than ours,
		 decrease our one */
	      BufSize = ntohs(SignOn.NCCIBFSZ);
	      if (BufSize < temp->MaxXmitSize)
		temp->MaxXmitSize = BufSize;
	      logger(1, "PROTOCOL: Line %s (%d), Signed on with bufsize=%d\n",
		     Aline, Index, temp->MaxXmitSize);
	      inform_users_about_line(Index, -1);
	      /* Ack it */
	      temp->OutBCB = 0;
	      SEND_ACK();
	      temp->state = ACTIVE;
/* XX: Lets (re)queueing with link-activation time  'debug_rescan_queue()'
       routine..  (Gerald Hanush) */
	      debug_rescan_queue("<link signon>",'+');
	      return;
	  case E_B:	/* Signoff */
	      logger(1, "PROTOCOL: Line %s, Signoff received.\n",
		     temp->HostName);
	      temp->state = SIGNOFF;
	      inform_users_about_line(Index, -1);
	      temp->state = DRAIN;	/* Drain the channel for re-connection */
	      if ((temp->flags & F_RELIABLE) != 0)
		temp->state = INACTIVE;	/* Clear the connection */
	      restart_channel(Index);
	      return;
	default:
	      logger(1,
		     "PROTOCOL: Line %s, Illegal control record. SRCB=x^%x\n",
		     temp->HostName, SignOn.NCCSRCB);
		return;
	}
}


/*
 | A request to start a stream was received. Check that this is an eligible
 | check that this stream is inactive, try openning
 | an output file, and if ok, confirm it. In all other cases, reject it.
 | Called by Handle_text_block().
 */
static void
income_file_request(Index, temp, StreamNumberP)
const int	Index;
struct	LINE	*temp;
const unsigned char	*StreamNumberP;
{
	int	DecimalStreamNumber;	/* the stream number in the range 0-7*/

	/* Convert the RSCS's stream number to a decimal one */
	DecimalStreamNumber = ((*StreamNumberP & 0xf0) >> 4) - 9;
	if (DecimalStreamNumber < 0 ||
	    DecimalStreamNumber >= temp->MaxStreams) {
	  logger(1,"PROTOCOL: **BUG** Bad Stream number 0x%02X on REQUEST_RCB on line %d (%s)\n",*StreamNumberP,Index,temp->HostName);
	  trace(StreamNumberP-4,8,1);

	  /* Reject it.. */
	  RejectFile.SRCB = *StreamNumberP;
	  temp->flags &= ~F_XMIT_CAN_WAIT;
	  send_data(Index, &RejectFile,
		    (sizeof(struct REJECT_FILE)),
		    ADD_BCB_CRC);
	  return;
	}

	/* Check whether we have to shut this line.
	   If so, refuse the request */
	if ((temp->flags & F_SHUT_PENDING) != 0) {
	  RejectFile.SRCB = *StreamNumberP;	/* Stream # */
	  temp->flags &= ~F_XMIT_CAN_WAIT;
	  send_data(Index, &RejectFile,
		    (sizeof(struct REJECT_FILE)),
		    ADD_BCB_CRC);
	  return;
	}

	/* Check that this stream is inactive */
	if ((DecimalStreamNumber >= temp->MaxStreams) || /* Out of range */
	    ((temp->InStreamState)[DecimalStreamNumber] != S_INACTIVE)) {
	  /* Stream must be in in INACTIVE state to start a new connection. */
	  logger(2, "PROTOCOL: Rejecting request for line %s:%d\n",
		 temp->HostName, DecimalStreamNumber);
	  if (DecimalStreamNumber >= temp->MaxStreams)
	    logger(2, "PROTOCOL, Stream is out of range for this line\n");
	  else
	    logger(2, "PROTOCOL, Stream state is %d\n",
		   temp->InStreamState[DecimalStreamNumber]);
	  RejectFile.SRCB = *StreamNumberP; /* Stream # */
	  temp->flags &= ~F_XMIT_CAN_WAIT;
	  send_data(Index, &RejectFile,
		    (sizeof(struct REJECT_FILE)),
		    ADD_BCB_CRC);
	  return;
	}

	/* It's of a stream we can handle. Try opening the output file. */
	/* via recv_file.c  localized method ... */
	if (recv_file_open(Index, DecimalStreamNumber) == 0) {
	  /* Can't open file for some reason - reject request */
	  temp->InStreamState[DecimalStreamNumber] = S_INACTIVE;
	  RejectFile.SRCB = *StreamNumberP;
	  temp->flags &= ~F_XMIT_CAN_WAIT;
	  send_data(Index, &RejectFile,
		    sizeof(struct REJECT_FILE),
		    ADD_BCB_CRC);
	  return;
	} else {
	  /* The file is opened and we can confirm it.
	     However, if the link is of TCP type, the other
	     side might have already sent locally this permission
	     to enhance line performance. In this case, simply do
	     not send the ack */
	  temp->InStreamState[DecimalStreamNumber] = S_REQUEST_SENT;
	  if (((temp->flags & F_RELIABLE) == 0) ||
	      ((temp->flags & F_FAST_OPEN) == 0)) {
	    temp->flags &= ~F_XMIT_CAN_WAIT;
	    PermitFile.SRCB = *StreamNumberP;
	    send_data(Index, &PermitFile,
		      sizeof(struct PERMIT_FILE),
		      ADD_BCB_CRC);
	    /* Other side requested transmission */
	  }
	  temp->flags &= ~F_FAST_OPEN; /* Clear it */
	}
}


/*
 | Send the signon record. The flag tells whether this is the initial or
 | a response signon.
 */
static void
send_signon(Index, flag)
const int	Index, flag;
{
	if (flag == INITIAL_SIGNON) {
	  logger(3, "PROTOCOL: Sending initial signon record.\n");

	  /* Take the local name from a pre-defined string in EBCDIC */
	  memcpy(InitialSignon.NCCINODE, E_BITnet_name, E_BITnet_name_length);
	  /* Put it in IBM order */
	  InitialSignon.NCCIBFSZ = htons(IoLines[Index].PMaxXmitSize);

	  /* Create an empty password (all blanks) */
	  memcpy(InitialSignon.NCCILPAS, EightSpaces, 8);
	  memcpy(InitialSignon.NCCINPAS, EightSpaces, 8);
	  send_data(Index, &InitialSignon, sizeof(struct SIGNON),
		    ADD_BCB_CRC);

	} else {		/* Response signon */

	  logger(3, "PROTOCOL: Sending response signon record.\n");

	  memcpy(ResponseSignon.NCCINODE, E_BITnet_name, E_BITnet_name_length);
	  if (IoLines[Index].MaxXmitSize > IoLines[Index].PMaxXmitSize)
	    IoLines[Index].MaxXmitSize = IoLines[Index].PMaxXmitSize;
	  
	  /* Put it in IBM order */
	  ResponseSignon.NCCIBFSZ = htons(IoLines[Index].MaxXmitSize);

	  /* Create an empty password (all blanks) */
	  memcpy(ResponseSignon.NCCILPAS, EightSpaces, 8);
	  memcpy(ResponseSignon.NCCINPAS, EightSpaces, 8);

	  logger(3, "PROTOCOL: Line %s, Sending response signon with bufsize %d\n",
		 IoLines[Index].HostName, IoLines[Index].MaxXmitSize);
	  send_data(Index, &ResponseSignon, (sizeof(struct SIGNON)),
		    ADD_BCB_CRC);
	}
}


/*
 | Send a message to users registered in InformUsers with the new line's state.
 | If PreviousState >= 0, then it contains the previous state and we should
 | inform it.
 */
static char *states[] = {
  "INACTIVE", "SIGNOFF", "DRAIN", "ACTIVE", "ACTIVE",
  "ACTIVE", "LISTEN", "RETRYING", "TCP-SYNC" };

static void
inform_users_about_line(Index, PreviousState)
const int	Index, PreviousState;
{
	char	From[20], line[LINESIZE];
	int	size, i;
	struct	LINE	*temp;

	if (InformUsersCount == 0)	/* None registered */
		return;

	temp = &IoLines[Index];

	/* Create the sender address and the text */
	sprintf(From, "@%s", LOCAL_NAME);

	if (PreviousState >= 0)
	  sprintf(line, "NETMON: Line %d, host=%s, changed state from %s to %s",
		  Index, temp->HostName, states[PreviousState],
		  states[temp->state]);
	else
	  sprintf(line, "NETMON: Line %d, host=%s, changed state to %s",
		  Index, temp->HostName, states[temp->state]);
	size = strlen(line);

	/* Loop over list and send messages */
	for (i = 0; i < InformUsersCount; i++)
	  send_nmr(From, InformUsers[i], line, size, ASCII, CMD_MSG);
}
