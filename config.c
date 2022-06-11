/* A collection of stuff heavily dependent on the configuration info
 * in config.h. The idea is that configuration-dependent tables should
 * be located here to avoid having to pepper lots of .c files with #ifdefs,
 * requiring them to include config.h and be recompiled each time config.h
 * is modified.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */

#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#ifdef	IPSEC
#include "ipsec.h"
#include "photuris.h"
#endif
#include "tcp.h"
#include "udp.h"
#include "smtp.h"
#ifdef	ARCNET
#include "arcnet.h"
#endif
#include "lapb.h"
#include "ax25.h"
#include "enet.h"
#include "kiss.h"
#include "nr4.h"
#include "nrs.h"
#include "netrom.h"
#include "pktdrvr.h"
#include "ppp.h"
#include "slip.h"
#include "arp.h"
#include "icmp.h"
#include "hardware.h"	/***/
#include "usock.h"
#include "cmdparse.h"
#include "commands.h"
#include "mailbox.h"
#include "ax25mail.h"
#include "nr4mail.h"
#include "tipmail.h"
#include "daemon.h"
#include "bootp.h"
#include "asy.h"
#include "trace.h"
#include "session.h"
#ifdef	QTSO
#include "qtso.h"
#endif
#ifdef	CDMA_DM
#include "dm.h"
#include "rlp.h"
#endif
#ifdef	DMLITE
#include "dmlite.h"
#include "rlp.h"
#endif
#ifdef	SPPP
#include "sppp.h"
#endif
#include "dialer.h"
#ifdef	KSP
#include "ksp.h"
#endif
#ifdef	SOUND
#include "sb.h"
#endif

int dotest(int argc,char *argv[],void *p);	/**/
static int dostart(int argc,char *argv[],void *p);
static int dostop(int argc,char *argv[],void *p);

#ifdef	AX25
static void axip(struct iface *iface,struct ax25_cb *axp,uint8 *src,
	uint8 *dest,struct mbuf **bp,int mcast);
static void axarp(struct iface *iface,struct ax25_cb *axp,uint8 *src,
	uint8 *dest,struct mbuf **bp,int mcast);
static void axnr(struct iface *iface,struct ax25_cb *axp,uint8 *src,
	uint8 *dest,struct mbuf **bp,int mcast);
#endif	/* AX25 */

struct mbuf *Hopper;		/* Queue of incoming packets */
unsigned Nsessions = NSESSIONS;
unsigned Nsock = DEFNSOCK;		/* Number of socket entries */

/* Free memory threshold, below which things start to happen to conserve
 * memory, like garbage collection, source quenching and refusing connects
 */
int32 Memthresh = MTHRESH;

unsigned Nfiles = DEFNFILES;

long Sfsize = 1000;	/* Default size of session scrollback file */

