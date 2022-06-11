/* net/rom user command processing
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 */

#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "mailbox.h"
#include "netrom.h"
#include "nr4.h"
#include "timer.h"
#include "iface.h"
#include "lapb.h"
#include "cmdparse.h"
#include "session.h"
#include "socket.h"
#include "commands.h"

uint8 Nr4user[AXALEN];

char *Nr4states[] = {
	"Disconn",
	"Conn Pend",
	"Connected",
	"Disc Pend",
	"Listening"
} ;

char *Nr4reasons[] = {
	"Normal",
	"By Peer",
	"Timeout",
	"Reset",
	"Refused"
} ;
static int dobcnodes(int argc,char *argv[],void *p);
static int dointerface(int argc,char *argv[],void *p);
static int donfadd(int argc,char *argv[],void *p);
static int donfdrop(int argc,char *argv[],void *p);
static int donfdump(void);
static int donfmode(int argc,char *argv[],void *p);
static int donodefilter(int argc,char *argv[],void *p);
static void donodetick(void);
static int donodetimer(int argc,char *argv[],void *p);
static int donracktime(int argc,char *argv[],void *p);
static int donrchoketime(int argc,char *argv[],void *p);
static int donrconnect(int argc,char *argv[],void *p);
static int donrirtt(int argc,char *argv[],void *p);
static int donrkick(int argc,char *argv[],void *p);
static int dorouteadd(int argc,char *argv[],void *p);
static int doroutedrop(int argc,char *argv[],void *p);
static int donrqlimit(int argc,char *argv[],void *p);
static int donrreset(int argc,char *argv[],void *p);
static int donrretries(int argc,char *argv[],void *p);
static int donrroute(int argc,char *argv[],void *p);
static int donrstatus(int argc,char *argv[],void *p);
static int donrttl(int argc,char *argv[],void *p);
static int donruser(int argc,char *argv[],void *p);
static int donrverbose(int argc,char *argv[],void *p);
static int donrwindow(int argc,char *argv[],void *p);
static void doobsotick(void);
static int doobsotimer(int argc,char *argv[],void *p);

static struct cmds Nrcmds[] = {
	"acktime",	donracktime,	0, 0,	NULL,
	"bcnodes",	dobcnodes,	0, 2,	"netrom bcnodes <interface>",
	"connect",	donrconnect, 1024, 2,	"netrom connect <node>",
	"choketime",	donrchoketime,	0, 0,	NULL,
	"interface",	dointerface,	0, 4,
		"netrom interface <interface> <alias> <quality>",
	"irtt",		donrirtt,	0, 0,	NULL,
	"kick",		donrkick,	0, 2,	"netrom kick <&nrcb>",
	"nodefilter",	donodefilter,	0, 0,	NULL,
	"nodetimer",	donodetimer,	0, 0,	NULL,
	"obsotimer",	doobsotimer,	0, 0,	NULL,
	"qlimit",	donrqlimit,	0, 0,	NULL,
	"reset",	donrreset,	0, 2,	"netrom reset <&nrcb>",
	"retries",	donrretries,	0, 0,	NULL,
	"route",	donrroute,	0, 0,	NULL,
	"status",	donrstatus,	0, 0,	NULL,
	"ttl",		donrttl,	0, 0,	NULL,
	"user",		donruser,	0, 0,	NULL,
	"verbose",	donrverbose,	0, 0,	NULL,
	"window",	donrwindow,	0, 0,	NULL,
	NULL,
} ;

static struct timer Nodetimer ;	/* timer for nodes broadcasts */
static struct timer Obsotimer ;	/* timer for aging routes */

static int keychar(int c);

/* Command multiplexer */
int
donetrom(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return subcmd(Nrcmds,argc,argv,p) ;
}

static struct cmds Routecmds[] = {
	"add",	dorouteadd,	0, 6,
		"netrom route add <alias> <destination> <interface> <quality> <neighbor>",
	"drop",	doroutedrop, 0, 4,
		"netrom route drop <destination> <neighbor> <interface>",
	"info", dorouteinfo, 0, 2,
		"netrom route info <destination>",
	NULL,
} ;

/* Route command multiplexer */
static int
donrroute(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	if (argc < 2) {
		doroutedump() ;
		return 0 ;
	}
	return subcmd(Routecmds,argc,argv,p) ;
}

