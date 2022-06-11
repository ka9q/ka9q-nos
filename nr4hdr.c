/* Net/rom transport layer header conversion routines.
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 */
#include "global.h"
#include "mbuf.h"
#include "nr4.h"

/* Convert a net/rom transport header to host format structure.
 * Return -1 if error, 0 if OK.
 */
int
ntohnr4(hdr,bpp)
register struct nr4hdr *hdr;
struct mbuf **bpp;
{
	uint8 tbuf[NR4MINHDR];
	int i;

	if(pullup(bpp, tbuf, NR4MINHDR) < NR4MINHDR)
		return -1;

	hdr->opcode = tbuf[4];

	switch(tbuf[4] & NR4OPCODE){
		case NR4OPPID:		/* protocol ID extension */
			hdr->u.pid.family = tbuf[0];
			hdr->u.pid.proto = tbuf[1];
			break;
		case NR4OPCONRQ:	/* connect request */
			hdr->u.conreq.myindex = tbuf[0];
			hdr->u.conreq.myid = tbuf[1];
			if((i = PULLCHAR(bpp)) == -1)
				return -1;
			hdr->u.conreq.window = i;
			if(pullup(bpp,hdr->u.conreq.user,AXALEN) < AXALEN)
				return -1;
			if(pullup(bpp,hdr->u.conreq.node,AXALEN) < AXALEN)
				return -1;
			break;
		case NR4OPCONAK:	/* connect acknowledge */
			hdr->yourindex = tbuf[0];
			hdr->yourid = tbuf[1];
			hdr->u.conack.myindex = tbuf[2];
			hdr->u.conack.myid = tbuf[3];
			if((i = PULLCHAR(bpp)) == -1)
				return -1;
			hdr->u.conack.window = i;
			break;
		case NR4OPDISRQ:	/* disconnect request */
			hdr->yourindex = tbuf[0];
			hdr->yourid = tbuf[1];
			break;
		case NR4OPDISAK:	/* disconnect acknowledge */
			hdr->yourindex = tbuf[0];
			hdr->yourid = tbuf[1];
			break;
		case NR4OPINFO:		/* information frame */
			hdr->yourindex = tbuf[0];
			hdr->yourid = tbuf[1];
			hdr->u.info.txseq = tbuf[2];
			hdr->u.info.rxseq = tbuf[3];
			break;
		case NR4OPACK:		/* information acknowledge */
			hdr->yourindex = tbuf[0];
			hdr->yourid = tbuf[1];
			hdr->u.ack.rxseq = tbuf[3];
			break;
		default:		/* what kind of frame is this? */
			return -1;
	}
	return 0;
}

/* Convert host-format level 4 header to network format */
struct mbuf *
htonnr4(hdr)
register struct nr4hdr *hdr;
{
	static uint16 hlen[NR4NUMOPS] = {5,20,6,5,5,5,5};
	struct mbuf *rbuf;
	register uint8 *cp;
	unsigned char opcode;

	opcode = hdr->opcode & NR4OPCODE;

	if(opcode >= NR4NUMOPS)
		return NULL;

	if(hdr == (struct nr4hdr *)NULL)
		return NULL;

	if((rbuf = alloc_mbuf(hlen[opcode])) == NULL)
		return NULL;

	rbuf->cnt = hlen[opcode];
	cp = rbuf->data;

	cp[4] = hdr->opcode;
	
	switch(opcode){
		case NR4OPPID:
			*cp++ = hdr->u.pid.family;
			*cp = hdr->u.pid.proto;
			break;
		case NR4OPCONRQ:
			*cp++ = hdr->u.conreq.myindex;
			*cp++ = hdr->u.conreq.myid;
			cp += 3; /* skip to sixth byte */
			*cp++ = hdr->u.conreq.window;
			memcpy(cp,hdr->u.conreq.user,AXALEN);
			cp += AXALEN;
			memcpy(cp,hdr->u.conreq.node,AXALEN);
			cp += AXALEN;
			break;
		case NR4OPCONAK:
			*cp++ = hdr->yourindex;
			*cp++ = hdr->yourid;
			*cp++ = hdr->u.conack.myindex;
			*cp++ = hdr->u.conack.myid;
			cp++;	/* already loaded pid */
			*cp = hdr->u.conack.window;
			break;
		case NR4OPDISRQ:
			*cp++ = hdr->yourindex;
			*cp = hdr->yourid;
			break;
		case NR4OPDISAK:
			*cp++ = hdr->yourindex;
			*cp = hdr->yourid;
			break;
		case NR4OPINFO:
			*cp++ = hdr->yourindex;
			*cp++ = hdr->yourid;
			*cp++ = hdr->u.info.txseq;
			*cp = hdr->u.info.rxseq;
			break;
		case NR4OPACK:
			*cp++ = hdr->yourindex;
			*cp++ = hdr->yourid;
			*++cp = hdr->u.ack.rxseq;	/* skip third byte (tricky yuck) */
			break;
	}
	return rbuf;
}
