/* CONSTS   5.8
 | The constants used by the various programs.
 | We can currently process files destined to one receiver only.
 | NOTE: If this file is included in the MAIN.C file (which defines the symbol
 |       MAIN), we define some global vars. If not, we define them as external.
 |       After an upgrade of EXOS, check that this file is ok.
 |
 | Two macros are defined here: ASCII_TO_EBCDIC and PAD_BLANKS, which use
 | a variable named TempVar which should be defined in the functions that
 | call these macros.
 |
 | When running on UNIX systems, the function SIZEOF doesn't return always the
 | real size. For dealing with this case, near a structure definition we
 | use define also its size in bytes.
 */
#include "site_consts.h"
/*   VMS sepcific code   */
#ifdef VMS
#define	NULL	(void *) 0
#include <rms.h>	/* File descriptors for RMS I/O */
unsigned long	DMF_routine_address;	/* The address of DMF framing routine */
#define	MAILBOX_NAME	"HUJI_NJE_CMD"	/* The command mailbox */
#define	CONFIG_FILE	"SYS$MANAGER:HUJI_NJE.DAT" /* Our configuration file */
#endif

/*   Unix specific includes   */
#ifdef unix
#define	UNIX
#include <stdio.h>	/* Unix standard file descriptors */
#include <sys/types.h>
#include <netinet/in.h> /* Sockets definitions */
extern int	errno;
#define	COMMAND_MAILBOX	175	/* Socket number for the command channel */
#define	CONFIG_FILE	"/usr/lib/huji_nje.dat"
#endif

/* if MULTINET or DEC_TCP were defined, define MULTINET_or_DEC so we can compile
   code common to both
*/
#ifdef MULTINET
#define	MULTINET_or_DEC
#endif
#ifdef DEC_TCP
#define	MULTINET_or_DEC
#endif

#define	MAX_BUF_SIZE	10240	/* Maximum buffer size */
#define	MAX_ERRORS	10	/* Maximum recovery trials before restart */
#define	MAX_XMIT_QUEUE	10	/* Maximum pending xmissions for reliable links */

/* VMS treats each external variable as a separate PSECT (C is not yet a native
   language on it...). Hence, we use GLOBALDEF and GLOBALREF instead of externalks.
*/
#ifdef VMS
#define	INTERNAL	globaldef
#define	EXTERNAL	globalref
#else
#define	INTERNAL	/* Nothing */
#define	EXTERNAL	extern
#endif

#ifdef MAIN
INTERNAL char	BITNET_QUEUE[80];	/* The BITnet queue directory */
INTERNAL char	LOCAL_NAME[10];		/* Our BITnet name (In ASCII) */
INTERNAL char	IP_ADDRESS[80];		/* Our IP address */
INTERNAL char	LOG_FILE[80];		/* The LogFile name */
INTERNAL char	TABLE_FILE[80];		/* The routing table */
INTERNAL char	ADDRESS_FILE[80];	/* The name of the file holding the DMF routine's address */
INTERNAL char	InformUsers[MAX_INFORM][20];	/* Users to inform when a line change state */
INTERNAL int	InformUsersCount;	/* How many are in the previous array */
INTERNAL char	DefaultRoute[16];	/* Default route node (if exists) */
INTERNAL char	ClusterNode[16];	/* DECnet name of the cluster's node
					   conncted with NJE to the outer world */
#else
EXTERNAL char	BITNET_QUEUE[80];
EXTERNAL char	LOCAL_NAME[10];
EXTERNAL char	IP_ADDRESS[80];
EXTERNAL char	TABLE_FILE[80];
EXTERNAL char	LOG_FILE[80];
EXTERNAL char	ADDRESS_FILE[80];
EXTERNAL char	InformUsers[MAX_INFORM][20];
EXTERNAL int	InformUsersCount;
EXTERNAL char	DefaultRoute[16];
EXTERNAL char	ClusterNode[16];
#endif

#define	VERSION		"2.1.4"	/* Major.Minor.Edition */
#define	SERIAL_NUMBER	"0"	/* Don't touch it !!! */
#define	LICENSED_TO	"  *** Licensed to: The Hebrew University of Jerusalem, CC ***"

