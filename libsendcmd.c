/*	FUNET-NJE/HUJI-NJE		Client utilities
 *
 *	Common set of client used utility routines.
 *	These are collected to here from various modules for eased
 *	maintance.
 *
 *	Matti Aarnio <mea@nic.funet.fi>  12-Feb-1991, 26-Sep-1993
 */

/* Slightly edited copy of this is inside the   file_queue.c! */

#include "consts.h"
#include "prototypes.h"
#include "clientutils.h"

#include <sys/un.h>
#include <sysexits.h>

static int gotalarm;

static void send_alarm()
{
  gotalarm = 1;
}

int  send_cmd_msg(cmdbuf,cmdlen,offlineok)
const void *cmdbuf;
const int cmdlen, offlineok;
{
	u_int32	accesskey;
	int	oldgid, rc = 0, fd;
	char	buf[LINESIZE];
	char	buflen = cmdlen+4;
	FILE	*pidfile;
	int	hujipid = 0;
	extern	int errno;
#ifdef	COMMAND_MAILBOX_SOCKET
	int	Socket, i;
	struct	sockaddr_un	SocketName;
#endif
#ifdef	COMMAND_MAILBOX_UDP
	int	Socket;
	u_int32 ui;
	struct	sockaddr_in	SocketName;
#endif


	gotalarm = 0;

	oldgid = getgid();
	setgid(getegid());

	if ((pidfile = fopen(PID_FILE,"r"))) {
	  fscanf(pidfile,"%d",&hujipid);
	  fclose(pidfile);
	}
	if (!hujipid ||( kill(hujipid,0) && errno == ESRCH)) {
	    if (offlineok) return 0;
	    logger(1,"NJECLIENTLIB: NJE transport module not online!\n");
	    return EX_CANTCREAT;
	}


	sprintf(buf,"%s/.socket.key",BITNET_QUEUE);
	accesskey = 0;
	if ((fd = open(buf,O_RDONLY,0)) >= 0) {
	  read(fd,(void*)&accesskey,4);
	  close(fd);
	} else {
	  logger(1,"NJECLIENTLIB: Can't read access key, errno=%s\n",PRINT_ERRNO);
	  /* DON'T QUIT QUITE YET! */
	}
	*(u_int32*)&buf[0] = accesskey;
	memcpy(buf+4,cmdbuf,cmdlen);

#if	defined(COMMAND_MAILBOX_FIFO)

	if (!*COMMAND_MAILBOX) {
	  logger(1,"NJECLIENTLIB: The COMMAND_MAILBOX  is not configured!\n");
	  setgid(oldgid);
	  return EX_SOFTWARE;;
	}

	signal(SIGALRM, send_alarm);
	alarm(60); /* 60 seconds.. */

	fd = open(COMMAND_MAILBOX,O_WRONLY,0);
	if (fd < 0) {
	  if (errno == EACCES) {
	    logger(1,"NJECLIENTLIB:  No permission to open W-only the `%s'\n",
		   COMMAND_MAILBOX);
	    return EX_NOPERM;
	  } else {
	    logger(1,"NJECLIENTLIB: Failed to open COMMAND_MAILBOX into WRONLY mode!  Error: %s\n",PRINT_ERRNO);
	    if (offlineok)
	      return 0;
	    else
	      return EX_CANTCREAT;
	  }
	}

	write(fd, buf, buflen);
	close(fd);

	alarm(0);	/* Turn it off.. */

#else
#if	defined(COMMAND_MAILBOX_SOCKET)

	/* Create a local socket */
	if ((Socket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
	  perror("Can't create command socket");
	  return(errno);
	}

	memset((void *) &SocketName, 0, sizeof(SocketName));

	/* Create the connect request. We connect to our local machine */
	SocketName.sun_family = AF_UNIX;
	strcpy(SocketName.sun_path, COMMAND_MAILBOX);
	                                                  
	i = sizeof(SocketName.sun_family) + strlen(SocketName.sun_path);

	signal(SIGALRM, send_alarm);
	alarm(60); /* 60 seconds.. */

	if (connect(Socket, (struct sockaddr *)&SocketName, i) < 0) {
	  perror("Access not permitted");
	} else if (write(Socket, buf, buflen) < 0) {
	  perror("Can't send command");
	  if (offlineok)
	    return 0;
	  return EX_CANTCREAT;
	}
	close(Socket);

	alarm(0);

#else
#if	defined(COMMAND_MAILBOX_UDP)

	/* Create a local socket */
	if ((Socket = socket( AF_INET, SOCK_DGRAM, 0)) == -1) {
	  perror("Can't create command socket");
	  if (offlineok)
	    return 0;
	  else
	    return EX_IOERR;
	}

	/* Create the connect request. We connect to our local machine */
	memset((void *) &SocketName, 0, sizeof(SocketName));
	SocketName.sin_family = AF_INET;
	SocketName.sin_port   = htons(175);	/* The VMNET PORT */
	ui = inet_addr(COMMAND_MAILBOX);
	if (ui == 0xFFFFFFFF)  ui = INADDR_LOOPBACK;
	SocketName.sin_addr.s_addr = ui;	/* Configure if you can */
	
	rc = 0;
	if (sendto(Socket, buf, buflen, 0,
		   &SocketName,sizeof(SocketName)) == -1) {
	  perror("Can't send command");
	  if (!offlineok)
	    rc = EX_IOERR;
	}
	close(Socket);

#else
  ::: error "NO COMMAND_MAILBOX MECHANISM DEFINED!" ::: 
#endif
#endif
#endif

	setgid(oldgid);
	return rc;
}
