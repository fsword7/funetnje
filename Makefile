#
#	Makefile for FUNET-NJE package.
#
#	This file is made by  mea@nic.funet.fi  while compiling
#	HUJI-NJE/FUNET-NJE on Sun4/330 SunOS4.0.3 (and later versions
#	of FINFILES machine)
#	Also contains a few other target systems over the years..
#

#       -DHAS_PUTENV    The system has putenv() instead of setenv().
#                       This should be valid for most of the SysV based
#                       systems while the BSD based systems usually have
#                       setenv().  POSIX.1 doesn't say either way :-(
#	-DHAS_LSTAT	System has lstat(2) call in addition to stat(2).
#			(If you have it, USE IT!)
#	-D_POSIX_SOURCE	As POSIX.1 as possible..
#	-DDEBUG		Well, as name says..
#	-DNBCONNECT	Can do non-blocking connect(2) and associated
#			tricks.  WERRY USEFULL as the system won't have to
#			wait, and timeout synchronously for some dead system..
#	-DNBSTREAM	Does whole TCP stream in Non-blocking mode!
#			(This contains NBCONNECT in it -- DOES NOT WORK YET!)
#	-DUSE_SOCKOPT	Does  setsockopt() for SO_RCVBUF, and SO_SNDBUF to
#			set them to 32k instead of the default whatever (4k?)
#	-DBSD_SIGCHLDS	Do SIGC(H)LD handling via a signal trapper.
#			Some (most?) SYSV's can safely ignore the child, but
#			BSDs (SunOS 4.1.3) can't.
#	-DUSE_XMIT_QUEUE  Propably obsolete code;  never used on TCP/IP lines..
#	-DUSE_ENUM_TYPES  If your compiler allows it, do it!
#			  Debugging is smarter..  (Not finished thing!)
#
#	You can override a couple configuration things (these are defaults):
#	-DCONFIG_FILE='"/etc/funetnje.cf"'
#	-DPID_FILE='"/etc/funetnje.cf"'
#
#	Pick only one of following four COMMAND_MAILBOX methods:
#	-DCOMMAND_MAILBOX_FIFO
#			Uses named pipe (mkfifo()) for an IPC channel.
#			Uses also special 32-bit signature prefix on commands
#			to ensure authenticity..
#	-DFIFO_0_READ	Do a close()/open() pair on a FIFO when read from
#			fifo returns 0.  On Linux0.99pl13 at least.
#	-DCOMMAND_MAILBOX_SOCKET
#			Use  AF_UNIX, SOCK_STREAM for command channel instead
#			of named pipe (mkfifo()) stuff.
#			Uses also special 32-bit signature prefix on commands
#			to ensure authenticity..
#			Can be controlled by  `CMDMAILBOX' entry in the
#			`funetnje.cf' -file.  Default  INADDR_LOOPBACK.
#	-DCOMMAND_MAILBOX_UDP
#			Use  AF_INET, SOCK_DGRAM  for command channel...
#			Uses also special 32-bit signature prefix on commands
#			to ensure authenticity..
#
#
#	Define following for Zmailer as a system mailer, othervice will use
#	/usr/lib/sendmail to send the email..  Needed ONLY by  mailify.c
#		-DUSE_ZMAILER -I/usr/local/include
#
#	-fno-builtin	Trouble with over-eager optimization on SPARC.
#			A fixed-size memcpy (2/4/8 bytes) was altered to
#			fetches and stored, but it dies on nonalignment..
#			(A typical CISC vs. RISC program trouble, says FSF
#			 and notes that it should not matter in well written
#			 program..)
#			8-Oct-93: Code fixed => this isn't needed anymore..
#
#	-DUSE_OWN_PROTOS  If the "prototypes.h" -prototypes for various things
#			can fit your system, and your system does not have
#			ANSI headers of its own, then use this...

