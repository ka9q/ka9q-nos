/*
 * Routines to compress and uncompress tcp packets (for transmission
 * over low speed serial lines).
 *
 * Copyright (c) 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Van Jacobson (van@helios.ee.lbl.gov), Dec 31, 1989:
 *	- Initial distribution.
 *
 *
 * modified for KA9Q Internet Software Package by
 * Katie Stevens (dkstevens@ucdavis.edu)
 * University of California, Davis
 * Computing Services
 *	- 01-31-90	initial adaptation (from 1.19)
 *	PPP.05	02-15-90 [ks]
 *	PPP.08	05-02-90 [ks]	use PPP protocol field to signal compression
 *	PPP.15	09-90	 [ks]	improve mbuf handling
 *	PPP.16	11-02	 [karn]	substantially rewritten to use NOS facilities
 *
 *	- Feb 1991	Bill_Simpson@um.cc.umich.edu
 *			variable number of conversation slots
 *			allow zero or one slots
 *			separate routines
 *			status display
 */

#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "ip.h"
#include "tcp.h"
#include "slhc.h"

static uint8 *encode(uint8 *cp,uint16 n);
static long decode(struct mbuf **bpp);


/* Initialize compression data structure
 *	slots must be in range 0 to 255 (zero meaning no compression)
 */
struct slcompress *
slhc_init(rslots,tslots)
int rslots;
int tslots;
{
	register uint16 i;
	register struct cstate *ts;
	struct slcompress *comp;

	comp = callocw( 1, sizeof(struct slcompress) );

	if ( rslots > 0  &&  rslots < 256 ) {
		comp->rstate = callocw( rslots, sizeof(struct cstate) );
		comp->rslot_limit = rslots - 1;
	}

	if ( tslots > 0  &&  tslots < 256 ) {
		comp->tstate = callocw( tslots, sizeof(struct cstate) );
		comp->tslot_limit = tslots - 1;
	}

	comp->xmit_oldest = 0;
	comp->xmit_current = 255;
	comp->recv_current = 255;

	if ( tslots > 0 ) {
		ts = comp->tstate;
		for(i = comp->tslot_limit; i > 0; --i){
			ts[i].this = i;
			ts[i].next = &(ts[i - 1]);
		}
		ts[0].next = &(ts[comp->tslot_limit]);
		ts[0].this = 0;
	}
	return comp;
}


/* Free a compression data structure */
void
slhc_free(comp)
struct slcompress *comp;
{
	if ( comp == NULL )
		return;

	if ( comp->rstate != NULL )
		free( comp->rstate );

	if ( comp->tstate != NULL )
		free( comp->tstate );

	free( comp );
}


/* Encode a number */
static uint8 *
encode(cp,n)
register uint8 *cp;
uint16 n;
{
	if(n >= 256 || n == 0){
		*cp++ = 0;
		cp = put16(cp,n);
	} else {
		*cp++ = n;
	}
	return cp;
}

/* Decode a number */
static long
decode(bpp)
struct mbuf **bpp;
{
	register int x;

	x = PULLCHAR(bpp);
	if(x == 0){
		return pull16(bpp);	/* pull16 returns -1 on error */
	} else {
		return (long)x;		/* -1 if PULLCHAR returned error */
	}
}

