/* Asynchronous HDLC routines */
#include "global.h"
#include "ahdlc.h"
#include "crc.h"
#include "trace.h"	/******/

static uint8 *putbyte(uint8 *,uint8);

void
init_hdlc(hp,maxsize)
struct ahdlc *hp;
int maxsize;
{
	hp->escaped = 0;
	hp->hunt = 0;
	hp->inframe = NULL;
	hp->maxsize = maxsize;
	hp->fcs = FCS_START;
	hp->rxframes = 0;
	hp->aborts = 0;
	hp->toobigs = 0;
	hp->crcerrs = 0;
}

/* Process incoming data. Return completed packets, NULL otherwise */
struct mbuf *
ahdlcrx(ap,c)
struct ahdlc *ap;	/* HDLC Receiver control block */
uint8 c;
{
	struct mbuf *bp;

	if(c == HDLC_ESC_ASYNC){
		ap->escaped = 1;
		return NULL;
	}
	if(c != HDLC_FLAG){
		if(ap->hunt)
			return NULL; /* Ignore until next packet */
		/* Normal character within packet */
		if(ap->escaped){
			c ^= HDLC_ESC_COMPL;
			ap->escaped = 0;
		}
		if(ap->inframe == NULL)
			ap->inframe = ambufw(ap->maxsize);
		if(ap->inframe->cnt == ap->maxsize){
			/* Frame too large */
			ap->toobigs++;
#ifdef	debug
			printf("FRAME TOO LARGE (>%u bytes)\n",ap->maxsize);
#endif
			free_p(&ap->inframe);
			ap->inframe = NULL;
			ap->escaped = 0;
			ap->fcs = FCS_START;
			ap->hunt = 1;
			return NULL;
		}
		/* Store character, update FCS */
		ap->inframe->data[ap->inframe->cnt++] = c;
		ap->fcs = FCS(ap->fcs,c);
		return NULL;
	}
	/* We get here only if the character is a flag */
	if(ap->escaped){
		/* ESC, FLAG is frame abort */
		ap->aborts++;
#ifdef	debug
		printf("AHDLC ABORT, cnt = %u\n",ap->inframe->cnt);
#endif
		ap->hunt = 1;
		ap->escaped = 0;
		free_p(&ap->inframe);
		ap->inframe = NULL;
		ap->fcs = FCS_START;
		return NULL;
	}
	if(ap->hunt){
		/* Found flag in hunt mode. Reset for new frame */
		ap->hunt = 0;
		return NULL;
	}
	if(ap->inframe == NULL){
		/* Padding flags, ignore */
		return NULL;
	}
	if(ap->fcs != FCS_FINAL){
		/* CRC error */
		ap->crcerrs++;
#ifdef	debug
		printf("AHDLC CRC ERROR, cnt = %u\n",ap->inframe->cnt);
		hex_dump(stdout,&ap->inframe);
#endif
		free_p(&ap->inframe);
		ap->inframe = NULL;
		ap->fcs = FCS_START;
		return NULL;
	}
	if(ap->inframe->cnt < 2){
		/* Runt frame */
		ap->runts++;
#ifdef	debug
		printf("AHDLC RUNT, cnt = %u\n",ap->inframe->cnt);
#endif
		free_p(&ap->inframe);
		ap->inframe = NULL;
		ap->fcs = FCS_START;
		return NULL;
	}
	/* Normal end-of-frame */
	ap->rxframes++;
	bp = ap->inframe;
	ap->inframe = NULL;
	ap->fcs = FCS_START;
	bp->cnt -= 2;
#ifdef	debug
	printf("Normal AHDLC receive, len %u\n",bp->cnt);
#endif
	return bp;
}
/* Encode a packet in asynchronous HDLC for transmission */
struct mbuf *
ahdlctx(bp)
struct mbuf *bp;
{
	struct mbuf *obp;
	uint8 *cp;
	int c;
	uint16 fcs;

	fcs = FCS_START;
	obp = ambufw(5+2*len_p(bp));	/* Allocate worst-case */
	cp = obp->data;
	while((c = PULLCHAR(&bp)) != -1){
		fcs = FCS(fcs,c);
		cp = putbyte(cp,c);
	}
	free_p(&bp);	/* Shouldn't be necessary */
	fcs ^= 0xffff;
	cp = putbyte(cp,fcs);
	cp = putbyte(cp,fcs >> 8);
	*cp++ = HDLC_FLAG;

	obp->cnt = cp - obp->data;
	return obp;
}
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
