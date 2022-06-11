/* Low level AX.25 code:
 *  incoming frame processing (including digipeating)
 *  IP encapsulation
 *  digipeater routing
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "arp.h"
#include "slip.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "ip.h"
#include "devparam.h"
#include <ctype.h>

/* List of AX.25 multicast addresses in network format (shifted ascii).
 * Only the first entry is used for transmission, but an incoming
 * packet with any one of these destination addresses is recognized
 * as a multicast.
 */
uint8 Ax25multi[][AXALEN] = {
	'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1,	/* QST */
	'M'<<1, 'A'<<1, 'I'<<1, 'L'<<1, ' '<<1, ' '<<1, '0'<<1,	/* MAIL */
	'N'<<1, 'O'<<1, 'D'<<1, 'E'<<1, 'S'<<1, ' '<<1, '0'<<1,	/* NODES */
	'I'<<1, 'D'<<1, ' '<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1,	/* ID */
	'O'<<1, 'P'<<1, 'E'<<1, 'N'<<1, ' '<<1, ' '<<1, '0'<<1,	/* OPEN */
	'C'<<1, 'Q'<<1, ' '<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1,	/* CQ */
	'B'<<1, 'E'<<1, 'A'<<1, 'C'<<1, 'O'<<1, 'N'<<1, '0'<<1,	/* BEACON */
	'R'<<1, 'M'<<1, 'N'<<1, 'C'<<1, ' '<<1, ' '<<1, '0'<<1,	/* RMNC */
	'A'<<1, 'L'<<1, 'L'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1,	/* ALL */
	'\0',
};
uint8 Mycall[AXALEN];
struct ax_route *Ax_routes;	/* Routing table header */
int Digipeat = 1;	/* Controls digipeating */

int
axi_send(
struct mbuf **bpp,
struct iface *iface,
int32 gateway,
uint8 tos
){
	return axui_send(bpp,iface,gateway,tos);
}

/* Send IP datagrams across an AX.25 link */
int
axui_send(
struct mbuf **bpp,
struct iface *iface,
int32 gateway,
uint8 tos
){
	struct mbuf *tbp;
	uint8 *hw_addr;
	struct ax25_cb *axp;

	if((hw_addr = res_arp(iface,ARP_AX25,gateway,bpp)) == NULL)
		return 0;	/* Wait for address resolution */

	/* UI frames are used for any one of the following three conditions:
	 * 1. The "low delay" bit is set in the type-of-service field.
	 * 2. The "reliability" TOS bit is NOT set and the interface is in
	 *    datagram mode.
	 * 3. The destination is the broadcast address (this is helpful
	 *    when broadcasting on an interface that's in connected mode).
	 */
	if((tos & IP_COS) == DELAY
	 || ((tos & IP_COS) != RELIABILITY && (iface->send == axui_send))
	 || addreq(hw_addr,Ax25multi[0])){
		/* Use UI frame */
		return (*iface->output)(iface,hw_addr,iface->hwaddr,PID_IP,bpp);
	}
	/* Reliability is needed; use I-frames in AX.25 connection */
	if((axp = find_ax25(hw_addr)) == NULL){
		/* Open a new connection */
		axp = open_ax25(iface,iface->hwaddr,hw_addr,
		 AX_ACTIVE,Axwindow,s_arcall,s_atcall,s_ascall,-1);
		if(axp == NULL){
			free_p(bpp);
			return -1;
		}
	}
	if(axp->state == LAPB_DISCONNECTED){
		est_link(axp);
		lapbstate(axp,LAPB_SETUP);
	}
	/* Insert the PID */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = PID_IP;
	if((tbp = segmenter(bpp,axp->paclen)) == NULL){
		free_p(bpp);
		return -1;
	}
	return send_ax25(axp,&tbp,-1);
}
/* Add header and send connectionless (UI) AX.25 packet.
 * Note that the calling order here must match enet_output
 * since ARP also uses it.
 */
