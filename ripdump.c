/* RIP packet tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "rip.h"
#include "trace.h"

void
rip_dump(fp,bpp)
FILE *fp;
struct mbuf **bpp;
{
	struct rip_route entry;
	int i;
	int cmd,version;
	uint16 len;
	
	fprintf(fp,"RIP: ");
	cmd = PULLCHAR(bpp);
	version = PULLCHAR(bpp);
	switch(cmd){
	case RIPCMD_REQUEST:
		fprintf(fp,"REQUEST");
		break;
	case RIPCMD_RESPONSE:
		fprintf(fp,"RESPONSE");
		break;
	default:
		fprintf(fp," cmd %u",cmd);
		break;
	}

	pull16(bpp);	/* remove one word of padding */

	len = len_p(*bpp);
	fprintf(fp," vers %u entries %u:\n",version,len / RIPROUTE);

	i = 0;
	while(len >= RIPROUTE){
		/* Pull an entry off the packet */
		pullentry(&entry,bpp);
		len -= RIPROUTE;

		if(entry.addr_fam != RIP_IPFAM) {
			/* Skip non-IP addresses */
			continue;
		}
		fprintf(fp,"%-16s%-3u ",inet_ntoa(entry.target),entry.metric);
		if((++i % 3) == 0){
			putc('\n',fp);
		}
	}
	if((i % 3) != 0)
		putc('\n',fp);
}
