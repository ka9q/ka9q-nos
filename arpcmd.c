/* ARP commands
 * Copyright 1991, Phil Karn, KA9Q
 */
#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "enet.h"
#include "ax25.h"
#include "arp.h"
#include "netuser.h"
#include "cmdparse.h"
#include "commands.h"

static int doarpadd(int argc,char *argv[],void *p);
static int doarpdrop(int argc,char *argv[],void *p);
static int doarpflush(int argc,char *argv[],void *p);
static void dumparp(void);

static struct cmds Arpcmds[] = {
	"add", doarpadd, 0, 4,
	"arp add <hostid> ether|ax25|netrom|arcnet <ether addr|callsign>",

	"drop", doarpdrop, 0, 3,
	"arp drop <hostid> ether|ax25|netrom|arcnet",

	"flush", doarpflush, 0, 0,
	NULL,

	"publish", doarpadd, 0, 4,
	"arp publish <hostid> ether|ax25|netrom|arcnet <ether addr|callsign>",

	NULL,
};
char *Arptypes[] = {
	"NET/ROM",
	"10 Mb Ethernet",
	"3 Mb Ethernet",
	"AX.25",
	"Pronet",
	"Chaos",
	"",
	"Arcnet",
	"Appletalk"
};

int
doarp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		dumparp();
		return 0;
	}
	return subcmd(Arpcmds,argc,argv,p);
}
static
doarpadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 hardware;
	int32 addr;
	uint8 *hwaddr;
	struct arp_tab *ap;
	struct arp_type *at;
	int pub = 0;

	if(argv[0][0] == 'p')	/* Is this entry published? */
		pub = 1;
	if((addr = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	}
	/* This is a kludge. It really ought to be table driven */
	switch(tolower(argv[2][0])){
	case 'n':	/* Net/Rom pseudo-type */
		hardware = ARP_NETROM;
		break;
	case 'e':	/* "ether" */
		hardware = ARP_ETHER;
		break;		
	case 'a':	/* "ax25" */
		switch(tolower(argv[2][1])) {
		case 'x':
			hardware = ARP_AX25;
			break;
		case 'r':
			hardware = ARP_ARCNET;
			break;
		default:
			printf("unknown hardware type \"%s\"\n",argv[2]);
			return -1;
		}
		break;
	case 'm':	/* "mac appletalk" */
		hardware = ARP_APPLETALK;
		break;
	default:
		printf("unknown hardware type \"%s\"\n",argv[2]);
		return -1;
	}
	/* If an entry already exists, clear it */
	if((ap = arp_lookup(hardware,addr)) != NULL)
		arp_drop(ap);

	at = &Arp_type[hardware];
	if(at->scan == NULL){
		printf("Attach device first\n");
		return 1;
	}
	/* Allocate buffer for hardware address and fill with remaining args */
	hwaddr = mallocw(at->hwalen);
	/* Destination address */
	(*at->scan)(hwaddr,argv[3]);
	ap = arp_add(addr,hardware,hwaddr,pub);	/* Put in table */
	free(hwaddr);				/* Clean up */
	stop_timer(&ap->timer);			/* Make entry permanent */
	set_timer(&ap->timer,0L);
	return 0;
}
/* Remove an ARP entry */
static
doarpdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 hardware;
	int32 addr;
	struct arp_tab *ap;

	if((addr = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	}
	/* This is a kludge. It really ought to be table driven */
	switch(tolower(argv[2][0])){
	case 'n':
		hardware = ARP_NETROM;
		break;
	case 'e':	/* "ether" */
		hardware = ARP_ETHER;
		break;		
	case 'a':	/* "ax25" */
		switch(tolower(argv[2][1])) {
		case 'x':
			hardware = ARP_AX25;
			break;
		case 'r':
			hardware = ARP_ARCNET;
			break;
		default:
			hardware = 0;
			break;
		}
		break;
	case 'm':	/* "mac appletalk" */
		hardware = ARP_APPLETALK;
		break;
	default:
		hardware = 0;
		break;
	}
	if((ap = arp_lookup(hardware,addr)) == NULL)
		return -1;
	arp_drop(ap);
	return 0;	
}
/* Flush all automatic entries in the arp cache */
static int
doarpflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct arp_tab *ap;
	struct arp_tab *aptmp;
	int i;

	for(i=0;i<HASHMOD;i++){
		for(ap = Arp_tab[i];ap != NULL;ap = aptmp){
			aptmp = ap->next;
			if(dur_timer(&ap->timer) != 0)
				arp_drop(ap);
		}
	}
	return 0;
}

/* Dump ARP table */
static void
dumparp()
{
	register int i;
	register struct arp_tab *ap;
	char e[128];

	printf("received %u badtype %u bogus addr %u reqst in %u replies %u reqst out %u\n",
	 Arp_stat.recv,Arp_stat.badtype,Arp_stat.badaddr,Arp_stat.inreq,
	 Arp_stat.replies,Arp_stat.outreq);

	printf("IP addr         Type           Time Q Addr\n");
	for(i=0;i<HASHMOD;i++){
		for(ap = Arp_tab[i];ap != (struct arp_tab *)NULL;ap = ap->next){
			printf("%-16s",inet_ntoa(ap->ip_addr));
			printf("%-15s",smsg(Arptypes,NHWTYPES,ap->hardware));
			printf("%-5ld",read_timer(&ap->timer)/1000L);
			if(ap->state == ARP_PENDING)
				printf("%-2u",len_q(ap->pending));
			else
				printf("  ");
			if(ap->state == ARP_VALID){
				if(Arp_type[ap->hardware].format != NULL){
					(*Arp_type[ap->hardware].format)(e,ap->hw_addr);
				} else {
					e[0] = '\0';
				}
				printf("%s",e);
			} else {
				printf("[unknown]");
			}
			if(ap->pub)
				printf(" (published)");
			printf("\n");
		}
	}
}
