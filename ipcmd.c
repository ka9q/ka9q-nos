/* IP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "timer.h"
#include "netuser.h"
#include "iface.h"
#include "ip.h"
#include "cmdparse.h"
#include "commands.h"
#include "rip.h"

int32 Ip_addr;

static int doadd(int argc,char *argv[],void *p);
static int dodrop(int argc,char *argv[],void *p);
static int doflush(int argc,char *argv[],void *p);
static int doipaddr(int argc,char *argv[],void *p);
static int doipstat(int argc,char *argv[],void *p);
static int dolook(int argc,char *argv[],void *p);
static int dortimer(int argc,char *argv[],void *p);
static int dottl(int argc,char *argv[],void *p);
static int doiptrace(int argc,char *argv[],void *p);
static int dumproute(struct route *rp);

static struct cmds Ipcmds[] = {
	"address",	doipaddr,	0,	0, NULL,
	"rtimer",	dortimer,	0,	0, NULL,
	"status",	doipstat,	0,	0, NULL,
	"trace",	doiptrace,	0,	0, NULL,
	"ttl",		dottl,		0,	0, NULL,
	NULL,
};
/* "route" subcommands */
static struct cmds Rtcmds[] = {
	"add",		doadd,		0,	3,
	"route add <dest addr>[/<bits>] <if name> [gateway] [metric]",

	"addprivate",	doadd,		0,	3,
	"route addprivate <dest addr>[/<bits>] <if name> [gateway] [metric]",

	"drop",		dodrop,		0,	2,
	"route drop <dest addr>[/<bits>]",

	"flush",	doflush,	0,	0,
	NULL,

	"lookup",	dolook,		0,	2,
	"route lookup <dest addr>",

	NULL,
};

int
doip(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Ipcmds,argc,argv,p);
}
static int
doiptrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Ip_trace,"IP rx tracing",argc,argv);
}

static int
doipaddr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int32 n;

	if(argc < 2) {
		printf("%s\n",inet_ntoa(Ip_addr));
	} else if((n = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	} else
		Ip_addr = n;
	return 0;
}
static int
dortimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setlong(&ipReasmTimeout,"IP reasm timeout (sec)",argc,argv);
}
static int
dottl(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setlong(&ipDefaultTTL,"IP Time-to-live",argc,argv);
}


/* Display and/or manipulate routing table */
int
doroute(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register int i,bits;
	register struct route *rp;

	if(argc >= 2)
		return subcmd(Rtcmds,argc,argv,p);

	/* Dump IP routing table
	 * Dest            Len Interface    Gateway          Use
	 * 192.001.002.003 32  sl0          192.002.003.004  0
	 */
	printf(
"Dest            Len Interface    Gateway          Metric  P Timer  Use\n");

	for(bits=31;bits>=0;bits--){
		for(i=0;i<HASHMOD;i++){
			for(rp = Routes[bits][i];rp != NULL;rp = rp->next){
				if(dumproute(rp) == EOF)
					return 0;
			}
		}
	}
	if(R_default.iface != NULL)
		dumproute(&R_default);

	return 0;
}
/* Add an entry to the routing table
 * E.g., "add 1.2.3.4 ax0 5.6.7.8 3"
 */
static int
doadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;
	int32 dest,gateway;
	unsigned bits;
	char *bitp;
	int32 metric;
	char private;

	if(strncmp(argv[0],"addp",4) == 0)
		private = 1;
	else
		private = 0;
	if(strcmp(argv[1],"default") == 0){
		dest = 0;
		bits = 0;
	} else {
		/* If IP address is followed by an optional slash and
		 * a length field, (e.g., 128.96/16) get it;
		 * otherwise assume a full 32-bit address
		 */
		if((bitp = strchr(argv[1],'/')) != NULL){
			/* Terminate address token for resolve() call */
			*bitp++ = '\0';
			bits = atoi(bitp);
		} else
			bits = 32;

		if((dest = resolve(argv[1])) == 0){
			printf(Badhost,argv[1]);
			return 1;
		}
	}
	if((ifp = if_lookup(argv[2])) == NULL){
		printf("Interface \"%s\" unknown\n",argv[2]);
		return 1;
	}
	if(argc > 3){
		if((gateway = resolve(argv[3])) == 0){
			printf(Badhost,argv[3]);
			return 1;
		}
	} else {
		gateway = 0;
	}
	if (argc > 4)
		metric = atol(argv[4]);
	else
		metric = 1;

	if(rt_add(dest,bits,gateway,ifp,metric,0,private) == NULL)
		printf("Can't add route\n");
	return 0;
}
/* Drop an entry from the routing table
 * E.g., "drop 128.96/16
 */
