/* IP interface control and configuration routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "icmp.h"
#include "netuser.h"
#include "ax25.h"
#include "enet.h"
#include "pktdrvr.h"
#include "cmdparse.h"
#include "commands.h"
#include "trace.h"

static void showiface(struct iface *ifp);
static int mask2width(int32 mask);
static int ifipaddr(int argc,char *argv[],void *p);
static int iflinkadr(int argc,char *argv[],void *p);
static int ifbroad(int argc,char *argv[],void *p);
static int ifnetmsk(int argc,char *argv[],void *p);
static int ifrxbuf(int argc,char *argv[],void *p);
static int ifmtu(int argc,char *argv[],void *p);
static int ifforw(int argc,char *argv[],void *p);
static int ifencap(int argc,char *argv[],void *p);
static int iftxqlen(int argc,char *argv[],void *p);

/* Interface list header */
struct iface *Ifaces = &Loopback;

/* Loopback pseudo-interface */
struct iface Loopback = {
	&Encap,		/* Link to next entry */
	"loopback",	/* name		*/
	0x7f000001L,	/* addr		127.0.0.1 */
	0xffffffffL,	/* broadcast	255.255.255.255 */
	0xffffffffL,	/* netmask	255.255.255.255 */
	MAXINT16,	/* mtu		No limit */
	0,		/* trace	*/
	NULL,	/* trfp		*/
	NULL,		/* forw		*/
	NULL,	/* rxproc	*/
	NULL,	/* txproc	*/
	NULL,	/* supv		*/
	NULL,	/* outq		*/
	0,		/* outlim	*/
	0,		/* txbusy	*/
	NULL,		/* dstate	*/
	NULL,		/* dtickle	*/
	NULL,		/* dstatus	*/
	0,		/* dev		*/
	NULL,		/* (*ioctl)	*/
	NULL,		/* (*iostatus)	*/
	NULL,		/* (*stop)	*/
	NULL,	/* hwaddr	*/
	NULL,		/* extension	*/
	0,		/* xdev		*/
	&Iftypes[0],	/* iftype	*/
	NULL,		/* (*send)	*/
	NULL,		/* (*output)	*/
	NULL,		/* (*raw)	*/
	NULL,		/* (*status)	*/
	NULL,		/* (*discard)	*/
	NULL,		/* (*echo)	*/
	0,		/* ipsndcnt	*/
	0,		/* rawsndcnt	*/
	0,		/* iprecvcnt	*/
	0,		/* rawrcvcnt	*/
	0,		/* lastsent	*/
	0,		/* lastrecv	*/
};
/* Encapsulation pseudo-interface */
struct iface Encap = {
	NULL,
	"encap",	/* name		*/
	INADDR_ANY,	/* addr		0.0.0.0 */
	0xffffffffL,	/* broadcast	255.255.255.255 */
	0xffffffffL,	/* netmask	255.255.255.255 */
	MAXINT16,	/* mtu		No limit */
	0,		/* trace	*/
	NULL,	/* trfp		*/
	NULL,		/* forw		*/
	NULL,	/* rxproc	*/
	NULL,	/* txproc	*/
	NULL,	/* supv		*/
	NULL,	/* outq		*/
	0,		/* outlim	*/
	0,		/* txbusy	*/
	NULL,		/* dstate	*/
	NULL,		/* dtickle	*/
	NULL,		/* dstatus	*/
	0,		/* dev		*/
	NULL,		/* (*ioctl)	*/
	NULL,		/* (*iostatus)	*/
	NULL,		/* (*stop)	*/
	NULL,	/* hwaddr	*/
	NULL,		/* extension	*/
	0,		/* xdev		*/
	&Iftypes[0],	/* iftype	*/
	ip_encap,	/* (*send)	*/
	NULL,		/* (*output)	*/
	NULL,		/* (*raw)	*/
	NULL,		/* (*status)	*/
	NULL,		/* (*discard)	*/
	NULL,		/* (*echo)	*/
	0,		/* ipsndcnt	*/
	0,		/* rawsndcnt	*/
	0,		/* iprecvcnt	*/
	0,		/* rawrcvcnt	*/
	0,		/* lastsent	*/
	0,		/* lastrecv	*/
};

char Noipaddr[] = "IP address field missing, and ip address not set\n";

