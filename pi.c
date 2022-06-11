/*
 * Interface driver for the VE3IFB 8530 card (PI card)
 * Copyright 1990 by Dave Perry, VE3IFB
 * Minor delinting - KA9Q 2/2/91
 *
 * Portions of this driver were derived from the Eagle card
 * driver by Art Goldman, WA3CVG. It has been very extensively
 * modified from his work, due to the addition of DMA support
 * and due to differences in the hardware.  The PI card is NOT
 * an Eagle card clone. It is an original design by Dave Perry,
 * VE3IFB.  Art's copyright notice follows:
 *
 *  Written by Art Goldman, WA3CVG - (c) Copyright 1987 All Rights Reserved
 *  Permission for non-commercial use is hereby granted provided this notice
 *  is retained.  For info call: (301) 997-3838.
 *
 */

#include <time.h>
#include <stdio.h>
#include <dos.h>
#include <bios.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "pi.h"
#include "z8530.h"
#include "ax25.h"
#include "trace.h"
#include "nospc.h"

#include "session.h"
#include "lapb.h"
#include "proc.h"
#include "ip.h"
#include "devparam.h"

#ifndef FP_OFF
#define FP_OFF(fp)	((unsigned)(fp))
#endif
#ifndef FP_SEG
#define FP_SEG(fp)	((unsigned)((unsigned long)(fp) >> 16))
#endif

void b_rxint(struct pichan *);
void b_txint(struct pichan *);
void b_exint(struct pichan *);
void a_rxint(struct pichan *);
void a_txint(struct pichan *);
void a_exint(struct pichan *);

static void xwrite_scc(struct pichan *hp,uint16 ctl,uint16 reg,
	uint16 val );
static char xread_scc(struct pichan *hp, uint16 ctl, char reg);
static int32 pi_ctl(struct iface *iface,int cmd,int set,int32 val);
static int pi_raw(struct iface *iface,struct mbuf **bpp);
static int pi_stop(struct iface *iface);
static void rts(struct pichan *hp, uint16 x);
void setup_rx_dma(struct pichan *hp);
void setup_tx_dma(struct pichan *hp);
static void set_acc_delay(void);
static void tdelay(register struct pichan *hp,unsigned int time);
static int scc_init(register struct pichan *hp);

static struct PITAB Pi[PIMAX];	/* Device table - one entry per card */
static INTERRUPT (*pihandle[])() = {	/* handler interrupt vector table */
	pi0vec,
	pi1vec,
	pi2vec
};
static uint16 Page_regs[] = {
	0x87,0x83,0x81,0x82,0,0x8b,0x89,0x8a
};
static struct pichan Pichan[2*PIMAX];	/* channel table - 2 entries per card */
static uint16 pinbr;

extern uint16 acc_delay;	/* Delay for the 8530 chip access recovery time */


/* This calculates the constant to be used in the delay loops
 *  which satify the SCC's access recovery time.  It needs to be timed and
 *  calculated because a fixed value would not work in a 4.77mhz XT
 *  to a 40mhz 486 (and beyond).
 */
static void
set_acc_delay()
{	
	long starttime, endtime;
	int n;
	int ticks;

	starttime = bioscnt();
	for(n = 0; n < 10; n++)
		mloop();
	endtime = bioscnt();
	ticks = (int) (endtime - starttime);
	if(ticks == 0)
		ticks = 1;
	acc_delay = 61/ticks;
	if(acc_delay == 0)
		acc_delay = 1;
	fflush(stdout);
}

/* Write 8530 register */
static void
xwrite_scc(hp,ctl,reg,val)
register struct pichan *hp;
register uint16 ctl;
uint16 reg,val;
{
	wrtscc(hp->cardbase,ctl,reg,val);
}

/* Read 8530 register */
static char
xread_scc(hp,ctl,reg)
register struct pichan *hp;
register uint16 ctl;
char reg;
{
	return(rdscc(hp->cardbase,ctl,reg));
}

/* Setup 8253 chip for time delay */
static void
tdelay(hp,time)
register struct pichan *hp;
unsigned int time;		 /* Time to delay in milliseconds */
{
	int n,port;
	unsigned int t1;
	unsigned char sc;

	if(hp->base & 2){ /* If A channel */
		sc = SC1;
		t1 = time;
		port = hp->cardbase+TMR1;
	} else {
		sc = SC2;
		t1 = 10 * time; /* 10s of milliseconds for the B channel */
		port = hp->cardbase+TMR2;
	}

	/* Setup timer sc */
	outportb(hp->cardbase+TMRCMD, sc|LSB_MSB|MODE0);
	
	/* satisfy access time restriction */
	for(n=0; n<5;n++)
		;
	/* times 2 to make millisecs */
	outportb(port, (t1 << 1) & 0xFF);

	/* satisfy access time restriction */
	for(n=0; n<5;n++)
		;
	outportb(port, (t1 >> 7) & 0xFF);

	/* Enable correct int for timeout */
	xwrite_scc(hp,hp->base+CTL,R15,CTSIE);
	xwrite_scc(hp,hp->base+CTL,R1,EXT_INT_ENAB);
	xwrite_scc(hp,hp->base+CTL,R0,RES_EXT_INT);
}

/* Master interrupt handler.  One interrupt at a time is handled.
 * here. Service routines are called from here.
 */
INTERRUPT (far *(piint)(dev))()
int dev;
{
	register char st;
	register uint16 pcbase;
	struct pichan *hp;
	struct PITAB *pip;

	pip = &Pi[dev];
	pip->ints++;
	pcbase = pip->addr;

	/* Read interrupt status register (only valid from channel A)
	 * Process all pending interrupts in while loop
	 */
	hp = &Pichan[2 * dev];	/* Assume channel A */
	while((st = xread_scc(hp,pcbase+CHANA+CTL,R3)) != 0){
		if(st & CHARxIP){
			/* Channel A Rcv Interrupt Pending */
			hp = &Pichan[2 * dev];
			a_rxint(hp);
		} else if(st & CHATxIP){
			/* Channel A Transmit Int Pending */
			hp = &Pichan[2 * dev];
			a_txint(hp);
		} else if(st & CHAEXT){
			/* Channel A External Status Int */
			hp = &Pichan[2 * dev];
			a_exint(hp);
		} else if(st & CHBRxIP){
			/* Channel B Rcv Interrupt Pending */
			hp = &Pichan[(2 * dev)+1];
			b_rxint(hp);
		} else if(st & CHBTxIP){
			/* Channel B Transmit Int Pending */
			hp = &Pichan[(2 * dev)+1];
			b_txint(hp);
		} else if(st & CHBEXT){
			/* Channel B External Status Int */
			hp = &Pichan[(2 * dev)+1];
			b_exint(hp);
		}
		/* Reset highest interrupt under service */
		xwrite_scc(hp,hp->base+CTL,R0,RES_H_IUS);
	} /* End of while loop on int processing */
	return pip->chain ? pip->oldvec : NULL;
}

