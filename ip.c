#undef SIM
/* Upper half of IP, consisting of send/receive primitives, including
 * fragment reassembly, for higher level protocols.
 * Not needed when running as a standalone gateway.
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "internet.h"
#include "netuser.h"
#include "iface.h"
#include "pktdrvr.h"
#include "ip.h"
#include "icmp.h"

static int fraghandle(struct ip *ip,struct mbuf **bpp);
static void ip_timeout(void *arg);
static void free_reasm(struct reasm *rp);
static void freefrag(struct frag *fp);
static struct reasm *lookup_reasm(struct ip *ip);
static struct reasm *creat_reasm(struct ip *ip);
static struct frag *newfrag(uint16 offset,uint16 last,struct mbuf **bpp);
void ttldec(struct iface *ifp);

struct mib_entry Ip_mib[20] = {
	"",			0,
	"ipForwarding",		1,
	"ipDefaultTTL",		MAXTTL,
	"ipInReceives",		0,
	"ipInHdrErrors",	0,
	"ipInAddrErrors",	0,
	"ipForwDatagrams",	0,
	"ipInUnknownProtos",	0,
	"ipInDiscards",		0,
	"ipInDelivers",		0,
	"ipOutRequests",	0,
	"ipOutDiscards",	0,
	"ipOutNoRoutes",	0,
	"ipReasmTimeout",	TLB,
	"ipReasmReqds",		0,
	"ipReasmOKs",		0,
	"ipReasmFails",		0,
	"ipFragOKs",		0,
	"ipFragFails",		0,
	"ipFragCreates",	0,
};

struct reasm *Reasmq;
uint16 Id_cntr = 0;	/* Datagram serial number */
static struct raw_ip *Raw_ip;
int Ip_trace = 0;

#define	INSERT	0
#define	APPEND	1
#define	PREPEND	2

/* Send an IP datagram. Modeled after the example interface on p 32 of
 * RFC 791
 */
int
ip_send(
int32 source,			/* source address */
int32 dest,			/* Destination address */
char protocol,			/* Protocol */
char tos,			/* Type of service */
char ttl,			/* Time-to-live */
struct mbuf **bpp,		/* Data portion of datagram */
uint16 length,			/* Optional length of data portion */
uint16 id,			/* Optional identification */
char df				/* Don't-fragment flag */
){
	struct ip ip;			/* IP header */

	ipOutRequests++;

	if(bpp == NULL)
		return -1;
	if(source == INADDR_ANY)
		source = locaddr(dest);
	if(length == 0 && *bpp != NULL)
		length = len_p(*bpp);
	if(id == 0)
		id = Id_cntr++;
	if(ttl == 0)
		ttl = ipDefaultTTL;

	/* Fill in IP header */
	ip.version = IPVERSION;
	ip.tos = tos;
	ip.length = IPLEN + length;
	ip.id = id;
	ip.offset = 0;
	ip.flags.mf = 0;
	ip.flags.df = df;
	ip.flags.congest = 0;
	ip.ttl = ttl;
	ip.protocol = protocol;
	ip.source = source;
	ip.dest = dest;
	ip.optlen = 0;
	if(Ip_trace)
		dumpip(NULL,&ip,*bpp,0);

	htonip(&ip,bpp,IP_CS_NEW);
	if(ismyaddr(ip.dest)){
		/* Pretend it has been sent by the loopback interface before
		 * it appears in the receive queue
		 */
#ifdef	SIM
		net_sim(bpp);
#else
		net_route(&Loopback,bpp);
#endif
		Loopback.ipsndcnt++;
		Loopback.rawsndcnt++;
		Loopback.lastsent = secclock();
	} else
		net_route(NULL,bpp);
	return 0;
}

/* Reassemble incoming IP fragments and dispatch completed datagrams
 * to the proper transport module
 */
