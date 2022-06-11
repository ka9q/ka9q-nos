/* This file contains code to implement the Routing Information Protocol (RIP)
 * and is derived from 4.2BSD code. Mike Karels of Berkeley has stated on
 * TCP-IP that the code may be freely used as long as UC Berkeley is
 * credited. (Well, here's some credit :-). AGB 4-28-88

 * Further documentation on the RIP protocol is now available in Charles
 * Hedrick's draft RFC, as yet unnumbered. AGB 5-6-88
 *
 * The RIP RFC has now been issued as RFC1058. AGB 7-23-88
 *
 * Code gutted and substantially rewritten. KA9Q 9/89
 */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "udp.h"
#include "timer.h"
#include "iface.h"
#include "ip.h"
#include "internet.h"
#include "rip.h"
#include "arp.h"

struct rip_stat Rip_stat;
uint16 Rip_trace;
int Rip_merge;
struct rip_list *Rip_list;
struct udp_cb *Rip_cb;

struct rip_refuse *Rip_refuse;

static void rip_rx(struct iface *iface,struct udp_cb *sock,int cnt);
static void proc_rip(struct iface *iface,int32 gateway,
	struct rip_route *ep,int32 ttl);
static uint8 *putheader(uint8 *cp,enum ripcmd command,uint8 version);
static uint8 *putentry(uint8 *cp,uint16 fam,int32 target,int32 metric);
static void rip_shout(void *p);
static void send_routes(int32 dest,uint16 port,int split,int trig,
	int us);

/* Send RIP CMD_RESPONSE packet(s) to the specified rip_list entry */
static void
rip_shout(p)
void *p;
{
	register struct rip_list *rl;

	rl = (struct rip_list *)p;
	stop_timer(&rl->rip_time);
	send_routes(rl->dest,RIP_PORT,rl->flags.rip_split,0,rl->flags.rip_us);
	set_timer(&rl->rip_time,rl->interval*1000L);
	start_timer(&rl->rip_time);
}