static void
a_exint(hp)
register struct pichan *hp;
{
	register uint16 cmd;
	char st;
	int32 t,ca;
	struct mbuf *bp;
	int i_state;

	i_state = dirps();

	st = xread_scc(hp,hp->base+CTL,R0);     /* Fetch status */

	/* reset external status latch */
	xwrite_scc(hp,CTL+hp->base,R0,RES_EXT_INT);
	cmd = hp->base+CTL;
	hp->exints++;

	if((hp->rstate >= ACTIVE) && (st & BRK_ABRT)){	
		setup_rx_dma(hp);
		hp->rstate = ACTIVE;
	}
	switch(hp->tstate){
	case ACTIVE:
		hp->tstate = FLAGOUT;
		tdelay(hp,hp->squeldelay);
		break;
	case FLAGOUT:
		if((bp = dequeue(&hp->sndq)) == NULL){
			/* Nothing to send - return to receive mode */
			hp->tstate = IDLE;
			rts(hp,OFF);
			restore(i_state);
			return;
		}
		/* Get all chars */
		hp->txcnt = pullup(&bp,hp->sndbuf,hp->bufsiz);
		free_p(&bp);	/* Truncate overly long packets */

		/* NOTE - fall through if more to send */
	case ST_TXDELAY:
		/* Disable DMA chan */
		outportb(DMA_MASK, DMA_DISABLE|hp->dmachan);

		/* Set up for TX dma */
		xwrite_scc(hp,cmd,R1,WT_FN_RDYFN|EXT_INT_ENAB);

		/* Setup DMA controller for tx */
		setup_tx_dma(hp);

		/* select transmit interrupts to enable */
		/* Allow DMA on chan */
		outportb(DMA_MASK,DMA_ENABLE|hp->dmachan);

		/* reset CRC, Txint pend*/
		xwrite_scc(hp,cmd,R0,RES_Tx_CRC|RES_Tx_P);

		/* allow Underrun int only */
		xwrite_scc(hp,cmd,R15,TxUIE);

		/* Enable TX DMA */
		xwrite_scc(hp,cmd,R1,WT_RDY_ENAB|WT_FN_RDYFN|EXT_INT_ENAB);

		/* Send CRC on underrun */
		xwrite_scc(hp,cmd,R0,RES_EOM_L);

		/* packet going out now */
		hp->tstate = ACTIVE;
		break;
	case DEFER:
		/* we have deferred prev xmit attempt
		 * See Intel Microcommunications Handbook, p2-308
		 */
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		if((xread_scc(hp,cmd,R0) & DCD) != 0){
			hp->tstate = DEFER;
			tdelay(hp,100);
			/* Defer until dcd transition or 100mS timeout */
			xwrite_scc(hp,CTL+hp->base,R15,CTSIE|DCDIE);
			restore(i_state);
			return;
		}
		/* Defer logic. Wait until deftime is in the past (so we
		 * defer to any overheard CTS messages) AND the p-persistence
		 * dice roll succeeds. The computation of ca allows for clock
		 * rollover (which happens every 49+ days).
		 */
		t = msclock();
		ca = hp->deftime - t;
		if(ca > 0){
			hp->tstate = DEFER;
			tdelay(hp,ca);
			restore(i_state);
			return;
		}
		hp->deftime = t;	/* Keep from getting too old */
		if((rand() & 0xff) > hp->persist){
			hp->tstate = DEFER;
			tdelay(hp,hp->slotime);
			restore(i_state);
			return;
		}
		/* Assert RTS early minimize collision window */
		xwrite_scc(hp,cmd,R5,TxCRC_ENAB|RTS|Tx8);
		rts(hp,ON);	/* Transmitter on */
		hp->tstate = ST_TXDELAY;
		tdelay(hp,hp->txdelay);
		restore(i_state);
		return;
	} /* switch(hp->tstate) */

	restore(i_state);
} /* a_exint() */

/* Receive interrupt handler for the A channel 
 */
static void
a_rxint(hp)
register struct pichan *hp;
{
	register uint16 cmd;
	register uint16 bytecount;
	char rse;
	struct mbuf *bp;
	int i_state;
	
	hp->rxints++;
	cmd = hp->base+CTL;

	i_state = dirps();
	rse = xread_scc(hp,cmd,R1); /* Get special condition bits from R1 */
	if(rse & Rx_OVR){
		/* If receiver overrun */
		hp->rovers++;
		hp->rstate = RXERROR;
	}

	if(rse & END_FR){
		/* If end of frame */
		/* figure length of frame from 8237 */
		outportb(DMA_RESETFF,0); /* reset firstlast ff */
		bytecount = inportb(DMABASE+2*hp->dmachan+1);
		bytecount += inportb(DMABASE+2*hp->dmachan+1) << 8;
		bytecount = hp->bufsiz - 1 - bytecount;

		if((rse & CRC_ERR)||(hp->rstate > ACTIVE)||(bytecount < 10)){
			if((bytecount >= 10) && (rse & CRC_ERR))
				hp->crcerr++; /* Ignore noise */

			/* Reset buffer pointers */
			hp->rstate = ACTIVE;
			setup_rx_dma(hp);
		} else {
			/* Here we have a valid frame. Copy to buffer,
			 * minus 2 CRC bytes
			 */
			bytecount -= 2;
			if((bp = alloc_mbuf(bytecount+sizeof(struct iface *)))
			 != NULL){
				bp->data += sizeof(struct iface *);
				bp->cnt = bytecount;
				memcpy(bp->data,hp->rcvbuf,bytecount);
				net_route(hp->iface,&bp);
				hp->rxcnt = 0;
				hp->rxframes++;
			}
			/* packet queued - get buffer for next frame */
			setup_rx_dma(hp);
		} /* end good frame queued */
	} /* end EOF check */

	xwrite_scc(hp,hp->base+CTL,R0,ERR_RES);	/* error reset */
	restore(i_state);
}

