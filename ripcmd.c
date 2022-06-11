/* RIP-related user commands
 *   Al Broscious, N3FCT
 *   Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "cmdparse.h"
#include "timer.h"
#include "iface.h"
#include "udp.h"
#include "rip.h"
#include "commands.h"

struct cmds Ripcmds[] = {
	"accept",	dodroprefuse,	0,	2,
		"rip accept <gateway> ",
	"add",		doripadd,	0,	3,
		"rip add <dest> <interval> [<flags>]",
	"drop",		doripdrop,	0,	2,
		"rip drop <dest>",
	"merge",	doripmerge,	0,	0,	NULL,
	"refuse",	doaddrefuse,	0,	2,
		"rip refuse <gateway>",	
	"request",	doripreq,	0,	2,	NULL,
	"status",	doripstat,	0,	0,	NULL,
	"trace",	doriptrace,	0,	0,	NULL,
	NULL,
};

int
dorip(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Ripcmds,argc,argv,p);
}

/* Add an entry to the RIP output list */
int
doripadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int split = 1;
	int us = 0;

	if(argc > 3)
		split = atoi(argv[3]);
	if(argc > 4)
		us = atoi(argv[4]);

	return rip_add(resolve(argv[1]),atol(argv[2]),split,us);
}

/* Add an entry to the RIP refuse list */
int
doaddrefuse(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return riprefadd(resolve(argv[1]));
}

/* Drop an entry from the RIP output list */
int
doripdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return rip_drop(resolve(argv[1]));
}

/* Drop an entry from the RIP refuse list */
int
dodroprefuse(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return riprefdrop(resolve(argv[1]));
}

/* Initialize the RIP listener */
int
doripinit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return rip_init();
}
int
doripstop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	del_udp(Rip_cb);
	Rip_cb = NULL;
	return 0;
}
int
doripreq(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 replyport;

	if(argc > 2)
		replyport = atoi(argv[2]);
	else
		replyport = RIP_PORT;
	return ripreq(resolve(argv[1]),replyport);
}
/* Dump RIP statistics */
int
doripstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct rip_list *rl;
	struct rip_refuse *rfl;

	printf("RIP: sent %lu rcvd %lu reqst %lu resp %lu unk %lu refused %lu\n",
	 Rip_stat.output, Rip_stat.rcvd, Rip_stat.request, Rip_stat.response,
	 Rip_stat.unknown,Rip_stat.refusals);
	if(Rip_list != NULL){
		printf("Active RIP output interfaces:\n");
		printf("Dest Addr       Interval Split\n");
		for(rl=Rip_list; rl != NULL; rl = rl->next){
			printf("%-16s%-9lu%-6u\n",inet_ntoa(rl->dest),
			 rl->interval,rl->flags.rip_split);
		}
	}
	if(Rip_refuse != NULL){
		printf("Refusing announcements from gateways:\n");
		for(rfl=Rip_refuse; rfl != NULL;rfl = rfl->next){
			printf("%s\n",inet_ntoa(rfl->target));
		}
	}
	return 0;
}

int
doriptrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Rip_trace,"RIP tracing",argc,argv);
}
int
doripmerge(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Rip_merge,"RIP merging",argc,argv);
}
