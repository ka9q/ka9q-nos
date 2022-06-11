/* IP header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "ip.h"
#include "internet.h"

/* Convert IP header in host format to network mbuf
 * If cflag != 0, take checksum from structure,
 * otherwise compute it automatically.
 */
void
htonip(
struct ip *ip,
struct mbuf **bpp,
int cflag
){
	uint16 hdr_len;
	register uint8 *cp;
	uint16 fl_offs;

	if(bpp == NULL)
		return;
	hdr_len = IPLEN + ip->optlen;
	if(hdr_len > IPLEN + IP_MAXOPT)
		hdr_len = IPLEN + IP_MAXOPT;
	pushdown(bpp,NULL,hdr_len);
	cp = (*bpp)->data;
	
	*cp++ = (ip->version << 4) | (hdr_len >> 2);
	*cp++ = ip->tos;
	cp = put16(cp,ip->length);
	cp = put16(cp,ip->id);
	fl_offs = ip->offset >> 3;
	if(ip->flags.congest)
		fl_offs |= 0x8000;
	if(ip->flags.df)
		fl_offs |= 0x4000;
	if(ip->flags.mf)
		fl_offs |= 0x2000;

	cp = put16(cp,fl_offs);
	*cp++ = ip->ttl;
	*cp++ = ip->protocol;
	if(cflag){
		/* Use checksum from host structure */
		cp = put16(cp,ip->checksum);
	} else {
		/* Clear checksum for later recalculation */
		*cp++ = 0;
		*cp++ = 0;
	}
	cp = put32(cp,ip->source);
	cp = put32(cp,ip->dest);
	if(ip->optlen != 0)
		memcpy(cp,ip->options,min(ip->optlen,IP_MAXOPT));

	/* If requested, recompute checksum and insert into header */
	if(!cflag)
		put16(&(*bpp)->data[10],cksum(NULL,*bpp,hdr_len));
}
/* Extract an IP header from mbuf */
int
ntohip(
struct ip *ip,
struct mbuf **bpp
){
	int ihl;
	uint16 fl_offs;
	uint8 ipbuf[IPLEN];

	if(pullup(bpp,ipbuf,IPLEN) != IPLEN)
		return -1;

	ip->version = (ipbuf[0] >> 4) & 0xf;
	ip->tos = ipbuf[1];
	ip->length = get16(&ipbuf[2]);
	ip->id = get16(&ipbuf[4]);
	fl_offs = get16(&ipbuf[6]);
	ip->offset = (fl_offs & 0x1fff) << 3;
	ip->flags.mf = (fl_offs & 0x2000) ? 1 : 0;
	ip->flags.df = (fl_offs & 0x4000) ? 1 : 0;
	ip->flags.congest = (fl_offs & 0x8000) ? 1 : 0;
	ip->ttl = ipbuf[8];
	ip->protocol = ipbuf[9];
	ip->checksum = get16(&ipbuf[10]);
	ip->source = get32(&ipbuf[12]);
	ip->dest = get32(&ipbuf[16]);

	ihl = (ipbuf[0] & 0xf) << 2;
	if(ihl < IPLEN){
		/* Bogus packet; header is too short */
		ip->optlen = 0;
		return -1;
	}
	if ( (ip->optlen = ihl - IPLEN) != 0 ) {
		if ( pullup(bpp,ip->options,ip->optlen) < ip->optlen )
			return -1;
	}
	return ihl;
}
/* Perform end-around-carry adjustment */
uint16
eac(
int32 sum	/* Carries in high order 16 bits */
){
	register uint16 csum;

	while((csum = sum >> 16) != 0)
		sum = csum + (sum & 0xffffL);
	return (uint16) (sum & 0xffffl);	/* Chops to 16 bits */
}
/* Checksum a mbuf chain, with optional pseudo-header */
uint16
cksum(
struct pseudo_header *ph,
struct mbuf *m,
uint16 len
){
	register uint16 cnt, total;
	register int32 sum, csum;
	register uint8 *up;
	uint16 csum1;
	int swap = 0;

	sum = 0l;

	/* Sum pseudo-header, if present */
	if(ph != NULL){
		sum = hiword(ph->source);
		sum += loword(ph->source);
		sum += hiword(ph->dest);
		sum += loword(ph->dest);
		sum += ph->protocol;
		sum += ph->length;
	}
	/* Now do each mbuf on the chain */
	for(total = 0; m != NULL && total < len; m = m->next) {
		cnt = min(m->cnt, len - total);
		up = m->data;
		csum = 0;

		if(((long)up) & 1){
			/* Handle odd leading byte */
			if(swap)
				csum = *up++;
			else
				csum = (uint16)*up++ << 8;
			cnt--;
			swap = !swap;
		}
		if(cnt > 1){
			/* Have the primitive checksumming routine do most of
			 * the work. At this point, up is guaranteed to be on
			 * a short boundary
			 */
			csum1 = lcsum((unsigned short *)up, (uint16)(cnt >> 1));
			if(swap)
				csum1 = (csum1 << 8) | (csum1 >> 8);
			csum += csum1;
		}
		/* Handle odd trailing byte */
		if(cnt & 1){
			if(swap)
				csum += up[--cnt];
			else
				csum += (uint16)up[--cnt] << 8;
			swap = !swap;
		}
		sum += csum;
		total += m->cnt;
	}
	/* Do final end-around carry, complement and return */
	return (uint16)(~eac(sum) & 0xffff);
}