/* Send the routing table. */
static void
send_routes(dest,port,split,trig,us)
int32 dest;		/* IP destination address to send to */
uint16 port;
int split;		/* Do split horizon? */
int trig;		/* Send only triggered updates? */
int us;			/* Include our address in update */
{
	uint8 *cp;
	int i,bits,numroutes,maxroutes;
	uint16 pktsize;
	struct mbuf *bp;
	struct route *rp;
	struct socket lsock,fsock;
	struct iface *iface;

	if((rp = rt_lookup(dest)) == NULL)
		return;	/* No route exists, can't do it */
	iface = rp->iface;

	/* Compute maximum packet size and number of routes we can send */
	pktsize = ip_mtu(dest) - IPLEN;
	pktsize = min(pktsize,MAXRIPPACKET);
	maxroutes = (pktsize - RIPHEADER) / RIPROUTE;

	lsock.address = INADDR_ANY;
	lsock.port = RIP_PORT;
	fsock.address = dest;
	fsock.port = port;

	/* Allocate space for a full size RIP packet and generate header */
	if((bp = alloc_mbuf(pktsize)) == NULL)
		return; 
	numroutes = 0;
	cp = putheader(bp->data,RIPCMD_RESPONSE,RIPVERSION);

	/* Emit route to ourselves, if requested */
	if(us){
		cp = putentry(cp,RIP_IPFAM,iface->addr,1);
		numroutes++;
	}
	/* Emit default route, if appropriate */
	if(R_default.iface != NULL && !R_default.flags.rtprivate
	 && (!trig || R_default.flags.rttrig)){
		if(!split || iface != R_default.iface){
	 		cp = putentry(cp,RIP_IPFAM,0,R_default.metric);
			numroutes++;
		} else if(trig){
			cp = putentry(cp,RIP_IPFAM,0,RIP_INFINITY);
			numroutes++;
		}
	}
	for(bits=0;bits<32;bits++){
		for(i=0;i<HASHMOD;i++){
			for(rp = Routes[bits][i];rp != NULL;rp=rp->next){
				if(rp->flags.rtprivate
				 || (trig && !rp->flags.rttrig)) 
					continue;

				if(numroutes >= maxroutes){
					/* Packet full, flush and make another */
					bp->cnt = RIPHEADER + numroutes * RIPROUTE;
					send_udp(&lsock,&fsock,0,0,&bp,bp->cnt,0,0);
					Rip_stat.output++;
					if((bp = alloc_mbuf(pktsize)) == NULL)
						return; 
					numroutes = 0;
					cp = putheader(bp->data,RIPCMD_RESPONSE,RIPVERSION);
				}
				if(!split || iface != rp->iface){
			 		cp = putentry(cp,RIP_IPFAM,rp->target,rp->metric+1);
					numroutes++;
				} else if(trig){
			 		cp = putentry(cp,RIP_IPFAM,rp->target,RIP_INFINITY);
					numroutes++;
				}
			}
		}
	}
	if(numroutes != 0){
		bp->cnt = RIPHEADER + numroutes * RIPROUTE;
		send_udp(&lsock,&fsock,0,0,&bp,bp->cnt,0,0);
		Rip_stat.output++;
	} else {
		free_p(&bp);
	}
}
/* Add an entry to the rip broadcast list */
int
rip_add(dest,interval,split,us)
int32 dest;
int32 interval;
int split,us;
{
	register struct rip_list *rl;
	struct route *rp;

	if((rp = rt_lookup(dest)) == NULL){
		printf("%s is unreachable\n",inet_ntoa(dest));
		return 1;
	}
	for(rl = Rip_list; rl != NULL; rl = rl->next)
		if(rl->dest == dest)
			break;

	if(rl == NULL){
		/* get a chunk of memory for the rip interface descriptor */
		rl = (struct rip_list *)callocw(1,sizeof(struct rip_list));

		/* tack this record on as the first in the list */
		rl->next = Rip_list;
		if(rl->next != NULL)
			rl->next->prev = rl;
		Rip_list = rl;
		rl->dest = dest;
	}
	/* and the interface ptr, tick interval and flags */
	rl->iface = rp->iface;
	rl->interval = interval;
	rl->flags.rip_split = split;
	rl->flags.rip_us = us;

	/* set up the timer stuff */
	rl->rip_time.func = rip_shout;
	rl->rip_time.arg = rl;
	/* This will initialize the timer and do an immediate broadcast */
	rip_shout(rl);
	return 0;
}

/* add a gateway to the rip_refuse list which allows us to ignore their
 * advertisements
*/
int
riprefadd(gateway)
int32 gateway;
{
	register struct rip_refuse *rl;

	for(rl = Rip_refuse; rl != NULL; rl = rl->next)
		if(rl->target == gateway)
			return 0;	/* Already in table */
  

	/* get a chunk of memory for the rip interface descriptor */
	rl = (struct rip_refuse *)callocw(1,sizeof(struct rip_refuse));

	/* tack this record on as the first in the list */
	rl->next = Rip_refuse;
	if(rl->next != NULL)
		rl->next->prev = rl;
	Rip_refuse = rl;

	/* fill in the gateway to ignore */
	rl->target = gateway;
	return 0;
}

/* drop a RIP target */
int
rip_drop(dest)
int32	dest;
{
	register struct rip_list *rl;

	for(rl = Rip_list; rl != NULL; rl = rl->next)
		if(rl->dest == dest)
			break;

	/* leave if we didn't find it */
	if(rl == NULL)
		return 0;

	/* stop the timer */
	stop_timer(&rl->rip_time);

	/* Unlink from list */
	if(rl->next != NULL)
		rl->next->prev = rl->prev;
	if(rl->prev != NULL)
		rl->prev->next = rl->next;
	else
		Rip_list = rl->next;

	/* and deallocate the descriptor memory */
	free(rl);
	return 0;
}

/* drop a RIP-refuse target from the rip_refuse list */
int
riprefdrop(gateway)
int32 gateway;
{
	register struct rip_refuse *rl;
	
	for(rl = Rip_refuse; rl != NULL; rl = rl->next)
		if(rl->target == gateway)
			break;
  
	/* leave if we didn't find it */
	if(rl == NULL)
		return 0;

	/* Unlink from list */
	if(rl->next != NULL)
		rl->next->prev = rl->prev;
	if(rl->prev != NULL)
		rl->prev->next = rl->next;
	else
		Rip_refuse = rl->next;

	/* and deallocate the structure memory */
	free(rl);
	return 0;
}