# Convex OS V10.2 -- very POSIX.1 beast indeed..
#CC     = gcc -fno-builtin -fpcc-struct-return
#CPP    = gcc -E
#CDEFS  = -O -D_POSIX_SOURCE -DCOMMAND_MAILBOX_FIFO -DHAS_LSTAT
#     Using  -DCOMMAND_MAILBOX_FIFO  didn't work..
#CFLAGS = -g $(CDEFS)
# Have MAILIFY compiled by uncommenting following ones:
#MAILIFY=mailify
#NETW   = -L/usr/local/lib -lresolv # -lulsock
#LIBS=$(NETW)
#RANLIB = ranlib
#INSTALL=install

# SunOS 5.3 (Solaris 2.3) -- GNU-CC 2.4.6 on SPARC
#   Your PATH  MUST contain  /usr/ccs/bin:/opt/gnu/bin:/usr/ucb
#   for compilation to succeed without pains..
#CC=gcc -Wall -D__STDC__=0
#CPP=gcc -E
#CDEFS=  -O -I. -DUSG -DUSE_POLL -DCOMMAND_MAILBOX_UDP -DHAS_LSTAT -DHAS_PUTENV -DNBCONNECT -DUSE_SOCKOPT #-DNBSTREAM #-DDEBUG
#CFLAGS= -g $(CDEFS)
## Have MAILIFY compiled by uncommenting following ones:
##MAILIFY=mailify
##MAILIFYCFLAGS= $(CFLAGS) -DUSE_ZMAILER -I/usr/local/include
##LIBMAILIFY= -lzmail
##MAILIFYCFLAGS= $(CFLAGS)
##LIBMAILIFY=
#NETW=
#LIBS= -lsocket -lnsl $(NETW)
#RANLIB= :
#INSTALL=/usr/ucb/install

# SunOS --  GNU-CC 2.4.5 on SPARC SunOS 4.1.3
CC=gcc -Wall #-fno-builtin
CPP=gcc -E
CDEFS=  -O -DBSD_SIGCHLDS -DCOMMAND_MAILBOX_FIFO -DHAS_LSTAT -DHAS_PUTENV -DNBCONNECT -DUSE_SOCKOPT #-DNBSTREAM #-DDEBUG
CFLAGS= -g $(CDEFS)
# Have MAILIFY compiled by uncommenting following ones:
#MAILIFY=mailify
#MAILIFYCFLAGS= $(CFLAGS) -DUSE_ZMAILER -I/usr/local/include
#LIBMAILIFY= -lzmail
##MAILIFYCFLAGS= $(CFLAGS)
##LIBMAILIFY=
NETW=
LIBS=$(NETW)
RANLIB=ranlib
INSTALL=install

# SunOS -- SunOS 4.1.3 bundled cc
#CC=cc
#CPP=/lib/cpp
#CDEFS=  -O -DBSD_SIGCHLDS -DCOMMAND_MAILBOX_FIFO -DHAS_LSTAT -DHAS_PUTENV -DNBCONNECT #-DDEBUG
#CFLAGS=  $(CDEFS)
# Have MAILIFY compiled by uncommenting following ones:
#MAILIFY=mailify
#NETW=
#LIBS=$(NETW)
#RANLIB=ranlib
#INSTALL=install

# Linux 0.99pl13  (w/o using -D_POSIX_SOURCE)
#CDEFS= -O6 -DCOMMAND_MAILBOX_SOCKET -DHAS_LSTAT -DHAS_PUTENV -DNBCONNECT
#CC=gcc
#CPP=gcc -E
#CFLAGS= -g $(CDEFS)
#NETW=
#LIBS=$(NETW)
#RANLIB=ranlib
#INSTALL=install

# IBM AIX ?


# System V (mea.utu.fi) -- ISC 3.0 version:
#CC=gcc
#CDEFS=  -DUSG -DNO_ASM -DCOMMAND_MAILBOX_FIFO
#CFLAGS= -g $(CDEFS)
#NETW=	-lresolv -linet
#LIBS=$(NETW)