struct cmds Ifcmds[] = {
	"broadcast",		ifbroad,	0,	2,	NULL,
	"encapsulation",	ifencap,	0,	2,	NULL,
	"forward",		ifforw,		0,	2,	NULL,
	"ipaddress",		ifipaddr,	0,	2,	NULL,
	"linkaddress",		iflinkadr,	0,	2,	NULL,
	"mtu",			ifmtu,		0,	2,	NULL,
	"netmask",		ifnetmsk,	0,	2,	NULL,
	"txqlen",		iftxqlen,	0,	2,	NULL,
	"rxbuf",		ifrxbuf,	0,	2,	NULL,
	NULL,
};
/*
 * General purpose interface transmit task, one for each device that can
 * send IP datagrams. It waits on the interface's IP output queue (outq),
 * extracts IP datagrams placed there in priority order by ip_route(),
 * and sends them to the device's send routine.
 */
void
if_tx(int dev,void *arg1,void *unused)
{
	struct mbuf *bp;	/* Buffer to send */
	struct iface *iface;	/* Pointer to interface control block */
	struct qhdr qhdr;

	iface = arg1;
	for(;;){
		while(iface->outq == NULL)
			kwait(&iface->outq);

		iface->txbusy = 1;
		bp = dequeue(&iface->outq);
		pullup(&bp,&qhdr,sizeof(qhdr));
		if(iface->dtickle != NULL && (*iface->dtickle)(iface) == -1){
#ifdef	notdef	/* Confuses some non-compliant hosts */
			struct ip ip;

			/* Link redial failed; bounce with unreachable */
			ntohip(&ip,&bp);
			icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,
			 NULL);
#endif
			free_p(&bp);
		} else {
			(*iface->send)(&bp,iface,qhdr.gateway,qhdr.tos);
		}
		iface->txbusy = 0;

		/* Let other tasks run, just in case send didn't block */
		kwait(NULL);
	}
}
/* Process packets in the Hopper */
void
network(int i,void *v1,void *v2)
{
	struct mbuf *bp;
	char i_state;
	struct iftype *ift;
	struct iface *ifp;

loop:
	for(;;){
		i_state = dirps();
		bp = Hopper;
		if(bp != NULL){
			bp = dequeue(&Hopper);
			restore(i_state);
			break;
		}
		restore(i_state);
		kwait(&Hopper);
	}
	/* Process the input packet */
	pullup(&bp,&ifp,sizeof(ifp));
	if(ifp != NULL){
		ifp->rawrecvcnt++;
		ifp->lastrecv = secclock();
		ift = ifp->iftype;
	} else {
		ift = &Iftypes[0];
	}
	dump(ifp,IF_TRACE_IN,bp);
	
	if(ift->rcvf != NULL)
		(*ift->rcvf)(ifp,&bp);
	else
		free_p(&bp);	/* Nowhere to send it */

	/* Let everything else run - this keeps the system from wedging
	 * when we're hit by a big burst of packets
	 */
	kwait(NULL);
	goto loop;
}

/* put mbuf into Hopper for network task
 * returns 0 if OK
 */
int
net_route(struct iface *ifp,struct mbuf **bpp)
{
	pushdown(bpp,&ifp,sizeof(ifp));
	enqueue(&Hopper,bpp);
	return 0;
}

/* Null send and output routines for interfaces without link level protocols */
int
nu_send(struct mbuf **bpp,struct iface *ifp,int32 gateway,uint8 tos)
{
	return (*ifp->raw)(ifp,bpp);
}
int
nu_output(struct iface *ifp,uint8 *dest,uint8 *src,uint16 type,struct mbuf **bpp)
{
	return (*ifp->raw)(ifp,bpp);
}

/* Set interface parameters */
int
doifconfig(int argc,char *argv[],void *p)
{
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(ifp = Ifaces;ifp != NULL;ifp = ifp->next)
			showiface(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc == 2){
		showiface(ifp);
		if(ifp->show != NULL){
			(*ifp->show)(ifp);
		}
		return 0;
	}
	if(argc == 3){
		printf("Argument missing\n");
		return 1;
	}
	for(i=2;i<argc-1;i+=2)
		subcmd(Ifcmds,3,&argv[i-1],ifp);

	return 0;
}