/* function to output a RIP CMD_RESPONSE packet for the rip_trigger list */
void
rip_trigger()
{
	register struct rip_list *rl;
	int bits,i;
	struct route *rp;

	for(rl=Rip_list;rl != NULL;rl = rl->next){
		send_routes(rl->dest,RIP_PORT,rl->flags.rip_split,1,0);
	}
	/* Clear the trigger list */
	R_default.flags.rttrig = 0;
	for(bits=0;bits<32;bits++){
		for(i=0;i<HASHMOD;i++){
			for(rp = Routes[bits][i];rp != NULL;rp = rp->next){
				rp->flags.rttrig = 0;
			}
		}
	}
}

/* Start RIP agent listening at local RIP UDP port */
int
rip_init()
{
	struct socket lsock;

	lsock.address = INADDR_ANY;
	lsock.port = RIP_PORT;

	if(Rip_cb == NULL)
		Rip_cb = open_udp(&lsock,rip_rx);

	return 0;
}

/* Process RIP input received from 'interface'. */
static void
rip_rx(iface,sock,cnt)
struct iface *iface;
struct udp_cb *sock;
int cnt;
{
	struct mbuf *bp;
	struct socket fsock;
	enum ripcmd cmd;
	register struct rip_refuse *rfl;
	struct rip_route entry;
	struct route *rp;
	struct rip_list *rl;
	int32 ttl;

	/* receive the RIP packet */
	recv_udp(sock,&fsock,&bp);

	/* increment the rcvd cnt */
	Rip_stat.rcvd++;

	/* check the gateway of this packet against the rip_refuse list and
	 * discard it if a match is found
	 */
	for(rfl=Rip_refuse;rfl != NULL;rfl = rfl->next){
		if(fsock.address == rfl->target){
			Rip_stat.refusals++;
			if(Rip_trace > 1)
				printf("RIP refused from %s\n",
				 inet_ntoa(fsock.address));
			free_p(&bp);
			return;
		 }
	}
	cmd = PULLCHAR(&bp);
	/* Check the version of the frame */
	if(PULLCHAR(&bp) != RIPVERSION){
		free_p(&bp);
		Rip_stat.version++;
		return;
	}
	switch(cmd){
	case RIPCMD_RESPONSE:
		if(Rip_trace > 1)
			printf("RIPCMD_RESPONSE from %s \n",inet_ntoa(fsock.address));

		Rip_stat.response++;
		/* See if this interface is on our broadcast list; if so,
		 * use its interval to calculate entry lifetimes. Otherwise,
		 * use default
		 */
		ttl = RIP_TTL;
		for(rl=Rip_list; rl != NULL; rl = rl->next){
			if(rl->iface == iface){
				ttl = rl->interval * 4;
				break;
			}
		}
		(void)pull16(&bp);	/* remove one word of padding */
		while(len_p(bp) >= RIPROUTE){
			pullentry(&entry,&bp);
			proc_rip(iface,fsock.address,&entry,ttl);
		}
		/* If we can't reach the sender of this update, or if
		 * our existing route is not through the interface we
		 * got this update on, add him as a host specific entry
		 */
		if((rp = rt_blookup(fsock.address,32)) != NULL){
			/* Host-specific route already exists, refresh it */
			start_timer(&rp->timer);
		} else if((rp = rt_lookup(fsock.address)) == NULL
		 || rp->iface != iface){
			entry.addr_fam = RIP_IPFAM;
			entry.target = fsock.address;
			entry.metric = 0; /* will get incremented to 1 */
			proc_rip(iface,fsock.address,&entry,ttl);
		}
		if(Rip_merge)
			rt_merge(Rip_trace);
		rip_trigger();
		break;
	case RIPCMD_REQUEST:
		if(Rip_trace > 1)
			printf("RIPCMD_REQUEST\n");

		Rip_stat.request++;
		/* For now, just send the whole table with split horizon
		 * enabled when the source port is RIP_PORT, and send
		 * the whole table with split horizon disable when another
		 * source port is used. This should be replaced with a more
		 * complete implementation that checks for non-global requests
		 */
		if(fsock.port == RIP_PORT)
			send_routes(fsock.address,fsock.port,1,0,1);
		else
			send_routes(fsock.address,fsock.port,0,0,1);
		break;
	default:
		if(Rip_trace > 1)
			printf("RIPCMD: Unknown Type\n");

		Rip_stat.unknown++;
		break;
	} /* switch */
	free_p(&bp);
}
/* Apply a set of heuristics for determining the number of significant bits
 * (i.e., the address mask) in the target address. Needed since RIP doesn't
 * include the address mask for each entry.
 */
