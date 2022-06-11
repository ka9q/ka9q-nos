/* This module implements the serial line framing method used by
 * net/rom nodes.  This allows the net/rom software to talk to
 * an actual net/rom over its serial interface, which is useful
 * if we want to do packet switching for multi-line wormholes.
 *
 * Copyright 1989 Dan Frank, W9NK
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "ax25.h"
#include "nrs.h"
#include "asy.h"
#include "trace.h"
#include "commands.h"

static struct mbuf *nrs_encode(struct mbuf *bp);
static struct mbuf *nrs_decode(int dev,uint8 c);

/* control structures, sort of overlayed on async control blocks */
struct nrs Nrs[ASY_MAX];

int
nrs_init(ifp)
struct iface *ifp;
{
	int xdev;
	struct nrs *np;
	char *ifn;

	/* Set up a net/rom serial iface */
	for(xdev = 0;xdev < NRS_MAX;xdev++){
		np = &Nrs[xdev];
		if(np->iface == NULL)
			break;
	}
	if(xdev >= NRS_MAX) {
		printf("Too many nrs devices\n");
		return -1;
	}
	/* no call supplied? */
	ifp->ioctl = asy_ioctl;
	ifp->raw = nrs_raw;

	ifp->hwaddr = mallocw(AXALEN);
	memcpy(ifp->hwaddr,Mycall,AXALEN);
	ifp->xdev = xdev;
	np->iface = ifp;
	np->send = asy_send;
	np->get = get_asy;
	ifp->rxproc = newproc( ifn = if_name( ifp, " nrs" ),
		256,nrs_recv,xdev,NULL,NULL,0);
	free(ifn);
	return 0;
}
int
nrs_free(ifp)
struct iface *ifp;
{
	if(Nrs[ifp->xdev].iface == ifp)
		Nrs[ifp->xdev].iface = NULL;
	return 0;
}
/* Send a raw net/rom serial frame */
int
nrs_raw(
struct iface *iface,
struct mbuf **bpp
){
	struct mbuf *bp1;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();

	if((bp1 = nrs_encode(*bpp)) == NULL){
		free_p(bpp);
		return -1;
	}
	return Nrs[iface->xdev].send(iface->dev,&bp1);
}

/* Encode a packet in net/rom serial format */
static struct mbuf *
nrs_encode(bp)
struct mbuf *bp;
{
	struct mbuf *lbp;	/* Mbuf containing line-ready packet */
	register uint8 *cp;
	int c;
	uint8 csum = 0;

	/* Allocate output mbuf that's twice as long as the packet.
	 * This is a worst-case guess (consider a packet full of STX's!)
	 * Add five bytes for STX, ETX, checksum, and two nulls.
	 */
	lbp = alloc_mbuf((uint16)(2*len_p(bp) + 5));
	if(lbp == NULL){
		/* No space; drop */
		free_p(&bp);
		return NULL;
	}
	cp = lbp->data;

	*cp++ = STX;

	/* Copy input to output, escaping special characters */
	while((c = PULLCHAR(&bp)) != -1){
		switch(c){
		case STX:
		case ETX:
		case DLE:
			*cp++ = DLE;
			/* notice drop through to default */
		default:
			*cp++ = c;
		}
		csum += c;
	}
	*cp++ = ETX;
	*cp++ = csum;
	*cp++ = NUL;
	*cp++ = NUL;
	
	lbp->cnt = cp - lbp->data;
	return lbp;
}
/* Process incoming bytes in net/rom serial format
 * When a buffer is complete, return it; otherwise NULL
 */
static struct mbuf *
nrs_decode(dev,c)
int dev;	/* net/rom unit number */
uint8 c;	/* Incoming character */
{
	struct mbuf *bp;
	register struct nrs *sp;

	sp = &Nrs[dev];
	switch(sp->state) {
		case NRS_INTER:
			if(c == STX) {	/* look for start of frame */
				sp->state = NRS_INPACK;	/* we're in a packet */
				sp->csum = 0;				/* reset checksum */
			}
			return NULL;
		case NRS_CSUM:
			bp = sp->rbp;
			sp->rbp = NULL;
			sp->rcnt = 0;
			sp->state = NRS_INTER;	/* go back to inter-packet state */
			if(sp->csum == c) {
				sp->packets++;
			} else {
				free_p(&bp);	/* drop packet with bad checksum */
				bp = NULL;
				sp->errors++;	/* increment error count */
			}
			return bp;
		case NRS_ESCAPE:
			sp->state = NRS_INPACK;	/* end of escape */
			break;			/* this will drop through to char processing */
		case NRS_INPACK:
			switch (c) {
			/* If we see an STX in a packet, assume that previous */
			/* packet was trashed, and start a new packet */
			case STX:
				free_p(&sp->rbp);
				sp->rbp = NULL;
				sp->rcnt = 0;
				sp->csum = 0;
				sp->errors++;
				return NULL;
			case ETX:
				sp->state = NRS_CSUM;	/* look for checksum */
				return NULL;
			case DLE:
				sp->state = NRS_ESCAPE;
				return NULL;
			}
	}
	/* If we get to here, it's with a character that's part of the packet.
	 * Make sure there's space for it.
	 */
	if(sp->rbp == NULL){
		/* Allocate first mbuf for new packet */
		if((sp->rbp1 = sp->rbp = alloc_mbuf(NRS_ALLOC)) == NULL) {
			sp->state = NRS_INTER;
			return NULL; /* No memory, drop */
		}
		sp->rcp = sp->rbp->data;
	} else if(sp->rbp1->cnt == NRS_ALLOC){
		/* Current mbuf is full; link in another */
		if((sp->rbp1->next = alloc_mbuf(NRS_ALLOC)) == NULL){
			/* No memory, drop whole thing */
			free_p(&sp->rbp);
			sp->rbp = NULL;
			sp->rcnt = 0;
			sp->state = NRS_INTER;
			return NULL;
		}
		sp->rbp1 = sp->rbp1->next;
		sp->rcp = sp->rbp1->data;
	}
	/* Store the character, increment fragment and total
	 * byte counts
	 */
	*sp->rcp++ = c;
	sp->rbp1->cnt++;
	sp->rcnt++;
	sp->csum += c;	/* add to checksum */
	return NULL;
}

/* Process net/rom serial line I/O */
void
nrs_recv(dev,v1,v2)
int dev;
void *v1;
void *v2;
{
	char c;
	struct mbuf *bp;
	struct nrs *np;

	np = &Nrs[dev];
	/* Process any pending input */
	while((c = np->get(np->iface->dev)) != EOF){
		if((bp = nrs_decode(dev,c)) == NULL)
			continue;
		net_route(np->iface,&bp);
	}
	if(np->iface->rxproc == Curproc)
		np->iface->rxproc = NULL;
}
/* donrstat:  display status of active net/rom serial interfaces */
int
donrstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct nrs *np;
	register int i;

	printf("Interface   RcvB  NumReceived  CSumErrors\n");

	for(i = 0, np = Nrs; i < ASY_MAX; i++, np++)
		if(np->iface != NULL)
			printf(" %8s   %4d   %10lu  %10lu\n",
			 np->iface->name, np->rcnt,
			 np->packets, np->errors);

	return 0;
}
