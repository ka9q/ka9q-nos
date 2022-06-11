/*
 *	PPPDUMP.C
 *
 *	12-89	-- Katie Stevens (dkstevens@ucdavis.edu)
 *		   UC Davis, Computing Services
 *	PPP.08	05-90	[ks] improve tracing reports
 *	PPP.09  05-90	[ks] add UPAP packet reporting
 *	PPP.14	08-90	[ks] change UPAP to PAP for consistency with RFC1172
 *	PPP.15	09-90	[ks] update to KA9Q NOS v900828
 *	Jan 91	[Bill Simpson] small changes to match rewrite of PPP
 *	Aug 91	[Bill Simpson] fixed some buffer loss
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "internet.h"
#include "ppp.h"
#include "trace.h"

#ifdef TURBOC_SWITCH_BUG
#pragma option -G-
#endif

/* dump a PPP packet */
void
ppp_dump(fp,bpp,unused)
FILE *fp;
struct mbuf **bpp;
int unused;
{
	struct ppp_hdr hdr;
	struct mbuf *tbp;

	fprintf(fp,"PPP: len %3u\t", len_p(*bpp));

	/* HDLC address and control fields may be compressed out */
	if ((byte_t)(*bpp)->data[0] != HDLC_ALL_ADDR) {
		fprintf(fp,"(compressed ALL/UI)\t");
	} else if ((byte_t)(*bpp)->data[1] != HDLC_UI) {
		fprintf(fp,"(missing UI!)\t");
	} else {
		/* skip address/control fields */
		pull16(bpp);
	}

	/* Initialize the expected header */
	hdr.addr = HDLC_ALL_ADDR;
	hdr.control = HDLC_UI;
	hdr.protocol = PULLCHAR(bpp);

	/* First byte of PPP protocol field may be compressed out */
	if ( hdr.protocol & 0x01 ) {
		fprintf(fp,"compressed ");
	} else {
		hdr.protocol = (hdr.protocol << 8) | PULLCHAR(bpp);

		/* Second byte of PPP protocol field must be odd */
		if ( !(hdr.protocol & 0x01) ) {
			fprintf(fp, "(not odd!) " );
		}
	}

	fprintf(fp,"protocol: ");
	switch(hdr.protocol){
		case PPP_IP_PROTOCOL:
			fprintf(fp,"IP\n");
			ip_dump(fp,bpp,1);
			break;
		case PPP_IPCP_PROTOCOL:
			fprintf(fp,"IPCP\n");
			break;
		case PPP_LCP_PROTOCOL:
			fprintf(fp,"LCP\n");
			break;
		case PPP_PAP_PROTOCOL:
			fprintf(fp,"PAP\n");
			break;
		case PPP_COMPR_PROTOCOL:
			fprintf(fp,"VJ Compressed TCP/IP\n");
			vjcomp_dump(fp,bpp,0);
			break;
		case PPP_UNCOMP_PROTOCOL:
			fprintf(fp,"VJ Uncompressed TCP/IP\n");
			/* Get our own copy so we can mess with the data */
			if ( (tbp = copy_p(*bpp, len_p(*bpp))) == NULL)
				return;

			fprintf(fp,"\tconnection 0x%02x\n",
				tbp->data[9]);		/* FIX THIS! */
			/* Restore the bytes used with Uncompressed TCP */
			tbp->data[9] = TCP_PTCL;	/* FIX THIS! */
			ip_dump(fp,&tbp,1);
			free_p(&tbp);
			break;
		default:
			fprintf(fp,"unknown 0x%04x\n",hdr.protocol);
			break;
	}
}

#ifdef TURBOC_SWITCH_BUG
#pragma option -G
#endif
