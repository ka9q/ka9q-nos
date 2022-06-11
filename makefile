#
#	Makefile for KA9Q TCP/IP package for PC clones with Borland C
#
# switches:
#	define the ones you want in the CFLAGS definition...
#
#	AMIGA		- include Amiga specific code
#	MSDOS		- include Messy-Dos specific code
#	UNIX		- Use UNIX file format conventions
#	CPM		- Use CP/M file format conventions

#
# parameters for typical IBM-PC installation
#
.autodepend
CC= bcc
ASM= tasm
RM= del
LIB= tlib
# Flags for BC++ 2.0 and earlier
#CFLAGS= -a -d -f- -A- -G- -O -Z -DMSDOS -I.

# Flags for BC++ 3.1
#CFLAGS= -a -d -f- -DMSDOS -I. -O1 -Oi
CFLAGS= -a -d -f- -DMSDOS -I. -DCPU386 -3 -O1 -Oi
MODEL=-ml

# Assembler flags. Important - if 386 mode is selected in CFLAGS, it must
# also be selected here to ensure 32-bit register saving in interrupts
# Note - the memory model is specified in asmglobal.h, included by all
# assembler routines
#AFLAGS=-mx -t
AFLAGS=-mx -t -j.386

# List of libraries
#LIBS = clients.lib servers.lib internet.lib ipsec.lib net.lib \
#	ppp.lib netrom.lib ax25.lib pc.lib dump.lib rsaref.lib

LIBS = clients.lib servers.lib internet.lib net.lib \
	ppp.lib netrom.lib ax25.lib pc.lib dump.lib

# Library object file lists
CLIENTS= telnet.obj ftpcli.obj finger.obj smtpcli.obj hop.obj tip.obj \
	dialer.obj nntpcli.obj bootp.obj popcli.obj lterm.obj

SERVERS= ttylink.obj ftpserv.obj smisc.obj smtpserv.obj \
        fingerd.obj mailbox.obj rewrite.obj bmutil.obj forward.obj tipmail.obj \
	bootpd.obj bootpdip.obj bootpcmd.obj popserv.obj

INTERNET= tcpcmd.obj tcpsock.obj tcpuser.obj \
	tcptimer.obj tcpout.obj tcpin.obj tcpsubr.obj tcphdr.obj \
	udpcmd.obj udpsock.obj udp.obj udphdr.obj \
	domain.obj domhdr.obj \
	ripcmd.obj rip.obj \
	ipcmd.obj ipsock.obj ip.obj iproute.obj iphdr.obj \
	icmpcmd.obj ping.obj icmp.obj icmpmsg.obj icmphdr.obj \
	arpcmd.obj arp.obj arphdr.obj \
	netuser.obj sim.obj

IPSEC=	ipsec.obj esp.obj deskey.obj des3borl.obj desborl.obj desspa.obj ah.obj

AX25=	ax25cmd.obj axsock.obj ax25user.obj ax25.obj \
	axheard.obj lapbtime.obj \
	lapb.obj kiss.obj ax25subr.obj ax25hdr.obj ax25mail.obj

NETROM=	nrcmd.obj nrsock.obj nr4user.obj nr4timer.obj nr4.obj nr4subr.obj \
	nr4hdr.obj nr3.obj nrs.obj nrhdr.obj nr4mail.obj

PPP=	asy.obj ppp.obj pppcmd.obj pppfsm.obj ppplcp.obj \
	ppppap.obj pppipcp.obj pppdump.obj \
	slhc.obj slhcdump.obj slip.obj sppp.obj

NET=	view.obj ftpsubr.obj sockcmd.obj sockuser.obj locsock.obj socket.obj \
	sockutil.obj iface.obj timer.obj ttydriv.obj cmdparse.obj \
	mbuf.obj misc.obj pathname.obj audit.obj files.obj \
	kernel.obj ksubr.obj alloc.obj getopt.obj wildmat.obj \
	devparam.obj stdio.obj vfprintf.obj ahdlc.obj crc.obj md5c.obj

