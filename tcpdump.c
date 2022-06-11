/* TCP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"
#include "ip.h"
#include "trace.h"

/* TCP segment header flags */
static char *Tcpflags[] = {
	"FIN",	/* 0x01 */
	"SYN",	/* 0x02 */
	"RST",	/* 0x04 */
	"PSH",	/* 0x08 */
	"ACK",	/* 0x10 */
	"URG",	/* 0x20 */
	"CE"	/* 0x40 */
};

/* Dump a TCP segment header. Assumed to be in network byte order */
void
tcp_dump(fp,bpp,source,dest,check)
FILE *fp;
struct mbuf **bpp;
int32 source,dest;	/* IP source and dest addresses */
int check;		/* 0 if checksum test is to be bypassed */
{
	struct tcp seg;
	struct pseudo_header ph;
	uint16 csum;
	uint16 dlen;

	if(bpp == NULL || *bpp == NULL)
		return;

	/* Verify checksum */
	ph.source = source;
	ph.dest = dest;
	ph.protocol = TCP_PTCL;
	ph.length = len_p(*bpp);
	csum = cksum(&ph,*bpp,ph.length);

	ntohtcp(&seg,bpp);

	fprintf(fp,"TCP: %u->%u Seq x%lx",seg.source,seg.dest,seg.seq,seg.ack);
	if(seg.flags.ack)
		fprintf(fp," Ack x%lx",seg.ack);
	if(seg.flags.congest)
		fprintf(fp," %s",Tcpflags[6]);
	if(seg.flags.urg)
		fprintf(fp," %s",Tcpflags[5]);
	if(seg.flags.ack)
		fprintf(fp," %s",Tcpflags[4]);
	if(seg.flags.psh)
		fprintf(fp," %s",Tcpflags[3]);
	if(seg.flags.rst)
		fprintf(fp," %s",Tcpflags[2]);
	if(seg.flags.syn)
		fprintf(fp," %s",Tcpflags[1]);
	if(seg.flags.fin)
		fprintf(fp," %s",Tcpflags[0]);

	fprintf(fp," Wnd %u",seg.wnd);
	if(seg.flags.urg)
		fprintf(fp," UP x%x",seg.up);
	/* Print options, if any */
	if(seg.flags.mss)
		fprintf(fp," MSS %u",seg.mss);
	if(seg.flags.wscale)
		fprintf(fp," WSCALE %u",seg.wsopt);
	if(seg.flags.tstamp)
		fprintf(fp," TSTAMP %lu TSECHO %lu",seg.tsval,seg.tsecr);
	if((dlen = len_p(*bpp)) != 0)
		fprintf(fp," Data %u",dlen);
	if(check && csum != 0)
		fprintf(fp," CHECKSUM ERROR (%u)",csum);
	putc('\n',fp);
}