void
a_txint(hp)
register struct pichan *hp;
{
	register uint16 cmd;
	int32 t,ca;
	struct mbuf *bp;
	int i_state;

	cmd = CTL+hp->base;

	i_state = dirps();
	switch(hp->tstate){
	case IDLE:
		/* Transmitter idle. Find a frame for transmission */
		if((bp = dequeue(&hp->sndq)) == NULL){
			rts(hp,OFF);
			restore(i_state);
			return;
		}
		/* If a buffer to send, we drop thru here */
		hp->txcnt = pullup(&bp,hp->sndbuf,hp->bufsiz);
		free_p(&bp);	/* Truncate overly long packet */
		hp->tcp = hp->sndbuf;
	case DEFER:
		/* we may have deferred prev xmit attempt */
		/* Check DCD - debounce it
		 * See Intel Microcommunications Handbook, p2-308
		 */
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		if((xread_scc(hp,cmd,R0) & DCD) != 0){
			hp->tstate = DEFER;
			tdelay(hp,100);
			/* defer until DCD transition or timeout */
			xwrite_scc(hp,cmd,R15,CTSIE|DCDIE);
			restore(i_state);
			return;
		}
		/* Defer logic. Wait until deftime is in the past (so we
		 * defer to any overheard CTS messages) AND the p-persistence
		 * dice roll succeeds. The computation of ca allows for clock
		 * rollover (which happens every 49+ days).
		 */
		t = msclock();
		ca = hp->deftime - t;
		if(ca > 0){
			hp->tstate = DEFER;
			tdelay(hp,ca);
			restore(i_state);
			return;
		}
		hp->deftime = t;	/* Keep from getting too old */
		if((rand() & 0xff) > hp->persist){
			hp->tstate = DEFER;
			tdelay(hp,hp->slotime);
			restore(i_state);
			return;
		}

		/* Assert RTS early minimize collision window */
		xwrite_scc(hp,cmd,R5,TxCRC_ENAB|RTS|Tx8);
		rts(hp,ON);	/* Transmitter on */
		hp->tstate = ST_TXDELAY;
		tdelay(hp,hp->txdelay);
		restore(i_state);
		return;
	default:
		break;
	} /* end switch(hp->state) */

	restore(i_state);
} /*a_txint */

static void
b_rxint(hp)
register struct pichan *hp;
{
	register uint16 cmd;
	char rse;
	struct mbuf *bp;
	int i_state;

	hp->rxints++;
	cmd = CTL+hp->base;

	i_state = dirps();
	if((xread_scc(hp,cmd,R0)) & Rx_CH_AV){
		/* there is a char to be stored
		 * read special condition bits before reading the data char
		 */
		rse = xread_scc(hp,cmd,R1); /* get status byte from R1 */
		if(rse & Rx_OVR){
			/* Rx overrun - toss buffer */
			/* reset buffer pointers */
			hp->rcp = hp->rcvbuf;
			hp->rxcnt = 0;
			hp->rstate = RXERROR;	/* set error flag */
			hp->rovers++;
		} else if(hp->rxcnt >= hp->bufsiz){
			/* Too large -- toss entire frame */
			/* reset buffer pointers */
			hp->rcp = hp->rcvbuf;
			hp->rxcnt = 0;
			hp->rstate = TOOBIG;	/* when set, chars are not stored */
		}
		/* ok, we can store the received character now */
		if(hp->rstate == ACTIVE){	/* If no errors... */
			*hp->rcp++ = xread_scc(hp,cmd,R8); /* char to rcv buff */
			hp->rxcnt++;		   /* bump count */
		} else {
			/* got to empty FIFO */
			(void) xread_scc(hp,cmd,R8);
			xwrite_scc(hp,cmd,R0,ERR_RES);	/* reset err latch */
			hp->rstate = ACTIVE;
		}
	}

	if(rse & END_FR){
		/* END OF FRAME -- Make sure Rx was active */
		if(hp->rxcnt > 0){
			if((rse & CRC_ERR)||(hp->rstate > ACTIVE)||(hp->rxcnt < 10)){
				if((hp->rxcnt >= 10) && (rse & CRC_ERR))
					hp->crcerr++; /* Ignore noise */

				hp->rcp = hp->rcvbuf;
				hp->rxcnt = 0;
			} else {
				/* Here we have a valid frame */
				hp->rxcnt -= 2;  /* Toss 2 crc bytes */
				if((bp = alloc_mbuf(hp->rxcnt+sizeof(struct iface *)))
				 != NULL){
					bp->data += sizeof(struct iface *);
					bp->cnt = hp->rxcnt;
					memcpy(bp->data,hp->rcvbuf,hp->rxcnt);
					net_route(hp->iface,&bp);
					hp->rxframes++;
				}
				/* packet queued - get ready for next frame */
				hp->rcp = hp->rcvbuf;
				hp->rxcnt = 0;
			} /* end good frame queued */
		}  /* end check for active receive upon EOF */
		hp->rstate = ACTIVE;	/* and clear error status */
	} /* end EOF check */

	restore(i_state);
}

