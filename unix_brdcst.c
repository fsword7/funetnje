/* UNIX_BRDCST.C (formerly UNIX_BROADCAST.C)	V1.3
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
 | Sections:  COMM - Communication with user and console.
 |
 | V1.1 - 22/3/90 - When cannot send message return 0 instead of -1 in order
 |        to help the GONE section.
 | V1.2 - 27/3/91 - If USER_PROCESS is defined assume we are running on an
 |        RS-6000 and check the type of process.
 | V1.3 - 26/12/91 - If we are running on RS/6000 change the tty name from
 |        /dev/pts/# to /dev/ttyp#.
*/

#include  <stdio.h>
#include  <pwd.h>
#include  <sys/types.h> /* [mea] Needed in some systems for <utmp.h> */
#include  <utmp.h>
#include  <sys/fcntl.h> /* [mea] Needed in some systems. */
#include  <sys/file.h>
#include  <sys/stat.h>  /* [mea] Needed for mesg state analyse */


#define UTMP    "/etc/utmp"

extern int errno;


/*
* Write the message msg to the user, on all ttys he is currently logged
* in.
* Returned value:
* 0 in case of error, number of messages sent otherwise.
* In case of error, errno can be examined.
*/
send_user(user, msg)
char *user, *msg;
{
	int cnt = 0, fdutmp;
	int ftty;
	char buf[BUFSIZ];
	int i, m, n = (BUFSIZ / sizeof(struct utmp));
	int bufsiz = n * sizeof(struct utmp);
	char tty[16];
	struct stat stats;

	for(cnt = 0; cnt < strlen(user); cnt++)
		if(user[cnt] == ' ') {
			user[cnt] = '\0'; break;
		}
		else if((user[cnt] >= 'A') && (user[cnt] <= 'Z'))
			user[cnt] += ' ';  /* lowercasify */

	if((fdutmp = open(UTMP, O_RDONLY)) <= 0)  {
		return(0);
	}

	cnt = 0;

	while((m = read(fdutmp, buf, bufsiz)) > 0)  {
		m /= sizeof(struct utmp);
		for(i = 0; i < m; i++)   {
/* IBM-RS6000 specific field: */
#ifdef USER_PROCESS
			if(((struct utmp *)buf)[i].ut_type != USER_PROCESS) continue;
#endif
			if(memcmp(user, ((struct utmp *)buf)[i].ut_name, strlen(user)) != 0)
				continue;
			sprintf(tty, "/dev/%s", ((struct utmp *)buf)[i].ut_line);

/* Remove the following paragraph (inside the #ifdef) on SGI systems and leave
   it on AIX systems. */
#ifdef USER_PROCESS
/* Change from /dev/pts/# to /dev/ttyp#. */
			{ char *p;
                        if((p = strrchr(tty, '/')) != NULL) {
                                *p = 'p'; p[-1] = 'y'; p[-3] = 't';
                        }}
#endif

			if((ftty = open(tty, O_WRONLY)) < 0)  {
				continue;  /* Some TTYs accept, some don't */
			}
			if(fstat(ftty,&stats)!=0 || (stats.st_mode & 022)==0){
				close(ftty); /* mesg -n! */
				continue;  /* Some TTYs accept, some don't */
			}
			if(write(ftty, msg, strlen(msg)) < strlen(msg))  {
				close(ftty);
				continue;  /* Some TTYs accept, some don't */
			}
			close(ftty);
			++cnt;
		}
	}
	close(fdutmp);

	if(cnt == 0)
		cnt = send_gone(user, msg);
	return(cnt);
}