/* Set interface IP address */
static int
ifipaddr(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	ifp->addr = resolve(argv[1]);
	return 0;
}


/* Set link (hardware) address */
static int
iflinkadr(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	if(ifp->iftype == NULL || ifp->iftype->scan == NULL){
		printf("Can't set link address\n");
		return 1;
	}
	if(ifp->hwaddr != NULL)
		free(ifp->hwaddr);
	ifp->hwaddr = mallocw(ifp->iftype->hwalen);
	(*ifp->iftype->scan)(ifp->hwaddr,argv[1]);
	return 0;
}

/* Set interface broadcast address. This is actually done
 * by installing a private entry in the routing table.
 */
static int
ifbroad(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;
	struct route *rp;

	rp = rt_blookup(ifp->broadcast,32);
	if(rp != NULL && rp->iface == ifp)
		rt_drop(ifp->broadcast,32);
	ifp->broadcast = resolve(argv[1]);
	rt_add(ifp->broadcast,32,0L,ifp,1L,0L,1);
	return 0;
}

/* Set the network mask. This is actually done by installing
 * a routing entry.
 */
static int
ifnetmsk(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;
	struct route *rp;

	/* Remove old entry if it exists */
	rp = rt_blookup(ifp->addr & ifp->netmask,mask2width(ifp->netmask));
	if(rp != NULL)
		rt_drop(rp->target,rp->bits);

	ifp->netmask = htol(argv[1]);
	rt_add(ifp->addr,mask2width(ifp->netmask),0L,ifp,0L,0L,0);
	return 0;
}

/* Command to set interface encapsulation mode */
static int
ifencap(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	if(setencap(ifp,argv[1]) != 0){
		printf("Encapsulation mode '%s' unknown\n",argv[1]);
		return 1;
	}
	return 0;
}
/* Function to set encapsulation mode */
int
setencap(struct iface *ifp,char *mode)
{
	struct iftype *ift;

	for(ift = &Iftypes[0];ift->name != NULL;ift++)
		if(strnicmp(ift->name,mode,strlen(mode)) == 0)
			break;
	if(ift->name == NULL)
		return -1;

	if(ifp != NULL){
		ifp->iftype = ift;
		ifp->send = ift->send;
		ifp->output = ift->output;
	}
	return 0;
}
/* Set interface receive buffer size */
static int
ifrxbuf(int argc,char *argv[],void *p)
{
	return 0;	/* To be written */
}

/* Set interface Maximum Transmission Unit */
static int
ifmtu(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	ifp->mtu = atoi(argv[1]);
	return 0;
}

/* Set interface forwarding */
static int
ifforw(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	ifp->forw = if_lookup(argv[1]);
	if(ifp->forw == ifp)
		ifp->forw = NULL;
	return 0;
}

/* Display the parameters for a specified interface */
static void
showiface(struct iface *ifp)
{
	char tmp[25];

	printf("%-10s IP addr %s MTU %u Link encap %s\n",ifp->name,
	 inet_ntoa(ifp->addr),(int)ifp->mtu,
	 ifp->iftype != NULL ? ifp->iftype->name : "not set");
	if(ifp->iftype != NULL && ifp->iftype->format != NULL && ifp->hwaddr != NULL){
		printf("           Link addr %s\n",
		 (*ifp->iftype->format)(tmp,ifp->hwaddr));
	}
	printf("           trace 0x%x netmask 0x%08lx broadcast %s\n",
		ifp->trace,ifp->netmask,inet_ntoa(ifp->broadcast));
	if(ifp->forw != NULL)
		printf("           output forward to %s\n",ifp->forw->name);
	printf("           sent: ip %lu tot %lu idle %s qlen %u",
	 ifp->ipsndcnt,ifp->rawsndcnt,tformat(secclock() - ifp->lastsent),
		len_q(ifp->outq));
	if(ifp->outlim != 0)
		printf("/%u",ifp->outlim);
	if(ifp->txbusy)
		printf(" BUSY");
	printf("\n");
	printf("           recv: ip %lu tot %lu idle %s\n",
	 ifp->iprecvcnt,ifp->rawrecvcnt,tformat(secclock() - ifp->lastrecv));
}

