/* UDP packet tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "udp.h"
#include "ip.h"
#include "socket.h"
#include "trace.h"

/* Dump a UDP header */
void
udp_dump(fp,bpp,source,dest,check)
FILE *fp;
struct mbuf **bpp;
int32 source,dest;
int check;		/* If 0, bypass checksum verify */
{
	struct udp udp;
	struct pseudo_header ph;
	uint16 csum;

	if(bpp == NULL || *bpp == NULL)
		return;

	fprintf(fp,"UDP:");

	/* Compute checksum */
	ph.source = source;
	ph.dest = dest;
	ph.protocol = UDP_PTCL;
	ph.length = len_p(*bpp);
	if((csum = cksum(&ph,*bpp,ph.length)) == 0)
		check = 0;	/* No checksum error */

	ntohudp(&udp,bpp);

	fprintf(fp," len %u",udp.length);
	fprintf(fp," %u->%u",udp.source,udp.dest);
	if(udp.length > UDPHDR)
		fprintf(fp," Data %u",udp.length - UDPHDR);
	if(udp.checksum == 0)
		check = 0;
	if(check)
		fprintf(fp," CHECKSUM ERROR (%u)",csum);

	putc('\n',fp);

	switch(udp.dest){
	case IPPORT_RIP:
		rip_dump(fp,bpp);
	}

}

