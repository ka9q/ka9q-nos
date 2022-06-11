/* Internet User Data Protocol (UDP)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "iface.h"
#include "udp.h"
#include "ip.h"
#include "internet.h"
#include "icmp.h"

static struct udp_cb *lookup_udp(struct socket *socket);

struct mib_entry Udp_mib[] = {
	"",			0,
	"udpInDatagrams",	0,
	"udpNoPorts",		0,
	"udpInErrors",		0,
	"udpOutDatagrams",	0,
};

/* UDP control structures list */
struct udp_cb *Udps;

/* Create a UDP control block for lsocket, so that we can queue
 * incoming datagrams.
 */
struct udp_cb *
open_udp(lsocket,r_upcall)
struct socket *lsocket;
void (*r_upcall)();
{
	register struct udp_cb *up;

	if((up = lookup_udp(lsocket)) != NULL){
		/* Already exists */
		Net_error = CON_EXISTS;
		return NULL;
	}
	up = (struct udp_cb *)callocw(1,sizeof (struct udp_cb));
	up->socket.address = lsocket->address;
	up->socket.port = lsocket->port;
	up->r_upcall = r_upcall;

	up->next = Udps;
	Udps = up;
	return up;
}

/* Send a UDP datagram */
int
send_udp(
struct socket *lsocket,		/* Source socket */
struct socket *fsocket,		/* Destination socket */
char tos,			/* Type-of-service for IP */
char ttl,			/* Time-to-live for IP */
struct mbuf **bpp,		/* Data field, if any */
uint16 length,			/* Length of data field */
uint16 id,			/* Optional ID field for IP */
char df				/* Don't Fragment flag for IP */
){
	struct pseudo_header ph;
	struct udp udp;
	int32 laddr;

	if(bpp == NULL)
		return -1;
	if(length != 0 && *bpp != NULL)
		trim_mbuf(bpp,length);
	else
		length = len_p(*bpp);

	length += UDPHDR;

	laddr = lsocket->address;
	if(laddr == INADDR_ANY)
		laddr = locaddr(fsocket->address);

	udp.source = lsocket->port;
	udp.dest = fsocket->port;
	udp.length = length;

	/* Create IP pseudo-header, compute checksum and send it */
	ph.length = length;
	ph.source = laddr;
	ph.dest = fsocket->address;
	ph.protocol = UDP_PTCL;

	htonudp(&udp,bpp,&ph);
	udpOutDatagrams++;
	ip_send(laddr,fsocket->address,UDP_PTCL,tos,ttl,bpp,length,id,df);
	return (int)length;
}
/* Accept a waiting datagram, if available. Returns length of datagram */
int
recv_udp(up,fsocket,bp)
register struct udp_cb *up;
struct socket *fsocket;		/* Place to stash incoming socket */
struct mbuf **bp;		/* Place to stash data packet */
{
	struct socket sp;
	struct mbuf *buf;
	uint16 length;

	if(up == NULL){
		Net_error = NO_CONN;
		return -1;
	}
	if(up->rcvcnt == 0){
		Net_error = WOULDBLK;
		return -1;
	}
	buf = dequeue(&up->rcvq);
	up->rcvcnt--;

	/* Strip socket header */
	pullup(&buf,&sp,sizeof(struct socket));

	/* Fill in the user's foreign socket structure, if given */
	if(fsocket != NULL){
		fsocket->address = sp.address;
		fsocket->port = sp.port;
	}
	/* Hand data to user */
	length = len_p(buf);
	if(bp != NULL)
		*bp = buf;
	else
		free_p(&buf);
	return (int)length;
}
/* Delete a UDP control block */
int
del_udp(conn)
struct udp_cb *conn;
{
	struct mbuf *bp;
	register struct udp_cb *up;
	struct udp_cb *udplast = NULL;

