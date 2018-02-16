/* UNIX_TCP.C	V2.4
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
 | NOTE: Currently can be only the active side.
 | It works accoriding to VMnet specs, except that we not send FAST-OPEN
 |
 | INPORTANT NOTE: If we try to connect to a dead machine, or write to a
 | dying connection, the whole process will block untill the TCP layer will
 | fund that. During this time, other links will timeout.
 | When a link cannot be established, it'll be retried after 5 minutes. The line
 | is placed in RETRYING state during that period. When a line fails, we try
 | to re-initiate the connection.
 | After doing LISTEN on a line, we put it in LISTEN state. When a connection is
 | attempted on this line, Select() will notify that there is data ready to read
 | on that line. In this case, we issue the accept.
 | In order to not touch the upper layers, we still queue a timeout when a read
 | is queued. However, when the timer expires we simply re-queue it.
 |
 | The function close_unix_tcp_channel() is called by Restart-channel is the
 | line's state is not one of the active ones.
 |
 | V1.3 - When sending a TcpIp packet, we should call Handle-Ack to send the
 |        next one. However, this will cause too deep function calls. So,
 |        we set the F_CALL_ACK flag and main loop will call Handle-Ack.
 | V1.4 - Remove the setting of F_DONT_ACK in the receive function. Replace
 |        it with F_WAIT_V_A_BIT
 | V1.5 - When receiving a buffer, add a check that the size in TTR is less than
 |        the size remained in the buffer (Sun has bugs in its TCP...).
 | V1.6 - Change the way we handle auto retsarts with TCP lines. Instead of
 |        the closing routine queueing a retstart, there is a scan every 5 minut
es
 |        to detect such dead lines and restart them if the AUTO-RESTART flag
 |        is on.
 | V1.7 - Add passive end. We forgot to have it...
 | V1.8 - 14/2/90 - Change SWAP_xxx to the relevant Unix routines; Use namesever
 |        instead of looking in /etc/hosts
 |        If there is a value in Device field, we use it in irder to find the IP
 |        address of the other side. If no value there, we use the hostname.
 | V1.9 - 19/2/90 - 1. When sending NAK control block, give a reasonable code.
 |        2. When completing a connection, do not send ENQUIRE but SYN+NAK.
 | V2.0 - 4/4/90 - Change ttr pointer to a structure, in order to prevent odd
 |        addresses refference.
 | V2.1 - 4/4/90 - Replace printouts of Errno number to error messages from
 |        sys_nerrlist[].
 | V2.2 - 20/9/90 - Add debugging code (level 5) in order to trace problems
 |        during initial handshaking.
 | V2.3 - 28.3.91 - Remove a double call to HTONL() when finding the IP address.
 | V2.4 - 13/9/92 - Add more calls to Logger() function for debugging.
 */
#include "consts.h"
#include "headers.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

extern struct LINE	IoLines[MAX_LINES];
extern struct ENQUIRE	Enquire;

#define	VMNET_IP_PORT		175
#define	EXPLICIT_ACK		0
#define	IMPLICIT_ACK		1

static struct	sockaddr_in	PassiveSocket;
int		PassiveSocketChannel,	/* On which we listen */
		PassiveReadChannel;	/* On which we wait for an initial VMnet control record */

extern int	get_host_ip_address();

extern int	sys_nerr;	/* Maximum error number recognised */
extern char	*sys_errlist[];	/* List of error messages */
#define	PRINT_ERRNO	(errno > sys_nerr ? "***" : sys_errlist[errno])

/*
 | Create a local socket.
 | If we are the initiating side, queue a non-blocking receive. If it fails,
 | close the channel, and re-queue a retry for 5 minutes later.
 */
