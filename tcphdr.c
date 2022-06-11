/* TCP header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "tcp.h"
#include "ip.h"
#include "internet.h"

/* Convert TCP header in host format into mbuf ready for transmission,
 * link in data (if any).
 *
 * If checksum field is zero, recompute it, otherwise take the value
 * in the host header.
 */
void
htontcp(
struct tcp *tcph,
struct mbuf **bpp,	/* Data in, packet out */
int32 ipsrc,		/* For computing header checksum */
int32 ipdest
){
	uint16 hdrlen;
	register uint8 *cp;

	if(bpp == NULL)
		return;
	hdrlen = TCPLEN;
	if(tcph->flags.mss)
		hdrlen += MSS_LENGTH;
	if(tcph->flags.tstamp)
		hdrlen += TSTAMP_LENGTH;
	if(tcph->flags.wscale)
		hdrlen += WSCALE_LENGTH;

	hdrlen = (hdrlen + 3) & 0xfc;	/* Round up to multiple of 4 */
	pushdown(bpp,NULL,hdrlen);
	cp = (*bpp)->data;
	memset(cp,0,hdrlen);
	cp = put16(cp,tcph->source);
	cp = put16(cp,tcph->dest);
	cp = put32(cp,tcph->seq);
	cp = put32(cp,tcph->ack);
	*cp++ = hdrlen << 2;	/* Offset field */
	*cp = 0;
	if(tcph->flags.congest)
		*cp |= 64;
	if(tcph->flags.urg)
		*cp |= 32;
	if(tcph->flags.ack)
		*cp |= 16;
	if(tcph->flags.psh)
		*cp |= 8;
	if(tcph->flags.rst)
		*cp |= 4;
	if(tcph->flags.syn)
		*cp |= 2;
	if(tcph->flags.fin)
		*cp |= 1;
	cp++;
	cp = put16(cp,tcph->wnd);
	cp = put16(cp,tcph->checksum);
	cp = put16(cp,tcph->up);

	/* Write options, if any */
	if(tcph->flags.mss){
		*cp++ = MSS_KIND;
		*cp++ = MSS_LENGTH;
		cp = put16(cp,tcph->mss);
	}
	if(tcph->flags.tstamp){
		*cp++ = TSTAMP_KIND;
		*cp++ = TSTAMP_LENGTH;
		cp = put32(cp,tcph->tsval);
		cp = put32(cp,tcph->tsecr);
	}
	if(tcph->flags.wscale){
		*cp++ = WSCALE_KIND;
		*cp++ = WSCALE_LENGTH;
		*cp++ = tcph->wsopt;
	}
	if(tcph->checksum == 0){
		/* Recompute header checksum */
		struct pseudo_header ph;

		ph.source = ipsrc;
		ph.dest = ipdest;
		ph.protocol = TCP_PTCL;
		ph.length = len_p(*bpp);
		put16(&(*bpp)->data[16],cksum(&ph,*bpp,ph.length));
	}
}
/* Pull TCP header off mbuf */
int
ntohtcp(
struct tcp *tcph,
struct mbuf **bpp
){
	int hdrlen,i,optlen,kind;
	register int flags;
	uint8 hdrbuf[TCPLEN],*cp;
	uint8 options[TCP_MAXOPT];

	memset(tcph,0,sizeof(struct tcp));
	i = pullup(bpp,hdrbuf,TCPLEN);
	/* Note that the results will be garbage if the header is too short.
	 * We don't check for this because returned ICMP messages will be
	 * truncated, and we at least want to get the port numbers.
	 */
	tcph->source = get16(&hdrbuf[0]);
	tcph->dest = get16(&hdrbuf[2]);
	tcph->seq = get32(&hdrbuf[4]);
	tcph->ack = get32(&hdrbuf[8]);
	hdrlen = (hdrbuf[12] & 0xf0) >> 2;
	flags = hdrbuf[13];
	tcph->flags.congest = (flags & 64) ? 1 : 0;
	tcph->flags.urg = (flags & 32) ? 1 : 0;
	tcph->flags.ack = (flags & 16) ? 1 : 0;
	tcph->flags.psh = (flags & 8) ? 1 : 0;
	tcph->flags.rst = (flags & 4) ? 1 : 0;
	tcph->flags.syn = (flags & 2) ? 1 : 0;
	tcph->flags.fin = (flags & 1) ? 1 : 0;
	tcph->wnd = get16(&hdrbuf[14]);
	tcph->checksum = get16(&hdrbuf[16]);
	tcph->up = get16(&hdrbuf[18]);
	optlen = hdrlen - TCPLEN;

	/* Check for option field */
	if(i < TCPLEN || hdrlen < TCPLEN)
		return -1;	/* Header smaller than legal minimum */
	if(optlen == 0)
		return (int)hdrlen;	/* No options, all done */

	if(optlen > len_p(*bpp)){
		/* Remainder too short for options length specified */
		return -1;
	}
	pullup(bpp,options,optlen);	/* "Can't fail" */
	/* Process options */
	for(cp=options,i=optlen; i > 0;){
		kind = *cp++;
		i--;
		/* Process single-byte options */
		switch(kind){
		case EOL_KIND:
			return (int)hdrlen;	/* End of options list */
		case NOOP_KIND:
			continue;	/* Go look for next option */
		}
		/* All other options have a length field */
		optlen = *cp++;

		/* Process valid multi-byte options */
		switch(kind){
		case MSS_KIND:
			if(optlen == MSS_LENGTH){
				tcph->mss = get16(cp);
				tcph->flags.mss = 1;
			}
			break;
		case WSCALE_KIND:
			if(optlen == WSCALE_LENGTH){
				tcph->wsopt = *cp;
				tcph->flags.wscale = 1;
			}
			break;
		case TSTAMP_KIND:
			if(optlen == TSTAMP_LENGTH){
				tcph->tsval = get32(cp);
				tcph->tsecr = get32(cp+4);
				tcph->flags.tstamp = 1;
			}
			break;
		}
		optlen = max(2,optlen);	/* Enforce legal minimum */
		i -= optlen;
		cp += optlen - 2;
	}
	return (int)hdrlen;
}