int
slhc_compress(comp, bpp, compress_cid)
struct slcompress *comp;
struct mbuf **bpp;
int compress_cid;
{
	register struct cstate *ocs = &(comp->tstate[comp->xmit_oldest]);
	register struct cstate *lcs = ocs;
	register struct cstate *cs = lcs->next;
	register uint16 hlen,iplen;
	register struct tcp *oth;
	register unsigned long deltaS, deltaA;
	register uint16 changes = 0;
	uint8 new_seq[16];
	register uint8 *cp = new_seq;
	struct tcp th;
	struct ip iph;
	struct mbuf *copy;

	/* Copy TCP/IP header, allowing for worst-case options in both
	 * Using dup_p seemed to result in unexplained
	 * memory leaks -- but only some of the time. Must find out why.
	 */
/*	dup_p(&copy,*bpp,0,IPLEN+IP_MAXOPT+TCPLEN+TCP_MAXOPT); */
	copy = copy_p(*bpp,IPLEN+IP_MAXOPT+TCPLEN+TCP_MAXOPT);

	/* Peek at IP header */
	iplen = hlen = ntohip(&iph,&copy);

	/* Bail if this packet isn't TCP, or is an IP fragment */
	if(iph.protocol != TCP_PTCL || iph.offset != 0 || iph.flags.mf){
		/* Send as regular IP */
		if(iph.protocol != TCP_PTCL)
			comp->sls_o_nontcp++;
		else
			comp->sls_o_tcp++;
		free_p(&copy);
		return SL_TYPE_IP;
	}
	/* Extract TCP header */
	hlen += ntohtcp(&th,&copy);
	free_p(&copy);	/* Done with copy */

	/*  Bail if the TCP packet isn't `compressible' (i.e., ACK isn't set or
	 *  some other control bit is set, or has options).
	 */
	if(th.flags.syn || th.flags.fin || th.flags.rst || !th.flags.ack
	 || th.flags.mss || th.flags.wscale || th.flags.tstamp){
		/* TCP connection stuff; send as regular IP */
		comp->sls_o_tcp++;
		return SL_TYPE_IP;
	}
	/*
	 * Packet is compressible -- we're going to send either a
	 * COMPRESSED_TCP or UNCOMPRESSED_TCP packet.  Either way,
	 * we need to locate (or create) the connection state.
	 *
	 * States are kept in a circularly linked list with
	 * xmit_oldest pointing to the end of the list.  The
	 * list is kept in lru order by moving a state to the
	 * head of the list whenever it is referenced.  Since
	 * the list is short and, empirically, the connection
	 * we want is almost always near the front, we locate
	 * states via linear search.  If we don't find a state
	 * for the datagram, the oldest state is (re-)used.
	 */
	for ( ; ; ) {
		if( iph.source == cs->cs_ip.source
		 && iph.dest == cs->cs_ip.dest
		 && th.source == cs->cs_tcp.source
		 && th.dest == cs->cs_tcp.dest)
			goto found;

		/* if current equal oldest, at end of list */
		if ( cs == ocs )
			break;
		lcs = cs;
		cs = cs->next;
		comp->sls_o_searches++;
	};
	/*
	 * Didn't find it -- re-use oldest cstate.  Send an
	 * uncompressed packet that tells the other side what
	 * connection number we're using for this conversation.
	 *
	 * Note that since the state list is circular, the oldest
	 * state points to the newest and we only need to set
	 * xmit_oldest to update the lru linkage.
	 */
	comp->sls_o_misses++;
	comp->xmit_oldest = lcs->this;

	goto uncompressed;

found:
	/*
	 * Found it -- move to the front on the connection list.
	 */
	if(lcs == ocs) {
		/* found at most recently used */
	} else if (cs == ocs) {
		/* found at least recently used */
		comp->xmit_oldest = lcs->this;
	} else {
		/* more than 2 elements */
		lcs->next = cs->next;
		cs->next = ocs->next;
		ocs->next = cs;
	}

	/*
	 * Make sure that only what we expect to change changed.
	 * Check the following:
	 * IP protocol version, header length & type of service.
	 * The "Don't fragment" bit.
	 * The time-to-live field.
	 * The TCP header length.
	 * IP options, if any.
	 * TCP options, if any.
	 * If any of these things are different between the previous &
	 * current datagram, we send the current datagram `uncompressed'.
	 */
	oth = &cs->cs_tcp;

	if(iph.version != cs->cs_ip.version || iph.optlen != cs->cs_ip.optlen
	 || iph.tos != cs->cs_ip.tos
	 || iph.flags.df != cs->cs_ip.flags.df
	 || iph.ttl != cs->cs_ip.ttl
	 || (iph.optlen > 0 && memcmp(iph.options,cs->cs_ip.options,iph.optlen) != 0)){
		goto uncompressed;
	}
	/*
	 * Figure out which of the changing fields changed.  The
	 * receiver expects changes in the order: urgent, window,
	 * ack, seq (the order minimizes the number of temporaries
	 * needed in this section of code).
	 */
	if(th.flags.urg){
		deltaS = th.up;
		cp = encode(cp,deltaS);
		changes |= NEW_U;
	} else if(th.up != oth->up){
		/* argh! URG not set but urp changed -- a sensible
		 * implementation should never do this but RFC793
		 * doesn't prohibit the change so we have to deal
		 * with it. */
		goto uncompressed;
	}
	if((deltaS = th.wnd - oth->wnd) != 0){
		cp = encode(cp,deltaS);
		changes |= NEW_W;
	}
	if((deltaA = th.ack - oth->ack) != 0L){
		if(deltaA > 0x0000ffff)
			goto uncompressed;
		cp = encode(cp,deltaA);
		changes |= NEW_A;
	}
	if((deltaS = th.seq - oth->seq) != 0L){
		if(deltaS > 0x0000ffff)
			goto uncompressed;
		cp = encode(cp,deltaS);
		changes |= NEW_S;
	}

	switch(changes){
	case 0:	/* Nothing changed. If this packet contains data and the
		 * last one didn't, this is probably a data packet following
		 * an ack (normal on an interactive connection) and we send
		 * it compressed.  Otherwise it's probably a retransmit,
		 * retransmitted ack or window probe.  Send it uncompressed
		 * in case the other side missed the compressed version.
		 */
		if(iph.length != cs->cs_ip.length && cs->cs_ip.length == hlen)
			break;
		goto uncompressed;
	case SPECIAL_I:
	case SPECIAL_D:
		/* actual changes match one of our special case encodings --
		 * send packet uncompressed.
		 */
		goto uncompressed;
	case NEW_S|NEW_A:
		if(deltaS == deltaA &&
		    deltaS == cs->cs_ip.length - hlen){
			/* special case for echoed terminal traffic */
			changes = SPECIAL_I;
			cp = new_seq;
		}
		break;
	case NEW_S:
		if(deltaS == cs->cs_ip.length - hlen){
			/* special case for data xfer */
			changes = SPECIAL_D;
			cp = new_seq;
		}
		break;
	}
	deltaS = iph.id - cs->cs_ip.id;
	if(deltaS != 1){
		cp = encode(cp,deltaS);
		changes |= NEW_I;
	}
	if(th.flags.psh)
		changes |= TCP_PUSH_BIT;
	/* Grab the cksum before we overwrite it below.  Then update our
	 * state with this packet's header.
	 */
	deltaA = th.checksum;
	ASSIGN(cs->cs_ip,iph);
	ASSIGN(cs->cs_tcp,th);
	/* We want to use the original packet as our compressed packet.
	 * (cp - new_seq) is the number of bytes we need for compressed
	 * sequence numbers.  In addition we need one byte for the change
	 * mask, one for the connection id and two for the tcp checksum.
	 * So, (cp - new_seq) + 4 bytes of header are needed.
	 */
	deltaS = cp - new_seq;
	pullup(bpp,NULL,hlen);		/* Strip TCP/IP headers */
	if(compress_cid == 0 || comp->xmit_current != cs->this){
		pushdown(bpp,NULL,deltaS + 4);
		cp = (*bpp)->data;
		*cp++ = changes | NEW_C;
		*cp++ = cs->this;
		comp->xmit_current = cs->this;
	} else {
		pushdown(bpp,NULL,deltaS + 3);
		cp = (*bpp)->data;
		*cp++ = changes;
	}
	cp = put16(cp,(uint16)deltaA);	/* Write TCP checksum */
	memcpy(cp,new_seq,deltaS);	/* Write list of deltas */
	comp->sls_o_compressed++;
	return SL_TYPE_COMPRESSED_TCP;

	/* Update connection state cs & send uncompressed packet (i.e.,
	 * a regular ip/tcp packet but with the 'conversation id' we hope
	 * to use on future compressed packets in the protocol field).
	 */
uncompressed:
	iph.protocol = cs->this;
	ASSIGN(cs->cs_ip,iph);
	ASSIGN(cs->cs_tcp,th);
	comp->xmit_current = cs->this;
	comp->sls_o_uncompressed++;
	pullup(bpp,NULL,iplen);	/* Strip old IP header */
	htonip(&iph,bpp,IP_CS_OLD);	/* replace with new one */
	return SL_TYPE_UNCOMPRESSED_TCP;
}


