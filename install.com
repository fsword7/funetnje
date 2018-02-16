$! INSTALL.COM	V1.0
$! This procedure will install the NJEF using some default configuration.
$! The error handling is very minimal!!!
$ SET NOON
$ CURRENT_VERSION = "1.7"
$ PREVIOUS_VERSION = "NONE"
$!
$! Check whether this is an upgrade phase or not.
$ UPGRADE_PROCESS = "FALSE"
$ IF P1 .EQS. "UPGRADE" THEN UPGRADE_PROCESS = "TRUE"
$!
$ IF UPGRADE_PROCESS
$ THEN
$	TYPE SYS$INPUT:
  UPGRADE is not supported yet. Aborting...
$	EXIT
$ ENDIF
$!
$ WRITE SYS$OUTPUT "        *** HUJI-NJE version ''CURRENT_VERSION' installation ***"
$! Check whether the source directory exists.
$ IF F$SEARCH("BITNET_ROOT:[000000]NJE_SRC.DIR") .EQS. ""
$ THEN
$	TYPE SYS$INPUT:

  You must install HUyMail before installing the NJE software.

$ 	EXIT
$ ENDIF
$!
$ TYPE SYS$INPUT:

   Assuming that the NJE sources has been unloaded into BITNET_ROOT:[NJE_SRC]
the installation procedure will proceed now to configure the NJE files.

   The file SYS$SPECIFIC:[SYSMGR]HUJI_NJE.DAT will be created to hold the
configuration. The file BITNET_ROOT:[ETC]NJE_ROUTE.HDR will be created to
hold your local routing table. The file [ETC]NJE.ROUTE is the indexed version
of it.
The NJE emulator will be compiled with a default of maximum
one line, two INFORM users and MultiNet TcpIp package.

$ SET DEFAULT BITNET_ROOT:[NJE_SRC]	! To be sure we are there.
$ COPY SYS$INPUT: SITE_CONSTS.H
$DECK
/*  SITE_CONSTS..H
 |   The Hebrew University of Jerusalem.
 |
 | BEFORE COMPILATION: You must tailor this file to your system. The following
 | definiotions are available (#define):
 | UNIX - This is compiled on a UNIX system.
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
#define	GMT_OFFSET	"+0300"

/* Site dependent part which is changed quite rarely */
#define	MAX_INFORM	3
#define	MAX_LINES	1
#define	MAX_STREAMS	1

/*** This is for   NMR.C -module  */
/* The following condition tells us whether to send to this address or not.
*/
#define DONT_SEND_TO_ME(ADDRESS) \
        ((strchr(ADDRESS, '-') != NULL) ||  /* Don't send to address xx-yy */ \
         (compare(ADDRESS, "MAILER") == 0) || \
         (compare(ADDRESS, "NETMON") == 0) || \
         (compare(ADDRESS, "VMMAIL") == 0))
$EOD
$!
$ OPEN/WRITE CONFIG SYS$SPECIFIC:[SYSMGR]HUJI_NJE.DAT
$ OPEN/WRITE ROUTE BITNET_ROOT:[ETC]NJE_ROUTE.HDR
$ INQUIRE BITNET_NAME "Your BITnet name"
$ WRITE CONFIG "NAME ''BITNET_NAME'"
$ WRITE ROUTE "* Local part of routing table for ''BITNET_NAME'"
$ WRITE ROUTE "*"
$ WRITE ROUTE "''BITNET_NAME'   LOCAL   EBCDIC"
$ WRITE ROUTE "*"
$!
$ WRITE CONFIG "QUEUE BITNET_ROOT:[QUEUE]"
$ WRITE CONFIG "LOG BITNET_ROOT:[ETC]NJE.LOG"
$ WRITE CONFIG "TABLE BITNET_ROOT:[ETC]NJE.ROUTE"
$ WRITE CONFIG "DMF-FILE BITNET_ROOT:[ETC]ADDRESS.DAT"
$!
$ TYPE SYS$INPUT:

   Please supply now your IP address in dotted format. Please supply this
information even if you are not using now TcpIp lines.

$ INQUIRE ANS "Local node IP address"
$ WRITE CONFIG "IPADDRESS ''ANS'"
$!
$ TYPE SYS$INPUT:

   The NJE emulator can send messages about line states transitions to some
users when they are logged-in. These users are also authorized to issue
privileged commands to the emulator (in adittion to username SYSTEM). Please
supply now a username who will control this emulator.

$ INQUIRE ANS "Username to control emulator"
$ WRITE CONFIG "INFORM ''ANS'@''BITNET_NAME'"
$!
$ TYPE SYS$INPUT:

  Now we'll set the information for your outgoing link. This will also serve
as your default link. The information needed is the BITnet name of the other
side and the link type used (the rest of the questions is dependent on the
link type used). The following link types are available:
DMF - DMF or DMB in GENBYTE protocol.
DMB - DMB in BiSync mode (preferable if you have DMB).
MULTINET - VMnet using MultiNet package
DECNET - DECnet link to another HUJI-NJE package.
Other TcpIp types must be configured manually.