/* Command lookup and branch tables */
struct cmds Cmds[] = {
	/* The "go" command must be first */
	"",		go,		0, 0, NULL,
#ifndef	AMIGA
	"!",		doshell,	0, 0, NULL,
#endif
#ifdef	AMIGA
	"amiga",	doamiga,	0, 0, NULL,
#endif
#if	(defined(MAC) && defined(APPLETALK))
	"applestat",	doatstat,	0,	0, NULL,
#endif
#if	(defined(AX25) || defined(ETHER) || defined(APPLETALK))
	"arp",		doarp,		0, 0, NULL,
#endif
#ifdef	ASY
	"asystat",	doasystat,	0, 0, NULL,
#endif
	"attach",	doattach,	0, 2,
		"attach <hardware> <hw specific options>",
#ifdef	AX25
	"ax25",		doax25,		0, 0, NULL,
#endif
#ifdef	BOOTP
	"bootp",	dobootp,	0, 0, NULL,
	"bootpd",	bootpdcmd,	0, 0, NULL,
#endif
/* This one is out of alpabetical order to allow abbreviation to "c" */
#ifdef	AX25
	"connect",	doconnect,	1024, 3,
	"connect <interface> <callsign>",
#endif
#if	!defined(UNIX) && !defined(AMIGA)
	"cd",		docd,		0, 0, NULL,
#endif
	"close",	doclose,	0, 0, NULL,
/* This one is out of alpabetical order to allow abbreviation to "d" */
	"disconnect",	doclose,	0, 0, NULL,
	"delete",	dodelete,	0, 2, "delete <file>",
	"detach",	dodetach,	0, 2, "detach <interface>",
	"debug",	dodebug,	0, 1, "debug [on|off]",
#ifdef	DIALER
	"dialer",	dodialer,	0, 2,
		 "dialer <iface> <timeout> [device-dependent args]",
#endif
#ifndef	AMIGA
	"dir",		dodir,		0, 0, NULL, /* note sequence */
#endif
#ifdef	CDMA_DM
	"dm",		dodm,		0, 0, NULL,
#endif
#ifdef	DMLITE
	"dmlite",	dodml,		0, 0, NULL,
#endif
	"domain",	dodomain,	0, 0, NULL,
#ifdef	DRSI
	"drsistat",	dodrstat,	0, 0, NULL,
#endif
#ifdef	EAGLE
	"eaglestat",	doegstat,	0, 0, NULL,
#endif
	"echo",		doecho,		0, 0, NULL,
	"eol",		doeol,		0, 0, NULL,
#if	!defined(MSDOS)
	"escape",	doescape,	0, 0, NULL,
#endif
	"exit",		doexit,		0, 0, NULL,
#ifdef	QFAX
	"fax",		dofax,		4096, 2, "fax <server>",
#endif	
	"files",	dofiles,	0, 0, NULL,
	"finger",	dofinger,	1024, 2, "finger name@host",
	"ftp",		doftp,		2048, 2, "ftp <address>",
#ifdef HAPN
	"hapnstat",	dohapnstat,	0, 0, NULL,
#endif
	"help",		dohelp,		0, 0, NULL,
#ifdef	HOPCHECK
	"hop",		dohop,		0, 0, NULL,
#endif
	"hostname",	dohostname,	0, 0, NULL,
#ifdef	HS
	"hs",		dohs,		0, 0, NULL,
#endif
	"icmp",		doicmp,		0, 0, NULL,
	"ifconfig",	doifconfig,	0, 0, NULL,
	"ip",		doip,		0, 0, NULL,
#if defined(MSDOS) && !defined(CPU386)
	"isat",		doisat,		0, 0, NULL,
#endif
	"kick",		dokick,		0, 0, NULL,
#ifdef	KSP
	"ksp",		doksp,		0, 0, NULL,
#endif
	"log",		dolog,		0, 0, NULL,
#ifdef	LTERM
	"lterm",	dolterm,	512, 3, "lterm <iface> <address> [<port>]",
#endif
#ifdef	MAILBOX
	"mbox",		dombox,		0, 0, NULL,
#endif
#ifndef	UNIX
	"memory",	domem,		0, 0, NULL,
#endif
	"mkdir",	domkd,		0, 2, "mkdir <directory>",
	"more",		doview,		0, 2, "more <filename>",
#ifdef	NETROM
	"netrom",	donetrom,	0, 0, NULL,
#endif	/* NETROM */
#ifdef	NNTP
	"nntp",		donntp,		0, 0, NULL,
#endif	/* NNTP */
#ifdef	NRS
	"nrstat",	donrstat,	0, 0, NULL,
#endif	/* NRS */
	"page",		dopage,		0, 2, "page <command> [args...]",
	"param",	doparam,	0, 2, "param <interface>",
	"ping",		doping,		512, 2,
	"ping <hostid> [<length> [<interval> [incflag]]]",
#ifdef	PI
	"pistatus",	dopistat,	0, 0, NULL,
#endif
#ifdef POP
	"pop",		dopop,		0, 0, NULL,
#endif
#ifdef PPP
	"ppp",		doppp_commands,	0, 0, NULL,
#endif
	"ps",		ps,		0, 0, NULL,
#if	!defined(UNIX) && !defined(AMIGA)
	"pwd",		docd,		0, 0, NULL,
#endif
#ifdef	QTSO
	"qtso",		doqtso,		0, 0, NULL,
#endif
	"record",	dorecord,	0, 0, NULL,
	"remote",	doremote,	0, 3, "remote [-p port] [-k key] [-a kickaddr] <address> exit|reset|kick",
	"rename",	dorename,	0, 3, "rename <oldfile> <newfile>",
	"repeat",	dorepeat,	1024, 3, "repeat <interval> <command> [args...]",
	"reset",	doreset,	0, 0, NULL,
#ifdef	RIP
	"rip",		dorip,		0, 0, NULL,
#endif
	"rmdir",	dormd,		0, 2, "rmdir <directory>",
	"route",	doroute,	0, 0, NULL,
	"session",	dosession,	0, 0, NULL,
#ifdef	IPSEC
	"secure",	dosec,		0, 0, "secure [[add|delete] <host>]",
#endif
	"scrollback",	dosfsize,	0, 0, NULL,
#ifdef	SCC
	"sccstat",	dosccstat,	0, 0, NULL,
#endif
#if	!defined(AMIGA)
	"shell",	doshell,	0, 0, NULL,
#endif
#if	defined(SMTP)
	"smtp",		dosmtp,		0, 0, NULL,
#endif
	"socket",	dosock,		0, 0, NULL,
#ifdef	SOUND
	"sound",	dosound,	0, 2,
		"sound attach|detach|listen ...",
#endif
#ifdef	SERVERS
	"start",	dostart,	0, 2, "start <servername>",
	"stop",		dostop,		0, 2, "stop <servername>",
#endif
	"tcp",		dotcp,		0, 0, NULL,
	"telnet",	dotelnet,	1024, 2, "telnet <address>",
#ifndef	notdef
	"test",		dotest,		1024, 0, NULL,
#endif
	"tip",		dotip,		256, 2, "tip <iface>",
	"topt",		dotopt,		0, 0, NULL,
#ifdef	TRACE
	"trace",	dotrace,	512, 0, NULL,
#endif
	"udp",		doudp,		0, 0, NULL,
	"upload",	doupload,	0, 0, NULL,
	"view",		doview,		0, 2, "view <filename>",
#ifdef	MSDOS
	"watch",	doswatch,	0, 0, NULL,
#endif
	"wipe",		dowipe,		0, 0, NULL,
	"?",		dohelp,		0, 0, NULL,
	NULL,	NULL,		0, 0,
		"Unknown command; type \"?\" for list",
};