static void
b_txint(hp)
register struct pichan *hp;
{
	register uint16 cmd;
	char c;
	struct mbuf *bp;
	int i_state;

	cmd = CTL+hp->base;

	i_state = dirps();
	if(hp->tstate != DEFER && hp->tstate)
		hp->txints++;

	switch(hp->tstate){
	case CRCOUT:
		hp->tstate = FLAGOUT;
		tdelay(hp,hp->squeldelay);
		restore(i_state);
		return;
	case IDLE:
		/* Transmitter idle. Find a frame for transmission */
		if((bp = dequeue(&hp->sndq)) == NULL){
			/* Nothing to send - return to receive mode
			 * Tx OFF now - flag should have gone
			 */
			rts(hp,OFF);
			restore(i_state);
			return;
		}
		hp->txcnt = pullup(&bp,hp->sndbuf,hp->bufsiz);	
		free_p(&bp);	/* Truncate overly long packets */
		/* If a buffer to send, we drop thru here */
	case DEFER:	    /* we may have deferred prev xmit attempt */
		/* Check DCD - debounce it */
		/* See Intel Microcommunications Handbook, p2-308 */
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		if((xread_scc(hp,cmd,R0) & DCD) != 0){
			hp->tstate = DEFER;
			tdelay(hp,100);
			/* defer until DCD transition or timeout */
			xwrite_scc(hp,cmd,R15,CTSIE|DCDIE);
			restore(i_state);
			return;
		}
		/* p - persist calculation */
		if(inportb(hp->cardbase+TMR0) > hp->persist){
			inportb(hp->cardbase+TMR0); /* Discard MSB */
			hp->tstate = DEFER;
			tdelay(hp,hp->slotime);
			restore(i_state);
			return;
		}
		inportb(hp->cardbase+TMR0); /* Discard MSB */

		rts(hp,ON);   /* Transmitter on */
		hp->tstate = ST_TXDELAY;
		tdelay(hp,hp->txdelay);
		restore(i_state);
		return;

	case ACTIVE:
		/* Here we are actively sending a frame */
		if(hp->txcnt-- != 0){
			c = *hp->tcp++;
			/* next char is gone */
			xwrite_scc(hp,cmd,R8,c);
			/* stuffing a char satisfies Interrupt condition */
		} else {
			/* No more to send */
			if((xread_scc(hp,cmd,R0) & 0x40)){
				/* Did we underrun? */
				/* unexpected underrun */
				hp->tunders++;
				xwrite_scc(hp,cmd,R0,SEND_ABORT);
				hp->tstate = FLAGOUT;
				tdelay(hp,hp->squeldelay);
				restore(i_state);
				return;
			}
			hp->tstate = UNDERRUN; /* Now we expect to underrun */
			/* Send flags on underrun */
			if(hp->speed){ /* If externally clocked */
				xwrite_scc(hp,cmd,R10,CRCPS|NRZI);
			} else {
				xwrite_scc(hp,cmd,R10,CRCPS);
			}
			xwrite_scc(hp,cmd,R0,RES_Tx_P); /* reset Tx Int Pend */
		}
		restore(i_state);
		return;     /* back to wait for interrupt */
	} /* end switch */
	restore(i_state);
}

/* Pi SIO External/Status interrupts (for the B channel)
 * This can be caused by a receiver abort, or a Tx UNDERRUN/EOM.
 * Receiver automatically goes to Hunt on an abort.
 *
 * If the Tx Underrun interrupt hits, change state and
 * issue a reset command for it, and return.
 */
static void
b_exint(hp)
register struct pichan *hp;
{
	char st;
	register uint16 cmd;
	char c;
	struct mbuf *bp;
	int i_state;

	cmd = CTL+hp->base;
	hp->exints++;
	i_state = dirps();
	st = xread_scc(hp,cmd,R0);     /* Fetch status */
	/* reset external status latch */
	xwrite_scc(hp,cmd,R0,RES_EXT_INT);

	switch(hp->tstate){
	case ACTIVE:	/* Unexpected underrun */
		xwrite_scc(hp,cmd,R0,SEND_ABORT);
		hp->tstate = FLAGOUT;
		hp->tunders++;
		tdelay(hp,hp->squeldelay);
		restore(i_state);
		return;
	case UNDERRUN:
		hp->tstate = CRCOUT;
		restore(i_state);
		return;
	case FLAGOUT: 
		/* Find a frame for transmission */
		if((bp = dequeue(&hp->sndq)) == NULL){
			/* Nothing to send - return to receive mode
			 * Tx OFF now - flag should have gone
			 */
			rts(hp,OFF);
			hp->tstate = IDLE;
			restore(i_state);
			return;
		}
		hp->txcnt = pullup(&bp,hp->sndbuf,hp->bufsiz);
                free_p(&bp); /* Truncate overly long packets */
		hp->tcp = hp->sndbuf;

		/* Get next char to send */
		c = *hp->tcp++;
		hp->txcnt--;
		xwrite_scc(hp,cmd,R0,RES_Tx_CRC);/* reset for next frame */

		/* Send abort on underrun */
		if(hp->speed){ /* If externally clocked */
			xwrite_scc(hp,cmd,R10,CRCPS|NRZI|ABUNDER);
		} else {
			xwrite_scc(hp,cmd,R10,CRCPS|ABUNDER);
		}

		xwrite_scc(hp,cmd,R8,c);	/* First char out now */
		xwrite_scc(hp,cmd,R0,RES_EOM_L);/* Reset end of message latch */

		/* select transmit interrupts to enable */

		xwrite_scc(hp,cmd,R15,TxUIE);	/* allow Underrun int only */
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		xwrite_scc(hp,cmd,R1,TxINT_ENAB|EXT_INT_ENAB);  /* Tx/Extern ints on */

		hp->tstate = ACTIVE;	/* char going out now */
		restore(i_state);
		return;

	case DEFER:
		/* Check DCD - debounce it
		 * See Intel Microcommunications Handbook, p2-308
		 */
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		if((xread_scc(hp,cmd,R0) & DCD) != 0){
			hp->tstate = DEFER;
			tdelay(hp,100);
			/* defer until DCD transition or timeout */
			xwrite_scc(hp,cmd,R15,CTSIE|DCDIE);
			restore(i_state);
			return;
		}
		/* p - persist calculation */
		if(inportb(hp->cardbase+TMR0) > hp->persist){
			inportb(hp->cardbase+TMR0); /* Discard MSB */
			hp->tstate = DEFER;
			tdelay(hp,hp->slotime);
			restore(i_state);
			return;
		}
		inportb(hp->cardbase+TMR0); /* Discard MSB */

		rts(hp,ON);   /* Transmitter on */
		hp->tstate = ST_TXDELAY;
		tdelay(hp,hp->txdelay);
		restore(i_state);
		return;

	case ST_TXDELAY:

		/* Get next char to send */
		c = *hp->tcp++;
		hp->txcnt--;
		xwrite_scc(hp,cmd,R0,RES_Tx_CRC);/* reset for next frame */

		/* Send abort on underrun */
		if(hp->speed){ /* If externally clocked */
			xwrite_scc(hp,cmd,R10,CRCPS|NRZI|ABUNDER);
		} else {
			xwrite_scc(hp,cmd,R10,CRCPS|ABUNDER);
		}

		xwrite_scc(hp,cmd,R8,c);	/* First char out now */
		xwrite_scc(hp,cmd,R0,RES_EOM_L);/* Reset end of message latch */

		/* select transmit interrupts to enable */

		xwrite_scc(hp,cmd,R15,TxUIE);	/* allow Underrun int only */
		xwrite_scc(hp,cmd,R0,RES_EXT_INT);
		/* Tx/Extern ints on */
		xwrite_scc(hp,cmd,R1,TxINT_ENAB|EXT_INT_ENAB);

		hp->tstate = ACTIVE;	/* char going out now */
		restore(i_state);
		return;
	}

	/* Receive Mode only
	 * This triggers when hunt mode is entered, & since an ABORT
	 * automatically enters hunt mode, we use that to clean up
	 * any waiting garbage
	 */
	if((hp->rstate == ACTIVE) && (st & BRK_ABRT)){
		(void) xread_scc(hp,cmd,R8);
		(void) xread_scc(hp,cmd,R8);
		(void) xread_scc(hp,cmd,R8);
		hp->rcp = hp->rcvbuf;
		hp->rxcnt = 0;	      /* rewind on DCD transition */
	}
	restore(i_state);
}