int
nbits(target)
int32 target;
{
	int bits;

	if(target == 0)
		return 0;	/* Special case: 0.0.0.0 is the default route */

	/* Check the host-part bytes of
	 * the address to check for byte-wide zeros
	 * which we'll consider to be subnet routes.
	 * e.g.	44.80.0.0 will be considered to be equal to 44.80/16
	 * whereas 44.80.1.0 will be considered to be 44.80.1/24
	 */
	switch (hibyte(hiword(target)) >> 6) {
	case 3:	/* Class C address */
		/*is it a host address ? i.e. are there any 1's in the
		 * host part ?
		 */
		if(target & 0xff)
			bits = 32;
		else
			bits = 24;
		break;
	case 2:  /* Class B address */
		if(target & 0xff)
			bits = 32;
		else if(target & 0xff00)
			bits = 24;
		else
			bits = 16;
		break;
        case 0:	  /* Class A address */
        case 1:
		if(target & 0xff)
			bits = 32;
		else if(target & 0xff00)
			bits = 24;
		else if(target & 0xff0000)
			bits = 16;
		else
			bits = 8;
	}

	return bits;
}
/* Remove and process a RIP response entry from a packet */
static void
proc_rip(iface,gateway,ep,ttl)
struct iface *iface;
int32 gateway;
register struct rip_route *ep;
int32 ttl;
{
	unsigned int bits;
	register struct route *rp;
	int add = 0;	/* action flags */
	int drop = 0;
	int trigger = 0;

	if(ep->addr_fam != RIP_IPFAM) {
		/* Skip non-IP addresses */
		if(Rip_trace > 1)
			printf("RIP_rx: Not an IP RIP packet !\n");
		Rip_stat.addr_family++;
		return;
	}
	/* Guess at the mask, since it's not explicit */
	bits = nbits(ep->target);

	/* Don't ever add a route to myself through somebody! */
	if(bits == 32 && ismyaddr(ep->target) != NULL){
		if(Rip_trace > 1){
			printf("route to self: %s %ld\n",
			 inet_ntoa(ep->target),ep->metric);
		}
		return;
	}
	/* Find existing entry, if any */
	rp = rt_blookup(ep->target,bits);

	/* Don't touch private routes */
	if(rp != NULL && rp->flags.rtprivate)
		return;

	if(rp == NULL){
		if(ep->metric < RIP_INFINITY){
			/* New route; add it and trigger an update */
			add++;
			trigger++;
		}
	} else if(rp->metric == RIP_INFINITY){
		/* Route is in hold-down; ignore this guy */
		if(Rip_trace > 0){
			printf("ignored (hold-down): %s %lu\n",
			 inet_ntoa(ep->target),ep->metric);
		}
	} else if(rp->gateway == gateway && rp->iface == iface){
		/* This is the gateway for the entry we already have;
		 * restart the timer
		 */
		set_timer(&rp->timer,ttl*1000L);
		start_timer(&rp->timer);
		if(rp->metric != ep->metric){
			/* Metric has changed. Update it and trigger an
			 * update. If route has become unavailable, start
			 * the hold-down timeout.
			 */
			if(Rip_trace){
				printf("metric change: %s %lu -> %lu\n",
				 inet_ntoa(ep->target),rp->metric,ep->metric);
			}
			if(ep->metric == RIP_INFINITY)
				rt_timeout(rp);	/* Enter hold-down timeout */
			else
				rp->metric = ep->metric;
			trigger++;
		}
	} else {
		/* Entry is from a different gateway than the current route */
		if(ep->metric < rp->metric){
			/* Switch to a new gateway */
			if(Rip_trace > 0){
				printf("metric better: %s %lu\n",
				 inet_ntoa(ep->target),ep->metric);
			}
			drop++;
			add++;
			trigger++;
		} else {
			/* Metric is no better, stay with current route */
			if(Rip_trace > 1){
				printf("metric not better: %s %lu\n",
				 inet_ntoa(ep->target),ep->metric);
			}
		}
	}
	if(drop){
		/* Switching to a better gateway; delete old entry */
		if(Rip_trace){
			printf("route drop [%s]/%u",
			 inet_ntoa(ep->target),bits);
			if(rp != NULL)
				printf(" %s %s %lu",rp->iface->name,
				 inet_ntoa(rp->gateway),rp->metric);
			printf("\n");
		}
		rt_drop(ep->target,bits);
	}
	if(add){
		/* Add a new entry */
		if(Rip_trace > 0){
			printf("route add [%s]/%u %s",inet_ntoa(ep->target),
			 bits,iface->name);
			printf(" [%s] %u\n",inet_ntoa(gateway),
			 (int)ep->metric);
		}
		rp = rt_add(ep->target,(unsigned) bits,gateway,iface,
		 (int) ep->metric,ttl,0);
	}
	/* If the route changed, mark it for a triggered update */
	if(trigger){
		rp->flags.rttrig = 1;
	}
}
/* Send a RIP request packet to the specified destination */
int
ripreq(dest,replyport)
int32 dest;
uint16 replyport;
{
	struct mbuf *bp;
	struct socket lsock,fsock;
	uint8 *cp;

	lsock.address = INADDR_ANY;
	lsock.port = replyport;

	/* if we were given a valid dest addr, ask it (the routers on that net)
	 * for a default gateway
	 */
	if(dest == 0)
		return 0;

	fsock.address = dest;
	fsock.port = RIP_PORT;

	/* Send out one RIP Request packet as a broadcast to 'dest'  */
	if((bp = alloc_mbuf(RIPHEADER + RIPROUTE)) == NULL)
		return -1;

	cp = putheader(bp->data,RIPCMD_REQUEST,RIPVERSION);
	cp = putentry(cp,0,0L,RIP_INFINITY);
	bp->cnt = RIPHEADER + RIPROUTE;
	send_udp(&lsock, &fsock,0,0,&bp,bp->cnt,0,0);
	Rip_stat.output++;
	return 0;
}
void
pullentry(ep,bpp)
register struct rip_route *ep;
struct mbuf **bpp;
{
	ep->addr_fam = pull16(bpp);
	(void)pull16(bpp);
	ep->target = pull32(bpp);
	(void)pull32(bpp);
	(void)pull32(bpp);
	ep->metric = pull32(bpp);
}