/* List of supported hardware devices */
struct cmds Attab[] = {
#ifdef	ASY
	/* Ordinary PC asynchronous adaptor */
	"asy", asy_attach, 0, 8,
#ifndef	AMIGA
	"attach asy <address> <vector> slip|vjslip|ax25ui|ax25i|nrs|ppp <label> <buffers> <mtu> <speed> [ip_addr]",
#else
	"attach asy <driver> <unit> slip|vjslip|ax25ui|ax25i|nrs|ppp <label> <buffers> <mtu> <speed> [ip_addr]",
#endif	/* AMIGA */
#endif	/* ASY */
#ifdef	PC100
	/* PACCOMM PC-100 8530 HDLC adaptor */
	"pc100", pc_attach, 0, 8,
	"attach pc100 <address> <vector> ax25ui|ax25i <label> <buffers>\
 <mtu> <speed> [ip_addra] [ip_addrb]",
#endif
#ifdef	CDMA_DM
	"dm", dm_attach, 0, 8,
	"attach dm <address> <vector> <rxdrq> <txdrq> <label> <rxbuf> <mtu> <speed>",
#endif
#ifdef	DRSI
	/* DRSI PCPA card in low speed mode */
	"drsi", dr_attach, 0, 8,
	"attach drsi <address> <vector> ax25ui|ax25i <label> <bufsize> <mtu>\
<chan a speed> <chan b speed> [ip addr a] [ip addr b]",
#endif
#ifdef	EAGLE
	/* EAGLE RS-232C 8530 HDLC adaptor */
	"eagle", eg_attach, 0, 8,
	"attach eagle <address> <vector> ax25ui|ax25i <label> <buffers>\
 <mtu> <speed> [ip_addra] [ip_addrb]",
#endif
#ifdef	PI
	/* PI 8530 HDLC adaptor */
	"pi", pi_attach, 0, 8,
	"attach pi <address> <vector> <dmachannel> ax25ui|ax25i <label> <buffers>\
 <mtu> <speed> [ip_addra] [ip_addrb]",
#endif
#ifdef	HAPN
	/* Hamilton Area Packet Radio (HAPN) 8273 HDLC adaptor */
	"hapn", hapn_attach, 0, 8,
	"attach hapn <address> <vector> ax25ui|ax25i <label> <rx bufsize>\
 <mtu> csma|full [ip_addr]",
#endif
#ifdef	APPLETALK
	/* Macintosh AppleTalk */
	"0", at_attach, 0, 7,
	"attach 0 <protocol type> <device> arpa <label> <rx bufsize> <mtu> [ip_addr]",
#endif
#ifdef NETROM
	/* fake netrom interface */
	"netrom", nr_attach, 0, 1,
	"attach netrom [ip_addr]",
#endif
#ifdef	PACKET
	/* FTP Software's packet driver spec */
	"packet", pk_attach, 0, 4,
	"attach packet <int#> <label> <buffers> <mtu> [ip_addr]",
#endif
#ifdef	QTSO
	/* CDMA QTSO data interface */
	"qtso",	qtso_attach, 0, 2,
	"attach qtso <label> <com_port_label> [<com_port_label> ...]",
#endif

#ifdef	HS
	/* Special high speed driver for DRSI PCPA or Eagle cards */
	"hs", hs_attach, 0, 7,
	"attach hs <address> <vector> ax25ui|ax25i <label> <buffers> <mtu>\
 <txdelay> <persistence> [ip_addra] [ip_addrb]",
#endif
#ifdef SCC
	"scc", scc_attach, 0, 7,
	"attach scc <devices> init <addr> <spacing> <Aoff> <Boff> <Dataoff>\n"
	"   <intack> <vec> [p]<clock> [hdwe] [param]\n"
	"attach scc <chan> slip|kiss|nrs|ax25ui|ax25i <label> <mtu> <speed> <bufsize> [call] ",
#endif
#ifdef	ASY
	"4port",fp_attach, 0, 3, "attach 4port <base> <irq>",
#endif
#ifdef	KSP
	"ksp", ksp_attach, 0, 5,
	"attach ksp <base> <irq> <label> <mtu>",
#endif