#define	DEVLEN		64	/* Device name length (must be at least 9 chars
				   long) */
#define	LINESIZE	256	/* For various internal usage */
#define	MAX_NJH_HEADERS	5	/* These are the additional NJH fields for which 
				   we Malloc() memory dynamically */
#define	SHORTLINE	80	/* Same, but shorter... */

/* Define macros: */
#define	ASCII_TO_EBCDIC(INSTRING, OUTSTRING, LENGTH) \
	for(TempVar = 0; TempVar < LENGTH; TempVar++) \
		OUTSTRING[TempVar] = ASCII_EBCDIC[(unsigned)INSTRING[TempVar]];

#define	EBCDIC_TO_ASCII(INSTRING, OUTSTRING, LENGTH) \
	for(TempVar = 0; TempVar < LENGTH; TempVar++) \
		OUTSTRING[TempVar] = EBCDIC_ASCII[(unsigned)INSTRING[TempVar]];

#define	PAD_BLANKS(STRING, ORIG_SIZE, FINAL_SIZE) \
	for(TempVar = ORIG_SIZE; TempVar < FINAL_SIZE; TempVar++) \
		STRING[TempVar] = E_SP;


/* Line type */
#define	DMF		1	/* DMF line, or DMB in GENBYTE mode */
#define	EXOS_TCP	2	/* TCP line using EXOS */
#define	MNET_TCP	3	/* MultiNet TcpIp */
#define	DECNET		4	/* DECnet line to other RSCS daemon */
#define	X_25		5	/* Just thoughts for the future... */
#define	ASYNC		6	/* Same... */
#define	UNIX_TCP	7	/* Unix standard sockets calls */
#define	DMB		8	/* DMB in BySinc mode */
#define	DEC__TCP	9	/* DEC's TcpIp package */
#define	DSV		10	/* DSV-32 synchronous interface for uVAXes */

/* Line state. Main state (Line.state). After changing, correct the states[]
   array in PROTOCOL.C, before the function Infrom-users-about-lines.
*/
#define	INACTIVE	0	/* Not active - do not try to send/receive */
#define	SIGNOFF		1	/* Same as Inactive, but was intialy active.. */
#define	DRAIN		2	/* Line is drained - try to start line */
#define	I_SIGNON_SENT	3	/* Initial sign-on sent */
#define	ACTIVE		4	/* Line is working, or trying to... */
#define	F_SIGNON_SENT	5	/* Final sign-on sent */
#define	LISTEN		6	/* Unix TCP: Listen was issued. ACCEPT is needed */
#define	RETRYING	7	/* TCP - Will retry reconnect later. */
#define	TCP_SYNC	8	/* TCP - accepting a connection. */

/* Line state - for each stream (xxStreamState) */
#define	S_INACTIVE	0	/* Inactive but allows new requests */
#define	S_REFUSED	1	/* Refused - don't use it */
#define	S_WAIT_A_BIT	2	/* In temporary wait state */
#define	S_WILL_SEND	3	/* Request to start transfer will be sent soon */
#define	S_NJH_SENT	4	/* Network Job Header was sent */
#define	S_NDH_SENT	5	/* DataSet header sent (if needed) */
#define	S_SENDING_FILE	6	/* During file sending */
#define	S_NJT_SENT	7	/* File ended, sending Network Job Trailer */
#define	S_EOF_SENT	8	/* File ended, EOF sent, waiting for ACK */
#define	S_EOF_FOUND	9	/* EOF found, but not sent yet */
#define	S_REQUEST_SENT	10	/* Request to start transfer actually sent */

/* Line flags (Bit masks). When Adding flags correct Restart_channel() function
   in PROTOCOL.C to reset the needed flags. */