void
ip_recv(
struct iface *iface,	/* Incoming interface */
struct ip *ip,		/* Extracted IP header */
struct mbuf **bpp,	/* Data portion */
int rxbroadcast,	/* True if received on subnet broadcast address */
int32 spi		/* Security association, if any */
){
	/* Function to call with completed datagram */
	register struct raw_ip *rp;
	struct mbuf *bp1;
	int rxcnt = 0;
	register struct iplink *ipp;

	/* If we have a complete packet, call the next layer
	 * to handle the result. Note that fraghandle passes back
	 * a length field that does NOT include the IP header
	 */
	if(bpp == NULL || fraghandle(ip,bpp) == -1)
		return;		/* Not done yet */

	/* Trim data segment if necessary. */
	trim_mbuf(bpp,ip->length - (IPLEN + ip->optlen));

	ipInDelivers++;
	if(Ip_trace)
		dumpip(iface,ip,*bpp,spi);

	for(rp = Raw_ip;rp != NULL;rp = rp->next){
		if(rp->protocol != ip->protocol)
			continue;
		rxcnt++;
		/* Duplicate the data portion, and put the header back on */
		dup_p(&bp1,*bpp,0,len_p(*bpp));
		if(bp1 != NULL){
			htonip(ip,&bp1,IP_CS_OLD);
			enqueue(&rp->rcvq,&bp1);
			if(rp->r_upcall != NULL)
				(*rp->r_upcall)(rp);
		} else {
			free_p(&bp1);
		}
	}
	/* Look it up in the transport protocol table */
	for(ipp = Iplink;ipp->funct != NULL;ipp++){
		if(ipp->proto == ip->protocol)
			break;
	}
	if(ipp->funct != NULL){
		/* Found, call transport protocol */
		(*ipp->funct)(iface,ip,bpp,rxbroadcast,spi);
	} else {
		/* Not found */
		if(rxcnt == 0){
			/* Send an ICMP Protocol Unknown response... */
			ipInUnknownProtos++;
			/* ...unless it's a broadcast */
			if(!rxbroadcast){
				icmp_output(ip,*bpp,ICMP_DEST_UNREACH,
				 ICMP_PROT_UNREACH,NULL);
			}
		}
		free_p(bpp);
	}
}
/* Handle IP packets encapsulated inside IP */
void
ipip_recv(
struct iface *iface,	/* Incoming interface */
struct ip *ip,		/* Extracted IP header */
struct mbuf **bpp,	/* Data portion */
int rxbroadcast,	/* True if received on subnet broadcast address */
int32 spi
){
	net_route(&Encap,bpp);
}

/* Process IP datagram fragments
 * If datagram is complete, return its length (MINUS header);
 * otherwise return -1
 */