# Name of the group on which all communication using programs are
# SGIDed to..  Also the system manager must have that bit available
# to successfully use  UCP  program.
NJEGRP=huji
# On some machines there may exist `send' already, choose another name.
SEND=send
# Assign directories
MANDIR= /usr/local/man
LIBDIR= /usr/local/huji
BINDIR= /usr/local/bin
ETCDIR= /usr/local/etc



SRC=	bcb_crc.c  bmail.c  file_queue.c  headers.c  io.c  main.c	\
	nmr.c  protocol.c  read_config.c  recv_file.c  send.c		\
	send_file.c  unix.c  unix_brdcst.c  unix_build.c gone_server.c	\
	ucp.c  unix_mail.c  unix_route.c  unix_tcp.c  util.c detach.c	\
	unix_files.c sendfile.c bitsend.c read_config.c qrdr.c bitcat.c	\
	ndparse.c libndparse.c libreceive.c receive.c mailify.c		\
	mailify.sh clientutils.h sysin.sh version.sh unix_msgs.c	\
	bintree.c
HDR=	consts.h  ebcdic.h  headers.h  site_consts.h unix_msgs.h
OBJ=	file_queue.o  headers.o  io.o  main.o				\
	nmr.o nmr_unix.o protocol.o  read_config.o  recv_file.o  send.o	\
	send_file.o  unix.o  unix_brdcst.o  unix_build.o gone_server.o	\
	ucp.o  unix_mail.o  unix_route.o  unix_tcp.o  util.o		\
	bcb_crc.o  bmail.o detach.o unix_files.o sendfile.o bitsend.o	\
	qrdr.o logger.o uread.o bitcat.o unix_msgs.o
OBJmain=	main.o  headers.o  unix.o  file_queue.o	read_config.o	\
		io.o  nmr.o  unix_tcp.o  bcb_crc.o  unix_route.o	\
		util.o  protocol.o  send_file.o  recv_file.o logger.o	\
		unix_brdcst.o  unix_files.o gone_server.o detach.o	\
		libustr.o liblstr.o unix_msgs.o rscsacct.o version.o	\
		nmr_unix.o bintree.o
CLIENTLIBobj=		\
		clientlib.a(libndparse.o)	clientlib.a(libdondata.o)  \
		clientlib.a(libetbl.o)		clientlib.a(libsendcmd.o)  \
		clientlib.a(libreadcfg.o)	clientlib.a(libexpnhome.o) \
		clientlib.a(liburead.o)		clientlib.a(libuwrite.o)   \
		clientlib.a(libsubmit.o)	clientlib.a(libasc2ebc.o)  \
		clientlib.a(libebc2asc.o)	clientlib.a(libpadbla.o)   \
		clientlib.a(libhdrtbx.o)	clientlib.a(libndfuncs.o)  \
		clientlib.a(libustr.o)		clientlib.a(liblstr.o)	   \
		clientlib.a(logger.o)		clientlib.a(libstrsave.o)
OBJbmail=	bmail.o		clientlib.a
OBJsend=	send.o		clientlib.a
OBJsendfile=	sendfile.o	clientlib.a
OBJbitsend=	bitsend.o	clientlib.a
OBJtransfer=	transfer.o	clientlib.a
OBJqrdr=	qrdr.o		clientlib.a
OBJucp=		ucp.o		clientlib.a
OBJygone=	ygone.o		clientlib.a
OBJacctcat=	acctcat.o	clientlib.a
OBJreceive=	receive.o libreceive.o clientlib.a
OBJmailify=	mailify.o libreceive.o clientlib.a
OBJnjeroutes=	njeroutes.o	bintree.o
# Phase these out, once the `receive' works
#OBJbitcat=	bitcat.o	clientlib.a
#OBJnetdata=	ndparse.o	clientlib.a
PROGRAMS=	funetnje receive bmail ${SEND} sendfile njeroutes bitsend \
		qrdr ygone transfer acctcat ucp $(MAILIFY)