/* Command to detach an interface */
int
dodetach(int argc,char *argv[],void *p)
{
	register struct iface *ifp;

	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(if_detach(ifp) == -1)
		printf("Can't detach loopback or encap interface\n");
	return 0;
}
/* Detach a specified interface */
int
if_detach(struct iface *ifp)
{
	struct iface *iftmp;
	struct route *rp,*rptmp;
	int i,j;

	if(ifp == &Loopback || ifp == &Encap)
		return -1;

	/* Drop all routes that point to this interface */
	if(R_default.iface == ifp)
		rt_drop(0L,0);	/* Drop default route */

	for(i=0;i<HASHMOD;i++){
		for(j=0;j<32;j++){
			for(rp = Routes[j][i];rp != NULL;rp = rptmp){
				/* Save next pointer in case we delete this entry */
				rptmp = rp->next;
				if(rp->iface == ifp)
					rt_drop(rp->target,rp->bits);
			}
		}
	}
	/* Unforward any other interfaces forwarding to this one */
	for(iftmp = Ifaces;iftmp != NULL;iftmp = iftmp->next){
		if(iftmp->forw == ifp)
			iftmp->forw = NULL;
	}
	/* Call device shutdown routine, if any */
	if(ifp->stop != NULL)
		(*ifp->stop)(ifp);

	killproc(ifp->rxproc);
	killproc(ifp->txproc);
	killproc(ifp->supv);

	/* Free allocated memory associated with this interface */
	if(ifp->name != NULL)
		free(ifp->name);
	if(ifp->hwaddr != NULL)
		free(ifp->hwaddr);
	/* Remove from interface list */
	if(ifp == Ifaces){
		Ifaces = ifp->next;
	} else {
		/* Search for entry just before this one
		 * (necessary because list is only singly-linked.)
		 */
		for(iftmp = Ifaces;iftmp != NULL ;iftmp = iftmp->next)
			if(iftmp->next == ifp)
				break;
		if(iftmp != NULL && iftmp->next == ifp)
			iftmp->next = ifp->next;
	}
	/* Finally free the structure itself */
	free(ifp);
	return 0;
}
static int
iftxqlen(int argc,char *argv[],void *p)
{
	struct iface *ifp = p;

	setint(&ifp->outlim,"TX queue limit",argc,argv);
	return 0;
}

/* Given the ascii name of an interface, return a pointer to the structure,
 * or NULL if it doesn't exist
 */
struct iface *
if_lookup(char *name)
{
	register struct iface *ifp;

	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next)
		if(strcmp(ifp->name,name) == 0)
			break;
	return ifp;
}

/* Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULL otherwise.
 * This is used to tell if an incoming IP datagram is for us, or if it
 * has to be routed.
 */
struct iface *
ismyaddr(int32 addr)
{
	register struct iface *ifp;

	if(addr == INADDR_ANY)
		return &Loopback;
	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next)
		if(addr == ifp->addr)
			break;
	return ifp;
}

/* Given a network mask, return the number of contiguous 1-bits starting
 * from the most significant bit.
 */
static int
mask2width(int32 mask)
{
	int width,i;

	width = 0;
	for(i = 31;i >= 0;i--){
		if(!(mask & (1L << i)))
			break;
		width++;
	}
	return width;
}

/* return buffer with name + comment */
char *
if_name(struct iface *ifp,char *comment)
{
	char *result;

	result = mallocw(strlen(ifp->name) + strlen(comment) + 1);
	strcpy(result,ifp->name);
	strcat(result,comment);
	return result;
}

/* Raw output routine that tosses all packets. Used by dialer, tip, etc */
int
bitbucket(struct iface *ifp,struct mbuf **bpp)
{
	free_p(bpp);
	return 0;
}
/* 
 * dial <iface> <seconds> [device dependent args]	(begin autodialing)
 * dial <iface> 0	(stop autodialing) 
 * dial <iface>	(display status)
 */
int
dodialer(int argc,char *argv[],void *p)
{
	struct iface *ifp;
	int32 timeout;

	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc < 3){
		if(ifp->iftype->dstat != NULL)
			(*ifp->iftype->dstat)(ifp);
		return 0;
	}
	if(ifp->iftype->dinit == NULL){
		printf("Dialing not supported on %s\n",argv[1]);
		return 1;
	}
	timeout = atol(argv[2]) * 1000L;
	return (*ifp->iftype->dinit)(ifp,timeout,argc-3,argv+3);
}

