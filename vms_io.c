/* VMS_IO.C    V1.9
 | Copyright (c) 1988,1989,1990 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use oR misuse of this software.
 |
 | VMS specific I/O for synchronous connections.
 | At prsent, only DMF DECnet and Async lines are implemented.
 | WARNNING: Before a system upgrade, check the release notes for the DMF
 |           sync port changes.
 | Note: If a line can't be started (init_xxx_connection) the line is marked
 |       as inactive. Hence, if a modem is turned off when this program is
 |       started, the line will not be activated. This is for a debugging
 |       priod.
 |
 | Currently, after a write request to DMF, if there was an error, the line
 | is deactivated. This is in order to prevent from system's crash.
 | If the DMF/DMB line is setup with the AUTO-RESTART flag, we'll try to
 | restart it every 10 minutes when the line enters INACTIVE state duw to this
 | problem.
 | During the initialization process, the activeness of a line is determined
 | by checking whether its state is ACTIVE not by checking for DRAIN.
 |
 | ASYNC lines: The program set the line to be /TYPEAHD. Thus, on startup the
 |              line might be setup as /NOTYPEAHD to disable logins on it.
 |
 | Sections: DMF-IO    - DMF I/O routines.
 |           ASYNC-IO  - Routine for Async terminals.
 |
 | V1.1 - Add the following characteristics to DMF's SETMODE:
 |        MCL = ON,  MODE = Full duplex, BFN = 2 (instead of 4).
 |        Add also controller shutdown call before $DASSGN.
 | V1.2 - When there is a timeout in read from DMF/DMB, do not cancel
 |        read request. Leave it on, but call the Input-Arrived routine with
 |        error code.
 | V1.3 - Allow DUPLEX HALF keyword in configuration file.
 | V1.4 - Set a flag when DMF_SEND is called and clear it when the $QIO ends.
 |        If the line is in seding when another trial is done, do not issue
 |        another send (disconnected modem might cause this problem). Deactivate
 |        the line.
 | V1.5 - When the DMF/DMB line enters INACTIVE state and the AUTO-RESTART flag
 |        is set, we queue a restart after 10 minutes.
 | V1.6 - When initializing a DMF/DMB, if the type is DMF use GENBYTE protocol
 |        (Useable for DMB's also), and if the type is DMB use BISYNC protocol.
 | V1.7 - Change the way we handle auto retsarts with DMF/DMB lines. Instead of
 |        the closing routine queueing a retstart, there is a scan every 5 minutes
 |        to detect such dead lines and restart them if the AUTO-RESTART flag
 |        is on.
 | V1.8 - 17/4/90 - Add Close_line() after each Restart_channel() with state
 |        set to INACTIVE.
 | V1.9 - 7/3/91 - Add DSV-32 support. It is much like DMB, but some initialization
 |        fields are different.
 */

#include "consts.h"	/* Our constants. */
#include "headers.h"
#include <iodef.h>
#include <ssdef.h>
#include <ttdef.h>
#include <tt2def.h>
#include "dmf.h"

EXTERNAL struct	LINE	IoLines[MAX_LINES];
EXTERNAL struct	ENQUIRE	Enquire;

static short	TerminalMailboxChan[MAX_LINES];	/* For Async lines */

struct	terminal_char {	/* Terminal characteristics */
		unsigned char	class, type;
		unsigned short	PageWidth;
		long	BasicChars;
		long	ExtChars;
	};

#define	MSG$_TRMUNSOLIC		1	/* terminal mailbox messages */
#define	MSG$_TRMHANGUP		6


/*======================= DMF-IO ==================================*/
/*
 | Initialize the DMF connection. Open the port, load the controller with
 | the information relevant to NJE (Sync character, framing routine, etc),
 | and startup the controller. In case of errors, mark the line as inactive
 | (and write a log message ofcourse). Note that this will cause line shutdown
 | in case that the modem is turned off.
 | For more information, look in I/O user's guide, part 2, chapter 2, VMS-4.7
 | OMPORTANT: Do not change ExtendChar structure items without changing this
 | procedure also. The framing routine is placed at ExtendChar[0], the
 | buffer size must be in ExtendChar[2], and ExtendChar[0] holds the protocol
 | type !!!
 */