static int
fraghandle(
struct ip *ip,		/* IP header, host byte order */
struct mbuf **bpp	/* The fragment itself */
){
	register struct reasm *rp; /* Pointer to reassembly descriptor */
	struct frag *lastfrag,*nextfrag,*tfp;
	struct mbuf *tbp;
	uint16 i;
	uint16 last;		/* Index of first byte beyond fragment */

	last = ip->offset + ip->length - (IPLEN + ip->optlen);

	rp = lookup_reasm(ip);
	if(ip->offset == 0 && !ip->flags.mf){
		/* Complete datagram received. Discard any earlier fragments */
		if(rp != NULL){
			free_reasm(rp);
			ipReasmOKs++;
		}
		return ip->length;
	}
	ipReasmReqds++;
	if(rp == NULL){
		/* First fragment; create new reassembly descriptor */
		if((rp = creat_reasm(ip)) == NULL){
			/* No space for descriptor, drop fragment */
			ipReasmFails++;
			free_p(bpp);
			return -1;
		}
	}
	/* Keep restarting timer as long as we keep getting fragments */
	stop_timer(&rp->timer);
	start_timer(&rp->timer);

	/* If this is the last fragment, we now know how long the
	 * entire datagram is; record it
	 */
	if(!ip->flags.mf)
		rp->length = last;

	/* Set nextfrag to the first fragment which begins after us,
	 * and lastfrag to the last fragment which begins before us
	 */
	lastfrag = NULL;
	for(nextfrag = rp->fraglist;nextfrag != NULL;nextfrag = nextfrag->next){
		if(nextfrag->offset > ip->offset)
			break;
		lastfrag = nextfrag;
	}
	/* Check for overlap with preceeding fragment */
	if(lastfrag != NULL  && ip->offset < lastfrag->last){
		/* Strip overlap from new fragment */
		i = lastfrag->last - ip->offset;
		pullup(bpp,NULL,i);
		if(*bpp == NULL)
			return -1;	/* Nothing left */
		ip->offset += i;
	}
	/* Look for overlap with succeeding segments */
	for(; nextfrag != NULL; nextfrag = tfp){
		tfp = nextfrag->next;	/* save in case we delete fp */

		if(nextfrag->offset >= last)
			break;	/* Past our end */
		/* Trim the front of this entry; if nothing is
		 * left, remove it.
		 */
		i = last - nextfrag->offset;
		pullup(&nextfrag->buf,NULL,i);
		if(nextfrag->buf == NULL){
			/* superseded; delete from list */
			if(nextfrag->prev != NULL)
				nextfrag->prev->next = nextfrag->next;
			else
				rp->fraglist = nextfrag->next;
			if(tfp->next != NULL)
				nextfrag->next->prev = nextfrag->prev;
			freefrag(nextfrag);
		} else
			nextfrag->offset = last;
	}
	/* Lastfrag now points, as before, to the fragment before us;
	 * nextfrag points at the next fragment. Check to see if we can
	 * join to either or both fragments.
	 */
	i = INSERT;
	if(lastfrag != NULL && lastfrag->last == ip->offset)
		i |= APPEND;
	if(nextfrag != NULL && nextfrag->offset == last)
		i |= PREPEND;
	switch(i){
	case INSERT:	/* Insert new desc between lastfrag and nextfrag */
		tfp = newfrag(ip->offset,last,bpp);
		tfp->prev = lastfrag;
		tfp->next = nextfrag;
		if(lastfrag != NULL)
			lastfrag->next = tfp;	/* Middle of list */
		else
			rp->fraglist = tfp;	/* First on list */
		if(nextfrag != NULL)
			nextfrag->prev = tfp;
		break;
	case APPEND:	/* Append to lastfrag */
		append(&lastfrag->buf,bpp);
		lastfrag->last = last;	/* Extend forward */
		break;
	case PREPEND:	/* Prepend to nextfrag */
		tbp = nextfrag->buf;
		nextfrag->buf = *bpp;
		bpp = NULL;
		append(&nextfrag->buf,&tbp);
		nextfrag->offset = ip->offset;	/* Extend backward */
		break;
	case (APPEND|PREPEND):
		/* Consolidate by appending this fragment and nextfrag
		 * to lastfrag and removing the nextfrag descriptor
		 */
		append(&lastfrag->buf,bpp);
		append(&lastfrag->buf,&nextfrag->buf);
		nextfrag->buf = NULL;
		lastfrag->last = nextfrag->last;

		/* Finally unlink and delete the now unneeded nextfrag */
		lastfrag->next = nextfrag->next;
		if(nextfrag->next != NULL)
			nextfrag->next->prev = lastfrag;
		freefrag(nextfrag);
		break;
	}
	if(rp->fraglist->offset == 0 && rp->fraglist->next == NULL 
		&& rp->length != 0){

		/* We've gotten a complete datagram, so extract it from the
		 * reassembly buffer and pass it on.
		 */
		*bpp = rp->fraglist->buf;
		rp->fraglist->buf = NULL;
		/* Tell IP the entire length */
		ip->length = rp->length + (IPLEN + ip->optlen);
		free_reasm(rp);
		ipReasmOKs++;
		ip->offset = 0;
		ip->flags.mf = 0;
		return ip->length;
	} else
		return -1;
}
/* Arrange for receipt of raw IP datagrams */
struct raw_ip *
raw_ip(
int protocol,
void (*r_upcall)()
){
	register struct raw_ip *rp;

	rp = (struct raw_ip *)callocw(1,sizeof(struct raw_ip));
	rp->protocol = protocol;
	rp->r_upcall = r_upcall;
	rp->next = Raw_ip;
	Raw_ip = rp;
	return rp;
}
/* Free a raw IP descriptor */
void
del_ip(
struct raw_ip *rpp
){
	struct raw_ip *rplast = NULL;
	register struct raw_ip *rp;

	/* Do sanity check on arg */
	for(rp = Raw_ip;rp != NULL;rplast=rp,rp = rp->next)
		if(rp == rpp)
			break;
	if(rp == NULL)
		return;	/* Doesn't exist */

	/* Unlink */
	if(rplast != NULL)
		rplast->next = rp->next;
	else
		Raw_ip = rp->next;
	/* Free resources */
	free_q(&rp->rcvq);
	free(rp);
}

