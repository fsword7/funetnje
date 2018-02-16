/*   SHUTDOWN_DAEMONS.C
   Shutdown the mailer and NJE.
   DO NOT USE at HUJI, as there runs a program that shuts more processes.
*/
#include <jpidef.h>
#include <ssdef.h>
#include <iodef.h>
#include "consts.h"

struct	desc		/* String descriptor */
	{
	 short	length;		/* Length of text string */
	 short	classtype;	/* Class and type - both zero */
	 long	address;	/* address of string */
	} ;

/*
 | Stop the mailer and NJE.
*/
main()
{
	long	find_pid(), ret_pid;

/* First - shut down the NJE daemon */
	shutdown_nje();
	shutdown_mailer();
}


/*
 | Send the command. The command is in CMD, a numeric value (if needed) is in
 | VALUE, and a string value (if needed) is in STRING.
 */
shutdown_nje()
{
	struct	DESC	MailBoxDesc;
	char	line[LINESIZE];
	long	size, status;
	short	chan, iosb[4];

	*line = CMD_SHUTDOWN_ABRT;
	cuserid(&line[1]);	/* Add the username of sender */
	size = strlen(&line[1]) + 1;	/* 1 for the command code */

	MailBoxDesc.address = MAILBOX_NAME;
	MailBoxDesc.length = strlen(MAILBOX_NAME);
	MailBoxDesc.type = 0;
	status = sys$assign(&MailBoxDesc, &chan, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		printf("Can't connect to command mailbox, status=%d\n", status);
		return;
	}

	status = sys$qiow((long)(0), chan,
		(short)(IO$_WRITEVBLK), iosb,
		(long)(0), (long)(0),
		line, size, (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		printf("CP, Can't write mailbox, status=%d, iosb=%d\n",
			status, iosb[0]);
	}
	sys$dassgn(chan);
}


#define	MAILER_MAILBOX_NAME	"HUYMAIL_MBOX"

shutdown_mailer()
{
	struct	DESC	MailBoxDesc;
	char	line[LINESIZE];
	long	i, size, status;
	short	chan, iosb[4];

	*line = 2;
	MailBoxDesc.address = MAILER_MAILBOX_NAME;
	MailBoxDesc.length = strlen(MAILER_MAILBOX_NAME);
	MailBoxDesc.type = 0;
	status = sys$assign(&MailBoxDesc, &chan, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		printf("Can't connect to command mailbox, status=%d\n", status);
		return;
	}

	status = sys$qiow((long)(0), chan,
		(short)(IO$_WRITEVBLK), iosb,
		(long)(0), (long)(0),
		line, (int)(1), (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		printf("CP, Can't write mailbox, status=%d, iosb=%d\n",
			status, iosb[0]);
	}
	sys$dassgn(chan);
}