/* SET Transmit or Receive Mode
 * Set RTS (request-to-send) to modem on Transmit
 */
static void
rts(hp,x)
register struct pichan *hp;
uint16 x;
{
	uint16 tc;
	long br;
	uint16 cmd;

	cmd = CTL+hp->base;

	/* Reprogram BRG and turn on transmitter to send flags */
	if(x == ON){	/* Turn Tx ON and Receive OFF */
		/* Exints off first to avoid abort int */
		xwrite_scc(hp,cmd,R15,0);
		xwrite_scc(hp,cmd,R3,Rx8);	/* Rx off */
		hp->rstate = IDLE;
		if(cmd & 2){ /* if channel a */
			/* Set up for TX dma */
			xwrite_scc(hp,cmd,R1,WT_FN_RDYFN|EXT_INT_ENAB);
		} else {
			xwrite_scc(hp,cmd,R1,0);	/* No interrupts */
		}
		if(hp->speed){			/* if internally clocked */
			br = hp->speed;		/* get desired speed */
			tc = (XTAL/br)-2;	/* calc 1X BRG divisor */
			xwrite_scc(hp,cmd,R12,tc&0xFF);     /* lower byte */
			xwrite_scc(hp,cmd,R13,(tc>>8)&0xFF);/* upper byte */
		}
		xwrite_scc(hp,cmd,R5,TxCRC_ENAB|RTS|TxENAB|Tx8|DTR);
		/* Transmitter now on */
	} else {	/* Tx OFF and Rx ON */
		hp->tstate = IDLE;
		xwrite_scc(hp,cmd,R5,Tx8|DTR); 	/*  TX off */

		if(hp->speed){		/* if internally clocked */
			/* Reprogram BRG for 32x clock for receive DPLL */
			/* BRG off, keep Pclk source */
			xwrite_scc(hp,cmd,R14,BRSRC);
			br = hp->speed;			/* get desired speed */
			/* calc 32X BRG divisor */
			tc = ((XTAL/32)/br)-2;
			xwrite_scc(hp,cmd,R12,tc&0xFF);	/* lower byte */
			xwrite_scc(hp,cmd,R13,(tc>>8)&0xFF);/* upper byte */
			/* SEARCH mode, BRG source */
			xwrite_scc(hp,cmd,R14,BRSRC|SEARCH);
			/* Enable the BRG */
			xwrite_scc(hp,cmd,R14,BRSRC|BRENABL);
		}
		/* Now, turn on the receiver and hunt for a flag */
		xwrite_scc(hp,cmd,R3,RxENABLE|RxCRC_ENAB|Rx8);
		hp->rstate = ACTIVE;		/* Normal state */

		if(cmd & 2){/* if channel a */
			setup_rx_dma(hp);
		} else {
			/* reset error bits */
		/*	xwrite_scc(hp,cmd,R0,ERR_RES); */
			/* reset buffer pointers */
			hp->rcp = hp->rcvbuf;
			hp->rxcnt = 0;
			xwrite_scc(hp,cmd,R1,(INT_ALL_Rx|EXT_INT_ENAB));
		}
		xwrite_scc(hp,cmd,R15,BRKIE);		/* allow ABORT int */
		/* Hold tx off long enough for other station to reply */
		hp->deftime = msclock() + hp->txdelay + 500;
	}
}

void
setup_rx_dma(hp)
register struct pichan *hp;
{
	int cmd;
	int dmaport;
	int i_state;

	cmd = hp->base+CTL;
	dmaport = DMABASE+2*hp->dmachan;

	i_state = dirps();
	if(!hp->rcvbuf){
		/* No rx buffer available */
		restore(i_state);
		return;
	}

	/* Get ready for RX DMA */
	xwrite_scc(hp,cmd,R1,WT_FN_RDYFN|WT_RDY_RT|INT_ERR_Rx|EXT_INT_ENAB);
	outportb(DMA_MASK, DMA_DISABLE|hp->dmachan); /* Disable DMA chan */
	/* Set DMA mode register to single transfers, incrementing address,
	 *	auto init, writes
	 */
	outportb(DMA_MODE,DMA_RX_MODE|hp->dmachan);

	outportb(Page_regs[hp->dmachan],hp->rcvphys >> 16);/* Store in  64K DMA page */
	outportb(DMA_RESETFF,0);	 /* reset byte pointer flipflop */
	/* Output buffer start (dest) address */
	outportb(dmaport,hp->rcvphys);
	outportb(dmaport,hp->rcvphys >> 8);
	/* output DMA maximum byte count */
	dmaport++;
	outportb(dmaport,hp->bufsiz - 1);
	outportb(dmaport, (hp->bufsiz - 1) >> 8);
	/* Unmask channel 1 (start DMA) */
	outportb(DMA_MASK, DMA_ENABLE|hp->dmachan); /* Enable DMA chan */

	/* If a packet is already coming in, this line is supposed
	 *	to mess up the crc to avoid receiving a partial packet
	 */
	xwrite_scc(hp,cmd,R0,RES_Rx_CRC);

	/* Enable RX dma */
	xwrite_scc(hp,cmd,R1,WT_RDY_ENAB|WT_FN_RDYFN|WT_RDY_RT|INT_ERR_Rx|EXT_INT_ENAB);

	restore(i_state);
}