	NULL,
};

#ifdef	SERVERS
/* "start" and "stop" subcommands */
static struct cmds Startcmds[] = {
#if	defined(AX25) && defined(MAILBOX)
	"ax25",		ax25start,	256, 0, NULL,
#endif
	"bsr",		bsr1,		256, 2, "start bsr <interface> [<port>]",
	"discard",	dis1,		256, 0, NULL,
	"echo",		echo1,		256, 0, NULL,
#ifdef	QFAX
	"fax",		fax1,		256, 0, NULL,
#endif
	"finger",	finstart,	256, 0, NULL,
	"ftp",		ftpstart,	256, 0, NULL,
#if	defined(NETROM) && defined(MAILBOX)
	"netrom",	nr4start,	256, 0, NULL,
#endif
#ifdef POP
	"pop",		pop1,		256, 0, NULL,
#endif
#ifdef	RIP
	"rip",		doripinit,	0,   0, NULL,
#endif
#ifdef	SMTP
	"smtp",		smtp1,		256, 0, NULL,
#endif
#if	defined(MAILBOX)
	"telnet",	telnet1,	256, 0, NULL,
	"tip",		tipstart,	256, 2, "start tip <interface>",
#endif
	"term",		term1,		256, 0, NULL,
	"ttylink",	ttylstart,	256, 0, NULL,
	"remote",	rem1,		768, 0, NULL,
	NULL,
};

static struct cmds Stopcmds[] = {
#if	defined(AX25) && defined(MAILBOX)
	"ax25",		ax250,		0, 0, NULL,
#endif
	"bsr",		bsr0,		0, 0, NULL,
	"discard",	dis0,		0, 0, NULL,
	"echo",		echo0,		0, 0, NULL,
#if	defined(QFAX)
	"fax",		fax0,		0, 0, NULL,
#endif
	"finger",	fin0,		0, 0, NULL,
	"ftp",		ftp0,		0, 0, NULL,
#if	defined(NETROM) && defined(MAILBOX)
	"netrom",	nr40,		0, 0, NULL,
#endif
#ifdef	POP
	"pop",		pop0,		0, 0, NULL,
#endif
#ifdef	RIP
	"rip",		doripstop,	0, 0, NULL,
#endif
#ifdef	SMTP
	"smtp",		smtp0,		0, 0, NULL,
#endif
#ifdef	MAILBOX
	"telnet",	telnet0,	0, 0, NULL,
	"tip",		tip0,		0, 2, "stop tip <interface>",
#endif
	"term",		term0,		0, 0, NULL,
	"ttylink",	ttyl0,		0, 0, NULL,
	"remote",	rem0,		0, 0, NULL,
	NULL,
};
#endif	/* SERVERS */

/* Socket-protocol interface table */
struct socklink Socklink[] = {
	/* type,
	 * socket,	bind,		listen,		connect,
	 * accept,	recv,		send,		qlen,
	 * kick,	shut,		close,		check,
	 * error,	state,		status,		eol_seq
	 */
	TYPE_TCP,
	so_tcp,		NULL,		so_tcp_listen,	so_tcp_conn,
	TRUE,		so_tcp_recv,	so_tcp_send,	so_tcp_qlen,
	so_tcp_kick,	so_tcp_shut,	so_tcp_close,	checkipaddr,
	Tcpreasons,	tcpstate,	so_tcp_stat,	Inet_eol,

