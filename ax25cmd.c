/* AX25 control commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ax25.h"
#include "lapb.h"
#include "cmdparse.h"
#include "socket.h"
#include "mailbox.h"
#include "session.h"
#include "tty.h"
#include "nr4.h"
#include "commands.h"

static int axdest(struct iface *ifp);
static int axheard(struct iface *ifp);
static void axflush(struct iface *ifp);
static int doaxflush(int argc,char *argv[],void *p);
static int doaxirtt(int argc,char *argv[],void *p);
static int doaxkick(int argc,char *argv[],void *p);
static int doaxreset(int argc,char *argv[],void *p);
static int doaxroute(int argc,char *argv[],void *p);
static int doaxstat(int argc,char *argv[],void *p);
static int doaxwindow(int argc,char *argv[],void *p);
static int doblimit(int argc,char *argv[],void *p);
static int dodigipeat(int argc,char *argv[],void *p);
static int domaxframe(int argc,char *argv[],void *p);
static int domycall(int argc,char *argv[],void *p);
static int don2(int argc,char *argv[],void *p);
static int dopaclen(int argc,char *argv[],void *p);
static int dopthresh(int argc,char *argv[],void *p);
static int dot3(int argc,char *argv[],void *p);
static int doversion(int argc,char *argv[],void *p);

char *Ax25states[] = {
	"",
	"Disconn",
	"Listening",
	"Conn pend",
	"Disc pend",
	"Connected",
	"Recovery",
};

/* Ascii explanations for the disconnect reasons listed in lapb.h under
 * "reason" in ax25_cb
 */
char *Axreasons[] = {
	"Normal",
	"DM received",
	"Timeout"
};

static struct cmds Axcmds[] = {
	"blimit",	doblimit,	0, 0, NULL,
	"destlist",	doaxdest,	0, 0, NULL,
	"digipeat",	dodigipeat,	0, 0, NULL,
	"flush",	doaxflush,	0, 0, NULL,
	"heard",	doaxheard,	0, 0, NULL,
	"irtt",		doaxirtt,	0, 0, NULL,
	"kick",		doaxkick,	0, 2, "ax25 kick <axcb>",
	"maxframe",	domaxframe,	0, 0, NULL,
	"mycall",	domycall,	0, 0, NULL,
	"paclen",	dopaclen,	0, 0, NULL,
	"pthresh",	dopthresh,	0, 0, NULL,
	"reset",	doaxreset,	0, 2, "ax25 reset <axcb>",
	"retry",	don2,		0, 0, NULL,
	"route",	doaxroute,	0, 0, NULL,
	"status",	doaxstat,	0, 0, NULL,
	"t3",		dot3,		0, 0, NULL,
	"version",	doversion,	0, 0, NULL,
	"window",	doaxwindow,	0, 0, NULL,
	NULL,
};
static int keychar(int c);


/* Multiplexer for top-level ax25 command */
int
doax25(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Axcmds,argc,argv,p);
}

int
doaxheard(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;

	if(argc > 1){
		if((ifp = if_lookup(argv[1])) == NULL){
			printf("Interface %s unknown\n",argv[1]);
			return 1;
		}
		if(ifp->output != ax_output){
			printf("Interface %s not AX.25\n",argv[1]);
			return 1;
		}
		axheard(ifp);
		return 0;
	}
	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;	/* Not an ax.25 interface */
		if(axheard(ifp) == EOF)
			break;
	}
	return 0;
}
static int
axheard(ifp)
struct iface *ifp;
{
	struct lq *lp;
	char tmp[AXBUF];

	if(ifp->hwaddr == NULL)
		return 0;
	printf("%s:\n",ifp->name);
	printf("Station   Last heard           Pkts\n");
	for(lp = Lq;lp != NULL;lp = lp->next){
		if(lp->iface != ifp)
			continue;
		printf("%-10s%-17s%8lu\n",pax25(tmp,lp->addr),
		 tformat(secclock() - lp->time),lp->currxcnt);
	}
	return 0;
}
int
doaxdest(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;

	if(argc > 1){
		if((ifp = if_lookup(argv[1])) == NULL){
			printf("Interface %s unknown\n",argv[1]);
			return 1;
		}
		if(ifp->output != ax_output){
			printf("Interface %s not AX.25\n",argv[1]);
			return 1;
		}
		axdest(ifp);
		return 0;
	}
	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;	/* Not an ax.25 interface */
		if(axdest(ifp) == EOF)
			break;
	}
	return 0;
}
static int
axdest(ifp)
struct iface *ifp;
{
	struct ld *lp;
	struct lq *lq;
	char tmp[AXBUF];