#define	F_RESET_BCB	0x0001	/* Shall we send the next frame with Reset-BCB? */
#define	F_WAIT_A_BIT	0x0002	/* Has the other side sent Wait-a-bit FCS? */
#define	F_SUSPEND_ALLOWED 0x0004	/* Do we allow suspend mode? */
#define	F_IN_SUSPEND	0x0008	/* Are we now in a suspend? */
#define	F_ACK_NULLS	0x0010	/* Shall we ack with null blocks instead of DLE-ACK? */
/* BIT 32 IS FREE FOR USE */
#define	F_SHUT_PENDING	0x0020	/* Shut this line when all files are done. */
#define	F_FAST_OPEN	0x0040	/* For TCP - Other side comfirmed OPEN localy */
#define	F_SENDING	0x0080	/* DECnet/EXOSx - $QIO called but the final AST
				   not fired yet - we can't write now. */
#define	F_DONT_ACK	0x0100	/* DECnet/TCP is sending, so don't try to send
				   more now. */
#define	F_XMIT_QUEUE	0x0200	/* This link supports queue of pending xmissions */
#define	F_XMIT_MORE	0x0400	/* For TCP links - there is room in TCP buffer
				   for more blocks. */
#define	F_XMIT_CAN_WAIT	0x0800	/* In accordance with the previous flag - the
				   current SEND operation can handle the F_XMIT_MORE
				   flag. If clear, TCP must send now. */
#define	F_CALL_ACK	0x1000	/* For UNIX - the main loop should call HANDLE_ACK
				   instead of being called from the TCP send
				   routine (to not going too deep in stack) */
#define	F_WAIT_V_A_BIT	0x2000	/* Wait a bit for TCP and DECnet links. We use
				   this flag to not interfere with the normal NJE
				   flag. */
#define	F_HALF_DUPLEX	0x4000	/* Run the line in Half duplex mode (DMF and
				   DMB only. */
#define	F_AUTO_RESTART	0x8000	/* If the line enters INACTIVE state (DMF and
				   DMB only) then try to restart it after 10
				   minutes). */
#define	F_RELIABLE	0x10000	/* This is a reliable link (TCP or DECNET). Means
				   that it supports xmit-queue */
#define	F_IN_HEADER	0x20000	/* Used on Unix during binary/EBCDIC files receive
				   or transmit to know whether we write pur internal
				   header (thus have to use text mode) or writing
				   the file itself (and then use binary mode). */

/* Codes for timer routine (What action to do when timer expires) */
#define	T_DMF_CLEAN	1	/* Issue $QIO with IO$_CLEAN function for DMF */
#define	T_CANCEL	2	/* Issue $CANCEL system service */
#define	T_SEND_ACK	3	/* Send an ACK. Used to insert a delay before
				   sending ACK on an idle line. */
#define	T_AUTO_RESTART	5	/* Try starting lines which have the AUTo-RESTART
				  flag and are in INACTIVE mode */
#define	T_TCP_TIMEOUT	6	/* timeout on TCP sockets, and VMS TCPs */
#define	T_POLL		7	/* Unix has to poll sockets once a second */
#define	T_STATS		9	/* Time to collect our statistics */
#define	T_ASYNC_TIMEOUT	10	/* Timeout on Asynchronous lines. */
#define	T_DECNET_TIMEOUT 11	/* Timeout to keep DECnet pseudo-Ack */
#define	T_DMF_RESTART	13	/* Restart a DMF/DMB line */
#define	T_STATS_INTERVAL 3600	/* Compute each T_STATS_INT seconds. */
#define	T_AUTO_RESTART_INTERVAL 300	/* Try every 5 minutes */
#define	QUEUE_FILES	99	/* Temporary */

/* General status */
#define	G_INACTIVE	0	/* Not active or not allowed */
#define	G_ACTIVE	1	/* Active or allowed */

/* For the routine to get the link on which to send the message. All are
   negative. Set by Find_line_index(). After adding flags check all the
   places where it is used. */
#define	NO_SUCH_NODE	-1	/* No such node exists */
#define	LINK_INACTIVE	-2	/* Node exists, but inactive and no alternate route */
#define	ROUTE_VIA_LOCAL_NODE -3 /* Connected but not via NJE */

