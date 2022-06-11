/* Stuff generic to all ARCnet controllers
 * Copyright 1990 Russ Nelson
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "arp.h"
#include "ip.h"
#include "arcnet.h"

uint8 ARC_bdcst[] = { 0 };

/* Convert ARCnet header in host form to network mbuf */
void
htonarc(
struct arc *arc,
struct mbuf **bpp
){
	register uint8 *cp;

	pushdown(bpp,NULL,ARCLEN);

	cp = (*bpp)->data;

	memcpy(cp,arc->source,AADDR_LEN);
	cp += AADDR_LEN;
	memcpy(cp,arc->dest,AADDR_LEN);
	cp += AADDR_LEN;
	*cp++ = arc->type;
}
/* Extract ARCnet header */
int
ntoharc(arc,bpp)
struct arc *arc;
struct mbuf **bpp;
{
	pullup(bpp,arc->source,AADDR_LEN);
	pullup(bpp,arc->dest,AADDR_LEN);
	arc->type = PULLCHAR(bpp);
	return ARCLEN;
}

/* Format an ARCnet address into a printable ascii string */
char *
parc(out,addr)
char *out;
uint8 *addr;
{
	sprintf(out,"%02x", addr[0]);
	return  out;
}

/* Convert an ARCnet address from Hex/ASCII to binary */
int
garc(out,cp)
register uint8 *out;
register char *cp;
{
	*out = htoi(cp);
	return 0;
}
/* Send an IP datagram on ARCnet */
int
anet_send(
struct mbuf **bpp,	/* Buffer to send */
struct iface *iface,	/* Pointer to interface control block */
int32 gateway,		/* IP address of next hop */
uint8 tos
){
	uint8 *agate;

	agate = res_arp(iface,ARP_ARCNET,gateway,bpp);
	if(agate != NULL)
		return (*iface->output)(iface,agate,iface->hwaddr,ARC_IP,bpp);
	return 0;
}
/* Send a packet with ARCnet header */
int
anet_output(
struct iface *iface,	/* Pointer to interface control block */
uint8 *dest,		/* Destination ARCnet address */
uint8 *source,		/* Source ARCnet address */
uint16 type,		/* Type field */
struct mbuf **data	/* Data field */
){
	struct arc ap;

	memcpy(ap.dest,dest,AADDR_LEN);
	memcpy(ap.source,source,AADDR_LEN);
	ap.type = type;
	htonarc(&ap,data);
	return (*iface->raw)(iface,data);
}
/* Process incoming ARCnet packets. Shared by all ARCnet drivers. */
void
aproc(
struct iface *iface,
struct mbuf **bpp
){
	struct arc hdr;

	/* Remove ARCnet header and kick packet upstairs */
	ntoharc(&hdr,bpp);
	switch(hdr.type){
	case ARC_ARP:
		arp_input(iface,bpp);
		break;
	case ARC_IP:
		ip_route(iface,bpp,0);
		break;
	default:
		free_p(bpp);
		break;
	}
}