	TYPE_UDP,
	so_udp,		so_udp_bind,	NULL,		so_udp_conn,
	FALSE,		so_udp_recv,	so_udp_send,	so_udp_qlen,
	NULL,		NULL,		so_udp_close,	checkipaddr,
	NULL,		NULL,		so_udp_stat,	Inet_eol,

#ifdef	AX25
	TYPE_AX25I,
	so_ax_sock,	NULL,		so_ax_listen,	so_ax_conn,
	TRUE,		so_ax_recv,	so_ax_send,	so_ax_qlen,
	so_ax_kick,	so_ax_shut,	so_ax_close,	checkaxaddr,
	Axreasons,	axstate,	so_ax_stat,	Ax25_eol,

	TYPE_AX25UI,
	so_axui_sock,	so_axui_bind,	NULL,		so_axui_conn,
	FALSE,		so_axui_recv,	so_axui_send,	so_axui_qlen,
	NULL,		NULL,		so_axui_close,	checkaxaddr,
	NULL,		NULL,		NULL,		Ax25_eol,
#endif	/* AX25 */

	TYPE_RAW,
	so_ip_sock,	NULL,		NULL,		so_ip_conn,
	FALSE,		so_ip_recv,	so_ip_send,	so_ip_qlen,
	NULL,		NULL,		so_ip_close,	checkipaddr,
	NULL,		NULL,		NULL,		Inet_eol,

#ifdef	NETROM
	TYPE_NETROML3,
	so_n3_sock,	NULL,		NULL,		so_n3_conn,
	FALSE,		so_n3_recv,	so_n3_send,	so_n3_qlen,
	NULL,		NULL,		so_n3_close,	checknraddr,
	NULL,		NULL,		NULL,		Ax25_eol,

	TYPE_NETROML4,
	so_n4_sock,	NULL,		so_n4_listen,	so_n4_conn,
	TRUE,		so_n4_recv,	so_n4_send,	so_n4_qlen,
	so_n4_kick,	so_n4_shut,	so_n4_close,	checknraddr,
	Nr4reasons,	nrstate,	so_n4_stat,	Ax25_eol,
#endif	/* NETROM */

#ifdef	LOCSOCK
	TYPE_LOCAL_STREAM,
	so_los,		NULL,		NULL,		NULL,
	TRUE,		so_lo_recv,	so_los_send,	so_los_qlen,
	NULL,		so_loc_shut,	so_loc_close,	NULL,
	NULL,		NULL,		so_loc_stat,	Eol,

	TYPE_LOCAL_DGRAM,
	so_lod,		NULL,		NULL,		NULL,
	FALSE,		so_lo_recv,	so_lod_send,	so_lod_qlen,
	NULL,		so_loc_shut,	so_loc_close,	NULL,
	NULL,		NULL,		so_loc_stat,	Eol,
#endif

	-1
};

/* Table of functions for printing socket addresses */
char * (*Psock[]) () = {
	ippsocket,
#ifdef	AX25
	axpsocket,
#else
	NULL,
#endif
#ifdef	NETROM
	nrpsocket,
#else
	NULL,
#endif
#ifdef	LOCSOCK
	lopsocket,
#else
	NULL,
#endif
};

/* TCP port numbers to be considered "interactive" by the IP routing
 * code and given priority in queueing
 */
int Tcp_interact[] = {
	IPPORT_FTP,	/* FTP control (not data!) */
	IPPORT_TELNET,	/* Telnet */
	6000,		/* X server 0 */
	IPPORT_LOGIN,	/* BSD rlogin */
	IPPORT_MTP,	/* Secondary telnet */
	-1
};
int (*Kicklist[])() = {
	kick,
#ifdef	SMTP
	smtptick,
#endif
	NULL
};

/* Transport protocols atop IP */
struct iplink Iplink[] = {
	TCP_PTCL,	"TCP",	tcp_input,	tcp_dump,
	UDP_PTCL,	"UDP",	udp_input,	udp_dump,
	ICMP_PTCL,	"ICMP",	icmp_input,	icmp_dump,
	IP_PTCL,	"IP",	ipip_recv,	ipip_dump,
	IP4_PTCL,	"IP",	ipip_recv,	ipip_dump,
#ifdef	IPSEC
	ESP_PTCL,	"ESP",	esp_input,	esp_dump,
	AH_PTCL,	"AH",	ah_input,	ah_dump,
#endif
	0,		NULL,	NULL,		NULL
};

/* Transport protocols atop ICMP */
struct icmplink Icmplink[] = {
	TCP_PTCL,	tcp_icmp,
#ifdef	IPSEC
	ESP_PTCL,	esp_icmp,
/*	AH_PTCL,	ah_icmp, */
#endif
	0,		0
};