static int
dodrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *bitp;
	unsigned bits;
	int32 n;

	if(strcmp(argv[1],"default") == 0){
		n = 0;
		bits = 0;
	} else {
		/* If IP address is followed by an optional slash and length field,
		 * (e.g., 128.96/16) get it; otherwise assume a full 32-bit address
		 */
		if((bitp = strchr(argv[1],'/')) != NULL){
			/* Terminate address token for resolve() call */
			*bitp++ = '\0';
			bits = atoi(bitp);
		} else
			bits = 32;

		if((n = resolve(argv[1])) == 0){
			printf(Badhost,argv[1]);
			return 1;
		}
	}
	return rt_drop(n,bits);
}
/* Force a timeout on all temporary routes */
static int
doflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct route *rp;
	struct route *rptmp;
	int i,j;
	
	if(R_default.timer.state == TIMER_RUN){
		rt_drop(0,0);	/* Drop default route */
	}
	for(i=0;i<HASHMOD;i++){
		for(j=0;j<32;j++){
			for(rp = Routes[j][i];rp != NULL;rp = rptmp){
				rptmp = rp->next;
				if(rp->timer.state == TIMER_RUN){
					rt_drop(rp->target,rp->bits);
				}
			}
		}
	}
	return 0;
}
/* Dump a routing table entry */
static int
dumproute(rp)
register struct route *rp;
{
	char *cp;

	if(rp->target != 0)
		cp = inet_ntoa(rp->target);
	else
		cp = "default";
	printf("%-16s",cp);
	printf("%-4u",rp->bits);
	printf("%-13s",rp->iface->name);
	if(rp->gateway != 0)
		cp = inet_ntoa(rp->gateway);
	else
		cp = "";
	printf("%-17s",cp);
	printf("%-8lu",rp->metric);
	printf("%c ",rp->flags.rtprivate ? 'P' : ' ');
	printf("%-7lu",
	 read_timer(&rp->timer) / 1000L);
	return printf("%lu\n",rp->uses);
}

static int
dolook(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct route *rp;
	int32 addr;

	addr = resolve(argv[1]);
	if(addr == 0){
		printf("Host %s unknown\n",argv[1]);
		return 1;
	}
	if((rp = rt_lookup(addr)) == NULL){
		printf("Host %s (%s) unreachable\n",argv[1],inet_ntoa(addr));
		return 1;
	}
	dumproute(rp);
	return 0;
}

static int
doipstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct reasm *rp;
	register struct frag *fp;
	int i;

	for(i=1;i<=NUMIPMIB;i++){
		printf("(%2u)%-20s%10lu",i,
		 Ip_mib[i].name,Ip_mib[i].value.integer);
		if(i % 2)
			printf("     ");
		else
			printf("\n");
	}
	if((i % 2) == 0)
		printf("\n");
	printf("Routing lookups: %lu, cache hits %lu (%lu%%)\n",
	 Rtlookups,Rtchits,
	 Rtlookups != 0 ? (Rtchits*100 + Rtlookups/2)/Rtlookups: 0);

	if(Reasmq != NULL)
		printf("Reassembly fragments:\n");
	for(rp = Reasmq;rp != NULL;rp = rp->next){
		printf("src %s",inet_ntoa(rp->source));
		printf(" dest %s",inet_ntoa(rp->dest));
		printf(" id %u pctl %u time %lu len %u\n",
		 rp->id,rp->protocol,read_timer(&rp->timer),
		 rp->length);
		for(fp = rp->fraglist;fp != NULL;fp = fp->next){
			printf(" offset %u last %u\n",fp->offset,
			fp->last);
		}
	}
	return 0;
}
