/* AX25 header tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "trace.h"
#include "socket.h"

static char *decode_type(uint16 type);

/* Dump an AX.25 packet header */
void
ax25_dump(fp,bpp,check)
FILE *fp;
struct mbuf **bpp;
int check;	/* Not used */
{
	char tmp[AXBUF];
	char frmr[3];
	int control,pid,seg;
	uint16 type;
	int unsegmented;
	struct ax25 hdr;
	uint8 *hp;

	fprintf(fp,"AX25: ");
	/* Extract the address header */
	if(ntohax25(&hdr,bpp) < 0){
		/* Something wrong with the header */
		fprintf(fp," bad header!\n");
		return;
	}
	fprintf(fp,"%s",pax25(tmp,hdr.source));
	fprintf(fp,"->%s",pax25(tmp,hdr.dest));
	if(hdr.ndigis > 0){
		fprintf(fp," v");
		for(hp = hdr.digis[0]; hp < &hdr.digis[hdr.ndigis][0];
		 hp += AXALEN){
			/* Print digi string */
			fprintf(fp," %s%s",pax25(tmp,hp),
			 (hp[ALEN] & REPEATED) ? "*":"");
		}
	}
	if((control = PULLCHAR(bpp)) == -1)
		return;

	putc(' ',fp);
	type = ftype(control);
	fprintf(fp,"%s",decode_type(type));
	/* Dump poll/final bit */
	if(control & PF){
		switch(hdr.cmdrsp){
		case LAPB_COMMAND:
			fprintf(fp,"(P)");
			break;
		case LAPB_RESPONSE:
			fprintf(fp,"(F)");
			break;
		default:
			fprintf(fp,"(P/F)");
			break;
		}
	}
	/* Dump sequence numbers */
	if((type & 0x3) != U)	/* I or S frame? */
		fprintf(fp," NR=%d",(control>>5)&7);
	if(type == I || type == UI){	
		if(type == I)
			fprintf(fp," NS=%d",(control>>1)&7);
		/* Decode I field */
		if((pid = PULLCHAR(bpp)) != -1){	/* Get pid */
			if(pid == PID_SEGMENT){
				unsegmented = 0;
				seg = PULLCHAR(bpp);
				fprintf(fp,"%s remain %u",seg & SEG_FIRST ?
				 " First seg;" : "",seg & SEG_REM);
				if(seg & SEG_FIRST)
					pid = PULLCHAR(bpp);
			} else
				unsegmented = 1;

			switch(pid){
			case PID_SEGMENT:
				putc('\n',fp);
				break;	/* Already displayed */
			case PID_ARP:
				fprintf(fp," pid=ARP\n");
				arp_dump(fp,bpp);
				break;
			case PID_NETROM:
				fprintf(fp," pid=NET/ROM\n");
				/* Don't verify checksums unless unsegmented */
				netrom_dump(fp,bpp,unsegmented);
				break;
			case PID_IP:
				fprintf(fp," pid=IP\n");
				/* Don't verify checksums unless unsegmented */
				ip_dump(fp,bpp,unsegmented);
				break;
			case PID_X25:
				fprintf(fp," pid=X.25\n");
				break;
			case PID_TEXNET:
				fprintf(fp," pid=TEXNET\n");
				break;
			case PID_NO_L3:
				fprintf(fp," pid=Text\n");
				break;
			default:
				fprintf(fp," pid=0x%x\n",pid);
			}
		}
	} else if(type == FRMR && pullup(bpp,frmr,3) == 3){
		fprintf(fp,": %s",decode_type(ftype(frmr[0])));
		fprintf(fp," Vr = %d Vs = %d",(frmr[1] >> 5) & MMASK,
			(frmr[1] >> 1) & MMASK);
		if(frmr[2] & W)
			fprintf(fp," Invalid control field");
		if(frmr[2] & X)
			fprintf(fp," Illegal I-field");
		if(frmr[2] & Y)
			fprintf(fp," Too-long I-field");
		if(frmr[2] & Z)
			fprintf(fp," Invalid seq number");
		putc('\n',fp);
	} else
		putc('\n',fp);

}
static char *
decode_type(type)
uint16 type;
{
	switch(type){
	case I:
		return "I";
	case SABM:
		return "SABM";
	case DISC:
		return "DISC";
	case DM:
		return "DM";
	case UA:
		return "UA";
	case RR:
		return "RR";
	case RNR:
		return "RNR";
	case REJ:
		return "REJ";
	case FRMR:
		return "FRMR";
	case UI:
		return "UI";
	default:
		return "[invalid]";
	}
}

/* Return 1 if this packet is directed to us, 0 otherwise. Note that
 * this checks only the ultimate destination, not the digipeater field
 */
int
ax_forus(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	struct mbuf *bpp;
	uint8 dest[AXALEN];

	/* Duplicate the destination address */
	if(dup_p(&bpp,bp,0,AXALEN) != AXALEN){
		free_p(&bpp);
		return 0;
	}
	if(pullup(&bpp,dest,AXALEN) < AXALEN)
		return 0;
	if(addreq(dest,iface->hwaddr))
		return 1;
	else
		return 0;
}