/* The command mailbox messages' codes */
#define	CMD_SHUTDOWN_ABRT 1	/* Shutdown the NJE emulator immediately */
#define	CMD_SHUTDOWN	14	/* Shut down after all lines are idle. */
#define	CMD_SHOW_LINES	2	/* Show lines status */
#define	CMD_QUEUE_FILE	3	/* Add a file to the queue */
#define	CMD_SHOW_QUEUE	4	/* Show the files queued */
#define	CMD_SEND_MESSAGE 5	/* Send NMR message */
#define	CMD_SEND_COMMAND 6	/* Send NMR command */
#define	CMD_START_LINE	7	/* Start an INACTIVE or SIGNOFF line */
#define	CMD_STOP_LINE	8	/* Stop the line after the current file. */
#define	CMD_START_STREAM 9	/* Start a REFUSED stream */
#define	CMD_FORCE_LINE	11	/* Shut that line immediately. */
#define	CMD_FORCE_STREAM 12	/* Shut this stream immediately */
#define	CMD_DEBUG_DUMP	15	/* DEBUG - Dump all buffers to logfile */
#define	CMD_DEBUG_RESCAN 16	/* DEBUG - Rescan queue diretory (to be used when
				   manually rerouting files to another line) */
#define	CMD_LOGLEVEL	17	/* Change log level during run */
#define	CMD_CHANGE_ROUTE 18	/* Change the route in database */
#define	CMD_GONE_ADD	19	/* Register a user in GONE database by YGONE */
#define	CMD_GONE_DEL	20	/* Remove a user from GONE database by YGONE */
#define	CMD_GONE_ADD_UCP 21	/* Register a user in GONE database by UCP */
#define	CMD_GONE_DEL_UCP 22	/* Remove a user from GONE database by UCP */

#define	CMD_MSG		0	/* This is a message NMR */
#define	CMD_CMD		1	/* This is a command NMR */

/* For send_data() routine: */
#define	ADD_BCB_CRC	1	/* Add BCB+FCS in the beginning, ETB+CRC at the end */
#define	SEND_AS_IS	2	/* Do not add anything */

/* The codes that a message can be in */
#define	ASCII		0
#define	EBCDIC		1
#define	BINARY		2

/* The file's type (Bitmask) */
#define	F_MAIL		0x0	/* Mail message */
#define	F_FILE		0x0001	/* File - send it in NETDATA format */
#define	F_PUNCH		0x0002	/* File - send it in PUNCH format */
#define	F_PRINT		0x0004	/* File - send it in PRINT format */
#define	F_ASA		0x0008	/* ASA carriage control */
#define	F_NOQUIET	0x0080	/* Don;t use the QUIET form code */

/* For Close_file routine: */
#define	F_INPUT_FILE	0	/* Input file */
#define	F_OUTPUT_FILE	1	/* Output file (from the program) */

/* For rename_received_file: */
#define	RN_NORMAL	0	/* Normal rename to correct queue */
#define	RN_HOLD		1	/* Hold the file. */
#define	RN_HOLD_ABORT	2	/* Hold it because of sender's abort */

/* FileParams flags: */
#define	FF_LOCAL	1	/* Mail/file destination is on local machine */

/* Offsets inside the received/sent block: */
#define	BCB_OFFSET	2	/* BCB is the 3rd byte */
#define	FCS1_OFFSET	3	/* First byte of FCS */
#define	FCS2_OFFSET	4	/* Second byte */
#define	RCB_OFFSET	5	/* Record control block */
#define	SRCB_OFFSET	6	/* SRCB */

/* The RCB types: */
#define	SIGNON_RCB	0xf0	/* Signon record */
#define	NULL_RCB	0x0	/* End of block RCB */
#define	REQUEST_RCB	0x90	/* Request to initiate a stream */
#define	PERMIT_RCB	0xa0	/* Permission to initiate a stream */
#define	CANCEL_RCB	0xb0	/* Negative permission or receiver cancel */
#define	COMPLETE_RCB	0xc0	/* Stream completed */
#define	READY_RCB	0xd0	/* Ready to receive a stream */
#define	BCB_ERR_RCB	0xe0	/* Received BCB was in error */
#define	NMR_RCB		0x9a	/* Nodal message record */
#define	SYSOUT_0	0x99	/* SYSOUT record, stream 0 */
#define	SYSOUT_1	0xa9	/* SYSOUT record, stream 1 */
#define	SYSOUT_2	0xb9	/* SYSOUT record, stream 2 */
#define	SYSOUT_3	0xc9	/* SYSOUT record, stream 3 */
#define	SYSOUT_4	0xd9	/* SYSOUT record, stream 4 */
#define	SYSOUT_5	0xe9	/* SYSOUT record, stream 5 */
#define	SYSOUT_6	0xf9	/* SYSOUT record, stream 6 */