/* Dump a list of known routes */
int
doroutedump()
{
	register struct nrroute_tab *rp ;
	register int i, column ;
	char buf[16] ;
	char *cp ;
	
	column = 1 ;
	
	for (i = 0 ; i < NRNUMCHAINS ; i++)
		for (rp = Nrroute_tab[i] ; rp != NULL ; rp = rp->next) {
			strcpy(buf,rp->alias) ;
			/* remove trailing spaces */
			if ((cp = strchr(buf,' ')) == NULL)
				cp = &buf[strlen(buf)] ;
			if (cp != buf)		/* don't include colon for null alias */
				*cp++ = ':' ;
			pax25(cp,rp->call) ;
			printf("%-16s  ",buf) ;
			if (column++ == 4) {
				printf("\n");
				column = 1 ;
			}
		}

	if (column != 1)
		printf("\n") ;
		
	return 0 ;
}

/* print detailed information on an individual route */
int
dorouteinfo(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	register struct nrroute_tab *rp ;
	register struct nr_bind *bp ;
	register struct nrnbr_tab *np ;
	uint8 dest[AXALEN] ;
	char neighbor[AXBUF] ;

	if (setcall(dest,argv[1]) == -1) {
		printf ("bad destination name\n") ;
		return -1 ;
	}
		
	if ((rp = find_nrroute(dest)) == NULL) {
		printf("no such route\n") ;
		return -1 ;
	}

	for (bp = rp->routes ; bp != NULL ; bp = bp->next) {
		np = bp->via ;
		printf("%1s %3d  %3d  %-8s  %s\n",
		 (bp->flags & NRB_PERMANENT ? "P" :
		 bp->flags & NRB_RECORDED ? "R" : " "),
		 bp->quality,bp->obsocnt,
		 Nrifaces[np->iface].iface->name,
		 pax25(neighbor,np->call));
	}
	return 0 ;
}
		
/* convert a null-terminated alias name to a blank-filled, upcased */
/* version.  Return -1 on failure. */
int
putalias(to,from,complain)
register char *to, *from ;
int complain ;    
{
	int len, i ;
	
	if ((len = strlen(from)) > ALEN) {
		if (complain)
			printf ("alias too long - six characters max\n") ;
		return -1 ;
	}
	
	for (i = 0 ; i < ALEN ; i++) {
		if (i < len) {
			if (islower(*from))
				*to++ = toupper(*from++) ;
			else
				*to++ = *from++ ;
		}
		else
			*to++ = ' ' ;
	}
			
	*to = '\0' ;
	return 0 ;
}

/* Add a route */
static int
dorouteadd(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	char alias[AXALEN] ;
	uint8 dest[AXALEN] ;
	unsigned quality ;
	uint8 neighbor[AXALEN] ;
	register int i ;
	int naddr ;

	/* format alias (putalias prints error message if necessary) */
	if (putalias(alias,argv[1],1) == -1)
		return -1 ;

	/* format destination callsign */
	if (setcall(dest,argv[2]) == -1) {
		printf("bad destination callsign\n") ;
		return -1 ;
	}

	/* find interface */
	for (i = 0 ; i < Nr_numiface ; i++)
		if (!strcmp(Nrifaces[i].iface->name,argv[3]))
			break ;
	if (i == Nr_numiface) {
		printf("Interface \"%s\" not found\n",argv[3]) ;
		return -1 ;
	}
	
	/* get and check quality value */
	if ((quality = atoi(argv[4])) > 255) {
		printf("maximum route quality is 255\n") ;
		return -1 ;
	}

	/* Change from 871225 -- no digis in net/rom table */
	naddr = argc - 5 ;
	if (naddr > 1) {
		printf("Use the ax25 route command to specify digipeaters\n") ;
		return -1 ;
	}
	
	/* format neighbor address string */
	setcall(neighbor,argv[5]) ;

	return nr_routeadd(alias,dest,i,quality,neighbor,1,0) ;
}