int
ax_output(
struct iface *iface,	/* Interface to use; overrides routing table */
uint8 *dest,		/* Destination AX.25 address (7 bytes, shifted) */
uint8 *source,		/* Source AX.25 address (7 bytes, shifted) */
uint16 pid,		/* Protocol ID */
struct mbuf **bpp	/* Data field (follows PID) */
){
	/* Prepend pid to data */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = (uint8)pid;
	return axsend(iface,dest,source,LAPB_COMMAND,UI,bpp);
}
/* Common subroutine for sendframe() and ax_output() */
int
axsend(
struct iface *iface,	/* Interface to use; overrides routing table */
uint8 *dest,		/* Destination AX.25 address (7 bytes, shifted) */
uint8 *source,		/* Source AX.25 address (7 bytes, shifted) */
int cmdrsp,		/* Command/response indication */
int ctl,		/* Control field */
struct mbuf **bpp	/* Data field (includes PID) */
){
	struct ax25 addr;
	struct ax_route *axr;
	uint8 *idest;
	int rval;

	/* If the source addr is unspecified, use the interface address */
	if(source[0] == '\0')
		source = iface->hwaddr;

	/* If there's a digipeater route, get it */
	axr = ax_lookup(dest);

	memcpy(addr.dest,dest,AXALEN);
	memcpy(addr.source,source,AXALEN);
	addr.cmdrsp = cmdrsp;

	if(axr != NULL){
		memcpy(addr.digis,axr->digis,axr->ndigis*AXALEN);
		addr.ndigis = axr->ndigis;
		idest = addr.digis[0];
	} else {
		addr.ndigis = 0;
		idest = dest;
	}
	addr.nextdigi = 0;

	/* Allocate mbuf for control field, and fill in */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = ctl;

	htonax25(&addr,bpp);
	/* This shouldn't be necessary because redirection has already been
	 * done at the IP router layer, but just to be safe...
	 */
	if(iface->forw != NULL){
		logsrc(iface->forw,iface->forw->hwaddr);
		logdest(iface->forw,idest);
		rval = (*iface->forw->raw)(iface->forw,bpp);
	} else {
		logsrc(iface,iface->hwaddr);
		logdest(iface,idest);
		rval = (*iface->raw)(iface,bpp);
	}
	return rval;
}
/* Process incoming AX.25 packets.
 * After optional tracing, the address field is examined. If it is
 * directed to us as a digipeater, repeat it.  If it is addressed to
 * us or to QST-0, kick it upstairs depending on the protocol ID.
 */