	for(up = Udps;up != NULL;udplast = up,up = up->next){
		if(up == conn)
			break;
	}
	if(up == NULL){
		/* Either conn was NULL or not found on list */
		Net_error = INVALID;
		return -1;
	}
	/* Get rid of any pending packets */
	while(up->rcvcnt != 0){
		bp = up->rcvq;
		up->rcvq = up->rcvq->anext;
		free_p(&bp);
		up->rcvcnt--;
	}
	/* Remove from list */
	if(udplast != NULL)
		udplast->next = up->next;
	else
		Udps = up->next;	/* was first on list */

	free(up);
	return 0;
}
/* Process an incoming UDP datagram */
void
udp_input(
struct iface *iface,	/* Input interface */
struct ip *ip,		/* IP header */
struct mbuf **bpp,	/* UDP header and data */
int rxbroadcast,	/* The only protocol that accepts 'em */
int32 said
){
	struct pseudo_header ph;
	struct udp udp;
	struct udp_cb *up;
	struct socket lsocket;
	struct socket fsocket;
	uint16 length;

	if(bpp == NULL || *bpp == NULL)
		return;

	/* Create pseudo-header and verify checksum */
	ph.source = ip->source;
	ph.dest = ip->dest;
	ph.protocol = ip->protocol;
	length = ip->length - IPLEN - ip->optlen;
	ph.length = length;

	/* Peek at header checksum before we extract the header. This
	 * allows us to bypass cksum() if the checksum field was not
	 * set by the sender.
	 */
	udp.checksum = udpcksum(*bpp);
	if(udp.checksum != 0 && cksum(&ph,*bpp,length) != 0){
		/* Checksum non-zero, and wrong */
		udpInErrors++;
		free_p(bpp);
		return;
	}
	/* Extract UDP header in host order */
	if(ntohudp(&udp,bpp) != 0){
		/* Truncated header */
		udpInErrors++;
		free_p(bpp);
		return;
	}
	/* If this was a broadcast packet, pretend it was sent to us */
	if(rxbroadcast){
		lsocket.address = iface->addr;
	} else
		lsocket.address = ip->dest;

	lsocket.port = udp.dest;
	/* See if there's somebody around to read it */
	if((up = lookup_udp(&lsocket)) == NULL){
		/* Nope, return an ICMP message */
		if(!rxbroadcast){
			htonudp(&udp,bpp,&ph);
			icmp_output(ip,*bpp,ICMP_DEST_UNREACH,ICMP_PORT_UNREACH,NULL);
		}
		udpNoPorts++;
		free_p(bpp);
		return;
	}
	/* Prepend the foreign socket info */
	fsocket.address = ip->source;
	fsocket.port = udp.source;
	pushdown(bpp,&fsocket,sizeof(fsocket));

	/* Queue it */
	enqueue(&up->rcvq,bpp);
	up->rcvcnt++;
	udpInDatagrams++;
	if(up->r_upcall)
		(*up->r_upcall)(iface,up,up->rcvcnt);
}
/* Look up UDP socket. 
 * Return control block pointer or NULL if nonexistant
 * As side effect, move control block to top of list to speed future
 * searches.
 */
static struct udp_cb *
lookup_udp(socket)
struct socket *socket;
{
	register struct udp_cb *up;
	struct udp_cb *uplast = NULL;

	for(up = Udps;up != NULL;uplast = up,up = up->next){
		if(socket->port == up->socket.port
		 && (socket->address == up->socket.address
		 || up->socket.address == INADDR_ANY)){
			if(uplast != NULL){
				/* Move to top of list */
				uplast->next = up->next;
				up->next = Udps;
				Udps = up;
			}
			return up;
		}
	}
	return NULL;
}

/* Attempt to reclaim unused space in UDP receive queues */
void
udp_garbage(red)
int red;
{
	register struct udp_cb *udp;

	for(udp = Udps;udp != NULL; udp = udp->next){
		mbuf_crunch(&udp->rcvq);
	}
}