#ifdef	AX25
/* Linkage to network protocols atop ax25 */
struct axlink Axlink[] = {
	PID_IP,		axip,
	PID_ARP,	axarp,
#ifdef	NETROM
	PID_NETROM,	axnr,
#endif
	PID_NO_L3,	axnl3,
	0,		NULL,
};
#endif	/* AX25 */

/* ARP protocol linkages, indexed by arp's hardware type */
struct arp_type Arp_type[NHWTYPES] = {
#ifdef	NETROM
	AXALEN, 0, 0, 0, NULL, pax25, setcall,	/* ARP_NETROM */
#else
	0, 0, 0, 0, NULL,NULL,NULL,
#endif

#ifdef	ETHER
	EADDR_LEN,IP_TYPE,ARP_TYPE,1,Ether_bdcst,pether,gether, /* ARP_ETHER */
#else
	0, 0, 0, 0, NULL,NULL,NULL,
#endif

	0, 0, 0, 0, NULL,NULL,NULL,			/* ARP_EETHER */

#ifdef	AX25
	AXALEN, PID_IP, PID_ARP, 10, Ax25multi[0], pax25, setcall,
#else
	0, 0, 0, 0, NULL,NULL,NULL,			/* ARP_AX25 */
#endif

	0, 0, 0, 0, NULL,NULL,NULL,			/* ARP_PRONET */

	0, 0, 0, 0, NULL,NULL,NULL,			/* ARP_CHAOS */

	0, 0, 0, 0, NULL,NULL,NULL,			/* ARP_IEEE802 */

#ifdef	ARCNET
	AADDR_LEN, ARC_IP, ARC_ARP, 1, ARC_bdcst, parc, garc, /* ARP_ARCNET */
#else
	0, 0, 0, 0, NULL,NULL,NULL,
#endif

	0, 0, 0, 0, NULL,NULL,NULL,			/* ARP_APPLETALK */
};
/* Get rid of trace references in Iftypes[] if TRACE is turned off */
#ifndef	TRACE
#define	ip_dump		NULL
#define	ax25_dump	NULL
#define	ki_dump		NULL
#define	sl_dump		NULL
#define	ether_dump	NULL
#define	ppp_dump	NULL
#define	arc_dump	NULL
#endif	/* TRACE */

/* Table of interface types. Contains most device- and encapsulation-
 * dependent info
 */
struct iftype Iftypes[] = {
	/* This entry must be first, since Loopback refers to it */
	"None",		nu_send,	nu_output,	NULL,
	NULL,		CL_NONE,	0,		ip_proc,
	NULL,		ip_dump,	NULL,		NULL,

#ifdef	AX25
	"AX25UI",	axui_send,	ax_output,	pax25,
	setcall,	CL_AX25,	AXALEN,		ax_recv,
	ax_forus,	ax25_dump,	NULL,		NULL,

	"AX25I",	axi_send,	ax_output,	pax25,
	setcall,	CL_AX25,	AXALEN,		ax_recv,
	ax_forus,	ax25_dump,	NULL,		NULL,
#endif	/* AX25 */

#ifdef	KISS
	"KISSUI",	axui_send,	ax_output,	pax25,
	setcall,	CL_AX25,	AXALEN,		kiss_recv,
	ki_forus,	ki_dump,	NULL,		NULL,

