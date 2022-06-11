#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "ip.h"
#include "slhc.h"
#include "trace.h"

static uint16 decodeint(struct mbuf **bpp);

static uint16
decodeint(bpp)
struct mbuf **bpp;
{
	uint8 tmpbuf[2];

	pullup(bpp,tmpbuf,1);
	if (tmpbuf[0] == 0)
		pullup(bpp,tmpbuf,2);
	else {
	 	tmpbuf[1] = tmpbuf[0];
		tmpbuf[0] = 0;
	}
	return(get16(tmpbuf));
}

void
vjcomp_dump(fp,bpp,unused)
FILE *fp;
struct mbuf **bpp;
int unused;
{
	uint8 changes;
	uint8 tmpbuf[2];

	if(bpp == NULL || *bpp == NULL)
		return;	

	/* Dump compressed TCP/IP header */
	changes = pullchar(bpp);
	fprintf(fp,"\tchanges: 0x%02x",changes);
	if (changes & NEW_C) {
		pullup(bpp,tmpbuf,1);
		fprintf(fp,"   connection: 0x%02x",tmpbuf[0]);
	}
	pullup(bpp,tmpbuf,2);
	fprintf(fp,"   TCP checksum: 0x%04x",get16(tmpbuf));

	if (changes & TCP_PUSH_BIT)
		fprintf(fp,"   PUSH");
	fprintf(fp,"\n");

	switch (changes & SPECIALS_MASK) {
	case SPECIAL_I:
		fprintf(fp,"\tdelta ACK and delta SEQ implied by length of data\n");
		break;

	case SPECIAL_D:
		fprintf(fp,"\tdelta SEQ implied by length of data\n");
		break;

	default:
		if (changes & NEW_U) {
			fprintf(fp,"\tUrgent pointer: 0x%02x",decodeint(bpp));
		}
		if (changes & NEW_W)
			fprintf(fp,"\tdelta WINDOW: 0x%02x",decodeint(bpp));
		if (changes & NEW_A)
			fprintf(fp,"\tdelta ACK: 0x%02x",decodeint(bpp));
		if (changes & NEW_S)
			fprintf(fp,"\tdelta SEQ: 0x%02x",decodeint(bpp));
		break;
	};
	if (changes & NEW_I) {
		fprintf(fp,"\tdelta ID: 0x%02x\n",decodeint(bpp));
	} else {
		fprintf(fp,"\tincrement ID\n");
	}
}


/* dump serial line IP packet; may have Van Jacobson TCP header compression */
void
sl_dump(fp,bpp,unused)
FILE *fp;
struct mbuf **bpp;
int unused;
{
	struct mbuf *bp, *tbp;
	unsigned char c;
	int len;

	bp = *bpp;
	c = bp->data[0];
	if (c & SL_TYPE_COMPRESSED_TCP) {
		fprintf(fp,"serial line VJ Compressed TCP: len %3u\n",
			len_p(*bpp));
		vjcomp_dump(fp,bpp,0);
	} else if ( c >= SL_TYPE_UNCOMPRESSED_TCP ) {
		fprintf(fp,"serial line VJ Uncompressed TCP: len %3u\n",
			len = len_p(bp));
		/* Get our own copy so we can mess with the data */
		if ( (tbp = copy_p(bp, len)) == NULL )
			return;

		fprintf(fp,"\tconnection ID = %d\n",
			tbp->data[9]);	/* FIX THIS! */
		/* Restore the bytes used with Uncompressed TCP */
		tbp->data[0] &= 0x4f;		/* FIX THIS! */
		tbp->data[9] = TCP_PTCL;	/* FIX THIS! */	
		/* Dump contents as a regular IP packet */
		ip_dump(fp,&tbp,1);
		free_p(&tbp);
	} else {
		fprintf(fp,"serial line IP: len: %3u\n",len_p(*bpp));
		ip_dump(fp,bpp,1);
	}
}