void
ax_recv(
struct iface *iface,
struct mbuf **bpp
){
	uint8 control;
	struct ax25 hdr;
	struct ax25_cb *axp;
	struct ax_route *axr;
	uint8 (*mpp)[AXALEN];
	int mcast;
	uint8 *isrc,*idest;	/* "immediate" source and destination */

	/* Pull header off packet and convert to host structure */
	if(ntohax25(&hdr,bpp) < 0){
		/* Something wrong with the header */
		free_p(bpp);
		return;
	}
	/* If there were digis in this packet and at least one has
	 * been passed, then the last passed digi is the immediate source.
	 * Otherwise it is the original source.
	 */
	if(hdr.ndigis != 0 && hdr.nextdigi != 0)
		isrc = hdr.digis[hdr.nextdigi-1];
	else
		isrc = hdr.source;

	/* If there are digis in this packet and not all have been passed,
	 * then the immediate destination is the next digi. Otherwise it
	 * is the final destination.
	 */
	if(hdr.ndigis != 0 && hdr.nextdigi != hdr.ndigis)
		idest = hdr.digis[hdr.nextdigi];
	else
		idest = hdr.dest;

	/* Don't log our own packets if we overhear them, as they're
	 * already logged by axsend() and by the digipeater code.
	 */
	if(!addreq(isrc,iface->hwaddr)){
		logsrc(iface,isrc);
		logdest(iface,idest);
	}
	/* Examine immediate destination for a multicast address */
	mcast = 0;
	for(mpp = Ax25multi;(*mpp)[0] != '\0';mpp++){
		if(addreq(idest,*mpp)){
			mcast = 1;
			break;
		}
	}
	if(!mcast && !addreq(idest,iface->hwaddr)){
		/* Not a broadcast, and not addressed to us. Inhibit
		 * transmitter to avoid colliding with addressed station's
		 * response, and discard packet.
		 */
		if(iface->ioctl != NULL)
			(*iface->ioctl)(iface,PARAM_MUTE,1,-1);
		free_p(bpp);
		return;
	}
	if(!mcast && iface->ioctl != NULL){
		/* Packet was sent to us; abort transmit inhibit */
		(*iface->ioctl)(iface,PARAM_MUTE,1,0);
	}
	/* At this point, packet is either addressed to us, or is
	 * a multicast.
	 */
	if(hdr.nextdigi < hdr.ndigis){
		/* Packet requests digipeating. See if we can repeat it. */
		if(Digipeat && !mcast){
			/* Yes, kick it back out. htonax25 will set the
			 * repeated bit.
			 */
			hdr.nextdigi++;
			htonax25(&hdr,bpp);
			if(iface->forw != NULL){
				logsrc(iface->forw,iface->forw->hwaddr);
				logdest(iface->forw,hdr.digis[hdr.nextdigi]);
				(*iface->forw->raw)(iface->forw,bpp);
			} else {
				logsrc(iface,iface->hwaddr);
				logdest(iface,hdr.digis[hdr.nextdigi]);
				(*iface->raw)(iface,bpp);
			}
		}
		free_p(bpp);	/* Dispose if not forwarded */
		return;
	}
	/* If we reach this point, then the packet has passed all digis,
	 * and is either addressed to us or is a multicast.
	 */
	if(*bpp == NULL)
		return;		/* Nothing left */

	/* If there's no locally-set entry in the routing table and
	 * this packet has digipeaters, create or update it. Leave
	 * local routes alone.
	 */
	if(((axr = ax_lookup(hdr.source)) == NULL || axr->type == AX_AUTO)
	 && hdr.ndigis > 0){
		uint8 digis[MAXDIGIS][AXALEN];
		int i,j;

		/* Construct reverse digipeater path */
		for(i=hdr.ndigis-1,j=0;i >= 0;i--,j++){
			memcpy(digis[j],hdr.digis[i],AXALEN);
			digis[j][ALEN] &= ~(E|REPEATED);
		}
		ax_add(hdr.source,AX_AUTO,digis,hdr.ndigis);
	}
	/* Sneak a peek at the control field. This kludge is necessary because
	 * AX.25 lacks a proper protocol ID field between the address and LAPB
	 * sublayers; a control value of UI indicates that LAPB is to be
	 * bypassed.
	 */
	control = *(*bpp)->data & ~PF;

	if(control == UI){
		int pid;
		struct axlink *ipp;

		(void) PULLCHAR(bpp);
		if((pid = PULLCHAR(bpp)) == -1)
			return;		/* No PID */
		/* Find network level protocol and hand it off */
		for(ipp = Axlink;ipp->funct != NULL;ipp++){
			if(ipp->pid == pid)
				break;
		}
		if(ipp->funct != NULL)
			(*ipp->funct)(iface,NULL,hdr.source,hdr.dest,bpp,mcast);
		else
			free_p(bpp);
		return;
	}
	/* Everything from here down is connected-mode LAPB, so ignore
	 * multicasts
	 */
	if(mcast){
		free_p(bpp);
		return;
	}
	/* Find the source address in hash table */
	if((axp = find_ax25(hdr.source)) == NULL){
		/* Create a new ax25 entry for this guy,
		 * insert into hash table keyed on his address,
		 * and initialize table entries
		 */
		if((axp = cr_ax25(hdr.source)) == NULL){
			free_p(bpp);
			return;
		}
		/* Swap source and destination */
		memcpy(axp->remote,hdr.source,AXALEN);
		memcpy(axp->local,hdr.dest,AXALEN);
		axp->iface = iface;
	}
	if(hdr.cmdrsp == LAPB_UNKNOWN)
		axp->proto = V1;	/* Old protocol in use */

	lapb_input(axp,hdr.cmdrsp,bpp);
}
/* Find a route for an AX.25 address */
struct ax_route *
ax_lookup(
uint8 *target
){
	register struct ax_route *axr;
	struct ax_route *axlast = NULL;

	for(axr = Ax_routes; axr != NULL; axlast=axr,axr = axr->next){
		if(addreq(axr->target,target)){
			if(axr != Ax_routes){
				/* Move entry to top of list to speed
				 * future searches
				 */
				axlast->next = axr->next;
				axr->next = Ax_routes;
				Ax_routes = axr;

			}
			return axr;
		}
	}
	return axr;
}
/* Add an entry to the AX.25 routing table */
struct ax_route *
ax_add(
uint8 *target,
int type,
uint8 digis[][AXALEN],
int ndigis
){
	register struct ax_route *axr;

	if(ndigis < 0 || ndigis > MAXDIGIS)
		return NULL;

	if((axr = ax_lookup(target)) == NULL){
		axr = (struct ax_route *)callocw(1,sizeof(struct ax_route));
		axr->next = Ax_routes;
		Ax_routes = axr;
		memcpy(axr->target,target,AXALEN);
		axr->ndigis = ndigis;
	}
	axr->type = type;
	if(axr->ndigis != ndigis)
		axr->ndigis = ndigis;

	memcpy(axr->digis,digis[0],ndigis*AXALEN);
	return axr;
}
int
ax_drop(
uint8 *target
){
	register struct ax_route *axr;
	struct ax_route *axlast = NULL;

	for(axr = Ax_routes;axr != NULL;axlast=axr,axr=axr->next)
		if(memcmp(axr->target,target,AXALEN) == 0)
			break;
	if(axr == NULL)
		return -1;	/* Not in table! */
	if(axlast != NULL)
		axlast->next = axr->next;
	else
		Ax_routes = axr->next;
	
	free(axr);
	return 0;
}