DUMP= 	trace.obj enetdump.obj arcdump.obj \
	kissdump.obj ax25dump.obj arpdump.obj nrdump.obj \
	ipdump.obj icmpdump.obj udpdump.obj tcpdump.obj ripdump.obj \
# secdump.obj

PC=	random.obj display.obj pc.obj dirutil.obj pktdrvr.obj enet.obj \
	hapn.obj hs.obj pc100.obj eagle.obj drsi.obj drsivec.obj \
	z8530.obj n8250.obj pkvec.obj asyvec.obj hsvec.obj \
	pc100vec.obj eaglevec.obj hapnvec.obj \
	scc.obj sccvec.obj \
	pi.obj pivec.obj \
	pcgen.obj sw.obj stopwatch.obj arcnet.obj \
	sb.obj sbvec.obj \
	dma.obj stktrace.obj dos.obj dma.obj

# Implicit rules for compilation and assembly
.c.obj:
	$(CC) -c $(MODEL) $(CFLAGS) { $< }
.cas.obj:
	$(CC) -c $(MODEL) $(CFLAGS) { $< }
.s.obj:
        $(ASM) $(AFLAGS) $<;


# Implicit rule for building libraries
.tl.lib:
	$(RM) $*.lib
	$(LIB) /c $*.lib @$*.tl

all:	mktl.exe net.exe

disk:	net.exe
	pklite net.exe
	copy net.exe a:

makelist.exe: makelist.obj getopt.obj
	$(CC) $(MODEL) $**

net.exe: main.obj config.obj version.obj session.obj $(LIBS)
	$(CC) $(MODEL) -M -enet main.obj config.obj version.obj session.obj *.lib
#	pklite net.exe

mkpass.exe: mkpass.obj md5c.obj
	$(CC) $(MODEL) -emkpass $**

xref.out: main.obj config.obj version.obj session.obj $(LIBS)
	objxref /Oxref.out \tc\lib\c0l.obj main.obj config.obj version.obj \
	session.obj *.lib \tc\lib\cl.lib

# Program to build tlib control files
mktl.exe: mktl.c
	bcc mktl.c

mkdep.exe: mkdep.c
	bcc mkdep.c

# vfprintf must go into the _TEXT module
vfprintf.obj: vfprintf.c
	$(CC) -c $(MODEL) $(CFLAGS) -zC_TEXT vfprintf.c

# build DES SP table
desspa.c: gensp.exe
	gensp a > desspa.c
gensp.exe: gensp.c
	bcc -I/borlandc/include gensp.c

# Library dependencies
ax25.lib: $(AX25)
clients.lib: $(CLIENTS)
dump.lib: $(DUMP)
internet.lib: $(INTERNET)
ipsec.lib: $(IPSEC)
net.lib: $(NET)
netrom.lib: $(NETROM)
pc.lib: $(PC)
ppp.lib: $(PPP)
servers.lib: $(SERVERS)

# Create control files for tlib
ax25.tl: mktl.exe
	mktl > $< <<!
$(AX25)
!

clients.tl: mktl.exe
	mktl > $< <<!
$(CLIENTS)
!

dump.tl: mktl.exe
	mktl > $< <<!
$(DUMP)
!

internet.tl: mktl.exe
	mktl > $< <<!
$(INTERNET)
!

ipsec.tl: mktl.exe
	mktl > $< <<!
$(IPSEC)
!

net.tl: mktl.exe
	mktl > $< <<!
$(NET)
!

netrom.tl: mktl.exe
	mktl > $< <<!
$(NETROM)
!

pc.tl: mktl.exe
	mktl > $< <<!
$(PC)
!

ppp.tl: mktl.exe
	mktl > $< <<!
$(PPP)
!

servers.tl: mktl.exe
	mktl > $< <<!
$(SERVERS)
!

srcrcs.zip:
	-pkzip -urp srcrcs.zip makefile turboc.cfg dodeps.sh makefile.%v *.c%v *.h%v *.s%v 

src.zip:
	-pkzip -u src.zip makefile turboc.cfg dodeps.sh *.c *.h *.s

clean:	nul
	$(RM) *.lib
	$(RM) *.obj
	$(RM) *.exe
	$(RM) *.sym

