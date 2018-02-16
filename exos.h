/* EXOS.H */
#include	<iodef.h>

#define	HOSTS	"exos_etc:hosts."		/* host file */

#define SOACCEPT		51
#define SOCLOSE			56
#define SOCONNECT		52
#define SOSOCKET		50

#define EX__CTL		 0x32		/* = IO$_ACCESS, Control function */
#define CTL(x)	(x*256 + EX__CTL)	/* socket control functions */

#define EX__ACCEPT	 CTL(SOACCEPT)	/* Accept a remote connection */
#define EX__CLOSE	 CTL(SOCLOSE)	/* Close a socket */
#define EX__CONNECT	 CTL(SOCONNECT)	/* Connect to a remote socket */
#define EX__READ	 0x3A		/* = IO$_ENDRU1, read from TCP stream */
#define EX__RECEIVE	 0x3C		/* = IO$_CONINTREAD, Receive datagram */
#define EX__SELECT	 0x37		/* = IO$_SETCLOCK, select */
#define EX__SEND	 0x3D		/* = IO$_CONINTWRITE, Send datagram */
#define EX__SOCKET	 CTL(SOSOCKET)	/* Get a socket from EXOS */
#define EX__WRITE	 0x3B		/* = IO$_ENDRU2, Write to TCP stream */

#define	SOCK_STREAM	1		/* stream socket */
#define	IPPROTO_TCP	6
#define	AF_INET		2		/* internetwork: UDP, TCP, etc. */
#define	ETIMEDOUT	60

#define	SO_ACCEPTCONN	0x02		/* willing to accept connections */
#define	SO_REUSEADDR	0x40		/* permit local port ID duplication */
#define	SO_LARGE	0x100		/* use larger (8k) buffer quota */

/*  define IOSB data structure	*/

struct ex_iosb {
	unsigned short	ex_stat;		/* status	*/
	unsigned short	ex_count;		/* byte count	*/
	unsigned char	ex_flags;		/* Unused	*/
	unsigned char	ex_reply;		/* reply code	*/
	short		ex_unused;		/* not used	*/
};