	"KISSI",	axi_send,	ax_output,	pax25,
	setcall,	CL_AX25,	AXALEN,		kiss_recv,
	ki_forus,	ki_dump,	NULL,		NULL,
#endif	/* KISS */

#ifdef	SLIP
	"SLIP",		slip_send,	NULL,		NULL,
	NULL,		CL_NONE,	0,		ip_proc,
	NULL,		ip_dump,
#ifdef	DIALER
					sd_init,	sd_stat,
#else
					NULL,		NULL,
#endif
#endif	/* SLIP */

#ifdef	VJCOMPRESS
	"VJSLIP",	vjslip_send,	NULL,		NULL,
	NULL,		CL_NONE,	0,		ip_proc,
	NULL,		sl_dump,
#ifdef	DIALER
					sd_init,	sd_stat,
#else
					NULL,		NULL,
#endif
#endif	/* VJCOMPRESS */

#ifdef	ETHER
	/* Note: NULL is specified for the scan function even though
	 * gether() exists because the packet drivers don't support
	 * address setting.
	 */
	"Ethernet",	enet_send,	enet_output,	pether,
	NULL,		CL_ETHERNET,	EADDR_LEN,	eproc,
	ether_forus,	ether_dump,	NULL,		NULL,
#endif	/* ETHER */

#ifdef	NETROM
	"NETROM",	nr_send,	NULL,		pax25,
	setcall,	CL_NETROM,	AXALEN,		NULL,
	NULL,		NULL,	NULL,		NULL,
#endif	/* NETROM */

#ifdef	SLFP
	"SLFP",		pk_send,	NULL,		NULL,
	NULL,		CL_NONE,	0,		ip_proc,
	NULL,		ip_dump,	NULL,		NULL,
#endif	/* SLFP */

#ifdef	PPP
	"PPP",		ppp_send,	ppp_output,	NULL,
	NULL,		CL_PPP,		0,		ppp_proc,
	NULL,		ppp_dump,	NULL,		NULL,
#endif	/* PPP */

#ifdef	SPPP
	"sppp",		sppp_send,	NULL,		NULL,
	NULL,		CL_NONE,	0,		ip_proc,
	NULL,		ip_dump,	NULL,		NULL,
#endif	/* SPPP */

#ifdef	ARCNET
	"Arcnet",	anet_send,	anet_output,	parc,
	garc,		CL_ARCNET,	1,		aproc,
	arc_forus,	arc_dump,	NULL,		NULL,
#endif	/* ARCNET */

#ifdef	QTSO
	"QTSO",		qtso_send,	NULL,		NULL,
	NULL,		CL_NONE,	0,		ip_proc,
	NULL,		NULL,	NULL,		NULL,
#endif	/* QTSO */

#ifdef	CDMA_DM
	"CDMA",		rlp_send,	NULL,		NULL,
	NULL,		CL_NONE,	0,		ip_proc,
	NULL,		ip_dump,	dd_init,	dd_stat,
#endif

#ifdef	DMLITE
	"DMLITE",	rlp_send,	NULL,		NULL,
	NULL,		CL_NONE,	0,		ip_proc,
	NULL,		ip_dump,	dl_init,	dl_stat,
#endif

	NULL,	NULL,		NULL,		NULL,
	NULL,		-1,		0,		NULL,
	NULL,		NULL,	NULL,		NULL,
};

/* Asynchronous interface mode table */
#ifdef	ASY
struct asymode Asymode[] = {
#ifdef	SLIP
	"SLIP",		FR_END,		slip_init,	slip_free,
	"VJSLIP",	FR_END,		slip_init,	slip_free,
#endif
#ifdef	KISS
	"AX25UI",	FR_END,		kiss_init,	kiss_free,
	"AX25I",	FR_END,		kiss_init,	kiss_free,
	"KISSUI",	FR_END,		kiss_init,	kiss_free,
	"KISSI",	FR_END,		kiss_init,	kiss_free,
#endif
#ifdef	NRS
	"NRS",		ETX,		nrs_init,	nrs_free,
#endif
#ifdef	PPP
	"PPP",		HDLC_FLAG,	ppp_init,	ppp_free,
#endif
#ifdef	SPPP
	"SPPP",		HDLC_FLAG,	sppp_init,	sppp_free,
#endif
#ifdef	QTSO
	"QTSO",		HDLC_FLAG,	qtso_init,	qtso_free,
#endif
#ifdef	DMLITE
	"DMLITE",	HDLC_FLAG,	dml_init,	dml_stop,
#endif
	NULL
};

#else	/* not ASY */
/* Stubs for refs to asy I/O in stdio when ASY not configured */
int
asy_open(name)
char *name;
{
	return -1;
}
int
asy_close(dev)
int dev;
{
}
asy_write(dev,buf,cnt)
int dev;
char *buf;
int cnt;
{
	return -1;
}
int
asy_read(dev,buf,cnt);
int dev;
char *buf;
int cnt;
{
	return -1;
}
int
asy_close(dev)
int dev;
{
	return -1;
}
#endif	/* ASY */

#ifndef	IPSEC
void
rtype(uint16 c)
{
}
#endif

/* daemons to be run at startup time */
struct daemon Daemons[] = {
	"killer",	512,	killer,
	"gcollect",	256,	gcollect,
	"timer",	1024,	timerproc,
	"network",	1536,	network,
	"keyboard",	250,	keyboard,
	"random init",	650,	rand_init,
#ifdef	PHOTURIS
	"keygen",	2048,	gendh,
	"key mgmt",	2048,	phot_proc,
#endif
	NULL,	0,	NULL
};

/* Functions to be called on each clock tick */
void (*Cfunc[])() = {
	pctick,	/* Call PC-specific stuff to keep time */
	sesflush,	/* Flush current session output */
#ifdef	ASY
	asytimer,
#endif
#ifdef	SCC
	scctimer,
#endif
	NULL,
};