$ INQUIRE ANS "Your NJE neighbour name"
$ WRITE CONFIG "DEFAULT-ROUTE ''ANS'"
$ WRITE CONFIG "*"
$ WRITE CONFIG "LINE 0 ''ANS'"
$LINK_LOOP:
$ INQUIRE LINK_TYPE "Link type (DMF, DMB, DSV, MULTINET, DECNET)"
$ IF (LINK_TYPE .EQS. "DMF") .OR. (LINK_TYPE .EQS. "DMB") .OR. -
	(LINK_TYPE .EQS. "DSV")
$ THEN
$	WRITE CONFIG "TYPE ''LINK_TYPE'"
$	WRITE CONFIG "BUFSIZE 800"
$	WRITE SYS$OUTPUT "A default buffer size of 800 has been defined"
$	WRITE CONFIG "TIMEOUT 3"
$	INQUIRE DEV "Device name to use (DMF: XGc0:, DMB: SIc0:)"
$	WRITE CONFIG "DEVICE ''DEV'"
$ ENDIF
$ IF LINK_TYPE .EQS. "MULTINET"
$ THEN
$	WRITE CONFIG "TYPE ''LINK_TYPE'"
$	WRITE CONFIG "BUFSIZE 1024"
$	WRITE SYS$OUTPUT "A default buffer size of 1024 has been defined"
$	WRITE CONFIG "TCP-SIZE 8192"
$	WRITE CONFIG "TIMEOUT 1"
$	TYPE SYS$INPUT:
        If the hostname is different than its BITnet name, then enter now
        its InterNet name ot its IP address. Otherwise hit <CR> only.
$	INQUIRE DEV "Hostname InterNet address"
$	IF DEV .NES. "" THEN WRITE CONFIG "TCPNAME ''DEV'"
$	WRITE CONFIG "IPPORT 175"
$ ENDIF
$ IF LINK_TYPE .EQS. "DECNET"
$ THEN
$	WRITE CONFIG "TYPE ''LINK_TYPE'"
$	WRITE CONFIG "BUFSIZE 1024"
$	WRITE SYS$OUTPUT "A default buffer size of 1024 has been defined"
$	WRITE CONFIG "TCP-SIZE 8192"
$	WRITE CONFIG "TIMEOUT 1"
$	INQUIRE DEV "DECnet name of the other side"
$	WRITE CONFIG "DECNETNAME ''DEV'"
$ ENDIF
$ IF (LINK_TYPE .NES. "DMF") .AND. (LINK_TYPE .NES. "DMB") .AND. -
	(LINK_TYPE .NES. "MULTINET") .AND. (LINK_TYPE .NES. "DECNET") .AND. -
	(LINK_TYPE .NES. "DSV")
$ THEN
$	WRITE SYS$OUTPUT "Illegal link type. Re-enter"
$	GOTO LINK_LOOP
$ ENDIF
$!
$ CLOSE CONFIG
$ CLOSE ROUTE
$!
$ TYPE SYS$INPUT:

  All questions has been asked. The package will now be compiled and linked
to create then following files in BITNET_ROOT:[EXE]:
NJE_TASK.EXE - The NJE emulator.
UCP.EXE - The control program.
SENDCMDMSG.EXE - The SEND/COMMAND and SEND/MESSAGE user's interface.
NJE_BUILD.EXE  - builds the indexed table from the text source.
The image SHUT_MAILER will be deleted and replaced with SHUT_DAEMONS.

And the following files will be created in BITNET_ROOT:[ETC]:
NJE$STARTUP - Starts the NJE emulator.

$ IF LINK_TYPE .EQS. "DMF" 
$ THEN
$	COPY SYS$INPUT: BITNET_ROOT:[ETC]NJE$STARTUP.COM
$DECK
$ FNAME := "BITNET_ROOT:[ETC]ADDRESS.DAT"
$ IF F$SEARCH(FNAME) .EQS. "" THEN GOTO NO_BOOT
$ BOOT = F$CVTIME(F$GETSYI("BOOTTIME"), "COMPARISON", "DATETIME")
$ FILE = F$FILE_ATTRIBUTES(FNAME, "CDT")
$ FTIME = F$CVTIME(FILE, "COMPARISON", "DATETIME")
$!
$ IF FTIME .LES. BOOT THEN GOTO NO_BOOT
$ SET PROC/PRIV=(DETACH,ALTPRI)
$ RUN/DETACH/OUTPUT=NL:/ERROR=NL:/PROCESS_NAME="NJE_task"-
	/PRIV=(CMKRNL,LOG_IO,SYSNAM,PSWAPM,OPER) -
	/IO_DIRECT=32/IO_BUFFERED=32/PAGEFILE=10000/BUFFER_LIMIT=40960 -
	/AST_LIMIT=16/QUEUE_LIMIT=16 -
	/MAXIMUM_WORKING_SET=500 -
	/NORESOURCE_WAIT -
	/PRIOR=7 -
	BITNET_ROOT:[EXE]NJE_TASK