	if(ifp->hwaddr == NULL)
		return 0;
	printf("%s:\n",ifp->name);
	printf("Station   Last ref         Last heard           Pkts\n");
	for(lp = Ld;lp != NULL;lp = lp->next){
		if(lp->iface != ifp)
			continue;

		printf("%-10s%-17s",
		 pax25(tmp,lp->addr),tformat(secclock() - lp->time));

		if(addreq(lp->addr,ifp->hwaddr)){
			/* Special case; it's our address */
			printf("%-17s",tformat(secclock() - ifp->lastsent));
		} else if((lq = al_lookup(ifp,lp->addr,0)) == NULL){
			printf("%-17s","");
		} else {
			printf("%-17s",tformat(secclock() - lq->time));
		}
		printf("%8lu\n",lp->currxcnt);
	}
	return 0;
}
static int
doaxflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;

	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;	/* Not an ax.25 interface */
		axflush(ifp);
	}
	return 0;
}
static void
axflush(ifp)
struct iface *ifp;
{
	struct lq *lp,*lp1;
	struct ld *ld,*ld1;

	ifp->rawsndcnt = 0;
	for(lp = Lq;lp != NULL;lp = lp1){
		lp1 = lp->next;
		free(lp);
	}
	Lq = NULL;
	for(ld = Ld;ld != NULL;ld = ld1){
		ld1 = ld->next;
		free(ld);
	}
	Ld = NULL;
}

static
doaxreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)ltop(htol(argv[1]));
	if(!ax25val(axp)){
		printf(Notval);
		return 1;
	}
	reset_ax25(axp);
	return 0;
}

/* Display AX.25 link level control blocks */
static
doaxstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ax25_cb *axp;
	char tmp[AXBUF];

	if(argc < 2){
		printf("&AXB      Snd-Q   Rcv-Q   Remote    State\n");
		for(axp = Ax25_cb;axp != NULL; axp = axp->next){
			printf("%9p %-8d%-8d%-10s%s\n",
			 axp,
			 len_q(axp->txq),len_p(axp->rxq),
			 pax25(tmp,axp->remote),
			 Ax25states[axp->state]);
		}
		return 0;
	}
	axp = (struct ax25_cb *)ltop(htol(argv[1]));
	if(!ax25val(axp)){
		printf(Notval);
		return 1;
	}
	st_ax25(axp);
	return 0;
}
/* Dump one control block */
void
st_ax25(axp)
register struct ax25_cb *axp;
{
	char tmp[AXBUF];

	if(axp == NULL)
		return;
	printf("     &AXB Remote   RB V(S) V(R) Unack P Retry State\n");

	printf("%9p %-9s%c%c",axp,pax25(tmp,axp->remote),
	 axp->flags.rejsent ? 'R' : ' ',
	 axp->flags.remotebusy ? 'B' : ' ');
	printf(" %4d %4d",axp->vs,axp->vr);
	printf(" %02u/%02u %u",axp->unack,axp->maxframe,axp->proto);
	printf(" %02u/%02u",axp->retries,axp->n2);
	printf(" %s\n",Ax25states[axp->state]);

	printf("srtt = %lu mdev = %lu ",axp->srt,axp->mdev);
	printf("T1: ");
	if(run_timer(&axp->t1))
		printf("%lu",read_timer(&axp->t1));
	else
		printf("stop");
	printf("/%lu ms; ",dur_timer(&axp->t1));

	printf("T3: ");
	if(run_timer(&axp->t3))
		printf("%lu",read_timer(&axp->t3));
	else
		printf("stop");
	printf("/%lu ms\n",dur_timer(&axp->t3));

}

/* Display or change our AX.25 address */
static
domycall(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char tmp[AXBUF];

	if(argc < 2){
		printf("%s\n",pax25(tmp,Mycall));
		return 0;
	}
	if(setcall(Mycall,argv[1]) == -1)
		return -1;
	return 0;
}

/* Control AX.25 digipeating */
static
dodigipeat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Digipeat,"Digipeat",argc,argv);
}
/* Set limit on retransmission backoff */
static
doblimit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setlong(&Blimit,"blimit",argc,argv);
}
static
doversion(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Axversion,"AX25 version",argc,argv);
}

static
doaxirtt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setlong(&Axirtt,"Initial RTT (ms)",argc,argv);
}