void
setup_tx_dma(hp)
struct pichan *hp;
{
	int dmaport;
	uint16 length;
	int i_state;

	length = hp->txcnt - 1;
	dmaport = DMABASE + 2*hp->dmachan;
	i_state = dirps();

	outportb(DMA_MASK, DMA_DISABLE|hp->dmachan); /* Disable DMA chan */
	/* Set DMA mode register to single transfers, incrementing address,
	 *	no auto init, reads
 	 */
	outportb(DMA_MODE,DMA_TX_MODE|hp->dmachan);

	outportb(Page_regs[hp->dmachan],hp->sndphys >> 16); /* Store in 64K DMA page */
	outportb(DMA_RESETFF,0);	/* reset byte pointer flipflop */
	outportb(dmaport,hp->sndphys);	/* Output buffer start (source) address */
	outportb(dmaport,hp->sndphys >> 8);
	/* output byte count */
	dmaport++;
	outportb(dmaport,length);
	outportb(dmaport, length >> 8);

	restore(i_state);
}

/* Initialize pi controller parameters */
static int
scc_init(hp)
register struct pichan *hp;
{
	uint16 tc;
	long br;
	register uint16 cmd;
	int i_state;

	/* Initialize 8530 channel for SDLC operation */

	cmd = CTL+hp->base;
#ifdef	notdef
	printf("Pi: Initializing Channel %c - Base = %x\n",cmd&2?'A':'B',cmd&~CTL);
#endif
	i_state = dirps();

	switch(cmd & 2){
	case 2:
		xwrite_scc(hp,cmd,R9,CHRA);	/* Reset channel A */
		xwrite_scc(hp,cmd,R2,0xff); /* Initialize interrupt vector */
		break;
	case 0:
		xwrite_scc(hp,cmd,R9,CHRB);	/* Reset channel B */
		break;
	}

	/* Deselect all Rx and Tx interrupts */
	xwrite_scc(hp,cmd,R1,0);

	/* Turn off external interrupts (like CTS/CD) */
	xwrite_scc(hp,cmd,R15,0);

	/* X1 clock, SDLC mode */
	xwrite_scc(hp,cmd,R4,SDLC|X1CLK);

	/* Now some misc Tx/Rx parameters */
	/* CRC PRESET 1, NRZI Mode */
	if(hp->speed){
		xwrite_scc(hp,cmd,R10,CRCPS|NRZI);
		/* Tx Clk from BRG. Rcv Clk from DPLL, TRxC pin outputs DPLL */
		xwrite_scc(hp,cmd,R11,TCBR|RCDPLL|TRxCDP|TRxCOI);
	} else {
		xwrite_scc(hp,cmd,R10,CRCPS);
		/* Tx Clk from Trxcl. Rcv Clk from Rtxcl, TRxC pin is input */
		xwrite_scc(hp,cmd,R11,TCTRxCP);
	}

	/* Null out SDLC start address */
	xwrite_scc(hp,cmd,R6,0);

	/* SDLC flag */
	xwrite_scc(hp,cmd,R7,FLAG);

	/* Set up the Transmitter but don't enable it
	 *  DTR, 8 bit TX chars only - TX NOT ENABLED
	 */
	xwrite_scc(hp,cmd,R5,Tx8|DTR);

	/* Receiver - intial setup only - more later */
	xwrite_scc(hp,cmd,R3,Rx8); 	       /* 8 bits/char */

	/* Setting up BRG now - turn it off first */
	xwrite_scc(hp,cmd,R14,BRSRC);	     /* BRG off, but keep Pclk source */

	/* set the 32x time constant for the BRG in Receive mode */

	if(hp->speed){
		br = hp->speed; 	/* get desired speed */
		tc = ((XTAL/32)/br)-2;	/* calc 32X BRG divisor */
	} else {
		tc = 14;
	}

	xwrite_scc(hp,cmd,R12,tc&0xFF);      /* lower byte */
	xwrite_scc(hp,cmd,R13,(tc>>8)&0xFF); /* upper byte */

	/* Following subroutine sets up and ENABLES the receiver */
	rts(hp,OFF);		   /* TX OFF and RX ON */

	if(hp->speed){
		/* DPLL frm BRG, BRG src PCLK */
		xwrite_scc(hp,cmd,R14,BRSRC|SSBR);
	} else {
		/* DPLL frm rtxc,BRG src PCLK */
		xwrite_scc(hp,cmd,R14,BRSRC|SSRTxC);
	}
	xwrite_scc(hp,cmd,R14,BRSRC|SEARCH); /* SEARCH mode, keep BRG source */
	xwrite_scc(hp,cmd,R14,BRSRC|BRENABL);/* Enable the BRG */

	if(!(cmd & 2)) /* if channel b */
		xwrite_scc(hp,cmd,R1,(INT_ALL_Rx|EXT_INT_ENAB));

	xwrite_scc(hp,cmd,R15,BRKIE);	    /* ABORT int */

	/* Now, turn on the receiver and hunt for a flag */
	xwrite_scc(hp,cmd,R3,RxENABLE|RxCRC_ENAB|Rx8);

	restore(i_state);
	return 0;
}


/* Attach a PI interface to the system
 * argv[0]: hardware type, must be "pi"
 * argv[1]: I/O address, e.g., "0x300"
 * argv[2]: vector, e.g., "2"
 * argv[3]: dma channel (1..3)
 * argv[4]: mode, must be:
 *	    "ax25" (AX.25 UI frame format)
 * argv[5]: interface label, e.g., "pi0"
 * argv[6]: receiver packet buffer size in bytes
 * argv[7]: maximum transmission unit, bytes
 * argv[8]: channel A interface speed, e.g, "1200", 0 = ext. clock
 * argv[9]: channel B interface speed
 * argv[10]: First IP address, optional (defaults to Ip_addr);
 * argv[11]: Second IP address, optional (defaults to Ip_addr);
 */
