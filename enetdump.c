/* Ethernet header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "enet.h"
#include "trace.h"

void
ether_dump(
FILE *fp,
struct mbuf **bpp,
int check	/* Not used */
){
	struct ether ehdr;
	char s[20],d[20];

	ntohether(&ehdr,bpp);
	pether(s,ehdr.source);
	pether(d,ehdr.dest);
	fprintf(fp,"Ether: len %u %s->%s",ETHERLEN + len_p(*bpp),s,d);

	switch(ehdr.type){
		case IP_TYPE:
			fprintf(fp," type IP\n");
			ip_dump(fp,bpp,1);
			break;
		case REVARP_TYPE:
			fprintf(fp," type REVARP\n");
			arp_dump(fp,bpp);
			break;
		case ARP_TYPE:
			fprintf(fp," type ARP\n");
			arp_dump(fp,bpp);
			break;
		default:
			fprintf(fp," type 0x%x\n",ehdr.type);
			break;
	}
}
int
ether_forus(struct iface *iface,struct mbuf *bp)
{
	/* Just look at the multicast bit */

	if(bp->data[0] & 1)
		return 0;
	else
		return 1;
}
