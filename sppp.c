/* Simplified Point-to-Point Protocol
 * No negotiation, no address or ctl fields, 1-byte pids
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ip.h"
#include "slhc.h"
#include "asy.h"
#include "crc.h"
#include "ahdlc.h"
#include "sppp.h"
#include "trace.h"

static uint8 *putbyte(uint8 *cp,uint8 c);

int
sppp_init(ifp)
struct iface *ifp;
{
	char *ifn;

	ifp->ioctl = asy_ioctl;
	ifp->edv = slhc_init(16,16);
	ifp->rxproc = newproc(ifn = if_name(ifp," rx"),
		512,sppp_rx,ifp->dev,ifp,NULL,0);
	free(ifn);
	return 0;
}
int
sppp_free(ifp)
struct iface *ifp;
{
	free(ifp->edv);
	return 0;
}
/* Send an IP datagram using simplified PPP */
int
sppp_send(
struct mbuf **bpp,
struct iface *ifp,
int32 gateway,	/* Not used */
uint8 tos	/* Not used at this level */
){
	int type,protocol;
	struct slcompress *sp = ifp->edv;

	dump(ifp,IF_TRACE_OUT,*bpp);
	ifp->rawsndcnt++;
	ifp->lastsent = secclock();
	type = slhc_compress(sp,bpp,1);
	switch(type){
	default:	/* "Can't happen" */
	case SL_TYPE_IP:
		protocol = PPP_IP_PROTOCOL;
		break;
	case SL_TYPE_COMPRESSED_TCP:
		protocol = PPP_COMPR_PROTOCOL;
		break;
	case SL_TYPE_UNCOMPRESSED_TCP:
		protocol = PPP_UNCOMP_PROTOCOL;
		break;
	}
	return sppp_output(ifp,protocol,bpp);
}
int
sppp_output(
struct iface *iface,
int protocol,
struct mbuf **bpp
){
	struct mbuf *obp;
	uint8 *cp;
	int c;
	uint16 fcs;

	fcs = FCS_START;
	obp = ambufw(6+2*len_p(*bpp));	/* Allocate worst-case */
	cp = obp->data;
	*cp++ = HDLC_FLAG;
	cp = putbyte(cp,(char)protocol);
	fcs = FCS(fcs,protocol);
	while((c = PULLCHAR(bpp)) != -1){
		fcs = FCS(fcs,c);
		cp = putbyte(cp,c);
	}
	free_p(bpp);	/* Shouldn't be necessary */
	fcs ^= 0xffff;
	cp = putbyte(cp,fcs);
	cp = putbyte(cp,fcs >> 8);
	*cp++ = HDLC_FLAG;

	obp->cnt = cp - obp->data;
	return asy_send(iface->dev,&obp);
}
/* Add a byte to the PPP output stream, byte stuffing for transparency */
static uint8 *
putbyte(cp,c)
uint8 *cp;
uint8 c;
{
	switch(c){
	case HDLC_FLAG:
	case HDLC_ESC_ASYNC:
		*cp++ = HDLC_ESC_ASYNC;
		*cp++ = c ^ HDLC_ESC_COMPL;
		break;
	default:
		*cp++ = c;
	}
	return cp;
}

/* Process simplified PPP line input */
void
sppp_rx(dev,p1,p2)
int dev;
void *p1;
void *p2;
{
	struct mbuf *bp;
	int c;
	struct ahdlc ahdlc;
	struct iface *ifp = (struct iface *)p1;
	struct slcompress *sp = ifp->edv;

	init_hdlc(&ahdlc,2048);
	for(;;){
		c = get_asy(dev);
		if((bp = ahdlcrx(&ahdlc,c)) == NULL)
			continue;
		c = PULLCHAR(&bp);
		switch(c){	/* Turn compressed IP/TCP back to normal */
		case PPP_COMPR_PROTOCOL:
			if(slhc_uncompress(sp,&bp) <= 0)
				c = -1;	/* Failed; discard */
			else 
				c = PPP_IP_PROTOCOL;
			break;
		case PPP_UNCOMP_PROTOCOL:
			if(slhc_remember(sp,&bp) <= 0)
				c = -1;	/* Failed; discard */
			else
				c = PPP_IP_PROTOCOL;
			break;
		}
		switch(c){
		case PPP_IP_PROTOCOL:
			net_route(ifp,&bp);
			break;
		case -1:
			break;
		default:	/* Toss all unknown types */
			free_p(&bp);
			break;
		}
	}
}