init_dmf_connection(Index)
int	Index;	/* Index in our IoLines structure */
{
	int	dmf_read_ast_routine();
	long	i, status;
	static short	chan,			/* IO channel */
			iosb[4];		/* IO status block */
	struct	LINE	*temp;			/* Temporary pointer */
	static struct DESC	CharDesc,	/* Characteristics buffer */
				DMF_desc;	/* For $ASSIGN service */
	static struct EXTEND_CHAR {
		short	id;
		unsigned long	value;
		} DMFExtendChar[] = {
				NMA$C_PCLI_FRA, 0 ,
					/* Framing routine. MUST BE First item */
				NMA$C_PCLI_PRO, NMA$C_LINPR_BSY,
					/* GENBYTE protocol (DMF only option) */
				NMA$C_PCLI_BUS, MAX_BUF_SIZE,
					/* Maximum buffer size. MUST BE THIRD!!! */
				NMA$C_PCLI_NMS, 3,
					/* Transmit 3 sync characters */
				NMA$C_PCLI_BPC, 8,
					/* 8 bits per character */
				NMA$C_PCLI_SYC, 0x32,
					/* Sync character */
				NMA$C_PCLI_DUP, NMA$C_DPX_FUL,
					/* Full duplex mode */
				NMA$C_PCLI_BFN, 2,
					/* 4 buffers for receive */
				NMA$C_PCLI_CON, NMA$C_LINCN_NOR,
					/* Contoroller in normal mode */
				NMA$C_PCLI_MCL, NMA$C_STATE_ON,
					/* Contoroller in normal mode */
				NMA$C_PCLI_STI1, 0,
				NMA$C_PCLI_STI2, 0,
				NMA$C_PCLI_TMO, 2 };	/* MUST BE LAST !!!! */
					/* 2 Seconds timeout for CTS after RTS */

/* DSV needs a slightly different array */
	static struct EXTEND_CHAR DSVExtendChar[] = {
				NMA$C_PCLI_PRO, NMA$C_LINPR_BISYNC,
					/* BiSync done by the hardware */
				NMA$C_PCLI_BUS, MAX_BUF_SIZE,
					/* Maximum buffer size. MUST BE THIRD!!! */
				NMA$C_PCLI_NMS, 3,
					/* Transmit 3 sync characters */
				NMA$C_PCLI_DUP, NMA$C_DPX_FUL,
					/* Full duplex mode */
				NMA$C_PCLI_BFN, 2,
					/* 4 buffers for receive */
				NMA$C_PCLI_CON, NMA$C_LINCN_NOR,
					/* Contoroller in normal mode */
				NMA$C_PCLI_CLO, NMA$C_LINCL_EXT,
					/* External clock */
				NMA$C_PCLI_CODE, NMA$C_CODE_EBCDIC };
					/* Use EBCDIC */
	struct EXTEND_CHAR *ExtendChar;

	temp = &(IoLines[Index]);	/* Point to this line */
	switch(temp->type) {
	case DMF:
	case DMB: ExtendChar = DMFExtendChar; break;
	case DSV: ExtendChar = DSVExtendChar; break;
	}

/* Write the DMF framing routine address in the characteristics buffer */
	if(temp->type != DSV) {
		ExtendChar[0].value = DMF_routine_address;
		ExtendChar[2].value = temp->PMaxXmitSize + 20;
		/* Maximum buffer size on that line + overhead */
/* If the line is of type DMB, then use the BYSINC protocol instead of GENBYTE */
		if(temp->type == DMB)
			ExtendChar[1].value = NMA$C_LINPR_BISYNC;
	}

/* If half duplex mode requested - search for it here and set it */
	if((temp->flags & F_HALF_DUPLEX) != 0) {
		for(i = 0; ExtendChar[i].id != NMA$C_PCLI_TMO; i++) {
			if(ExtendChar[i].id == NMA$C_PCLI_DUP) {
				ExtendChar[i].value = NMA$C_DPX_HAL;
				break;
			}
		}
		if(ExtendChar[i].id == NMA$C_PCLI_TMO)
			logger((int)(1), "VMS_IO, Can't set HALF DUPLEX for line %d\n",
				Index);
		else	logger((int)(1), "VMS_IO, Line %d set to HALF DUPLEX\n",
				Index);
	}

/* Create string descriptors. */
	CharDesc.type = 0; DMF_desc.type = 0;
	DMF_desc.length = strlen(temp->device);
	DMF_desc.address = temp->device;
/* DMB does not using the framing routine, so skip its entry */
	if(temp->type == DMB) {
		CharDesc.length = sizeof(DMFExtendChar) - sizeof(struct EXTEND_CHAR);
		CharDesc.address = &ExtendChar[1];
	} else {
		CharDesc.length = sizeof(ExtendChar);
		CharDesc.address = ExtendChar;
	}

/* Assign a channel to the device */
	status = sys$assign(&DMF_desc, &chan, (long)(3), (long)(0));
	if((status & 0x1) == 0) {
		logger((int)(1), 
			"VMS_IO, Can't assign channel to DMF %s, status=%d\n",
				temp->device, status);
		temp->state = INACTIVE;	/* Deactivate it */
		return;
	}
	temp->channel = chan;	/* Store channel */

/* Load the characteristics buffer and start the controller */
	status = sys$qiow((long)(0), chan,
		(short)(IO$_SETMODE | IO$M_CTRL | IO$M_STARTUP), iosb,
		(long)(0), (long)(0),
		(long)(0), &CharDesc,
		(long)(0), (long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger((int)(1),
			"VMS_IO, can't startup DMF %s, status=%d, iosb=x^%x %x %x %x\n",
			temp->device,
			status, iosb[0], iosb[1], iosb[2], iosb[3]);
		sys$dassgn(chan);
		temp->state = INACTIVE;	/* Deactivate it */
		return;
	}
	temp->state = DRAIN;	/* Starting... */
}


/*
 | Write a buffer to the DMF line. Do it asynchronously. Call an AST routine
 | when finished. If there is a problem during write, close the line to
 | prevent from system's crash. If the AUTO-RESTART flag is set, queue a
 | restart for 10 minutes later.
 */
send_dmf(Index, buffer, size)
int	Index, size;		/* Index into IoLines */
unsigned char	*buffer;
{
	struct	LINE	*temp;
	long	i, status, send_dmf_ast();

	temp = &(IoLines[Index]);

	if((temp->flags & F_SENDING) != 0) {
		logger((int)(1), "VMS_IO, Trying to send on line #%d when it is already active\n",
				Index);
		temp->state = INACTIVE;
		restart_channel(Index);
		close_line(Index);
		return;
	}
	temp->flags |= F_SENDING;

	status = sys$qio((long)(0), temp->channel,
		(short)(IO$_WRITEVBLK), temp->iosb,
		send_dmf_ast, Index,	/* Ast routine and its parameter */
		buffer, size,
		(long)(0), (long)(0), (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		logger((int)(1),
			"VMS_IO: Can't write to DMF, status=%d. Line disabled\n",
				status);
		temp->state = INACTIVE;
		restart_channel(Index);
		close_line(Index);
		return 0;
	}
	return 1;
}


/*
 | The DMF send AST routine. Sometimes we get an IOSB of all zeroes (probably
 | the AST was fired before it was filled), so we ignore it. If the error
 | is anything else, we log it and disable the line.
 */
send_dmf_ast(Index)
long	Index;
{
	struct	LINE	*temp;

	temp = &(IoLines[Index]);
	temp->flags &= ~F_SENDING;
	if(((temp->iosb)[0] & 0x1) == 0) {
/* If it is all 0, then dismiss it */
		if((temp->iosb)[0] == 0) {
#ifdef DEBUG
			logger(2, "VMS_IO: DMF write ast - IOsb is zeroes.\n");
#endif
			return;
		}

/* Log it and disable line (only if the line is in some active state) */
		if((temp->state != INACTIVE) && (temp->state != SIGNOFF)) {
			logger((int)(1),
				"VMS_IO: line #%d: error in write to DMF, iosb=x^%x %x %x %x\n",
				Index, (temp->iosb)[0], (temp->iosb)[1],
				(temp->iosb)[2], (temp->iosb)[3]);
			IoLines[Index].state = INACTIVE;
			restart_channel(Index);	/* Requeue files */
			close_line(Index);
		}
	}
}


/*
 | Queue a receive for a DMF line. The timer is queued by the caller of this
 | routine.
 */
int
queue_dmf_receive(Index)
int	Index;		/* Index in IoLines array */
{
	struct	LINE	*temp;
	long	status, receive_dmf_ast();

	temp = &(IoLines[Index]);
	status = sys$qio((long)(0), temp->channel,
		(short)(IO$_READVBLK), temp->iosb,
		receive_dmf_ast, Index,	/* Ast routine and its parameter */
		temp->buffer, (long)(MAX_BUF_SIZE),
		(long)(0), (long)(0), (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		logger(1, "VMS_IO: Can't queue read to DMF, status=%d\n",
			status);
		temp->state = INACTIVE;
		restart_channel(Index);
		close_line(Index);
		return 0;
	}
	return 1;
}


/*
 | The routine which is called when a read request has been completed. Dequeue
 | the timer element, and then dispatch according to the device type.
 */
receive_dmf_ast(parameter)
long	parameter;		/* Index in IoLines array */
{
	struct	LINE	*temp;
	long	size, status;
	char	*buffer;

	temp = &(IoLines[parameter]);
	dequeue_timer(temp->TimerIndex);	/* Dequeue the timeout */

/* Get the values from the IoLines storage */
	status = (long)((temp->iosb)[0]);	/* The IO ending status */
	size = (long)((temp->iosb)[1]);		/* Number of chars read */
	buffer = temp->buffer;			/* Buffer address */

/* Call the routine to handle the input */
	input_arrived(parameter, status, buffer, size);

/* Now, requeue a read for that line (if not deactivated by the porevious
   routine */
	if((temp->state == ACTIVE) || (temp->state == DRAIN) ||
	   (temp->state == I_SIGNON_SENT) || (temp->state == F_SIGNON_SENT))
		queue_receive(parameter);
}


/*
 | Close the channel to the DMF.
 */
close_dmf_line(Index)
int	Index;
{
	short	iosb[4];
	register long	status;

/* Shutdown the controller */
/* Just a check for bugs in our program: */
	if(IoLines[Index].channel == 0) {
		logger(1, "VMS_IO, Close doubly called for line %d\n", Index);
		return;
	}
	status = sys$qiow((long)(0), IoLines[Index].channel,
		(short)(IO$_SETMODE | IO$M_CTRL | IO$M_SHUTDOWN), iosb,
		(long)(0), (long)(0),
		(long)(0), (long)(0),
		(long)(0), (long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		logger((int)(1),
			"VMS_IO, can't shut DMF %s, status=%d, iosb=x^%x %x %x %x\n",
			IoLines[Index].device,
			status, iosb[0], iosb[1], iosb[2], iosb[3]);
		IoLines[Index].state = INACTIVE;	/* Deactivate it */
	}
	sys$dassgn(IoLines[Index].channel);
	IoLines[Index].channel = 0;	/* To enable catching double call to this routine */
}


/*
 | This routine returns 0 if it handles the timeout by itself. If it doesn't,
 | it returns 1 so that the calling timer routine can put some value into the
 | timeout field. This is because the routine that handles the timeout will try
 | to dequeue that entry. Since we want to find real dequeues that dequeue 0
 | time entries, we must use this mechanism.
 */
dmf_timeout(Index)
int	Index;		/* Index in IoLines */
{
	struct	LINE	*temp;
	int	status, queue_timer();

/* First check whether the line is inactive. If so - ignore */
	temp = &(IoLines[Index]);
	if(temp->state == INACTIVE)
		return 0;			/* Ignore it */

/* Act depending the operation */
	if(temp->state == DRAIN) {
/* The line is in starting state. This might take hours, so leave the receive
   on, and just send an enquiry block. */
		send_dmf(Index, &Enquire, (int)(sizeof(struct ENQUIRE)));
		/* requeue again the timeout. */
		temp->TimerIndex =
			queue_timer(temp->TimeOut, Index, (short)(T_DMF_CLEAN));
		return 0;
	}

/* It's in other stages, So call INPUT_ARRIVED with error code of timeout */
	input_arrived(Index, (int)(SS$_TIMEOUT),
			"", (int)(0));	/* Empty buffer */
/* Requeue the entry if line stayed in some ON state: */
	if((temp->state != INACTIVE) && (temp->state != SIGNOFF))
		temp->TimerIndex =
			queue_timer(temp->TimeOut, Index, (short)(T_DMF_CLEAN));
	return 0;

}

/*======================= ASYNC ===============================*/
/*
 | Open a channel to terminal, set it to be /PASSALL/EIGHTBIT/TYPEAHD,
 | assiciate a mailbox with it (to receive a message when there is input for
 | that line) and queue an AST for that mailbox. Note: It is impossible to
 | queue a receive for the line, since this receive blocks writes on it.
 | The only way is to use mailbox and queue AST for it.
 */
init_async_connection(Index)
int	Index;
{
	int	status, async_receive_ast();
	char	*p, MailBoxName[SHORTLINE];
	struct	DESC	mail_box, Device;
	struct	LINE	*temp;
	struct	terminal_char	TerChar;

	temp = &IoLines[Index];

/* Create a unique mailbox name for each terminal */
	sprintf(MailBoxName, "TERM_%s", temp->device);
	if((p = strchr(MailBoxName, ':')) != NULL) *p = '\0';	/* Remove the : */

	mail_box.length = strlen(MailBoxName);
	mail_box.type = 0;
	mail_box.address = MailBoxName;

/* Create a temporary mailbox. Save its channel in Terminal-Mailbox-Chan array,
   in the position Index. */
	if(((status = sys$crembx((char)(0),	/* temporary mailbox */
			&TerminalMailboxChan[Index],
			(long)(0), (long)(0), (long)(0), (long)(3),
			&mail_box)) & 0x1) == 0) {
		logger((int)(1), "VMS_IO, Can't create mailbox for terminal line\n");
		temp->state = INACTIVE;
		return 0;
	}

/* Assign a channel to device */
	Device.address = temp->device;
	Device.type = 0;
	Device.length = strlen(temp->device);

	if(((status = sys$assign(&Device, &(temp->channel),
			(long)(3), &mail_box)) & 0x1) == 0) {
		logger((int)(1), "VMS_IO, Can't assign channel to '%s'\n",
			temp->device);
		sys$dassgn(TerminalMailboxChan[Index]);
		temp->state = INACTIVE;
		return 0;
	}

/* Set up the terminal to be PASSALL and PASTHRU and NOECHO */
/* Read the old ones: */
	status = sys$qiow((long)(0), temp->channel, (short)(IO$_SENSEMODE),
		(long)(0), (long)(0), (long)(0),
		&TerChar, (int)(sizeof TerChar),
		(int)(0), (int)(0), (int)(0), (int)(0));
	if((status & 0x1) == 0) {
		logger((int)(1), "VMS_IO, Can't read terminal setup, line=%d\n",
			Index);
		sys$dassgn(TerminalMailboxChan[Index]);
		temp->state = INACTIVE;
		close_line(Index);
		return 0;
	}

/* Setup the needed ones: */
	TerChar.BasicChars |= (TT$M_NOECHO | TT$M_EIGHTBIT | TT$M_PASSALL);
	TerChar.BasicChars &= ~TT$M_NOTYPEAHD;	/* set to /TYPEAHD */
	status = sys$qiow((long)(0), temp->channel, (short)(IO$_SETMODE),
		(long)(0), (long)(0), (long)(0),
		&TerChar, (int)(sizeof TerChar),
		(int)(0), (int)(0), (int)(0), (int)(0));
	if((status & 0x1) == 0) {
		logger((int)(1), "VMS_IO, Can't set terminal setup, line=%d\n",
			Index);
		sys$dassgn(TerminalMailboxChan[Index]);
		temp->state = INACTIVE;
		close_line(Index);
		return 0;
	}

/* All ok - Put line in DRAIN mode */
	temp->state = DRAIN;
	temp->RecvSize = temp->TcpState = 0;
	return 1;
}

/*
 | Close the line - deassign its mailbox and the channel to the line.
*/
close_async_line(Index)
{
	sys$dassgn(TerminalMailboxChan[Index]);
	sys$dassgn(IoLines[Index].channel);
}

/*
 | Send to terminal. Use $QIO without wait, and queue an AST to fire when write
 | is done.
 | We pad <LF> to the end of buffer, so "smart" terminal drivers can
 | recognize when a buffer is full.
 */
send_async(Index, buffer, size)
int	Index, size;
unsigned char	*buffer;
{
	long	status, async_write_ast();
	struct	LINE	*temp;

	temp = &IoLines[Index];

	buffer[size++] = '\n';
	status = sys$qio((long)(0), temp->channel,
			(short)(IO$_WRITEVBLK),
			temp->iosb, async_write_ast, Index,
			buffer, size, (long)(0),
			(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0)) {
		logger((int)(1), "VMS_IO, Can't write to terminal, status=%d,\
 iosb[0]=x^%x\n",
			status, (temp->iosb)[0]);
	}
}


/*
 | Called when the write to terminal is done.
 */
async_write_ast(Index)
int	Index;
{
	if((IoLines[Index].iosb[0] & 0x1) == 0) {	/* Log it */
		logger((int)(1), "VMS_IO, Error writing to terminal line,\
 iosb=x^%x\n",
		IoLines[Index].iosb[0]);
	}
}


/*
 | Called when there is something in the mailbox. If this is input from the
 | terminal, Calls $QIOW to receive the
 | input. To assure we don't block, we call it with timeout value of zero.
 | We don't care whether the input returned ok or with timeout - the framing
 | routine will find whether a frame is completed or not.
*/
#include "frame_common.h"
async_receive_ast(Index)
long	Index;
{
	register long	i, status, size;
	long		delimiter[2];
	unsigned char	*p, *q;
	char	buffer[SHORTLINE];	/* For reading mailbox */
	struct	LINE	*temp;

	temp = &IoLines[Index];
	dequeue_timer(temp->TimerIndex);

/* Read the message from mailbox. */
	status = sys$qiow((int)(0), TerminalMailboxChan[Index],
			(short)(IO$_READVBLK),
			(long)(0), (long)(0), (long)(0),
			buffer, (int)(sizeof buffer),
			(long)(0), (long)(0), (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		logger((int)(1), "VMS_IO, Can't read mailbox for terminal line\
 %d, status=%d\n",
			Index, status);
		IoLines[Index].state = INACTIVE;
		close_line(Index);
		return;
	}

	status = (buffer[1] << 8) + buffer[0];	/* Message type */
	switch(status) {
	case MSG$_TRMUNSOLIC:	/* There is input on the terminal line */
			break;	/* Handle it */
	case MSG$_TRMHANGUP:	/* Terminal hangup - Drain line */
			logger((int)(1), "VMS_IO, Line #%d, terminal hangup.\n",
				Index);
			temp->state = DRAIN;
			restart_channel(Index);
			queue_receive(Index); return;
	default:	logger((int)(1), "VMS_IO, Unrecognised mailbox type d^%d\n",
			status);
			queue_receive(Index); return;
	}

/* Read the input */
	size = MAX_BUF_SIZE - temp->RecvSize;	/* Place left in buffer */
	if(size <= 0) {		/* No room - clear buffer */
		logger((int)(1), "VMS_IO, Async input buffer overflow on line %d\n",
			Index);
		trace(temp->buffer, temp->RecvSize, (int)(1));
		temp->RecvSize = 0;	/* Clear buffer */
		queue_receive(Index); return;
	}

	delimiter[0] = 0;
	delimiter[1] = 0x400;	/* <LF> */
	status = sys$qiow((long)(0), temp->channel,
		(short)(IO$_READPBLK | IO$M_TIMED),
		temp->iosb, (long)(0), (long)(0),
		&temp->buffer[temp->RecvSize], size,
		(long)(1),	/* Allow waiting of up to 1 second for input */
		delimiter, (long)(0), (long)(0));

	if(((temp->iosb[0] & 0x1) == 0) &&
	   (temp->iosb[0] != SS$_TIMEOUT)) {
		logger((int)(1), "VMS_IO, Error reading from terminal, line=%d,\
 status=d^%d, iosb=x^%x\n",
			Index, status, (long)(temp->iosb[0]));
		input_arrived(Index, (int)(temp->iosb[0]), temp->buffer,
			(int)(0));	/* Zero characters */
		queue_receive(Index); return;
	}

/* Something received - assemble packet */
	size = (int)(temp->iosb[1] + (temp->iosb[3] & 0xff));
		/* [1] = length of text, [3](low 4 bits) = terminator length */
	p = q = &temp->buffer[temp->RecvSize];	/* Start of the new data */
	i = temp->RecvSize;	/* Size of old data */
	temp->RecvSize += size;	/* Add the new read data */

/* Check each character and decide what to do with it */
	while(size-- > 0) {
		HANDLE_CHARACTER(*p);	/* Return value in status */
		if((status & IGNORE_CHAR) != 0) {
			*p++;	/* Read next character */
			temp->RecvSize--;	/* Remove from count */
			continue;
		}
		if((status & COMPLETE_READ) == 0) {	/* Buffer it */
			*q++ = *p++;	/* Save the character */
			i++;		/* Increment the count */
			continue;
		}
/* A frame is complete - process it and clear buffer */
		input_arrived(Index, (int)(1),	/* Success status */
			temp->buffer, ++i);
			/* I is the size of frame. have to add the last
			   characters count also */
		temp->TcpState = temp->RecvSize = size = 0;
	}
	queue_receive(Index);
}

/*
 | Queue a write-attention AST for the mailbox that is associated with the
 | terminal.
 */
int
queue_async_receive(Index)
int	Index;
{
	long	status, async_receive_ast();
	struct	LINE	*temp;

	temp = &IoLines[Index];

/* Re-enable AST delivery for the mailbox */
	status = sys$qiow((long)(0),
			TerminalMailboxChan[Index],
			(short)(IO$_SETMODE|IO$M_WRTATTN),
			(long)(0), (long)(0), (long)(0),
			async_receive_ast, Index,
			(long)(3),	/* Access mode */
			(long)(0), (long)(0) ,(long)(0));
		/* Notify when message written to mailbox */
	if((status & 0x1) == 0) {
		logger((int)(1), "VMS_IO, Can't declare AST for terminal mailbox.\
 status=%d\n",
			status);
		temp->state = INACTIVE;
		sys$dassgn(TerminalMailboxChan[Index]);
		close_line(Index);
		return 0;
	}
	return 1;
}


/*
 | Called when there is a timeout event on an async line. If we are in DRAIN
 | simply send an Enquire. If in other active states, call Input-Arrived with
 | SS$_ABORT status (this signals timeout for us). We then re-queue the timer.
 | there is no need to call Queue-Receive in this routine, since the mailbox-
 | AST routine is left active on the terminal line.
 */
async_timeout(Index)
int	Index;		/* Index in IoLines */
{
	struct	LINE	*temp;
	int	status, queue_timer();

/* First check whether the line is inactive. If so - ignore */
	temp = &(IoLines[Index]);
	if(temp->state == INACTIVE)
		return 0;			/* Ignore it */

/* Act depending the operation */
	if(temp->state == DRAIN) {
/* The line is in starting state. This might take hours, so leave the receive
   on, and just send an enquiry block. */
		temp->TcpState = temp->RecvSize = 0;	/* Clear input buffer */
		send_async(Index, &Enquire, (int)(sizeof(struct ENQUIRE)));
		/* requeue again the timeout. */
		temp->TimerIndex =
			queue_timer(temp->TimeOut, Index, (short)(T_ASYNC_TIMEOUT));
		return 0;
	}

/* It's in other stages, so simulate an input with an error. */
	input_arrived(Index, (long)(SS$_ABORT),	/* ABORT signals our timeout */
			"", (int)(0));
	temp->TimerIndex =
		queue_timer(temp->TimeOut, Index, (short)(T_ASYNC_TIMEOUT));
	return 0;
}
