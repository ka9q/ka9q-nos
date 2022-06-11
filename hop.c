/*
 *	HOP.C   -- trace route packets take to a remote host
 *
 *	02-90	-- Katie Stevens (dkstevens@ucdavis.edu)
 *		   UC Davis, Computing Services
 *		   Davis, CA
 *	04-90	-- Modified by Phil Karn to use raw IP sockets to read replies
 *	08-90	-- Modified by Bill Simpson to display domain names
 */

#include <stdio.h>
#include <string.h>
#include "global.h"
#include "mbuf.h"
#include "usock.h"
#include "socket.h"
#include "session.h"
#include "timer.h"
#include "proc.h"
#include "netuser.h"
#include "domain.h"
#include "commands.h"
#include "tty.h"
#include "cmdparse.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "hardware.h"

#define HOPMAXQUERY	5		/* Max# queries each TTL value */
static uint16 Hoprport = 32768+666;	/* funny port for udp probes */
#define HOP_HIGHBIT	32768		/* Mask to check ICMP msgs */


#define HOPTRACE	1		/* Enable HOP tracing */
#ifdef HOPTRACE
static int Hoptrace = 0;
static int hoptrace(int argc,char *argv[],void *p);
#endif


static unsigned  short Hopmaxttl  = 30;		/* max attempts */
static unsigned  short Hopmaxwait = 5;		/* secs timeout each attempt */
static unsigned  short Hopquery   = 3;		/* #probes each attempt */

static int hopcheck(int argc,char *argv[],void *p);
static int hopttl(int argc,char *argv[],void *p);
static int hokwait(int argc,char *argv[],void *p);
static int hopnum(int argc,char *argv[],void *p);
static int geticmp(int s,uint16 lport,uint16 fport,
	int32 *sender,char *type,char *code);
static int keychar(int c);

static struct cmds Hopcmds[] = {
	"check",	hopcheck,	2048,	2,	"check <host>",
	"maxttl",	hopttl,		0,	0,	NULL,
	"maxwait",	hokwait,	0,	0,	NULL,
	"queries",	hopnum,		0,	0,	NULL,
#ifdef HOPTRACE
	"trace",	hoptrace,	0,	0,	NULL,
#endif
	NULL,
};

/* attempt to trace route to a remote host */
int
dohop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Hopcmds,argc,argv,p);
}

/* Set/show # queries sent each TTL value */
static int
hopnum(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 r;
	uint16 x = Hopquery;
	r = setshort(&x,"# queries each attempt",argc,argv);
	if ((x <= 0)||(x > HOPMAXQUERY)) {
		printf("Must be  0 < x <= %d\n",HOPMAXQUERY);
		return 0;
	} else {
		Hopquery = x;
	}
    return (int)r;
}
#ifdef HOPTRACE
/* Set/show tracelevel */
static int
hoptrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Hoptrace,"HOPCHECK tracing",argc,argv);
}
#endif
/* Set/show maximum TTL value for a traceroute query */
static int
hopttl(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 r;
	uint16 x = Hopmaxttl;
	r = setshort(&x,"Max attempts to reach host",argc,argv);
	if ((x <= 0)||(x > 255)) {
		printf("Must be  0 < x <= 255\n");
		return 0;
	} else {
		Hopmaxttl = x;
	}
    return (int)r;
}
/* Set/show #secs until timeout for a traceroute query */
static int
hokwait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 r;
	uint16 x = Hopmaxwait;
	r = setshort(&x,"# secs to wait for reply to query",argc,argv);
	if (x <= 0) {
		printf("Must be >= 0\n");
		return 0;
	} else {
		Hopmaxwait = x;
	}
    return (int)r;
}

