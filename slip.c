/* SLIP (Serial Line IP) encapsulation and control routines.
 * Copyright 1991 Phil Karn
 *
 * Van Jacobsen header compression hooks added by Katie Stevens, UC Davis
 *
 *	- Feb 1991	Bill_Simpson@um.cc.umich.edu
 *			reflect changes to header compression calls
 *			revise status display
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ip.h"
#include "slhc.h"
#include "asy.h"
#include "slip.h"
#include "trace.h"
#include "pktdrvr.h"

static struct mbuf *slip_decode(struct slip *sp,uint8 c);
static struct mbuf *slip_encode(struct mbuf **bpp);

/* Slip level control structure */
struct slip Slip[SLIP_MAX];

int
slip_init(ifp)
struct iface *ifp;
{
	int xdev;
	struct slip *sp;
	char *ifn;

	for(xdev = 0;xdev < SLIP_MAX;xdev++){
		sp = &Slip[xdev];
		if(sp->iface == NULL)
			break;
	}
	if(xdev >= SLIP_MAX) {
		printf("Too many slip devices\n");
		return -1;
	}
	ifp->ioctl = asy_ioctl;
	ifp->raw = slip_raw;
	ifp->show = slip_status;
	ifp->xdev = xdev;

	sp->iface = ifp;
	sp->send = asy_send;
	sp->get = get_asy;
	sp->type = CL_SERIAL_LINE;
	if(ifp->send == vjslip_send){
		sp->slcomp = slhc_init(16,16);
	}
	ifp->rxproc = newproc( ifn = if_name( ifp, " rx" ),
		512,slip_rx,xdev,NULL,NULL,0);
	free(ifn);
	return 0;
}
int
slip_free(ifp)
struct iface *ifp;
{
	struct slip *sp;

	sp = &Slip[ifp->xdev];
	if(sp->slcomp != NULL){
		slhc_free(sp->slcomp);
		sp->slcomp = NULL;
	}
	sp->iface = NULL;
	return 0;
}
/* Send routine for point-to-point slip, no VJ header compression */
int
slip_send(
struct mbuf **bpp,	/* Buffer to send */
struct iface *iface,	/* Pointer to interface control block */
int32 gateway,		/* Ignored (SLIP is point-to-point) */
uint8 tos
){
	if(iface == NULL){
		free_p(bpp);
		return -1;
	}
	return (*iface->raw)(iface,bpp);
}
/* Send routine for point-to-point slip, with VJ header compression */
int
vjslip_send(
struct mbuf **bpp,	/* Buffer to send */
struct iface *iface,	/* Pointer to interface control block */
int32 gateway,		/* Ignored (SLIP is point-to-point) */
uint8 tos
){
	register struct slip *sp;
	int type;

	if(iface == NULL){
		free_p(bpp);
		return -1;
	}
	sp = &Slip[iface->xdev];
	/* Attempt IP/ICP header compression */
	type = slhc_compress(sp->slcomp,bpp,TRUE);
	(*bpp)->data[0] |= type;
	return (*iface->raw)(iface,bpp);
}
/* Send a raw slip frame */
int
slip_raw(
struct iface *iface,
struct mbuf **bpp
){
	struct mbuf *bp1;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();
	if((bp1 = slip_encode(bpp)) == NULL){
		return -1;
	}
	if (iface->trace & IF_TRACE_RAW)
		raw_dump(iface,-1,bp1);
	return Slip[iface->xdev].send(iface->dev,&bp1);
}
/* Encode a packet in SLIP format */
static
struct mbuf *
slip_encode(struct mbuf **bpp)
{
	struct mbuf *lbp;	/* Mbuf containing line-ready packet */
	register uint8 *cp;
	int c;

	/* Allocate output mbuf that's twice as long as the packet.
	 * This is a worst-case guess (consider a packet full of FR_ENDs!)
	 */
	lbp = alloc_mbuf((uint16)(2*len_p(*bpp) + 2));
	if(lbp == NULL){
		/* No space; drop */
		free_p(bpp);
		return NULL;
	}
	cp = lbp->data;

	/* Flush out any line garbage */
	*cp++ = FR_END;

	/* Copy input to output, escaping special characters */
	while((c = PULLCHAR(bpp)) != -1){
		switch(c){
		case FR_ESC:
			*cp++ = FR_ESC;
			*cp++ = T_FR_ESC;
			break;
		case FR_END:
			*cp++ = FR_ESC;
			*cp++ = T_FR_END;
			break;
		default:
			*cp++ = c;
		}
	}
	*cp++ = FR_END;
	lbp->cnt = cp - lbp->data;
	return lbp;
}
/* Process incoming bytes in SLIP format
 * When a buffer is complete, return it; otherwise NULL
 */