int
slhc_uncompress(comp, bpp)
struct slcompress *comp;
struct mbuf **bpp;
{
	register int changes;
	long x;
	register struct tcp *thp;
	register struct cstate *cs;
	int len;

	if(bpp == NULL){
		comp->sls_i_error++;
		return 0;
	}
	/* We've got a compressed packet; read the change byte */
	comp->sls_i_compressed++;
	if(len_p(*bpp) < 3){
		comp->sls_i_error++;
		return 0;
	}
	changes = PULLCHAR(bpp);	/* "Can't fail" */
	if(changes & NEW_C){
		/* Make sure the state index is in range, then grab the state.
		 * If we have a good state index, clear the 'discard' flag.
		 */
		x = PULLCHAR(bpp);	/* Read conn index */
		if(x < 0 || x > comp->rslot_limit)
			goto bad;

		comp->flags &=~ SLF_TOSS;
		comp->recv_current = x;
	} else {
		/* this packet has an implicit state index.  If we've
		 * had a line error since the last time we got an
		 * explicit state index, we have to toss the packet. */
		if(comp->flags & SLF_TOSS){
			comp->sls_i_tossed++;
			return 0;
		}
	}
	cs = &comp->rstate[comp->recv_current];
	thp = &cs->cs_tcp;

	if((x = pull16(bpp)) == -1)	/* Read the TCP checksum */
		goto bad;
	thp->checksum = x;

	thp->flags.psh = (changes & TCP_PUSH_BIT) ? 1 : 0;