/* SRCB types: */
#define	NJH_SRCB	0xc0	/* Job Header */
#define	DSH_SRCB	0xe0	/* Data set header */
#define	NJT_SRCB	0xd0	/* Job trailer */
#define	DSHT_SRCB	0xf0	/* Data set trailer. Not used */
#define	CC_NO_SRCB	0x80	/* No carriage control record */
#define	CC_MAC_SRCB	0x90	/* Machine carriage control record */
#define	CC_ASA_SRCB	0xa0	/* ASA crriage control */
#define	CC_CPDS_SRCB	0xb0	/* CPDS carriage control */

/* SCB types: */
#define	EOS_SCB		0	/* End of string or end of file */
#define	ABORT_SCB	0x40	/* Abort transmission */

/* Commonly used macros. */
#ifdef VMS
#define	htons(xxx)	(((xxx >> 8) & 0xff) | ((xxx & 0xff) << 8))
#define	ntohs(xxx)	(((xxx >> 8) & 0xff) | ((xxx & 0xff) << 8))
#define	ntohl(xxx)	(((xxx >> 24) & 0xff) | ((xxx & 0xff0000) >> 8) | \
			 ((xxx & 0xff00) << 8) | ((xxx & 0xff) << 24))
#define	htonl(xxx)	(((xxx >> 24) & 0xff) | ((xxx & 0xff0000) >> 8) | \
			 ((xxx & 0xff00) << 8) | ((xxx & 0xff) << 24))
#endif

/* Cmmonly used structures definitions */
struct	DESC	{		/* String descriptor */
		short	length;
		short	type;	/* Set it to zero */
		char	*address;
		} ;

/* Statistics structure (used only in debug mode) */
struct	STATS {
	int	TotalIn, TotalOut,	/* Total number of blocks transmitted */
		WaitIn, WaitOut,	/* Number of Wait-A-Bits */
		AckIn, AckOut,		/* Number of "Ack-only" blocks */
		MessagesIn, MessagesOut, /* Number of intercative messages blocks */
		RetriesIn, RetriesOut;	/* Total NAKS */
	};

/* The queue structure */
struct	QUEUE {
		struct	QUEUE	*next;		/* Next queue item */
		int	FileSize;		/* File size in blocks */
		unsigned char	FileName[LINESIZE];	/* File name */
	} ;

/* File parameters for the file in process */
struct	FILE_PARAMS {
		long		format,			/* Character set */
				type,			/* Mail/File */
				NetData,		/* Is it in NETDATA format */
				FileId,			/* Pre-given ID */
				flags,			/* Our internal flags */
				RecordsCount,		/* How many records */
				FileSize;		/* File size in bytes */
		char	OrigFileName[LINESIZE],	/* VMS file name */
				FileName[20],		/* IBM Name */
				FileExt[20],		/* IBM Extension */
				From[20],		/* Sender */
				To[20],			/* Receiver */
				line[20],		/* On which line to queue */
				JobName[20],		/* RSCS job name */
				JobClass,		/* RSCS class */
#ifdef INCLUDE_TAG
				tag[136],		/* RSCS tag field */
#endif
				Filler;	/* To make even bytes count */
	} ;

struct	MESSAGE {		/* For outgoing interactive messages */
	struct MESSAGE	*next;	/* Next in chain */
	int		length;	/* Message's length */
	char	text[LINESIZE];	/* The message's text */
	};

#ifdef VMS
/* From the EXOS include files: */
struct in_addr {
	union {
		struct { char s_b1,s_b2,s_b3,s_b4; } S_un_b;
		struct { unsigned short s_w1,s_w2; } S_un_w;
		long S_addr;
	} S_un;
};