/* Entry points for garbage collection */
void (*Gcollect[])() = {
	tcp_garbage,
	ip_garbage,
	udp_garbage,
	st_garbage,
	mbuf_garbage,
#ifdef	AX25
	lapb_garbage,
#endif
#ifdef	NETROM
	nr_garbage,
#endif
	NULL
};

/* Functions to be called at shutdown */
void (*Shutdown[])() = {
#ifdef	ASY
	fp_stop,
#endif
#ifdef	SCC
	sccstop,
#endif
#ifdef	SOUND
	sbshut,
#endif
	uchtimer,	/* Unlink timer handler from timer chain */
	NULL,
};

#ifdef	MAILBOX
void (*Listusers)(FILE *network) = listusers;
#else
void (*Listusers)(FILE *network) = NULL;
#endif	/* MAILBOX */

#ifndef	BOOTP
int WantBootp = 0;

int
bootp_validPacket(ip,bp)
struct ip *ip;
struct mbuf *bp;
{
	return 0;
}
#endif	/* BOOTP */

/* Packet tracing stuff */
#ifdef	TRACE
#include "trace.h"

#else	/* TRACE */

/* Stub for packet dump function */
void
dump(iface,direction,type,bp)
struct iface *iface;
int direction;
unsigned type;
struct mbuf *bp;
{
}
void
raw_dump(iface,direction,bp)
struct iface *iface;
int direction;
struct mbuf *bp;
{
}
#endif	/* TRACE */

#ifndef	TRACEBACK
void
stktrace()
{
}
#endif

#ifndef	LZW
void
lzwfree(up)
struct usock *up;
{
}
#endif

#ifdef	AX25
/* Hooks for passing incoming AX.25 data frames to network protocols */
static void
axip(
struct iface *iface,
struct ax25_cb *axp,
uint8 *src,
uint8 *dest,
struct mbuf **bpp,
int mcast
){
	(void)ip_route(iface,bpp,mcast);
}

static void
axarp(
struct iface *iface,
struct ax25_cb *axp,
uint8 *src,
uint8 *dest,
struct mbuf **bpp,
int mcast
){
	(void)arp_input(iface,bpp);
}

#ifdef	NETROM
static void
axnr(
struct iface *iface,
struct ax25_cb *axp,
uint8 *src,
uint8 *dest,
struct mbuf **bpp, 
int mcast
){
	if(!mcast)
		nr_route(bpp,axp);
	else
		nr_nodercv(iface,src,bpp);
}

#endif	/* NETROM */
#endif	/* AX25 */

#ifndef	RIP
/* Stub for routing timeout when RIP is not configured -- just remove entry */
void
rt_timeout(s)
void *s;
{
	struct route *stale = (struct route *)s;

	rt_drop(stale->target,stale->bits);
}
#endif

/* Stubs for demand dialer */
#ifndef	DIALER
void
dialer_kick(asyp)
struct asy *asyp;
{
}
#endif

/* Stubs for Van Jacobsen header compression */
#if !defined(VJCOMPRESS) && defined(ASY)
struct slcompress *
slhc_init(rslots,tslots)
int rslots;
int tslots;
{
	return NULLSLCOMPR;
}
int
slhc_compress(comp, bpp, compress_cid)
struct slcompress *comp;
struct mbuf **bpp;
int compress_cid;
{
	return SL_TYPE_IP;
}
int
slhc_uncompress(comp, bpp)
struct slcompress *comp;
struct mbuf **bpp;
{
	return -1;	/* Can't decompress */
}
void
shlc_i_status(comp)
struct slcompress *comp;
{
}
void
shlc_o_status(comp)
struct slcompress *comp;
{
}
int
slhc_remember(comp, bpp)
struct slcompress *comp;
struct mbuf **bpp;
{
	return -1;
}
#endif /* !defined(VJCOMPRESS) && defined(ASY) */

#ifdef	SERVERS
static int
dostart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Startcmds,argc,argv,p);
}
static int
dostop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Stopcmds,argc,argv,p);
}
#endif	/* SERVERS */

dotest(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int32 stime,ftime;
	char *bufa,*bufb;
	int i;

	bufa = malloc(1024);
	bufb = malloc(1024);
	stime = msclock();
	for(i=0;i<1000;i++)
		memcpy(bufa,bufb,1024);
	ftime = msclock();
	printf("Time for 1000 1024-byte copies: %lu ms\n",ftime - stime);
	return 0;
}