/* Write the header of a RIP packet */
static uint8 *
putheader(cp,command,version)
register uint8 *cp;
enum ripcmd command;
uint8 version;
{
	*cp++ = command;
	*cp++ = version;
	return put16(cp,0);
}

/* Write a single entry into a rip packet */
static uint8 *
putentry(cp,fam,target,metric)
register uint8 *cp;
uint16 fam;
int32 target;
int32 metric;
{
	cp = put16(cp,fam);
	cp = put16(cp,0);
	cp = put32(cp,target);
	cp = put32(cp,0L);
	cp = put32(cp,0L);
	return put32(cp,metric);
}
/* Route timeout handler. If route has already been marked for deletion
 * then delete it. Otherwise mark for deletion and restart timer.
 */
void
rt_timeout(s)
void *s;
{
	register struct route *rp = (struct route *)s;

	stop_timer(&rp->timer);
	if(rp->metric < RIP_INFINITY){
		rp->metric = RIP_INFINITY;
		if(dur_timer(&rp->timer) == 0)
			set_timer(&rp->timer,RIP_TTL*1000L);
		/* wait 2/3 of timeout before garbage collect */
		set_timer(&rp->timer,dur_timer(&rp->timer)*2/3);
		rp->timer.func = (void *)rt_timeout;
		rp->timer.arg = (void *)rp;
		start_timer(&rp->timer);
		/* Route changed; mark it for triggered update */
		rp->flags.rttrig = 1;
		rip_trigger();
	} else {
		rt_drop(rp->target,rp->bits);
	}
}