static
struct mbuf *
slip_decode(sp,c)
register struct slip *sp;
uint8 c;		/* Incoming character */
{
	struct mbuf *bp;

	switch(c){
	case FR_END:
		bp = sp->rbp_head;
		sp->rbp_head = NULL;
		if(sp->escaped){
			/* Treat this as an abort - discard frame */
			free_p(&bp);
			bp = NULL;
		}
		sp->escaped &= ~SLIP_FLAG;
		return bp;	/* Will be NULL if empty frame */
	case FR_ESC:
		sp->escaped |= SLIP_FLAG;
		return NULL;
	}
	if(sp->escaped & SLIP_FLAG){
		/* Translate 2-char escape sequence back to original char */
		sp->escaped &= ~SLIP_FLAG;
		switch(c){
		case T_FR_ESC:
			c = FR_ESC;
			break;
		case T_FR_END:
			c = FR_END;
			break;
		default:
			sp->errors++;
			break;
		}
	}
	/* We reach here with a character for the buffer;
	 * make sure there's space for it
	 */
	if(sp->rbp_head == NULL){
		/* Allocate first mbuf for new packet */
		if((sp->rbp_tail = sp->rbp_head = alloc_mbuf(SLIP_ALLOC)) == NULL)
			return NULL; /* No memory, drop */
		sp->rcp = sp->rbp_head->data;
	} else if(sp->rbp_tail->cnt == SLIP_ALLOC){
		/* Current mbuf is full; link in another */
		if((sp->rbp_tail->next = alloc_mbuf(SLIP_ALLOC)) == NULL){
			/* No memory, drop whole thing */
			free_p(&sp->rbp_head);
			sp->rbp_head = NULL;
			return NULL;
		}
		sp->rbp_tail = sp->rbp_tail->next;
		sp->rcp = sp->rbp_tail->data;
	}
	/* Store the character, increment fragment and total
	 * byte counts
	 */
	*sp->rcp++ = c;
	sp->rbp_tail->cnt++;
	return NULL;
}


/* Process SLIP line input */
void
slip_rx(xdev,p1,p2)
int xdev;
void *p1;
void *p2;
{
	int c;
	struct mbuf *bp;
	register struct slip *sp;
	int cdev;

	sp = &Slip[xdev];
	cdev = sp->iface->dev;

	while ( (c = sp->get(cdev)) != -1 ) {
		if((bp = slip_decode(sp,c)) == NULL)
			continue;	/* More to come */

		if (sp->iface->trace & IF_TRACE_RAW)
			raw_dump(sp->iface,IF_TRACE_IN,bp);

		if ((c = bp->data[0]) & SL_TYPE_COMPRESSED_TCP) {
			if ( slhc_uncompress(sp->slcomp, &bp) <= 0 ) {
				free_p(&bp);
				sp->errors++;
				continue;
			}
		} else if (c >= SL_TYPE_UNCOMPRESSED_TCP) {
			bp->data[0] &= 0x4f;
			if ( slhc_remember(sp->slcomp, &bp) <= 0 ) {
				free_p(&bp);
				sp->errors++;
				continue;
			}
		}
		net_route( sp->iface, &bp);
		/* Especially on slow machines, serial I/O can be quite
		 * compute intensive, so release the machine before we
		 * do the next packet.  This will allow this packet to
		 * go on toward its ultimate destination. [Karn]
		 */
		kwait(NULL);
	}
	if(sp->iface->rxproc == Curproc)
		sp->iface->rxproc = NULL;
}



/* Show serial line status */
void
slip_status(iface)
struct iface *iface;
{
	struct slip *sp;

	if (iface->xdev > SLIP_MAX)
		/* Must not be a SLIP device */
		return;

	sp = &Slip[iface->xdev];
	if (sp->iface != iface)
		/* Must not be a SLIP device */
		return;

	slhc_i_status(sp->slcomp);
	slhc_o_status(sp->slcomp);
}