/* drop a route */
static int
doroutedrop(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	uint8 dest[AXALEN], neighbor[AXALEN] ;
	register int i ;

	/* format destination and neighbor callsigns */
	if (setcall(dest,argv[1]) == -1) {
		printf("bad destination callsign\n") ;
		return -1 ;
	}
	if (setcall(neighbor,argv[2]) == -1) {
		printf("bad neighbor callsign\n") ;
		return -1 ;
	}

	/* find interface */
	for (i = 0 ; i < Nr_numiface ; i++)
		if (!strcmp(Nrifaces[i].iface->name,argv[3]))
			break ;
	if (i == Nr_numiface) {
		printf("Interface \"%s\" not found\n",argv[3]) ;
		return -1 ;
	}

	return nr_routedrop(dest,neighbor,i) ;
}
	
	
/* make an interface available to net/rom */
static int
dointerface(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	int i ;
	register struct iface *ifp ;

	if (Nr_iface == NULL) {
		printf("Attach netrom interface first\n") ;
		return 1 ;
	}
	
	if (Nr_numiface >= NRNUMIFACE) {
		printf("Only %d net/rom interfaces available\n",NRNUMIFACE) ;
		return 1 ;
	}
	
	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	for (i = 0 ; i < Nr_numiface ; i++)
		if (Nrifaces[i].iface == ifp) {
			printf("Interface \"%s\" is already registered\n",argv[1]) ;
			return 1 ;
		}
		
	Nrifaces[Nr_numiface].iface = ifp ;

	if (putalias(Nrifaces[Nr_numiface].alias,argv[2],1) == -1)
		return 1 ;
		
	if ((Nrifaces[Nr_numiface].quality = atoi(argv[3])) > 255) {
		printf("Quality cannot be greater than 255\n") ;
		return 1 ;
	}
		
	Nr_numiface++ ;			/* accept this interface */
	return 0 ;
}

/* Broadcast nodes list on named interface. */
static int
dobcnodes(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	register int i ;

	for (i = 0 ; i < Nr_numiface ; i++)
		if (!strcmp(Nrifaces[i].iface->name,argv[1]))
			break ;
	if (i == Nr_numiface) {
		printf("Interface \"%s\" not found\n",argv[1]) ;
		return 1 ;
	}
		
	nr_bcnodes(i) ;
	return 0;
}

/* Set outbound node broadcast interval */
static int
donodetimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		printf("Nodetimer %lu/%lu seconds\n",
			read_timer(&Nodetimer)/1000L,
			dur_timer(&Nodetimer)/1000L);
		return 0;
	}
	stop_timer(&Nodetimer) ;	/* in case it's already running */
	Nodetimer.func = (void (*)())donodetick;/* what to call on timeout */
	Nodetimer.arg = NULL;		/* dummy value */
	set_timer(&Nodetimer,atoi(argv[1])*1000L);/* set timer duration */
	start_timer(&Nodetimer);		/* and fire it up */
	return 0;
}

static void
donodetick()
{
	register int i ;

	for (i = 0 ; i < Nr_numiface ; i++)
		nr_bcnodes(i) ;

	/* Restart timer */
	start_timer(&Nodetimer) ;
}

/* Set timer for aging routes */
static int
doobsotimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		printf("Obsotimer %lu/%lu seconds\n",
			read_timer(&Obsotimer)/1000L,
			dur_timer(&Obsotimer)/1000L);
		return 0;
	}
	stop_timer(&Obsotimer) ;	/* just in case it's already running */
	Obsotimer.func = (void (*)())doobsotick;/* what to call on timeout */
	Obsotimer.arg = NULL;		/* dummy value */
	set_timer(&Obsotimer,atoi(argv[1])*1000L);	/* set timer duration */
	start_timer(&Obsotimer);		/* and fire it up */
	return 0;
}


/* Go through the routing table, reducing the obsolescence count of
 * non-permanent routes, and purging them if the count reaches 0
 */
static void
doobsotick()
{
	register struct nrnbr_tab *np ;
	register struct nrroute_tab *rp, *rpnext ;
	register struct nr_bind *bp, *bpnext ;
	int i ;

	for (i = 0 ; i < NRNUMCHAINS ; i++) {
		for (rp = Nrroute_tab[i] ; rp != NULL ; rp = rpnext) {
			rpnext = rp->next ; 	/* save in case we free this route */
			for (bp = rp->routes ; bp != NULL ; bp = bpnext) {
				bpnext = bp->next ;	/* in case we free this binding */
				if (bp->flags & NRB_PERMANENT)	/* don't age these */
					continue ;
				if (--bp->obsocnt == 0) {		/* time's up! */
					if (bp->next != NULL)
						bp->next->prev = bp->prev ;
					if (bp->prev != NULL)
						bp->prev->next = bp->next ;
					else
						rp->routes = bp->next ;
					rp->num_routes-- ;			/* one less binding */
					np = bp->via ;				/* find the neighbor */
					free(bp) ;				/* now we can free the bind */
					/* Check to see if we can free the neighbor */
					if (--np->refcnt == 0) {
						if (np->next != NULL)
							np->next->prev = np->prev ;
						if (np->prev != NULL)
							np->prev->next = np->next ;
						else {
							Nrnbr_tab[nrhash(np->call)] = np->next ;
						}
						free(np) ;	/* free the storage */
					}
				}
			}
			if (rp->num_routes == 0) {		/* did we free them all? */
				if (rp->next != NULL)
					rp->next->prev = rp->prev ;
				if (rp->prev != NULL)
					rp->prev->next = rp->next ;
				else
					Nrroute_tab[i] = rp->next ;

				free(rp) ;
			}
		}
	}

	start_timer(&Obsotimer) ;
}