int
pi_attach(argc,argv)
int argc;
char *argv[];
{
	register struct iface *if_pca,*if_pcb;
	struct pichan *hp;
	int dev;
	int n;
	char *cp;

	if(acc_delay == 0){	/* Only do this once */
		/* Adapt recovery time delay to processor speed */
		set_acc_delay();
	}
	/* Quick check to make sure args are good and mycall is set */
	if(setencap(NULL,argv[4]) == -1){
		printf("PI: Mode %s unknown for interface %s\n",
			argv[4],argv[5]);
		return -1;
	}
	if(if_lookup(argv[5]) != NULL){
		printf("PI: Interface %s already exists\n",argv[5]);
		return -1;
	}
	if(Mycall[0] == '\0'){
		printf("PI: Set mycall first\n");
		return -1;
	}
	/* Note: each card must have a unique address, IRQ and DMA */

	if(pinbr >= PIMAX){
		printf("PI: Maximum of %d PI cards supported\n",PIMAX);
		return -1;
	}
	dev = pinbr++;

	/* Initialize hardware-level control structure */
	Pi[dev].addr = htoi(argv[1]);
	Pi[dev].vec = atoi(argv[2]);
	if(strchr(argv[2],'c') != NULL)
		Pi[dev].chain = 1;
	else
		Pi[dev].chain = 0;

	/* Set up counter chip */
	/* 500 uS square wave */
	outportb(Pi[dev].addr+TMRCMD, SC0|LSB_MSB|MODE3);
	for(n=0; n<5;n++) /* satisfy access time restriction */
		;
	outportb(Pi[dev].addr+TMR0, 922 & 0xFF); 
	for(n=0; n<5;n++) /* satisfy access time restriction */
		;
	outportb(Pi[dev].addr+TMR0, 922 >> 8);
	for(n=0; n<5;n++) /* satisfy access time restriction */
		;

	/* Save original interrupt vector */
	Pi[dev].oldvec = getirq(Pi[dev].vec);

	/* Set new interrupt vector */
	if(setirq(Pi[dev].vec,pihandle[dev]) == -1){
		printf("PI: IRQ %u out of range\n",Pi[dev].vec);
		pinbr--;
		return -1;
	}

	if((atoi(argv[3]) < 1) || (atoi(argv[3]) > 3)){
		printf("PI: DMA %d out of range\n",atoi(argv[3]));
		pinbr--;
		return -1;
	}

	/* set params in pichan table for CHANNEL B */
	hp = &Pichan[2*dev+1];			/* pi1 is offset 1 */
	hp->dmachan = 0; /* Channel B does not have dma */
	hp->cardbase = Pi[dev].addr;
	hp->stata = Pi[dev].addr + CHANA + CTL;	/* permanent status */
	hp->statb = Pi[dev].addr + CHANB + CTL;	/* addrs for CHANA/B*/
	hp->speed = (uint16)atoi(argv[9]);
	hp->base = Pi[dev].addr + CHANB;
	hp->bufsiz = atoi(argv[6]);
	hp->tstate = IDLE;
	/* default channel access Params */
	hp->txdelay = 30;		/* 300 Ms */
	hp->persist = 128;		/* 50% persistence */
	hp->slotime = 30;		/* 300 Ms */
	hp->squeldelay = 3;		/* 30 Ms */

	xwrite_scc(hp,CTL+hp->stata,R9,FHWRES); 	/* Hardware reset */
						/* one time only */
	/* Disable interrupts with Master interrupt ctrl reg */
	xwrite_scc(hp,CTL+hp->stata,R9,0);

	scc_init(hp);

	/* Pre-allocate a receive DMA buffer */
	hp->rcvbuf = dma_malloc(&hp->rcvphys,hp->bufsiz);
	if(hp->rcvbuf == NULL){
		/* No memory, abort receiver */
		printf("PI: No memory available for receive buffer\n");
		/* Restore original interrupt vector */
		setirq(Pi[dev].vec,Pi[dev].oldvec);
		pinbr--;
		return -1;
	}
	/* Pre-allocate a transmit DMA buffer */
	hp->sndbuf = dma_malloc(&hp->sndphys,hp->bufsiz);
	if(hp->sndbuf == NULL){
		/* No memory, abort */
		printf("PI: No memory available for transmit buffer\n");
		/* Restore original interrupt vector */
		setirq(Pi[dev].vec,Pi[dev].oldvec);
		pinbr--;
		free(hp->rcvbuf);
		return -1;
	}
	hp->sndq = NULL;

	/* set params in pichan table for CHANNEL A */
	hp = &Pichan[2*dev];	/* pi0a is offset 0 */
	hp->dmachan = (unsigned char)atoi(argv[3]);
	/* Verify dma channel */
	if(hp->dmachan >= 8){
		printf("PI: DMA channel %d out of range\n",hp->dmachan);
		free(hp->rcvbuf);
		free(hp->sndbuf);
		/* Restore original interrupt vector */
		setirq(Pi[dev].vec,Pi[dev].oldvec);
		pinbr--;
		return -1;
	}
	hp->cardbase = Pi[dev].addr;
	hp->speed = (uint16)atoi(argv[8]);
	hp->base = Pi[dev].addr + CHANA;
	hp->bufsiz = atoi(argv[6]);
	hp->tstate = IDLE;
	/* default channel access Params */
	hp->txdelay = 15;		/* 15 mS */
	hp->persist = 128;		/* 50% persistence */
	hp->slotime = 15;		/* 15 mS */
	hp->squeldelay = 1;		/* 1 mS */

	/* Pre-allocate a receive buffer */
	hp->rcvbuf = dma_malloc(&hp->rcvphys,hp->bufsiz);
	if(hp->rcvbuf == NULL){
		/* No memory, abort receiver */
		printf("PI: No memory available for receive buffers\n");
		/* Restore original interrupt vector */
		setirq(Pi[dev].vec,Pi[dev].oldvec);
		pinbr--;
		return -1;
	}
	hp->sndq = NULL;

	/* Get a buffer for tx which does not cross a dma boundary */
	hp->sndbuf = dma_malloc(&hp->sndphys,hp->bufsiz);
	if(hp->sndbuf == NULL){
		printf("PI: No memory available for transmit buffer");
		/* Restore original interrupt vector */
		setirq(Pi[dev].vec,Pi[dev].oldvec);
		pinbr--;
		free(hp->rcvbuf);
		return -1;
	}
	/* Create interface structures and fill in details */
	if_pca = (struct iface *)callocw(1,sizeof(struct iface));
	if_pcb = (struct iface *)callocw(1,sizeof(struct iface));

	if_pca->addr = if_pcb->addr = Ip_addr;
	if(argc > 10)
		if_pca->addr = resolve(argv[10]);
	if(argc > 11)
		if_pcb->addr = resolve(argv[11]);

	if(if_pca->addr == 0 || if_pcb->addr == 0)
		printf("PI: No IP address!\n");

	/* Append "a" to interface associated with A channel */
	if_pca->name = malloc((unsigned)strlen(argv[5])+2);
	strcpy(if_pca->name,argv[5]);
	strcat(if_pca->name,"a");
	/* Append "b" to interface associated with B channel */
	if_pcb->name = malloc((unsigned)strlen(argv[5])+2);
	strcpy(if_pcb->name,argv[5]);
	strcat(if_pcb->name,"b");

	if_pcb->mtu = if_pca->mtu = atoi(argv[7]);
	if_pcb->ioctl = if_pca->ioctl = pi_ctl;
	if_pca->dev = 2*dev;			/* pi0a */
	if_pcb->dev = 2*dev + 1;		/* pi0b */
	if_pcb->stop = if_pca->stop = pi_stop;
	if_pcb->raw = if_pca->raw = pi_raw;

	/* Must succeed, was checked at top */
	setencap(if_pca,argv[4]);
	setencap(if_pcb,argv[4]);

	if(if_pcb->hwaddr == NULL)
		if_pcb->hwaddr = mallocw(AXALEN);
	memcpy(if_pcb->hwaddr,Mycall,AXALEN);
	if(if_pca->hwaddr == NULL)
		if_pca->hwaddr = mallocw(AXALEN);
	memcpy(if_pca->hwaddr,Mycall,AXALEN);

        Pichan[2*dev].iface = if_pca;
        Pichan[2*dev+1].iface = if_pcb;

	/* Link em in to the interface chain */
	if_pca->next = if_pcb;
	if_pcb->next = Ifaces;
	Ifaces = if_pca;

	scc_init(hp);
	/* master interrupt enable */
	xwrite_scc(hp,CTL+hp->base,R9,MIE|NV);

	/* Enable interrupt */
	maskon(Pi[dev].vec);

	cp = if_name(if_pca," tx");
	if_pca->txproc = newproc(cp,512,if_tx,0,if_pca,NULL,0);
	free(cp);
	cp = if_name(if_pcb," tx");
	if_pcb->txproc = newproc(cp,512,if_tx,0,if_pcb,NULL,0);
	free(cp);
	return 0;
}