/* send probes to trace route of a remote host */
static int
hopcheck(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;		/* Session for trace output */
	int s;				/* Socket for queries */
	int s1;				/* Raw socket for replies */
	struct socket lsocket;		/* Local socket sending queries */
	struct socket rsocket;		/* Final destination of queries */
	int32 cticks;			/* Timer for query replies */
	int32 icsource;			/* Sender of last ICMP reply */
	char ictype;			/* ICMP type last ICMP reply */
	char iccode;			/* ICMP code last ICMP reply */
	int32 lastaddr;			/* Sender of previous ICMP reply */
	struct sockaddr_in sock;
	register struct usock *usp;
	register struct sockaddr_in *sinp;
	unsigned char sndttl, q;
	int tracedone = 0;
	int ilookup = 1;		/* Control of inverse domain lookup */
	int c;
	extern int optind;
	char *hostname;
	int save_trace;
	int user_reset = 0;

	optind = 1;
	while((c = getopt(argc,argv,"n")) != EOF){
		switch(c){
		case 'n':
			ilookup = 0;
			break;
		}
	}
	hostname = argv[optind];
	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,HOP,1)) == NULL){
		printf("Too many sessions\n");
		keywait(NULL,1);
		return 1;
	}
	sp->inproc = keychar;
	s = -1;

	/* Setup UDP socket to remote host */
	sock.sin_family = AF_INET;
	sock.sin_port = Hoprport;
	printf("Resolving %s... ",hostname);
	if((sock.sin_addr.s_addr = resolve(hostname)) == 0){
		printf("unknown\n",hostname);
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}

	/* Open socket to remote host */
	printf("%s ",psocket((struct sockaddr *)&sock));
	if((s = socket(AF_INET,SOCK_DGRAM,0)) == -1){
		printf("Can't create udp socket\n");
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	if(connect(s,(struct sockaddr *)&sock,sizeof(sock)) == -1){
		printf("Connect failed\n");
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	if((s1 = socket(AF_INET,SOCK_RAW,ICMP_PTCL)) == -1){
		printf("Can't create raw socket\n");
		keywait(NULL,1);
		close(s);
		freesession(sp);
		return 1;
	}
	printf("\n");
	/* turn off icmp tracing while hop-checking */
	save_trace = Icmp_trace;
	Icmp_trace = 0;

	/* Setup structures to send queries */
	/* Retrieve socket details for user socket control block */
	usp = itop(s);
	sinp = (struct sockaddr_in *)usp->name;
	lsocket.address = sinp->sin_addr.s_addr;
	lsocket.port = sinp->sin_port;
	sinp = (struct sockaddr_in *)usp->peername;
	rsocket.address = sinp->sin_addr.s_addr;

	/* Send queries with increasing TTL; start with TTL=1 */
	if (Hoptrace)
		logmsg(s,"HOPCHECK start trace to %s\n",sp->name);
	for (sndttl=1; (sndttl < Hopmaxttl); ++sndttl, sinp->sin_port++) {
		/* Increment funny UDP port number each round */
		rsocket.port = sinp->sin_port;
		printf("%3d:",sndttl);
		lastaddr = (int32)0;
		/* Send a round of queries */
		for (q=0; (q < Hopquery); ++q) {
			struct mbuf *bp;
			bp = ambufw(0);
			send_udp(&lsocket,&rsocket,0,sndttl,&bp,0,0,0);
			cticks = msclock();
			kalarm( ((long)Hopmaxwait*1000L) );

			/* Wait for a reply to our query */
			if(geticmp(s1,lsocket.port,rsocket.port,
			 &icsource,&ictype,&iccode) == -1){
				if(errno != EALARM){
					user_reset = 1;
					goto done;	/* User reset */
				}
				/* Alarm rang, give up waiting for replies */
				printf(" ***");
				continue;
			}
			/* Save #ticks taken for reply */
                        cticks = msclock() - cticks;
			/* Report ICMP reply */
			if (icsource != lastaddr) {
				struct rr *save_rrlp, *rrlp;

				if(lastaddr != (int32)0)
					printf("\n    ");
				printf(" %-15s",inet_ntoa(icsource));
				if(ilookup){
					for(rrlp = save_rrlp = inverse_a(icsource);
					    rrlp != NULL;
					    rrlp = rrlp->next){
						if(rrlp->rdlength > 0){
							switch(rrlp->type){
							case TYPE_PTR:
								printf(" %s", rrlp->rdata.name);
								goto got_name;
							case TYPE_A:
								printf(" %s", rrlp->name);
								goto got_name;
							}
#ifdef notdef
							if(rrlp->next != NULL)
								printf("\n%20s"," ");
#endif
						}
					}
					got_name: ;
					free_rr(save_rrlp);

				}
				lastaddr = icsource;
			}
                        printf(" (%ld ms)",cticks);
#ifdef HOPTRACE
			if (Hoptrace)
				logmsg(s,
				    "(hopcheck) ICMP from %s (%ldms) %s %s",
				    inet_ntoa(icsource),
				    cticks,
				    Icmptypes[ictype],
				    ((ictype == ICMP_TIME_EXCEED)?Exceed[iccode]:Unreach[iccode]));
#endif

			/* Check type of reply */
			if (ictype == ICMP_TIME_EXCEED)
				continue;
			/* Reply was: destination unreachable */
			switch(iccode) {
			case ICMP_PORT_UNREACH:
				++tracedone;
				break;
			case ICMP_NET_UNREACH:
				++tracedone;
				printf(" !N");
				break;
			case ICMP_HOST_UNREACH:
				++tracedone;
				printf(" !H");
				break;
			case ICMP_PROT_UNREACH:
				++tracedone;
				printf(" !P");
				break;
			case ICMP_FRAG_NEEDED:
				++tracedone;
				printf(" !F");
				break;
			case ICMP_ROUTE_FAIL:
				++tracedone;
				printf(" !S");
				break;
                        case ICMP_ADMIN_PROHIB:
                                ++tracedone;
                                printf(" !A");
                                break;
                        default:
                                printf(" !?");
                                break;
			}
		}
		/* Done with this round of queries */
		kalarm((long)0);
		printf("\n");
		/* Check if we reached remote host this round */
		if (tracedone != 0)
			break;
	}

	/* Done with traceroute */
done:	close(s);
	s = -1;
	close(s1);
	if(user_reset)
		printf("\n");	/* May have been in middle of line */
	printf("traceroute done: ");
	Icmp_trace = save_trace;
	if(user_reset){
		printf("user abort\n");
	} else if (sndttl >= Hopmaxttl) {
		printf("!! maximum TTL exceeded\n");
	} else if ((icsource == rsocket.address)
		    &&(iccode == ICMP_PORT_UNREACH)) {
		printf("normal (%s %s)\n",
			Icmptypes[ictype],Unreach[iccode]);
	} else {
		printf("!! %s %s\n",
			Icmptypes[ictype],Unreach[iccode]);
	}
#ifdef HOPTRACE
	if (Hoptrace)
		logmsg(s,"HOPCHECK to %s done",sp->name);
#endif
	keywait(NULL,1);
	freesession(sp);
	return 0;
}

/* Hop check session keyboard upcall routine -- handles ^C */
static int
keychar(c)
int c;
{
	switch(c){
	case CTLC:
		alert(Current->proc,EABORT);
		return 0;
	}
	return 1;
}

/* Read raw network socket looking for ICMP messages in response to our
 * UDP probes
 */
static int
geticmp(s,lport,fport,sender,type,code)
int s;
uint16 lport;
uint16 fport;
int32 *sender;
char *type,*code;
{
	int size;
	struct icmp icmphdr;
	struct ip iphdr;
	struct udp udphdr;
	struct mbuf *bp;
	struct sockaddr_in sock;

	for(;;){
		size = sizeof(sock);
		if(recv_mbuf(s,&bp,0,(struct sockaddr *)&sock,&size) == -1)
			return -1;
		/* It's an ICMP message, let's see if it's interesting */
		ntohicmp(&icmphdr,&bp);
		if((icmphdr.type != ICMP_TIME_EXCEED ||
		 icmphdr.code != ICMP_TTL_EXCEED)
		 && icmphdr.type != ICMP_DEST_UNREACH){
			/* We're not interested in these */
			free_p(&bp);
			continue;
		}
		ntohip(&iphdr,&bp);
		if(iphdr.protocol != UDP_PTCL){
			/* Not UDP, so can't be interesting */
			free_p(&bp);
			continue;
		}
		ntohudp(&udphdr,&bp);
		if(udphdr.dest != fport || udphdr.source != lport){
			/* Not from our hopcheck session */
			free_p(&bp);
			continue;
		}
		/* Passed all of our checks, so return it */
		*sender = sock.sin_addr.s_addr;
		*type = icmphdr.type;
		*code = icmphdr.code;
		free_p(&bp);
		return 0;
	}
}