static struct cmds Nfcmds[] = {
	"add",	donfadd,	0, 3,
		"netrom nodefilter add <neighbor> <interface>",
	"drop",	donfdrop,	0, 3,
		"netrom nodefilter drop <neighbor> <interface>",
	"mode",	donfmode,	0, 0,	NULL,
	NULL,	NULL,	0, 0,
		"nodefilter subcommands: add drop mode",
} ;

/* nodefilter command multiplexer */
static int
donodefilter(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	if (argc < 2) {
		donfdump() ;
		return 0 ;
	}
	return subcmd(Nfcmds,argc,argv,p) ;
}

/* display a list of <callsign,interface> pairs from the filter
 * list.
 */
static int
donfdump()
{
	int i, column = 1 ;
	struct nrnf_tab *fp ;
	char buf[16] ;

	for (i = 0 ; i < NRNUMCHAINS ; i++)
		for (fp = Nrnf_tab[i] ; fp != NULL ; fp = fp->next) {
			pax25(buf,fp->neighbor) ;
			printf("%-7s %-8s  ",
			 buf,Nrifaces[fp->iface].iface->name) ;
			if (column++ == 4) {
				printf("\n");
				column = 1 ;
			}
		}

	if (column != 1)
		printf("\n") ;

	return 0 ;
}

/* add an entry to the filter table */
static int
donfadd(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	uint8 neighbor[AXALEN] ;
	register int i ;

	/* format callsign */
	if (setcall(neighbor,argv[1]) == -1) {
		printf("bad neighbor callsign\n") ;
		return -1 ;
	}

	/* find interface */
	for (i = 0 ; i < Nr_numiface ; i++)
		if (!strcmp(Nrifaces[i].iface->name,argv[2]))
			break ;
	if (i == Nr_numiface) {
		printf("Interface \"%s\" not found\n",argv[2]) ;
		return -1 ;
	}

	return nr_nfadd(neighbor,i) ;
}

/* drop an entry from the filter table */
static int
donfdrop(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	uint8 neighbor[AXALEN] ;
	register int i ;

	/* format neighbor callsign */
	if (setcall(neighbor,argv[1]) == -1) {
		printf("bad neighbor callsign\n") ;
		return -1 ;
	}

	/* find interface */
	for (i = 0 ; i < Nr_numiface ; i++)
		if (!strcmp(Nrifaces[i].iface->name,argv[2]))
			break ;
	if (i == Nr_numiface) {
		printf("Interface \"%s\" not found\n",argv[2]) ;
		return -1 ;
	}

	return nr_nfdrop(neighbor,i) ;
}

/* nodefilter mode subcommand */
static int
donfmode(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	if (argc < 2) {
		printf("filter mode is ") ;
		switch (Nr_nfmode) {
			case NRNF_NOFILTER:
				printf("none\n") ;
				break ;
			case NRNF_ACCEPT:
				printf("accept\n") ;
				break ;
			case NRNF_REJECT:
				printf("reject\n") ;
				break ;
			default:
				printf("some strange, unknown value\n") ;
		}
		return 0 ;
	}
	
	switch (argv[1][0]) {
		case 'n':
		case 'N':
			Nr_nfmode = NRNF_NOFILTER ;
			break ;
		case 'a':
		case 'A':
			Nr_nfmode = NRNF_ACCEPT ;
			break ;
		case 'r':
		case 'R':
			Nr_nfmode = NRNF_REJECT ;
			break ;
		default:
			printf("modes are: none accept reject\n") ;
			return -1 ;
	}

	return 0 ;
}