ObsoletePROGRAMS= bitcat ndparse
OTHERFILES= finfiles.cf finfiles.header unix.install file-exit.cf msg-exit.cf
SOURCES= $(SRC) $(HDR) Makefile $(OTHERFILES) $(MAN1) $(MAN8)
MAN1=	man/send.1 man/sendfile.1 man/transfer.1 man/ygone.1		\
	man/submit.1 man/print.1 man/punch.1 man/receive.1
# ObsoleteMAN1= man/bitcat.1 man/ndparse.1
MAN5=	man/bitspool.5 man/ebcdictbl.5
MAN8=	man/funetnje.8 man/qrdr.8 man/njeroutes.8 man/bmail.8 man/ucp.8	\
	man/mailify.8 man/sysin.8
MANSRCS= man/bitspool.5 man/bmail.8 man/ebcdictbl.5			\
	man/funetnje.8 man/mailify.8 man/njeroutes.8 man/qrdr.8		\
	man/receive.1 man/send.1 man/sendfile.1 man/ucp.8		\
# Obsoletes: man/bitcat.1 
#				# For  man-ps  target

all:	$(PROGRAMS)

# Following need GNU TeXinfo package (3.1 or later)
info:	funetnje.info
dvi:	funetnje.dvi

.PRECIOUS:	clientlib.a

#   A couple default conversions..
.c.o:
	$(CC) -c $(CFLAGS) $<

.c.a:
	$(CC) -c $(CFLAGS) $<
	ar rc clientlib.a $%
	$(RANLIB) clientlib.a
	rm -f $%

.c.s:
	$(CC) -S $(CFLAGS) $<

