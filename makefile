#
#	Makefile for HUJI-NJE package.
#
#	This file is made by  mea@funic.funet.fi  while compiling
#	HUJI on Sun4/330 SunOS4.0.3.
#	Also applies for his other (System V machine...)
#

# Sun version:

CC=cc
## -DGDBM if you use gdbm instead of dbm.  -DUSG if on System V machine.
CDEFS= #-DGDBM -DDEBUG
CFLAGS= $(CDEFS)
NETW= # -lresolv	# SunOs and Ultrix doesn't like it...
DBASE=	-ldbm # use -lgdbm if CFLAGS above have -DGDBM

# System V (mea.utu.fi) version:

#CC=gcc
# -DGDBM if you use gdbm instead of dbm.  -DUSG if on System V machine.
#CDEFS=  -DGDBM -DUSG -DNO_ASM
#CFLAGS= -g $(CDEFS)
#NETW=	-lresolv -linet
#DBASE=	/usr2/mea/gnu/gdbm/gdbm.a -lmalloc_d -lPW # use -ldbm if CFLAGS above doesn't have -DGDBM

SRC=	bcb_crc.c  bmail.c  file_queue.c  headers.c  io.c  main.c	\
	nmr.c  protocol.c  read_config.c  recv_file.c  send.c		\
	send_file.c  unix.c  unix_brdcst.c  unix_build.c gone_server.c	\
	ucp.c  unix_mail.c  unix_route.c  unix_tcp.c  util.c detach.c
HDR=	consts.h  ebcdic.h  headers.h  site_consts.h
OBJ=	file_queue.o  headers.o  io.o  main.o				\
	nmr.o  protocol.o  read_config.o  recv_file.o  send.o		\
	send_file.o  unix.o  unix_brdcst.o  unix_build.o gone_server.o	\
	ucp.o  unix_mail.o  unix_route.o  unix_tcp.o  util.o	\
	bcb_crc.o  bmail.o detach.o
OBJbmail=	bmail.o
OBJmain=	main.o  read_config.o  headers.o  unix.o  file_queue.o	\
		io.o  nmr.o  unix_tcp.o  bcb_crc.o  unix_route.o	\
		util.o  protocol.o  send_file.o  recv_file.o		\
		unix_brdcst.o  unix_mail.o gone_server.o detach.o
OBJsend=	send.o


all:	bmail main send unix_build ucp
	@if [ ! -f /usr/lib/huji_nje.route ] ; then \
	 echo "You must make routing tables also!";fi

purge:	clean purgecode

purgecode:
	rm -f bmail main send unix_build ucp

clean:
	rm -f \#*\# core *.o *~ *.ln

route:
	@echo "THIS IS FOR FUNIC.FUNET.FI!"
	unix_build  finfunic.header finfunic.route huji_nje.route

route2:
	@echo "THIS IS FOR MEA.UTU.FI!"
	unix_build  fintest1.header fintest1.route huji_nje.route


install:
	@echo "To install actual control/config files do 'make install1' or 'make install2'"
	@echo "Must propably be root for this also."
	cp send /usr/local/bin/send

install1:	route
	@echo "MUST BE ROOT TO DO THIS!"
	@echo "THIS IS FOR FUNIC.FUNET.FI!"
	cp huji_nje.dat /usr/lib/huji_nje.dat
	cp huji_nje.route /usr/lib/huji_nje.route

install2:	route2
	@echo "MUST BE ROOT TO DO THIS!"
	@echo "THIS IS FOR MEA.UTU.FI!"
	cp huji_nje.dat2 /usr/lib/huji_nje.dat
	cp huji_nje.route /usr/lib/huji_nje.route

bmail:	$(OBJbmail)
	$(CC) $(CFLAGS) -o $@ $(OBJbmail) $(NETW)

main:	$(OBJmain)
	$(CC) $(CFLAGS) -o $@ $(OBJmain) $(NETW) $(DBASE)

send:	$(OBJsend)
	$(CC) $(CFLAGS) -o $@ $(OBJsend) $(NETW)

unix_build:	unix_build.o
	$(CC) $(CFLAGS) -o $@ unix_build.o $(DBASE)

ucp:	ucp.o
	$(CC) $(CFLAGS) -o $@ ucp.o $(NETW)


bcb_crc.o:	bcb_crc.c consts.h site_consts.h ebcdic.h
bmail.o:	bmail.c consts.h site_consts.h
file_queue.o:	file_queue.c consts.h site_consts.h
headers.o:	headers.c headers.h consts.h site_consts.h
io.o:		io.c headers.h consts.h site_consts.h
main.o:		main.c headers.h consts.h site_consts.h
nmr.o:		nmr.c headers.h consts.h site_consts.h
gone_server.o:	gone_server.c consts.h site_consts.h
protocol.o:	protocol.c headers.h consts.h site_consts.h
read_config.o:	read_config.c consts.h site_consts.h
recv_file.o:	recv_file.c headers.h consts.h site_consts.h
send.o:		send.c consts.h site_consts.h
send_file.o:	send_file.c headers.h consts.h site_consts.h
unix.o:		unix.c headers.h consts.h site_consts.h
unix_brdcst.o:	unix_brdcst.c
unix_build.o:	unix_build.c
ucp.o:	ucp.c consts.h site_consts.h
unix_mail.o:	unix_mail.c consts.h site_consts.h
unix_route.o:	unix_route.c consts.h site_consts.h
unix_tcp.o:	unix_tcp.c headers.h consts.h site_consts.h
util.o:		util.c consts.h site_consts.h ebcdic.h
detach.o:	detach.c

lint:	lintlib
	lint -hc $(CDEFS) llib-lhuji.ln $(SRC)

lintlib:	llib-lhuji.ln

llib-lhuji.ln:	$(SRC)
	lint -Chuji $(CDEFS) $(SRC)