/* netrom network packet time-to-live initializer */
static int
donrttl(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return setshort(&Nr_ttl,"Time to live",argc,argv);
}

/* verbose route broadcast */
static int
donrverbose(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return setbool(&Nr_verbose,"Verbose flag",argc,argv);
}

/* Initiate a NET/ROM transport connection */
static int
donrconnect(argc,argv,p)
int argc ;
char *argv[] ;
void *p;
{
	uint8 *np ;
	struct sockaddr_nr lsocket, fsocket;
	char alias[AXBUF];
	struct session *sp;
	int s;

	/* Get a session descriptor */
	if ((sp = newsession(Cmdline,NRSESSION,1)) == NULL) {
		printf("Too many sessions\n") ;
		return 1 ;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	if((s = socket(AF_NETROM,SOCK_SEQPACKET,0)) == -1){
		printf("Can't create socket\n");
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	lsocket.nr_family = AF_NETROM;
	/* Set up our local username, bind would use Mycall instead */
	memcpy(lsocket.nr_addr.user,Nr4user,AXALEN);
	/* Putting anything else than Mycall here will not work */
	memcpy(lsocket.nr_addr.node,Mycall,AXALEN);
	bind(s,(struct sockaddr *)&lsocket,sizeof(struct sockaddr_nr));

	/* See if the requested destination could be an alias, and */
	/* find and use it if it is.  Otherwise assume it is an ax.25 */
	/* address. */
	
	if (putalias(alias,argv[1],0) != -1 &&
	    (np = find_nralias(alias)) != NULL) {
	    memcpy(fsocket.nr_addr.user,np,AXALEN) ;
	    memcpy(fsocket.nr_addr.node,np,AXALEN) ;
	} else {	/* parse ax25 callsign */
	    /* Only the user callsign of the remote station is never used by */
	    /* NET/ROM, but it is needed for the psocket() call. */
		setcall(fsocket.nr_addr.user,argv[1]);
		setcall(fsocket.nr_addr.node,argv[1]);
	}
	fsocket.nr_family = AF_NETROM;
	pax25(alias,fsocket.nr_addr.node);
	sp->network = fdopen(s,"r+t");
	setvbuf(sp->network,NULL,_IOLBF,BUFSIZ);
	if(SETSIG(EABORT)){
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	return tel_connect(sp, (struct sockaddr *)&fsocket, sizeof(struct sockaddr_nr));
}

/* Reset a net/rom connection abruptly */
static int
donrreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct nr4cb *cb ;

	cb = (struct nr4cb *)htol(argv[1]);
	if(!nr4valcb(cb)){
		printf(Notval);
		return 1;
	}
	reset_nr4(cb);
	return 0;
}

/* Force retransmission on a net/rom connection */

static int
donrkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct nr4cb *cb ;

	cb = (struct nr4cb *)htol(argv[1]);

	if (kick_nr4(cb) == -1) {
		printf(Notval);
		return 1;
	} else
		return 0;
}

/* netrom transport ACK delay timer */
static int
donracktime(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return setlong(&Nr4acktime,"Ack delay time (ms)",argc,argv);
}

/* netrom transport choke timeout */
static int
donrchoketime(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return setlong(&Nr4choketime,"Choke timeout (ms)",argc,argv);
}

/* netrom transport initial round trip time */

static int
donrirtt(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return setlong(&Nr4irtt,"Initial RTT (ms)",argc,argv);
}

/* netrom transport receive queue length limit.  This is the */
/* threshhold at which we will CHOKE the sender. */

static int
donrqlimit(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return setshort(&Nr4qlimit,"Queue limit (bytes)",argc,argv);
}

/* Display or change our NET/ROM username */
static int
donruser(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char buf[AXBUF];

	if(argc < 2){
		pax25(buf,Nr4user);
		printf("%s\n",buf);
		return 0;
	}
	if(setcall(Nr4user,argv[1]) == -1)
		return -1;
	Nr4user[ALEN] |= E;
	return 0;
}

/* netrom transport maximum window.  This is the largest send and */
/* receive window we may negotiate */

static int
donrwindow(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return setshort(&Nr4window,"Window (frames)",argc,argv);
}

/* netrom transport maximum retries.  This is used in connect and */
/* disconnect attempts; I haven't decided what to do about actual */
/* data retries yet. */

static int
donrretries(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	return setshort(&Nr4retries,"Retry limit",argc,argv);
}

/* Display the status of NET/ROM connections */

static int
donrstatus(argc, argv,p)
int argc ;
char *argv[] ;
void *p;
{
	int i ;
	struct nr4cb *cb ;
	char luser[AXBUF], ruser[AXBUF], node[AXBUF] ;
	
	if (argc < 2) {
		printf("&CB       Snd-W Snd-Q Rcv-Q     LUser      RUser @Node     State\n");
		for (i = 0 ; i < NR4MAXCIRC ; i++) {
			if ((cb = Nr4circuits[i].ccb) == NULL)
				continue ;
			pax25(luser,cb->local.user) ;
			pax25(ruser,cb->remote.user) ;
			pax25(node,cb->remote.node) ;
			printf("%9p   %3d %5d %5d %9s  %9s %-9s %s\n",
			 cb, cb->nbuffered, len_q(cb->txq),
			 len_p(cb->rxq), luser, ruser, node,
			 Nr4states[cb->state]);
		}
		return 0 ;
	}

	cb = (struct nr4cb *)htol(argv[1]) ;
	if (!nr4valcb(cb)) {
		printf(Notval) ;
		return 1 ;
	}

	donrdump(cb) ;
	return 0 ;
}

/* Dump one control block */

void
donrdump(cb)
struct nr4cb *cb ;
{
	char luser[AXBUF], ruser[AXBUF], node[AXBUF] ;
	unsigned seq ;
	struct nr4txbuf *b ;
	struct timer *t ;

	pax25(luser,cb->local.user) ;
	pax25(ruser,cb->remote.user) ;
	pax25(node,cb->remote.node) ;

	printf("Local: %s %d/%d Remote: %s @ %s %d/%d State: %s\n",
		   luser, cb->mynum, cb->myid, ruser, node,
		   cb->yournum, cb->yourid, Nr4states[cb->state]) ;

	printf("Window: %-5u Rxpect: %-5u RxNext: %-5u RxQ: %-5d %s\n",
		   cb->window, cb->rxpected, cb->rxpastwin,
		   len_p(cb->rxq), cb->qfull ? "RxCHOKED" : "") ;

	printf(" Unack: %-5u Txpect: %-5u TxNext: %-5u TxQ: %-5d %s\n",
		   cb->nbuffered, cb->ackxpected, cb->nextosend,
		   len_q(cb->txq), cb->choked ? "TxCHOKED" : "") ;

	printf("TACK: ") ;
	if (run_timer(&cb->tack))
		printf("%lu", read_timer(&cb->tack)) ;
	else
		printf("stop") ;
	printf("/%lu ms; ", dur_timer(&cb->tack)) ;

	printf("TChoke: ") ;
	if (run_timer(&cb->tchoke))
		printf("%lu", read_timer(&cb->tchoke)) ;
	else
		printf("stop") ;
	printf("/%lu ms; ", dur_timer(&cb->tchoke)) ;

	printf("TCD: ") ;
	if (run_timer(&cb->tcd))
		printf("%lu", read_timer(&cb->tcd)) ;
	else
		printf("stop") ;
	printf("/%lu ms", dur_timer(&cb->tcd)) ;

	if (run_timer(&cb->tcd))
		printf("; Tries: %u\n", cb->cdtries) ;
	else
		printf("\n") ;

	printf("Backoff Level %u SRTT %ld ms Mean dev %ld ms\n",
		   cb->blevel, cb->srtt, cb->mdev) ;

	/* If we are connected and the send window is open, display */
	/* the status of all the buffers and their timers */
	
	if (cb->state == NR4STCON && cb->nextosend != cb->ackxpected) {

		printf("TxBuffers:  Seq  Size  Tries  Timer\n") ;

		for (seq = cb->ackxpected ;
			 nr4between(cb->ackxpected, seq, cb->nextosend) ;
			 seq = (seq + 1) & NR4SEQMASK) {

			b = &cb->txbufs[seq % cb->window] ;
			t = &b->tretry ;

			printf("            %3u   %3d  %5d  %lu/%lu\n",
			 seq, len_p(b->data), b->retries + 1,
			 read_timer(t), dur_timer(t));
		}

	}

}
static int
keychar(c)
int c;
{
	if(c != CTLC)
		return 1;	/* Ignore all but ^C */

	fprintf(Current->output,"^C\n");
	alert(Current->proc,EABORT);
	return 0;
}