dist:
	# This is at  FTP.FUNET.FI/FINFILES.BITNET  where FUNET edition
	# is developed..  Making a dump to the archive in easy way..
	./version.sh
	rm -f *~ man/*~ namesfilter/*~ smail-configs/*~
	rm -f njesrc/man/* njesrc/namesfilter/* njesrc/smail-configs/*
	cd njesrc; for x in `find . -links 1 -print`; do rm $$x; ln ../$$x . ; done
	cd njesrc; for x in submit print punch sf bitprt; do rm -f $$x; ln -s sendfile $$x;done
	# If you want to call 'send' with name 'tell'
	# cd njesrc; rm -f tell; ln -s ${SEND} tell
	ln man/* njesrc/man; ln namesfilter/* njesrc/namesfilter; ln smail-configs/* njesrc/smail-configs
	date=`date +%y%m%d`; mv njesrc njesrc-$$date; gtar czf /pub/unix/networking/bitnet/funetnje-$$date.tar.gz njesrc-$$date; mv njesrc-$$date njesrc; chmod 644 /pub/unix/networking/bitnet/funetnje-$$date.tar.gz
	# Make sure the archive directory cache notices some changes..
	cd /pub/unix/networking/bitnet; ls-regen

purge:	clean purgecode

purgecode:
	rm -f $(PROGRAMS)

clean:
	rm -f \#*\# core *.o *~ *.ln *.a

route:	nje.route

install-man:
	cd $(MANDIR)/cat1;for x in $(MAN1); do rm -f `basename $$x`;done
	cd $(MANDIR)/cat5;for x in $(MAN5); do rm -f `basename $$x`;done
	cd $(MANDIR)/cat8;for x in $(MAN8); do rm -f `basename $$x`;done
	for x in $(MAN1); do $(INSTALL) -c -m 644 $$x $(MANDIR)/man1;done
	for x in $(MAN5); do $(INSTALL) -c -m 644 $$x $(MANDIR)/man5;done
	for x in $(MAN8); do $(INSTALL) -c -m 644 $$x $(MANDIR)/man8;done

man-ps:
	for X in $(MANSRCS); do groff -man $$X >$$X.ps; done

nje.route:	finfiles.header finfiles.netinit
	@echo "THIS IS FOR NIC.FUNET.FI!"
	-rm nje.route*
	njeroutes  finfiles.header finfiles.netinit nje.route

route2:	nje.route2

nje.route2:	fintest1.header fintest1.netinit
	@echo "THIS IS FOR MEA.UTU.FI!"
	-rm nje.route*
	njeroutes  fintest1.header fintest1.netinit nje.route

route3: nje.route3

nje.route3:	finutu.header finutu.netinit
	@echo "THIS IS FOR POLARIS.UTU.FI!"
	-rm nje.route*
	njeroutes  finutu.header finutu.netinit nje.route

maketar:
	tar -cf huji.tar $(SOURCES)

makeuuetar: maketar
	-rm -f huji.tar.Z huji.tar.Z.uue
	compress huji.tar
	uuencode huji.tar.Z huji.tar.Z >huji.tar.Z.uue
	rm huji.tar.Z

install:
	echo "To install actual control/config files do 'make install1' or 'make install2'"
	@echo "Must propably be root for this also."
	-mkdir ${LIBDIR}
	#$(INSTALL) -s -m 755 ndparse ${BINDIR}  # Obsolete
	$(INSTALL) -s -m 755 bitsend ${BINDIR}
	$(INSTALL) -s -m 755 qrdr ${BINDIR}
	#$(INSTALL) -s -m 755 bitcat ${BINDIR}   # Obsolete
	$(INSTALL) -s -g ${NJEGRP} -m 750 ucp ${ETCDIR}
	$(INSTALL) -s -g ${NJEGRP} -m 755 sendfile ${BINDIR}
	rm -f ${BINDIR}/print ${BINDIR}/submit ${BINDIR}/punch
	rm -f ${BINDIR}/sf ${BINDIR}/bitprt
	ln ${BINDIR}/sendfile ${BINDIR}/sf
	ln ${BINDIR}/sendfile ${BINDIR}/print
	ln ${BINDIR}/sendfile ${BINDIR}/bitprt
	ln ${BINDIR}/sendfile ${BINDIR}/punch
	ln ${BINDIR}/sendfile ${BINDIR}/submit
	$(INSTALL) -s -g ${NJEGRP} -m 755 send ${BINDIR}/${SEND}
	# If you want to call 'send' with name 'tell'
	# rm -f ${BINDIR}/tell
	# ln ${BINDIR}/${SEND} ${BINDIR}/tell
	$(INSTALL) -s -g ${NJEGRP} -m 755 ygone ${BINDIR}
	$(INSTALL) -s -g ${NJEGRP} -m 755 receive ${BINDIR}
	$(INSTALL) -s -g ${NJEGRP} -m 750 bmail    ${LIBDIR}
	chgrp ${NJEGRP} /usr/spool/bitnet
	chmod g+w  /usr/spool/bitnet
	chmod g+s ${BINDIR}/sendfile ${BINDIR}/send ${BINDIR}/ygone \
		 ${LIBDIR}/bmail
	$(INSTALL) -s -m 755 transfer ${LIBDIR}/transfer
	$(INSTALL) -s -m 755 njeroutes ${LIBDIR}/njeroutes
	$(INSTALL) -c -g ${NJEGRP} -m 750 mailify.sh ${LIBDIR}/mailify
	$(INSTALL) -c -g ${NJEGRP} -m 750 sysin.sh ${LIBDIR}/sysin

install1:	route
	@echo "MUST BE ROOT TO DO THIS!"
	@echo "(this is for NIC.FUNET.FI)"
	-mkdir ${LIBDIR}
	cp finfiles.cf /etc/funetnje.cf
	cp nje.route* ${LIBDIR}
	cp file-exit.cf ${LIBDIR}/file-exit.cf
	cp msg-exit.cf ${LIBDIR}/msg-exit.cf

acctcat:	$(OBJacctcat)
	$(CC) $(CFLAGS) -o $@ $(OBJacctcat) $(LIBS)

bmail:	$(OBJbmail)
	$(CC) $(CFLAGS) -o $@ $(OBJbmail) $(LIBS)

funetnje:	$(OBJmain)
	$(CC) $(CFLAGS) -o $@.x $(OBJmain) $(LIBS)
	-mv $@ $2.old
	mv $@.x $@

clientlib.a: $(CLIENTLIBobj)

ygone:	$(OBJygone)
	$(CC) $(CFLAGS) -o $@ $(OBJygone) $(LIBS)

${SEND}:	$(OBJsend)
	$(CC) $(CFLAGS) -o $@ $(OBJsend) $(LIBS)

sendfile:	$(OBJsendfile)
	$(CC) $(CFLAGS) -o $@ $(OBJsendfile) $(LIBS)

bitsend:	$(OBJbitsend)
	$(CC) $(CFLAGS) -o $@ $(OBJbitsend) $(LIBS)

njeroutes:	$(OBJnjeroutes)
	$(CC) $(CFLAGS) -o $@ $(OBJnjeroutes)

ucp:	$(OBJucp)
	$(CC) $(CFLAGS) -o $@ $(OBJucp) $(LIBS)

mailify:	$(OBJmailify)
	$(CC) $(MAILIFYCFLAGS) -o $@ $(OBJmailify) $(LIBS) $(LIBMAILIFY)

transfer:	$(OBJtransfer)
	$(CC) $(CFLAGS) -o $@ $(OBJtransfer) $(LIBS)

qrdr:		$(OBJqrdr)
	$(CC) $(CFLAGS) -o $@ $(OBJqrdr) $(LIBS)

receive:	$(OBJreceive)
	$(CC) $(CFLAGS) -o $@ $(OBJreceive) $(LIBS)

#  OBSOLETES:
#bitcat:		$(OBJbitcat)
#	$(CC) $(CFLAGS) -o $@ $(OBJbitcat) $(LIBS)
#
#ndparse:	$(OBJnetdata)
#	$(CC) $(CFLAGS) -o $@ $(OBJnetdata) $(LIBS)
#

bintest:	bintest.o bintree.o
	$(CC) $(CFLAGS) -o $@ bintree.o bintest.o

version.c:
	@echo "** BUG!  version.c  is created at 'make dist',"
	@echo "**       and should be present all the time!"
	exit 1

version.o:	version.c
bcb_crc.o:	bcb_crc.c consts.h site_consts.h ebcdic.h
bitsend.o:	bitsend.c consts.h site_consts.h headers.h ebcdic.h
bmail.o:	bmail.c site_consts.h clientutils.h ndlib.h
detach.o:	detach.c
file_queue.o:	file_queue.c consts.h site_consts.h
gone_server.o:	gone_server.c consts.h site_consts.h
headers.o:	headers.c headers.h consts.h site_consts.h ebcdic.h
bintree.o:	bintree.c
bintest.o:	bintest.c bintree.h
io.o:		io.c headers.h consts.h site_consts.h ebcdic.h
clientlib.a(libasc2ebc.o):	libasc2ebc.c	clientutils.h ebcdic.h
clientlib.a(libdondata.o):	libdondata.c	clientutils.h ebcdic.h prototypes.h ndlib.h
clientlib.a(libebc2asc.o):	libebc2asc.c	clientutils.h ebcdic.h
clientlib.a(libetbl.o):		libetbl.c	ebcdic.h
clientlib.a(libexpnhome.o):	libexpnhome.c	clientutils.h
clientlib.a(libhdrtbx.o):	libhdrtbx.c	clientutils.h prototypes.h
clientlib.a(libndfuncs.o):	libndfuncs.c	clientutils.h prototypes.h ebcdic.h ndlib.h
clientlib.a(libndparse.o):	libndparse.c	clientutils.h prototypes.h ebcdic.h ndlib.h
clientlib.a(libpadbla.o):	libpadbla.c	clientutils.h ebcdic.h
clientlib.a(libreadcfg.o):	libreadcfg.c	clientutils.h prototypes.h consts.h
clientlib.a(libsendcmd.o):	libsendcmd.c	clientutils.h prototypes.h consts.h
clientlib.a(libsubmit.o):	libsubmit.c	clientutils.h prototypes.h
clientlib.a(liburead.o):	liburead.c	clientutils.h prototypes.h
clientlib.a(libuwrite.o):	libuwrite.c	clientutils.h prototypes.h
clientlib.a(libstrsave.o):	libstrsave.c	clientutils.h prototypes.h

clientlib.a(liblstr.o):	liblstr.c	clientutils.h
	$(CC) -c $(CFLAGS) $<
	ar rc clientlib.a $%
	$(RANLIB) clientlib.a
clientlib.a(libustr.o):	libustr.c	clientutils.h
	$(CC) -c $(CFLAGS) $<
	ar rc clientlib.a $%
	$(RANLIB) clientlib.a

mailify.o:	mailify.c consts.h clientutils.h prototypes.h ndlib.h
	$(CC) -c $(MAILIFYCFLAGS) mailify.c

locks.o:	locks.c
logger.o:	logger.c prototypes.h consts.h ebcdic.h
main.o:		main.c prototypes.h headers.h consts.h site_consts.h
ndparse.o:	ndparse.c consts.h prototypes.h clientutils.h ndlib.h
njeroutes.o:	njeroutes.c consts.h prototypes.h site_consts.h bintree.h
nmr.o:		nmr.c headers.h consts.h site_consts.h prototypes.h
nmr_unix.o:	nmr_unix.c headers.h consts.h site_consts.h prototypes.h
protocol.o:	protocol.c headers.h consts.h site_consts.h
qrdr.o:		qrdr.c consts.h headers.h
read_config.o:	read_config.c consts.h site_consts.h
receive.o:	receive.c clientutils.h prototypes.h ndlib.h
libreceive.o:	libreceive.c clientutils.h prototypes.h ndlib.h
recv_file.o:	recv_file.c headers.h consts.h site_consts.h
send.o:		send.c clientutils.h prototypes.h
send_file.o:	send_file.c headers.h consts.h site_consts.h
sendfile.o:	sendfile.c clientutils.h prototypes.h ndlib.h
transfer.o:	transfer.c clientutils.h prototypes.h
ucp.o:		ucp.c clientutils.h prototypes.h
unix.o:		unix.c headers.h consts.h site_consts.h prototypes.h
unix_brdcst.o:	unix_brdcst.c consts.h site_consts.h prototypes.h
unix_files.o:	unix_files.c consts.h prototypes.h
unix_build.o:	unix_build.c consts.h site_consts.h prototypes.h
unix_msgs.o:	unix_msgs.c unix_msgs.h consts.h site_consts.h prototypes.h
unix_route.o:	unix_route.c consts.h site_consts.h prototypes.h bintree.h
unix_tcp.o:	unix_tcp.c headers.h consts.h site_consts.h prototypes.h
util.o:		util.c consts.h site_consts.h ebcdic.h prototypes.h bintree.h

funetnje.info:	funetnje.texinfo
	makeinfo funetnje.texinfo

funetnje.dvi:	funetnje.texinfo
	tex funetnje.texinfo


lint:	lintlib
	lint -hc $(CDEFS) llib-lhuji.ln $(SRC)

lintlib:	llib-lhuji.ln

llib-lhuji.ln:	$(SRC)
	lint -Chuji $(CDEFS) $(SRC)

locktest: locktest.o locks.o
	$(CC) -o locktest locktest.o locks.o