struct sockaddr_in {
	short		sin_family;	/* family */
	unsigned short	sin_port;	/* port number */
	struct in_addr	sin_addr;	/* Internet address */
	char		sin_zero[8];	/* unused, must be 0 */
};

struct sockproto {
	short	sp_family;		/* protocol family */
	short	sp_protocol;		/* protocol within family */
};

struct	SOioctl {
	short	hassa;			/* non-zero if sa specified */
	union	{
		struct sockaddr_in sin;	/* socket address (internet style) */
	} sa;
	short	hassp;			/* non-zero if sp specified */
	struct	sockproto sp;		/* socket protocol (optional) */
	int	type;			/* socket type */
	int	options;		/* options */
	};

/* For MultiNet: */
#ifdef MULTINET
struct sockaddr {
	short		sa_family;	/* family */
	unsigned short	sa_port;	/* port number */
	unsigned long	sa_addr;	/* Internet address */
	char		sa_zero[8];	/* unused, must be 0 */
};
#endif
#ifdef DEC_TCP
struct sockaddr {
	short	inet_family;
	short	inet_port;
	long	inet_adrs;
	char	blkb[8];
	};
#endif
#endif

/* The definition of the lines */
struct	LINE	{
	short	type;		/* Line type (DMF, TCP, etc) */
	char	device[DEVLEN];	/* Device name to use for DMF and ASYNC lines,
				   DECnet name to connect to in DECnet links */
	char	HostName[DEVLEN]; /* The other side name */
	int	TimerIndex;	/* Index in timer queue for timeout */
	int	TotalErrors,	/* Total number of errors accumulated */
		errors;		/* Consequtive errors (for error routine) */
#ifdef VMS
	short	channel,	/* IO channel */
		iosb[4],	/* IOSB for that channel */
		Siosb[4];	/* Another IOSB for full-duplex lines */
#endif
#ifdef UNIX
	struct	sockaddr_in	/* For TcpIp communication */
		SocketName;	/* The socket's name and address */
	int	socket;		/* The socket FD */
#endif
	int	IpPort,		/* The IP port number to use */
		InBCB,		/* Incoming BCB count */
		OutBCB,		/* Outgoing BCB count */
		state,		/* Main line state */
		CurrentStream,	/* The stream we are sending now */
		ActiveStreams,	/* Bitmap of streams we are sending now */
		FreeStreams,	/* Number of inactive streams we can use */
		flags,		/* Some status flags regarding this line. */
		QueuedFiles,	/* Number of files queued for this link at this moment */
		MaxStreams;	/* Maximum streams active on this line */
#ifdef EXOS
	struct SOioctl LocalSocket,	/* Local socket structure for EXOS */
		RemoteSocket;	/* The other side */
#endif
#ifdef MULTINET
	struct	sockaddr MNsocket;	/* Remote socket for MultiNet */
#endif
#ifdef DEC_TCP
	struct	sockaddr LocalSocket, RemoteSocket;
#endif
#ifdef VMS
	struct FAB InFabs[MAX_STREAMS];	 /* The FAB for each stream (Sending) */
	struct FAB OutFabs[MAX_STREAMS]; /* The FAB for each stream (Recv) */
	struct RAB InRabs[MAX_STREAMS];		/* The RAB for each stream */
	struct RAB OutRabs[MAX_STREAMS];
#else
	FILE	*InFds[MAX_STREAMS];	/* The FD for each stream (Sending) */
	FILE	*OutFds[MAX_STREAMS];	/* The FD for each stream (Recv) */
#endif
	short	InStreamState[MAX_STREAMS],	/* This stream state */
		OutStreamState[MAX_STREAMS];	/* This stream state */
	struct	FILE_PARAMS
		InFileParams[MAX_STREAMS],	/* Params of file in process */
		OutFileParams[MAX_STREAMS];
	short	TimeOut,	/* Timeout value */
		PMaxXmitSize,	/* Proposed maximum xmission size */
		MaxXmitSize;	/* Agreed maximum xmission size */
	struct	QUEUE		/* The files queued for this line: */
		*QueueStart, *QueueEnd;	/* Both sides of the queue */
	struct	MESSAGE		/* Interactive messages waiting for this line */
		*MessageQstart, *MessageQend;
	unsigned char buffer[MAX_BUF_SIZE],	/* Buffer received */
		      XmitBuffer[MAX_BUF_SIZE];	/* Last buffer sent */
	int	XmitSize;	/* Size of this contents of this buffer */
	int	RecvSize;	/* Size of receive buffer (for TCP buffer completion */
	unsigned long	TcpState,	/* Used during receipt to TCP strem */
		TcpXmitSize;	/* Up to what size to block TCP xmissions */
/* For lines that support transmit queue (XMIT_QUEUE flag is on). These are
   normally only reliable links (TCP and DECnet) */
	char	*XmitQueue[MAX_XMIT_QUEUE];	/* Pointers to buffers */
	int	XmitQueueSize[MAX_XMIT_QUEUE];	/* Size of each entry */
	int	FirstXmitEntry, LastXmitEntry;	/* The queue bounds */
/* The received Dataset and JobHeader to save for later use. Since more than
   2 NJH fragments are rare, we save here only pointers for them. */
	unsigned char	SavedJobHeader[MAX_STREAMS][MAX_BUF_SIZE],
			SavedJobHeader2[MAX_STREAMS][MAX_BUF_SIZE],
			*SavedJobHeaderMore[MAX_STREAMS][MAX_NJH_HEADERS],
			SavedDatasetHeader[MAX_STREAMS][MAX_BUF_SIZE];
	int		SizeSavedJobHeader[MAX_STREAMS], SizeSavedJobHeader2[MAX_STREAMS],
			SizeSavedJobHeaderMore[MAX_STREAMS][MAX_NJH_HEADERS],
			SizeSavedDatasetHeader[MAX_STREAMS];
	char	CarriageControl[MAX_STREAMS];   /* Used when converting machine to ASA */
	int	NDcc[MAX_STREAMS];		/* Netdata carriage control */
/* We use here buffers of 2048 to enable sending binary files (each block is
   512 bytes) */
	unsigned char	SavedNdLine[MAX_STREAMS][2048];	/* For parsing Netdata */
	int SavedNdPosition[MAX_STREAMS];	/* For NetData parsing */
	/* For sending files: */
	unsigned char	XmitSavedLine[MAX_STREAMS][2048];	/* Make enough room */
	int	XmitSavedSize[MAX_STREAMS];
	struct	STATS	stats;	/* Line's statistics */
	} ;