$ SET PROC/PRIV=(NODETACH,NOALTPRI)
$NO_BOOT:
$ EXIT
$EOD
$!
$ ELSE
$	COPY SYS$INPUT: BITNET_ROOT:[ETC]NJE$STARTUP.COM
$DECK
$ SET PROC/PRIV=(DETACH,ALTPRI)
$ RUN/DETACH/OUTPUT=NL:/ERROR=NL:/PROCESS_NAME="NJE_task"-
	/PRIV=(CMKRNL,LOG_IO,SYSNAM,PSWAPM,OPER) -
	/IO_DIRECT=32/IO_BUFFERED=32/PAGEFILE=10000/BUFFER_LIMIT=40960 -
	/AST_LIMIT=16/QUEUE_LIMIT=16 -
	/MAXIMUM_WORKING_SET=500 -
	/NORESOURCE_WAIT -
	/PRIOR=7 -
	BITNET_ROOT:[EXE]NJE_TASK
$ SET PROC/PRIV=(NODETACH,NOALTPRI)
$NO_BOOT:
$ EXIT
$EOD
$ ENDIF
$!
$! Compile now the package
$@COMPILE
$@LINK
$ RENAME/LOG MAIN.EXE BITNET_ROOT:[EXE]NJE_TASK.EXE
$!
$ WRITE SYS$OUTPUT "   UCP"
$ CC/NOLIST UCP
$ LINK/EXE=BITNET_ROOT:[EXE] UCP,SYS$INPUT:/OPT
SYS$SHARE:VAXCRTL/SHARE
$ WRITE SYS$OUTPUT "   SENDCMDMSG"
$ CC/NOLIST SENDCMDMSG
$ LINK/EXE=BITNET_ROOT:[EXE] SENDCMDMSG,SYS$INPUT:/OPT
SYS$SHARE:VAXCRTL/SHARE
$ WRITE SYS$OUTPUT "   NJE_BUILD"
$ CC/NOLIST NJE_BUILD
$ LINK/EXE=BITNET_ROOT:[EXE] NJE_BUILD,SYS$INPUT:/OPT
SYS$SHARE:VAXCRTL/SHARE
$ WRITE SYS$OUTPUT "   SHUTDOWN_DAEMONS"
$ CC/NOLIST SHUTDOWN_DAEMONS
$ LINK/EXE=BITNET_ROOT:[EXE] SHUTDOWN_DAEMONS,SYS$INPUT:/OPT
SYS$SHARE:VAXCRTL/SHARE
$ WRITE SYS$OUTPUT "   YGONE"
$ CC/NOLIST YGONE
$ LINK/EXE=BITNET_ROOT:[EXE] YGONE,SYS$INPUT:/OPT
SYS$SHARE:VAXCRTL/SHARE
$!
$! If the link type is DMF we have to build now the LOAD_DMF program.
$ WRITE SYS$OUTPUT "   LOAD_DMF_MAIN"
$ CC/NOLIST LOAD_DMF_MAIN
$ MACRO/NOLIST LOAD_DMF
$ LINK/EXE=BITNET_ROOT:[EXE] LOAD_DMF_MAIN,LOAD_DMF,READ_CONFIG,-
	SYS$SYSTEM:SYS.STB/SELECTIVE_SEARCH,SYS$INPUT:/OPT
SYS$SHARE:VAXCRTL/SHARE
$ DELETE/NOLOG *.OBJ;*
$!
$ WRITE SYS$OUTPUT "   MX_TO_NJE  (Interface to the MX mailer)"
$ CC/NOLIST MX_TO_NJE
$ LINK/EXE=BITNET_ROOT:[EXE] MX_TO_NJE,SYS$INPUT:/OPTION
SYS$SHARE:VAXCRTL/SHARE
$!
$ TYPE SYS$INPUT:

   Compiling the routing table into indexed format:

$ RUN BITNET_ROOT:[EXE]NJE_BUILD
BITNET_ROOT:[ETC]NJE_ROUTE.HDR
NL:
BITNET_ROOT:[ETC]NJE.ROUTE
$!
$ TYPE SYS$INPUT:

  The installation has been finished. You have now to do the following tasks
(which are described in details in the installation manual):
2. Include the file NJE$STARTUP in SYSTARTUP_V5, and modify SYSHUTDWN.CO
   to run SHUTDOWN_DAEMONS instead of SHUT_MAILER.
3. Verify that the table NJE_ROUTE.HDR and SYS$MANAGER:HUJI_NJE.DAT seems
   reasonable.
4. Execute NJE$STARTUP in order to start the emulator.
5. If you are going to use the YGONE facility, enter the following command
   into SYLOGIN:
     YGONE/DISABLE
   Note that YGONE/DISABLE includes the functionality of MAIL_CHECK also.

$ WRITE SYS$OUTPUT "        *** HUJI-NJE version ''CURRENT_VERSION' installed ***"
$ EXIT
