/*  SITE_CONSTS..H
 |   The Hebrew University of Jerusalem, HUJIVMS.
 |
 | BEFORE COMPILATION: You must tailor this file to your system. The following
 | definiotions are available (#define):
 | VMS  - This program is compiled on VMS systems.
 | MULTINET - If you are going to use Multinet TcpIp package.
 | EXOS - If you are going to use EXOS TcpIp package.
 | MAX_LINES - How many lines you can define in the line's database.
 | MAX_STREAMS - Do not raise it !!!
 | MAX_INFORM - The maximum number (+1) of the users listed in INFORM command.
 | GDBM - If you use GDBM database instead of DBM. (recommended)
 |        (You can have GDBM in Makefile as well.)
 |
 | Parameters that you might want to change (in CONSTS.H):
 | MAX_BUF_SIZE - The maximum buffer size that can be defined for a line.
 | MAX_ERRORS - After consequtive MAX_ERROR errors a line is restarted.
 |
 */
#define	VMS	-1
#define	MULTINET	-1
#define	GMT_OFFSET	"+0200"

/* Site dependent part which is changed quite rarely */
#define	MAX_INFORM	3
#define	MAX_LINES	9	/* Maximum 9 lines at present */
#define	MAX_STREAMS	3	/* Maximum streams we support per line */

/* Unix specific parts (thanks to Matti Aarnio for the suggestions): */
/* #define	BITNET_HOST	"Host" */

/*  s_addr  is needed if 's_addr' isn't known entity in  <netinet/in.h>
   file  'struct in_addr' structure.  S_un.S_addr is obsolete old style
   format of arguments... (an union)                                    */
/* That file is included in UNIXes in  consts.h -file.  */
/* #define s_addr S_un.S_addr */

/*  Now: <dirent.h>  vs. <sys/dir.h> vs. ...
    Also 'struct dirent' vs. 'struct direct' vs. possible others... */
#define DIRENTFILE  <sys/dir.h>
#define DIRENTTYPE  struct direct

/* #define DIRENTFILE  <dirent.h>
   #define DIRENTTYPE  struct dirent */
/* Note: SystemV r 3.2 compability relies that 'DIRBUF' preproc. symbol isn't
         found in BSD compatible systems.  */

/*** This is for   NMR.C -module  */
/* The following condition tells us whether to send to this address or not.
*/
#define DONT_SEND_TO_ME(ADDRESS) \
        ((strchr(ADDRESS, '-') != NULL) ||  /* Don't send to address xx-yy */ \
         (compare(ADDRESS, "MAILER") == 0) || \
         (compare(ADDRESS, "NETMON") == 0) || \
         (compare(ADDRESS, "VMMAIL") == 0))