/* Shut down interface */
int
pi_stop(iface)
struct iface *iface;
{
	uint16 dev;
	struct pichan *hp;

	dev = iface->dev;
	if(dev & 1) /* Because there are 2 devices per card */
		return 0;
	dev >>= 1;		/* Convert back into pi number */
	hp = &Pichan[2*dev];	/* pi0a is offset 0 */

	outportb(DMA_MASK, DMA_DISABLE|hp->dmachan); /* Disable DMA channel */

	/* Turn off interrupts */
	maskoff(Pi[dev].vec);

	/* Restore original interrupt vector */
	setirq(Pi[dev].vec,Pi[dev].oldvec);

	/* Force hardware reset */
	xwrite_scc(&Pichan[2*dev],CTL+Pi[dev].addr + CHANA,R9,FHWRES);

	return 0;
}

/* Send raw packet on pi card */
int
pi_raw(
struct iface *iface,
struct mbuf **bpp
){
	char kickflag;
	struct pichan *hp;
	int i_state;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();

	hp = &Pichan[iface->dev];
	kickflag = (hp->sndq == NULL) & (hp->sndbuf == NULL);
	enqueue(&hp->sndq,bpp);
	hp->enqueued++;
	if(kickflag){
		/* simulate interrupt to xmit */
		switch(hp->base & 2){
		case 2:
			a_txint(hp);		/* process interrupt */
			break;
		case 0:
			i_state = dirps();
			if(hp->tstate == IDLE)
				b_txint(hp);
			restore(i_state);
			break;
		}
	}
	return 0;
}

/* display PI Channel stats */
int
dopistat()
{
	struct pichan *hp;
	int i;

	printf("PI Board Statistics:\n\n");
	printf("Base Addr  Rxints  Txints  Exints  TxFrms  RxFrms  Crcerr  RxOvrs  TxUndr \n");
	printf("---------  ------  ------  ------  ------  ------  ------  ------  ------ \n");
	for(i=0; i<pinbr*2; i++){
		hp = &Pichan[i];

		printf("0x%03x    % 8lu% 8lu% 8lu% 8u% 8u% 8u% 8u% 8u\nRcv State=%s ",
		 hp->base, hp->rxints, hp->txints, hp->exints, hp->enqueued,
		 hp->rxframes, hp->crcerr, hp->rovers, hp->tunders,
		 hp->rstate==0 ?
		  "IDLE" : hp->rstate==1 ?
		   "ACTIVE" : hp->rstate==2 ?
		    "RXERROR" : hp->rstate==3 ?
		     "RXABORT":"TOOBIG"
		);

		printf("Tstate = %s\n",
		 hp->tstate == 0 ?
		  "IDLE" : hp->tstate == 1 ?
		   "ACTIVE" : hp->tstate == 2 ?
		    "UNDERRUN" : hp->tstate == 3 ?
		     "FLAGOUT" : hp->tstate == 4 ?
		      "DEFER" : hp->tstate == 5 ?
		       "TXDELAY" : "CRCOUT"
		);
	}
	return 0;
}

/* Subroutine to set kiss params in channel tables */
int32
pi_ctl(iface,cmd,set,val)
struct iface *iface;
int cmd;
int set;
int32 val;
{
	struct pichan *hp;
	int32 t,ca;

	hp = &Pichan[iface->dev]; /* point to channel table */
	switch(cmd){
	case PARAM_TXDELAY:
		if(set)
			hp->txdelay = val;
		return hp->txdelay;
	case PARAM_PERSIST:
		if(set)
			hp->persist = val;
		return hp->persist;
	case PARAM_SLOTTIME:
		if(set)
			hp->slotime = val;
		return hp->slotime;
	case PARAM_TXTAIL:
		if(set)
			hp->squeldelay = val;
		return hp->squeldelay;
	case PARAM_MUTE:
		if(set){
			if(val == -1){
				/* Special case for duration of a CTS */
				val = hp->txdelay + 500;
			}
			hp->deftime = msclock() + val;
		}
		t = msclock();
		ca = hp->deftime - t;
		if(ca < 0){
			hp->deftime = t;
			ca = 0;
		}
		return ca;
	}
	return -1;
}
