/* ICMP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "icmp.h"
#include "ip.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "timer.h"
#include "socket.h"
#include "proc.h"
#include "cmdparse.h"
#include "commands.h"

static int doicmpec(int argc, char *argv[],void *p);
static int doicmpstat(int argc, char *argv[],void *p);
static int doicmptr(int argc, char *argv[],void *p);

static struct cmds Icmpcmds[] = {
	"echo",		doicmpec,	0, 0, NULL,
	"status",	doicmpstat,	0, 0, NULL,
	"trace",	doicmptr,	0, 0, NULL,
	NULL
};

int Icmp_trace;
int Icmp_echo = 1;

int
doicmp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Icmpcmds,argc,argv,p);
}

static int
doicmpstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register int i;
	int lim;

	/* Note that the ICMP variables are shown in column order, because
	 * that lines up the In and Out variables on the same line
	 */
	lim = NUMICMPMIB/2;
	for(i=1;i<=lim;i++){
		printf("(%2u)%-20s%10lu",i,Icmp_mib[i].name,
		 Icmp_mib[i].value.integer);
		printf("     (%2u)%-20s%10lu\n",i+lim,Icmp_mib[i+lim].name,
		 Icmp_mib[i+lim].value.integer);
	}
	return 0;
}
static int
doicmptr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Icmp_trace,"ICMP tracing",argc,argv);
}
static int
doicmpec(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Icmp_echo,"ICMP echo response accept",argc,argv);
}
