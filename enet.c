/* Stuff generic to all Ethernet controllers
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "arp.h"
#include "ip.h"
#include "enet.h"

uint8 Ether_bdcst[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

/* Convert Ethernet header in host form to network mbuf */
void
htonether(
struct ether *ether,
struct mbuf **bpp
){
	register uint8 *cp;

	if(bpp == NULL)
		return;
	pushdown(bpp,NULL,ETHERLEN);
	cp = (*bpp)->data;
	memcpy(cp,ether->dest,EADDR_LEN);
	cp += EADDR_LEN;
	memcpy(cp,ether->source,EADDR_LEN);
	cp += EADDR_LEN;
	put16(cp,ether->type);
}
/* Extract Ethernet header */
int
ntohether(
struct ether *ether,
struct mbuf **bpp
){
	pullup(bpp,ether->dest,EADDR_LEN);
	pullup(bpp,ether->source,EADDR_LEN);
	ether->type = pull16(bpp);
	return ETHERLEN;
}

/* Format an Ethernet address into a printable ascii string */
char *
pether(
char *out,
uint8 *addr
){
	sprintf(out,"%02x:%02x:%02x:%02x:%02x:%02x",
	 addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);
	return out;
}

/* Convert an Ethernet address from Hex/ASCII to binary */
int
gether(
register uint8 *out,
register char *cp
){
	register int i;

	for(i=6; i!=0; i--){
		*out++ = htoi(cp);
		if((cp = strchr(cp,':')) == NULL)	/* Find delimiter */
			break;
		cp++;			/* and skip over it */
	}
	return i;
}
/* Send an IP datagram on Ethernet */
int
enet_send(
struct mbuf **bpp,	/* Buffer to send */
struct iface *iface,	/* Pointer to interface control block */
int32 gateway,		/* IP address of next hop */
uint8 tos		/* Type of service, not used */
){
	uint8 *egate;

	egate = res_arp(iface,ARP_ETHER,gateway,bpp);
	if(egate != NULL)
		return (*iface->output)(iface,egate,iface->hwaddr,IP_TYPE,bpp);
	return 0;
}
/* Send a packet with Ethernet header */
int
enet_output(
struct iface *iface,	/* Pointer to interface control block */
uint8 *dest,		/* Destination Ethernet address */
uint8 *source,		/* Source Ethernet address */
uint16 type,		/* Type field */
struct mbuf **bpp	/* Data field */
){
	struct ether ep;

	memcpy(ep.dest,dest,EADDR_LEN);
	memcpy(ep.source,source,EADDR_LEN);
	ep.type = type;
	htonether(&ep,bpp);
	return (*iface->raw)(iface,bpp);
}
/* Process incoming Ethernet packets. Shared by all ethernet drivers. */
void
eproc(
struct iface *iface,
struct mbuf **bpp
){
	struct ether hdr;

	/* Remove Ethernet header and kick packet upstairs */
	ntohether(&hdr,bpp);

	switch(hdr.type){
	case REVARP_TYPE:
	case ARP_TYPE:
		arp_input(iface,bpp);
		break;
	case IP_TYPE:
		ip_route(iface,bpp,hdr.dest[0] & 1);
		break;
	default:
		free_p(bpp);
		break;
	}
}