static struct reasm *
lookup_reasm(
struct ip *ip
){
	register struct reasm *rp;
	struct reasm *rplast = NULL;

	for(rp = Reasmq;rp != NULL;rplast=rp,rp = rp->next){
		if(ip->id == rp->id && ip->source == rp->source
		 && ip->dest == rp->dest && ip->protocol == rp->protocol){
			if(rplast != NULL){
				/* Move to top of list for speed */
				rplast->next = rp->next;
				rp->next = Reasmq;
				Reasmq = rp;
			}
			return rp;
		}

	}
	return NULL;
}
/* Create a reassembly descriptor,
 * put at head of reassembly list
 */
static struct reasm *
creat_reasm(
struct ip *ip
){
	register struct reasm *rp;

	if((rp = (struct reasm *)calloc(1,sizeof(struct reasm))) == NULL)
		return rp;	/* No space for descriptor */
	rp->source = ip->source;
	rp->dest = ip->dest;
	rp->id = ip->id;
	rp->protocol = ip->protocol;
	set_timer(&rp->timer,ipReasmTimeout * 1000L);
	rp->timer.func = ip_timeout;
	rp->timer.arg = rp;

	rp->next = Reasmq;
	Reasmq = rp;
	return rp;
}

/* Free all resources associated with a reassembly descriptor */
static void
free_reasm(
struct reasm *r
){
	register struct reasm *rp;
	struct reasm *rplast = NULL;
	register struct frag *fp;

	for(rp = Reasmq;rp != NULL;rplast = rp,rp=rp->next)
		if(r == rp)
			break;
	if(rp == NULL)
		return;	/* Not on list */

	stop_timer(&rp->timer);
	/* Remove from list of reassembly descriptors */
	if(rplast != NULL)
		rplast->next = rp->next;
	else
		Reasmq = rp->next;

	/* Free any fragments on list, starting at beginning */
	while((fp = rp->fraglist) != NULL){
		rp->fraglist = fp->next;
		free_p(&fp->buf);
		free(fp);
	}
	free(rp);
}

/* Handle reassembly timeouts by deleting all reassembly resources */
static void
ip_timeout(
void *arg
){
	free_reasm((struct reasm *)arg);
	ipReasmFails++;
}
/* Create a fragment */
static struct frag *
newfrag(
uint16 offset,
uint16 last,
struct mbuf **bpp
){
	struct frag *fp;

	if((fp = (struct frag *)calloc(1,sizeof(struct frag))) == NULL){
		/* Drop fragment */
		free_p(bpp);
		return NULL;
	}
	fp->buf = *bpp;
	*bpp = NULL;
	fp->offset = offset;
	fp->last = last;
	return fp;
}
/* Delete a fragment, return next one on queue */
static void
freefrag(
struct frag *fp
){
	free_p(&fp->buf);
	free(fp);
}