/* Some usefull constants: */
/* If we are in MAIN, define the constants. If not, define them as external,
   to solve compilation problems on SUN.
*/
#ifdef MAIN
unsigned char EightNulls[10] = "\0\0\0\0\0\0\0\0";
unsigned char EightSpaces[10] = "\100\100\100\100\100\100\100\100"; /* 8 EBCDIC spaces */
INTERNAL unsigned char E_BITnet_name[10];	/* Our BITnet name in Ebcdic */
INTERNAL int	E_BITnet_name_length;		/* It's length */
#else
extern unsigned char EightNulls[10];
extern unsigned char EightSpaces[10];
EXTERNAL unsigned char E_BITnet_name[10];
EXTERNAL int	E_BITnet_name_length;
#endif


/*
 | The block headers used by VMnet verion 2
 */
#define	TTB_SIZE	8
struct	TTB {		/* Data block header */
	unsigned char	F,	/* Flags */
			U;	/* Unused */
	unsigned short	LN;	/* Length */
	unsigned long	UnUsed;	/* Unused */
	};

#define	TTR_SIZE	4
struct	TTR {		/* Record header */
	unsigned char	F,	/* Flags */
			U;	/* Unused */
	unsigned short	LN;	/* Length */
	};

#define	VMctl_SIZE	33
struct	VMctl {		/* Control record of VMnet */
	unsigned char	type[8],	/* Request type */
			Rhost[8];	/* Sender name */
	unsigned long	Rip;		/* Sender IP address */
	unsigned char	Ohost[8];	/* Receiver name */
	unsigned long	Oip;		/* Receiver IP address */
	unsigned char	R;		/* Error Reason code */
	};