init_active_tcp_connection(Index)
int	Index;
{
	struct	sockaddr_in	*SocketName;
	int	Socket;		/* The I/O channel */
	struct	VMctl	ControlBlock;
	unsigned char	HostName[128];
	register int	i, TempVar;

	SocketName = &(IoLines[Index].SocketName);

/* Create a local socket */
	if((Socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		logger(1, "UNIX_TCP, Can't get local socket. error: %s\n",
			PRINT_ERRNO);
		IoLines[Index].state = INACTIVE;
		close(IoLines[Index].socket);
		IoLines[Index].socket = 0;
		return;
	}

	SocketName->sin_family = (AF_INET);
	SocketName->sin_port = htons(IoLines[Index].IpPort);
/* Get the IP adrress */
	if(*IoLines[Index].device != '\0')
		strcpy(HostName, IoLines[Index].device);
	else	strcpy(HostName, IoLines[Index].HostName);
	(SocketName->sin_addr).s_addr = get_host_ip_address(Index, HostName);

/* Do the connection trial in a subprocess to not block the parent one */
	if(connect(Socket, SocketName, sizeof(struct sockaddr_in)) == -1) {
		logger(2, "UNIX_TCP, Can't connect. error: %s\n",
			PRINT_ERRNO);
		IoLines[Index].state = RETRYING;
		close(Socket);
		IoLines[Index].socket = 0;
		return;
	}
	IoLines[Index].socket = Socket;
	queue_receive(Index);	/* Queue a receive on the line */

/* Send the initial connection block */
	IoLines[Index].state = TCP_SYNC;	/* Expecting reply for the control block */
	IoLines[Index].RecvSize = IoLines[Index].XmitSize = 0;
/* Send the first control block */
	ASCII_TO_EBCDIC("OPEN", ControlBlock.type, 4);
	PAD_BLANKS(ControlBlock.type, 4, 8);
	memcpy(ControlBlock.Rhost, E_BITnet_name, E_BITnet_name_length);
	i = strlen(IoLines[Index].HostName);
	ASCII_TO_EBCDIC((IoLines[Index].HostName), (ControlBlock.Ohost), i);
	PAD_BLANKS(ControlBlock.Ohost, i, 8);
	i = get_host_ip_address(Index, IP_ADDRESS);
	ControlBlock.Rip = i;
	i = get_host_ip_address(Index, HostName);
	ControlBlock.Oip = i;
	ControlBlock.R = 0;

#ifdef DEBUG
	logger(5, "Writing OPEN control block to line #%d:\n", Index);
	trace(&ControlBlock, VMctl_SIZE, 5);
#endif

	if(write(IoLines[Index].socket, &ControlBlock, VMctl_SIZE) == -1) {
		logger(1, "UNIX_TCP, line=%d, Can't write control block, error: %s\n",
			Index, PRINT_ERRNO);
		close_unix_tcp_channel(Index);
	}
}


/*
 | Create a local socket. Bind a name to it if we must be the secondary side.
 */
init_passive_tcp_connection()
{

	PassiveReadChannel = 0;	/* To signal we haven't got any connection yet */

/* Create a local socket */
	if((PassiveSocketChannel = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		logger(1, "UNIX_TCP, Can't get local socket. error: %s\n",
			PRINT_ERRNO);
		PassiveSocketChannel = 0;	/* To signal others we did not succeed */
		return;
	}

/* Now, bind a local name for it */
	PassiveSocket.sin_family = (AF_INET);
	PassiveSocket.sin_port = htons(VMNET_IP_PORT);
	(PassiveSocket.sin_addr).s_addr = 0;	/* Local machine */
	if(bind(PassiveSocketChannel, &PassiveSocket, sizeof(struct sockaddr_in)) == -1
) {
		logger(1, "UNIX_TCP, Can't bind. error: %s\n",
			PRINT_ERRNO);
		close(PassiveSocketChannel);
		PassiveSocketChannel = 0;	/* To signal others we did not succeed */
		return;
	}
	if(listen(PassiveSocketChannel, 2) == -1) {
		logger(1, "UNIX_TCP, Can't listen, error: %s\n",
			PRINT_ERRNO);
		close(PassiveSocketChannel);
		PassiveSocketChannel = 0;	/* To signal others we did not succeed */
		return;
	}
}

/*
 | Some connection to our passive end. Try doing Accept on it; if successfull,
 | wait for input on it. This input will call Read-passive-TCP-connection.
 */
accept_tcp_connection()
{
	int	size;

	size = sizeof(struct sockaddr_in);
	if((PassiveReadChannel = accept(PassiveSocketChannel, &PassiveSocket,
	   &size)) == -1) {
		logger(1, "UNIX_TCP, Can't accept on passive end. error: %s\n",
			PRINT_ERRNO);
		PassiveReadChannel = 0;
	}
}

/*
 | Some input has been received for the passive end. Try receiving the OPEN
 | block in one read (and hopefully we'll receive it all).
 */
read_passive_tcp_connection()
{
	register int	i, size, Index, TempVar;
	short	ReasonCode;	/* For signaling the other side why we NAK him */
	char	HostName[10], Exchange[10], *p;
	struct	VMctl	ControlBlock;

	ReasonCode = 0;
/* Read input */
	if((size = read(PassiveReadChannel, &ControlBlock, sizeof(ControlBlock),
	    0)) == -1) {
		logger(1, "UNIX_TCP, Error reading VMnet block, error: %s\n",
			PRINT_ERRNO);
		close(PassiveReadChannel);
		PassiveReadChannel = 0;	/* To signal it is closed */
		return;
	}

#ifdef DEBUG
	logger(5, "Received %d chars for passive end (when expecting OPEN record):\n",
		size);
	trace(&ControlBlock, size, 5);
#endif

/* Check that we've received enough information */
	if(size < VMctl_SIZE) {
		logger(1, "UNIX_TCP, Received too small control record\n");
		logger(1, "      Expecting %d, received size of %d\n",
			VMctl_SIZE, size);
		goto RetryConnection;
	}

/* Check first that this is an OPEN block. If not - reset connection */
	EBCDIC_TO_ASCII((ControlBlock.type), Exchange, 8); Exchange[8] = '\0';
	if(strncmp(Exchange, "OPEN", 4) != 0) {
		logger(1, "UNIX_TCP, Illegal control block '%s' received. REason code=%d\n",
			Exchange, ControlBlock.R);
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
		logger(2, "UNIX_TCP, Host %s incorrectly connected to us (%s)\n",
			HostName, Exchange);
		goto RetryConnection;
	}
/* Look for its line */
	for(Index = 0; Index < MAX_LINES; Index++) {
		if(compare(IoLines[Index].HostName, HostName) == 0) {
			/* Found - now do some checks */
			if(IoLines[Index].type != UNIX_TCP) {	/* Illegal */
				logger(2, "UNIX_TCP, host %s, line %d, is not a UNIX_TCP type but type %d\n",
					IoLines[Index].type);
				goto RetryConnection;
			}
			if((IoLines[Index].state != LISTEN) &&
			   (IoLines[Index].state != RETRYING)) { /* Break its previous connection */
				logger(2, "UNIX_TCP, line %s got OPEN record while in state %d\n",
					IoLines[Index].state);
				if(IoLines[Index].state == ACTIVE)
					ReasonCode = 2;
				else	ReasonCode = 3;
				IoLines[Index].state = INACTIVE;
				restart_channel(Index);
				/* Will close line and put it into correct state */
				goto RetryConnection;
			}
/* Copy the parameters from the Accept block, so we can post a new one */
			ASCII_TO_EBCDIC("ACK", (ControlBlock.type), 3);
			PAD_BLANKS((ControlBlock.type), 3, 8);
			IoLines[Index].socket = PassiveReadChannel;
			PassiveReadChannel = 0;	/* We've moved it... */
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

#ifdef DEBUG
			logger(5, "Writing ACK control block to line #%d:\n", Index);
			trace(&ControlBlock, VMctl_SIZE, 5);
#endif

			write(IoLines[Index].socket, &ControlBlock, VMctl_SIZE);
			return;
		}
	}

/* Line not found - log it, and dismiss the connection */
	logger(2, "UNIX_TCP, Can't find line for host '%s'\n", HostName);
/* Send a reject to other side and re-queue the read */
RetryConnection:
	if(ReasonCode == 0) ReasonCode = 1;	/* Link not found */
	ASCII_TO_EBCDIC("NAK", (ControlBlock.type), 3);
	PAD_BLANKS((ControlBlock.type), 3, 8);
	memcpy((ControlBlock.Rhost), E_BITnet_name, E_BITnet_name_length);
#ifdef DEBUG
	logger(5, "Writing NAK control block to unidentified line\n");
	trace(&ControlBlock, VMctl_SIZE, 5);
#endif

	write(PassiveReadChannel, &ControlBlock, VMctl_SIZE);
	close(PassiveReadChannel);
	PassiveReadChannel = 0;
}



/*
 | Called when something was received from TCP. Receive the data and call the
 | appropriate routine to handle it.
 | When receiving,  the first 4 bytes are the count field. When we get the
 | count, we continue to receive untill the count exceeded. Then we call the
 | routine that handles the input data.
 */
unix_tcp_receive(Index)
int	Index;
{
	register long	i, size, TempVar;
	struct	LINE	*temp;
	char	Type[10];	/* Control block type */
	struct	VMctl	*ControlBlock;
	struct	TTR	ttr;
	register unsigned char	*p, *q;
	struct SYN_NAK {	/* To start the link we send SYN NAK */
		unsigned char	Syn, Nak;
		} SynNak;

	SynNak.Syn = SYN;
	SynNak.Nak = NAK;

	temp = &(IoLines[Index]);
	dequeue_timer(temp->TimerIndex);	/* Dequeue the timeout */

/* Append the data to our buffer */
	if((size = (MAX_BUF_SIZE - temp->RecvSize)) <= 0)
		bug_check("UNIX_TCP, TCP receive buffer is full");
	if((size = read(temp->socket, &((temp->buffer)[temp->RecvSize]), size,
	    0)) == -1) {
		logger(1, "UNIX_TCP, Error reading, line =%d, error: %s\n",
			Index, PRINT_ERRNO);
		temp->state = INACTIVE;
		restart_channel(Index);	/* Will close line and put it into correct state */
		return;
	}

#ifdef DEBUG
	logger(5, "Received from line #%d\n", Index);
	trace(&((temp->buffer)[temp->RecvSize]), size, 5);
#endif

/* If we read 0 characters, it usually signals that other side closed connection
 */
	if(size == 0) {
		logger(1, "UNIX_TCP, Zero characters read. Disabling line=%d\n",
			Index);
		temp->state = INACTIVE;
		restart_channel(Index);	/* Will close line and put it into correct state */
		return;
	}

/* If we are in the TCP_SYNC stage, then this is the reply from other side */
	if(temp->state == TCP_SYNC) {
		if(size < 10) {	/* Too short block */
			logger(1, "UNIX_TCP, Too small OPen record received on line %d\n",
				Index);
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		}
		ControlBlock = (struct VMctl*)temp->buffer;
		EBCDIC_TO_ASCII((ControlBlock->type), Type, 8); Type[8] = '\0';
		if(strncmp(Type, "ACK", 3) != 0) {	/* Something wrong */
			logger(1, "UNIX_TCP, Illegal control record '%s', line=%d\n",
				Type, Index);
			/* Print error code */
			switch(ControlBlock->R) {
			case 0x1: logger((int)(2), "     Link could not be found\n");
					break;
			case 0x2: logger((int)(2), "     Link is in active state at other host\n");
					break;
			case 0x3: logger((int)(2), "     Other side is attempting active open\n");
					break;
			default:  logger((int)(2), "     Illegal error code %d\n",
					ControlBlock->R);
			}
			temp->state = INACTIVE;
			restart_channel(Index);	/* Will close line and put it into correct state */
			return;
		}
		/* It's ok - set channel into DRAIN and send the first Enquire */
		temp->state = DRAIN;
		send_data(Index, &SynNak, sizeof(struct SYN_NAK),
			SEND_AS_IS);	/* Send an enquiry there */
		queue_receive(Index);
		temp->TcpState = temp->RecvSize = 0;
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
		else {	/* We need at least 4 bytes */
			queue_receive(Index);	/* Rqueue the read request */
			return;
		}
	}
Loop:	if(temp->RecvSize >= temp->TcpState) {	/* Frame completed */
/* Loop over it */
#ifdef DEBUG
		logger(3, "UNIX_TCP, Going over received TCP buffer of size %d\n",
			temp->RecvSize);
#endif
		p = &temp->buffer[TTB_SIZE];	/* First TTR */
		i = temp->RecvSize - TTB_SIZE;	/* Size of TTB */
		for(;i > TTR_SIZE;) {
			memcpy(&ttr, p, sizeof(struct TTR));	/* Copy to our struct */
			if(ttr.LN == 0) break;	/* End of buffer */
			p += TTR_SIZE;
			TempVar = ntohs(ttr.LN);
/* Check whether the size in TTR is less than the remained size. If not - bug */
			if(TempVar >= i) {
				logger(1, "VMS_TCP, Line=%d, Size in TTR\
(%d) longer than input left(%d)\n",
					Index, TempVar, i);
				i = 0;	/* To ignore this deffective buffer */
				break;
			}
/* Check whether FAST-OPEN flag is on. If so - set our flag also */
			if((ttr.F & 0x80) != 0)	/* Yes */
				temp->flags |= F_FAST_OPEN;
			else		/* No - clear the flag */
				temp->flags &= ~F_FAST_OPEN;
			temp->flags |= F_WAIT_V_A_BIT;
			input_arrived(Index, (long)(1),	/* Success status */
				p, TempVar);
			p += TempVar;
			i -= (TempVar + TTR_SIZE);
		}
		if(i > TTR_SIZE) {	/* rest of buffer is next frame */
			i -= TTR_SIZE;	/* # of chrs from next frame */
			p += TTR_SIZE;
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
	temp->flags &= ~F_WAIT_V_A_BIT;
	if(temp->state == ACTIVE)
		handle_ack(Index, IMPLICIT_ACK);
	queue_receive(Index);	/* Rqueue the read request */
}


/*
 | Write a block to TCP.
 */
send_unix_tcp(Index, line, size)
int	Index, size;
unsigned char	*line;
{
	struct	LINE	*temp;

	temp = &IoLines[Index];
#ifdef DEBUG
	logger(3, "UNIX_TCP, Line %d, Sending a TCP buffer of size %d\n",
		Index, size);
	trace(line, size, 5);
#endif
	if(write(temp->socket, line, size) == -1) {
		logger(1, "UNIX_TCP, Writing to TCP: line=%d, error: %s\n",
			Index, PRINT_ERRNO);
		restart_channel(Index);
	}
	temp->XmitSize = 0;

	temp->flags &= ~F_WAIT_V_A_BIT;
	if(temp->state == ACTIVE) {
			temp->flags |= F_CALL_ACK;	/* Main loop will call Handle-Ack */
	}
}



/*
 | Deaccess the channel and close it. If it is a primary channel, queue a restar
t
 | for it after 5 minutes to establish the link again. Don't do it immediately
 | since there is probably a fault at the remote machine.
 | If this line is a secondary, nothing has to be done. We have an accept active
 | always. When the connection will be accepted, the correct line will be
 | located by the accept routine.
 */
close_unix_tcp_channel(Index)
int	Index;
{
	close(IoLines[Index].socket);
	IoLines[Index].socket = 0;

/* Re-start the channel. */
	IoLines[Index].state = RETRYING;
}


/*
 | Get the other host's IP address using name-server routines.
 */
long
get_host_ip_address(Index, HostName)
char	*HostName;	/* Name or 4 dotted numbers */
{
	long	address;
	struct hostent *hent;
	extern struct	hostent *gethostbyname();

	if(( address = inet_addr(HostName)) != -1L)
		return address;

	while( NULL == (hent = gethostbyname(HostName))) {
		logger(1, "UNIX-TCP: Line %d: Can't resolve '%s', error: %s\n",
		       Index, HostName, PRINT_ERRNO);
		return -1;
	}
	memcpy(&address, hent->h_addr, 4);
	return address;
}