/* In red alert mode, blow away the whole reassembly queue. Otherwise crunch
 * each fragment on each reassembly descriptor
 */
void
ip_garbage(
int red
){
	struct reasm *rp,*rp1;
	struct frag *fp;
	struct raw_ip *rwp;
	struct iface *ifp;

	/* Run through the reassembly queue */
	for(rp = Reasmq;rp != NULL;rp = rp1){
		rp1 = rp->next;
		if(red){
			free_reasm(rp);
		} else {
			for(fp = rp->fraglist;fp != NULL;fp = fp->next){
				mbuf_crunch(&fp->buf);
			}
		}
	}
	/* Run through the raw IP queue */
	for(rwp = Raw_ip;rwp != NULL;rwp = rwp->next)
		mbuf_crunch(&rwp->rcvq);

	/* Walk through interface output queues and decrement IP TTLs.
	 * Discard and return ICMP TTL exceeded messages for any that
	 * go to zero. (Some argue that this ought to be done all the
	 * time, but it would probably break a lot of machines with
	 * small IP TTL settings using amateur packet radio paths.)
	 *
	 * Also send an ICMP source quench message to one
	 * randomly chosen packet on each queue. If in red mode,
	 * also drop the packet.
	 */
	for(ifp=Ifaces;ifp != NULL;ifp = ifp->next){
		ttldec(ifp);
		rquench(ifp,red);
	}
}
/* Decrement the IP TTL field in each packet on the send queue. If
 * a TTL goes to zero, discard the packet.
 */
void
ttldec(
struct iface *ifp
){
	struct mbuf *bp,*bpprev,*bpnext;
	struct qhdr qhdr;
	struct ip ip;

	bpprev = NULL;
	for(bp = ifp->outq; bp != NULL;bpprev = bp,bp = bpnext){
		bpnext = bp->anext;
		pullup(&bp,&qhdr,sizeof(qhdr));
		ntohip(&ip,&bp);
		if(--ip.ttl == 0){
			/* Drop packet */
			icmp_output(&ip,bp,ICMP_TIME_EXCEED,0,NULL);
			if(bpprev == NULL)	/* First on queue */
				ifp->outq = bpnext;
			else
				bpprev->anext = bpnext;
			free_p(&bp);
			bp = bpprev; 
			continue;
		}
		/* Put IP and queue headers back, restore to queue */
		htonip(&ip,&bp,0);
		pushdown(&bp,&qhdr,sizeof(qhdr));
		if(bpprev == NULL)	/* First on queue */
			ifp->outq = bp;
		else
			bpprev->anext = bp;
		bp->anext = bpnext;
	}
}

/* Execute random quench algorithm on an interface's output queue */
void
rquench(
struct iface *ifp,
int drop
){
	struct mbuf *bp,*bplast;
	int i;
	struct qhdr qhdr;
	struct ip ip;
	struct mbuf *bpdup;

	if((i = len_q(ifp->outq)) == 0)
		return;	/* Queue is empty */

	i = urandom(i);	/* Select a victim */

	/* Search for i-th message on queue */
	bplast = NULL;
	for(bp = ifp->outq;bp != NULL && i>0;i--,bplast=bp,bp=bp->anext)
		;
	if(bp == NULL)
		return;	/* "Can't happen" */

	/* Send a source quench */
	dup_p(&bpdup,bp,0,len_p(bp));
	pullup(&bpdup,&qhdr,sizeof(qhdr));
	ntohip(&ip,&bpdup);
	icmp_output(&ip,bpdup,ICMP_QUENCH,0,NULL);
	free_p(&bpdup);
	if(!drop)
		return;	/* All done */

	/* Drop the packet */
	if(bplast != NULL)
		bplast->anext = bp->anext;
	else
		ifp->outq = bp->anext;	/* First on list */
	free_p(&bp);
}
