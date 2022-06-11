/* ARP packet tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "arp.h"
#include "netuser.h"
#include "trace.h"

void
arp_dump(fp,bpp)
FILE *fp;
struct mbuf **bpp;
{
	struct arp arp;
	struct arp_type *at;
	int is_ip = 0;
	char tmp[25];

	if(bpp == NULL || *bpp == NULL)
		return;
	fprintf(fp,"ARP: len %d",len_p(*bpp));
	if(ntoharp(&arp,bpp) == -1){
		fprintf(fp," bad packet\n");
		return;
	}
	if(arp.hardware < NHWTYPES)
		at = &Arp_type[arp.hardware];
	else
		at = NULL;

	/* Print hardware type in Ascii if known, numerically if not */
	fprintf(fp," hwtype %s",smsg(Arptypes,NHWTYPES,arp.hardware));

	/* Print hardware length only if unknown type, or if it doesn't match
	 * the length in the known types table
	 */
	if(at == NULL || arp.hwalen != at->hwalen)
		fprintf(fp," hwlen %u",arp.hwalen);

	/* Check for most common case -- upper level protocol is IP */
	if(at != NULL && arp.protocol == at->iptype){
		fprintf(fp," prot IP");
		is_ip = 1;
	} else {
		fprintf(fp," prot 0x%x prlen %u",arp.protocol,arp.pralen);
	}
	switch(arp.opcode){
	case ARP_REQUEST:
		fprintf(fp," op REQUEST");
		break;
	case ARP_REPLY:
		fprintf(fp," op REPLY");
		break;
	case REVARP_REQUEST:
		fprintf(fp," op REVERSE REQUEST");
		break;
	case REVARP_REPLY:
		fprintf(fp," op REVERSE REPLY");
		break;
	default:
		fprintf(fp," op %u",arp.opcode);
		break;
	}
	fprintf(fp,"\n");
	fprintf(fp,"sender");
	if(is_ip)
		fprintf(fp," IPaddr %s",inet_ntoa(arp.sprotaddr));
	fprintf(fp," hwaddr %s\n",at->format(tmp,arp.shwaddr));

	fprintf(fp,"target");
	if(is_ip)
		fprintf(fp," IPaddr %s",inet_ntoa(arp.tprotaddr));
	fprintf(fp," hwaddr %s\n",at->format(tmp,arp.thwaddr));
}