/* Set idle timer */
static
dot3(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setlong(&T3init,"Idle poll timer (ms)",argc,argv);
}

/* Set retry limit count */
static
don2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&N2,"Retry limit",argc,argv);
}
/* Force a retransmission */
static
doaxkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)ltop(htol(argv[1]));
	if(!ax25val(axp)){
		printf(Notval);
		return 1;
	}
	kick_ax25(axp);
	return 0;
}
/* Set maximum number of frames that will be allowed in flight */
static
domaxframe(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Maxframe,"Window size (frames)",argc,argv);
}

/* Set maximum length of I-frame data field */
static
dopaclen(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Paclen,"Max frame length (bytes)",argc,argv);
}
/* Set size of I-frame above which polls will be sent after a timeout */
static
dopthresh(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Pthresh,"Poll threshold (bytes)",argc,argv);
}

/* Set high water mark on receive queue that triggers RNR */
static
doaxwindow(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Axwindow,"AX25 receive window (bytes)",argc,argv);
}
/* End of ax25 subcommands */

/* Initiate interactive AX.25 connect to remote station */
int
doconnect(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct sockaddr_ax fsocket;
	struct session *sp;
	int ndigis,i,s;
	uint8 digis[MAXDIGIS][AXALEN];
	uint8 target[AXALEN];

	/* If digipeaters are given, put them in the routing table */
	if(argc > 3){
		setcall(target,argv[2]);
		ndigis = argc - 3;
		if(ndigis > MAXDIGIS){
			printf("Too many digipeaters\n");
			return 1;
		}
		for(i=0;i<ndigis;i++){
			if(setcall(digis[i],argv[i+3]) == -1){
				printf("Bad digipeater %s\n",argv[i+3]);
				return 1;
			}
		}
		if(ax_add(target,AX_LOCAL,digis,ndigis) == NULL){
			printf("Route add failed\n");
			return 1;
		}
	}
	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,AX25TNC,1)) == NULL){
		printf("Too many sessions\n");
		return 1;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	if((s = socket(AF_AX25,SOCK_STREAM,0)) == -1){
		printf("Can't create socket\n");
		freesession(sp);
		keywait(NULL,1);
		return 1;
	}
	fsocket.sax_family = AF_AX25;
	setcall(fsocket.ax25_addr,argv[2]);
	strncpy(fsocket.iface,argv[1],ILEN);
	sp->network = fdopen(s,"r+t");
	setvbuf(sp->network,NULL,_IOLBF,BUFSIZ);
	if(SETSIG(EABORT)){
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	return tel_connect(sp, (struct sockaddr *)&fsocket, sizeof(struct sockaddr_ax));
}

/* Display and modify AX.25 routing table */
static int
doaxroute(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char tmp[AXBUF];
	int i,ndigis;
	register struct ax_route *axr;
	uint8 target[AXALEN],digis[MAXDIGIS][AXALEN];

	if(argc < 2){
		printf("Target    Type   Digipeaters\n");
		for(axr = Ax_routes;axr != NULL;axr = axr->next){
			printf("%-10s%-6s",pax25(tmp,axr->target),
			 axr->type == AX_LOCAL ? "Local":"Auto");
			for(i=0;i<axr->ndigis;i++){
				printf(" %s",pax25(tmp,axr->digis[i]));
			}
			printf("\n");
		}
		return 0;
	}
	if(argc < 3){
		printf("Usage: ax25 route add <target> [digis...]\n");
		printf("       ax25 route drop <target>\n");
		return 1;
	}
	if(setcall(target,argv[2]) == -1){
		printf("Bad target %s\n",argv[2]);
		return 1;
	}
	switch(argv[1][0]){
	case 'a':	/* Add route */
		if(argc < 3){
			printf("Usage: ax25 route add <target> [digis...]\n");
			return 1;
		}
		ndigis = argc - 3;
		if(ndigis > MAXDIGIS){
			printf("Too many digipeaters\n");
			return 1;
		}
		for(i=0;i<ndigis;i++){
			if(setcall(digis[i],argv[i+3]) == -1){
				printf("Bad digipeater %s\n",argv[i+3]);
				return 1;
			}
		}
		if(ax_add(target,AX_LOCAL,digis,ndigis) == NULL){
			printf("Failed\n");
			return 1;
		}
		break;
	case 'd':	/* Drop route */
		if(ax_drop(target) == -1){
			printf("Not in table\n");
			return 1;
		}
		break;
	default:
		printf("Unknown command %s\n",argv[1]);
		return 1;
	}
	return 0;
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