	switch(changes & SPECIALS_MASK){
	case SPECIAL_I:		/* Echoed terminal traffic */
		{
		register uint16 i;
		i = cs->cs_ip.length;
		i -= (cs->cs_ip.optlen + IPLEN + TCPLEN);
		thp->ack += i;
		thp->seq += i;
		}
		break;

	case SPECIAL_D:			/* Unidirectional data */
		thp->seq += cs->cs_ip.length - (cs->cs_ip.optlen +IPLEN + TCPLEN);
		break;

	default:
		if(changes & NEW_U){
			thp->flags.urg = 1;
			if((x = decode(bpp)) == -1)
				goto bad;
			thp->up = x;
		} else
			thp->flags.urg = 0;
		if(changes & NEW_W){
			if((x = decode(bpp)) == -1)
				goto bad;
			thp->wnd += x;
		}
		if(changes & NEW_A){
			if((x = decode(bpp)) == -1)
				goto bad;
			thp->ack += x;
		}
		if(changes & NEW_S){
			if((x = decode(bpp)) == -1)
				goto bad;
			thp->seq += x;
		}
		break;
	}
	if(changes & NEW_I){
		if((x = decode(bpp)) == -1)
			goto bad;
		cs->cs_ip.id += x;
	} else
		cs->cs_ip.id++;

	/*
	 * At this point, bpp points to the first byte of data in the
	 * packet.  Put the reconstructed TCP and IP headers back on the
	 * packet.  Recalculate IP checksum (but not TCP checksum).
	 */
	len = len_p(*bpp) + IPLEN + TCPLEN + cs->cs_ip.optlen;
	cs->cs_ip.length = len;

	htontcp(thp,bpp,0,0);
	htonip(&cs->cs_ip,bpp,IP_CS_NEW);
	return len;
bad:
	comp->sls_i_error++;
	return slhc_toss( comp );
}


int
slhc_remember(comp, bpp)
struct slcompress *comp;
struct mbuf **bpp;
{
	register struct cstate *cs;
	struct ip iph;
	struct tcp th;
	uint16 len;
	uint16 hdrlen;
	int slot;

	if(bpp == NULL){
		comp->sls_i_error++;
		return slhc_toss(comp);
	}

	/* Sneak a peek at the IP header's IHL field to find its length */
	hdrlen = ((*bpp)->data[0] & 0xf) << 2;
	if(hdrlen < IPLEN){
		/* The IP header length field is too small to be valid */
		comp->sls_i_error++;
		return slhc_toss(comp);
	}
	len = len_p(*bpp);	/* Actual length of whole packet */
	ntohip(&iph,bpp);	/* Extract IP header */
	/* Verify indicated length <= actual length */
	if(iph.length > len){
		/* Packet has been truncated, or header is garbage */
		comp->sls_i_error++;
		return slhc_toss(comp);
	}
	/* Verify conn ID */
	slot = iph.protocol;
	if(slot > comp->rslot_limit){
		/* Out of range */
		comp->sls_i_error++;
		return slhc_toss(comp);
	}
	iph.protocol = TCP_PTCL;	/* Replace conn ID with TCP_PTCL */

	/* Extract TCP header and replace both headers
	 * Neither header checksum is recalculated
	 */
	ntohtcp(&th,bpp);
	htontcp(&th,bpp,0,0);
	htonip(&iph,bpp,IP_CS_OLD);

	/* Checksum IP header (now that protocol field is TCP again) */
	if(cksum(NULL,*bpp,hdrlen) != 0){
		/* Bad IP header checksum; discard */
		comp->sls_i_error++;
		return slhc_toss(comp);
	}
	/* Update local state */
	comp->recv_current = slot;
	cs = &comp->rstate[slot];
	comp->flags &=~ SLF_TOSS;

	ASSIGN(cs->cs_ip,iph);
	ASSIGN(cs->cs_tcp,th);
	comp->sls_i_uncompressed++;
	return len;
}


int
slhc_toss(comp)
struct slcompress *comp;
{
	if ( comp == NULL )
		return 0;

	comp->flags |= SLF_TOSS;
	return 0;
}

void
slhc_i_status(comp)
struct slcompress *comp;
{
	if (comp != NULL) {
		printf("\t%10ld Cmp,"
			" %10ld Uncmp,"
			" %10ld Bad, "
			" %10ld Tossed\n",
			comp->sls_i_compressed,
			comp->sls_i_uncompressed,
			comp->sls_i_error,
			comp->sls_i_tossed);
	}
}


void
slhc_o_status(comp)
struct slcompress *comp;
{
	if (comp != NULL) {
		printf("\t%10ld Cmp,"
			" %10ld Uncmp,"
			" %10ld AsIs,"
			" %10ld NotTCP\n",
			comp->sls_o_compressed,
			comp->sls_o_uncompressed,
			comp->sls_o_tcp,
			comp->sls_o_nontcp);
		printf("\t%10ld Searches,"
			" %10ld Misses\n",
			comp->sls_o_searches,
			comp->sls_o_misses);
	}
}


