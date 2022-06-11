/* UDP header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "ip.h"
#include "internet.h"
#include "udp.h"

/* Convert UDP header in internal format to an mbuf in external format */
void
htonudp(
struct udp *udp,
struct mbuf **bpp,
struct pseudo_header *ph
){
	register uint8 *cp;
	uint16 checksum;

	/* Allocate UDP protocol header and fill it in */
	pushdown(bpp,NULL,UDPHDR);
	cp = (*bpp)->data;
	cp = put16(cp,udp->source);	/* Source port */
	cp = put16(cp,udp->dest);	/* Destination port */
	cp = put16(cp,udp->length);	/* Length */
	*cp++ = 0;			/* Clear checksum */
	*cp-- = 0;

	/* All zeros and all ones is equivalent in one's complement arithmetic;
	 * the spec requires us to change zeros into ones to distinguish an
 	 * all-zero checksum from no checksum at all
	 */
	if((checksum = cksum(ph,*bpp,ph->length)) == 0)
		checksum = 0xffff;
	put16(cp,checksum);
}
/* Convert UDP header in mbuf to internal structure */
int
ntohudp(
struct udp *udp,
struct mbuf **bpp
){
	uint8 udpbuf[UDPHDR];

	if(pullup(bpp,udpbuf,UDPHDR) != UDPHDR)
		return -1;
	udp->source = get16(&udpbuf[0]);
	udp->dest = get16(&udpbuf[2]);
	udp->length = get16(&udpbuf[4]);
	udp->checksum = get16(&udpbuf[6]);
	return 0;
}
/* Extract UDP checksum value from a network-format header without
 * disturbing the header
 */
uint16
udpcksum(
struct mbuf *bp
){
	struct mbuf *dup;

	if(dup_p(&dup,bp,6,2) != 2)
		return 0;
	return pull16(&dup);
}

