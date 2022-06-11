/*
 * Version with Stopwatches
 *
 * 0 - Not used
 * 1 - rx_fsm run time
 * 2 - drtx_active run time (per character tx time)
 * 
 * Interface driver for the DRSI board for KA9Q's TCP/IP on an IBM-PC ONLY!
 *
 * Derived from a driver written by Art Goldman, WA3CVG
 * (c) Copyright 1987 All Rights Reserved
 * Permission for non-commercial use is hereby granted provided this notice
 * is retained.  For info call: (301) 997-3838.
 *
 * Heavily re-written from the original,  a driver for the EAGLE board into
 * a driver for the DRSI PC* Packet adpator. Copyright as original, all
 * amendments likewise providing credit given and notice retained.
 * Stu Phillips - N6TTO, W6/G8HQA (yes Virginia,  really !).
 * For info call: (408) 285-4142
 *
 * This driver supports 1 (one) DRSI board.
 * 
 * Reformatted and added ANSI-style declarations, integrated into NOS
 * by KA9Q, 10/14/89
 *
 * Latest set of defect fixes added 1/2/90 by N6TTO
 * 1. Made P-PERSIST work properly
 * 2. Fixed UNDERRUN bug when in DEFER state
 * 3. Tx now defers correctly when DCD is high (!)
 *
 * Changed 3/4/90 by N6TTO
 * Changed method of enabling the IRQ to the 8259 to call maskon()
 * instead of clrbit(); change made to allow interrupts > 8 to work
 * on an AT.
 *
 * Changed 11/14/90 by N6TTO
 * Fixed incompatiblity between current NOS memory allocation scheme
 * and changes made to speed up drsi transmit state machine.
 *
 */

#include <stdio.h>
#include <dos.h>
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "drsi.h"
#include "ax25.h"
#include "trace.h"
#include "nospc.h"
#include "z8530.h"
#include "devparam.h"

static int32 dr_ctl(struct iface *iface,int cmd,int set,int32 val);
static int dr_raw(struct iface *iface,struct mbuf **bpp);
static int dr_stop(struct iface *iface);
static void dr_wake(struct drchan *hp,int rx_or_tx,
	void (*routine)(struct drchan *),int ticks);
static int drchanparam(struct drchan *hp);
static void drexint(struct drchan *hp);
static void drinitctc(unsigned port);
static void drrx_active(struct drchan *hp);
static void drrx_enable(struct drchan *hp);
static void drtx_active(struct drchan *hp);
static void drtx_defer(struct drchan *hp);
static void drtx_downtx(struct drchan *hp);
static void drtx_flagout(struct drchan *hp);
static void drtx_idle(struct drchan *hp);
static void drtx_rrts(struct drchan *hp);
static void drtx_tfirst(struct drchan *hp);
static char read_ctc(unsigned port,unsigned reg);
static void rx_fsm(struct drchan *hp);
static void tx_fsm(struct drchan *hp);
static void write_ctc(uint16 port,uint8 reg,uint8 val);

struct DRTAB Drsi[DRMAX];	/* Device table - one entry per card */
INTERRUPT (*Drhandle[])(void) = { dr0vec };  /* handler interrupt vector table */
struct drchan Drchan[2*DRMAX];	 /* channel table - 2 entries per card */
uint16 Drnbr;

/* Set specified routine to be 'woken' up after specified number
 * of ticks (allows CPU to be freed up and reminders posted);
 */
static void
dr_wake(hp, rx_or_tx, routine, ticks)
struct drchan *hp;
int rx_or_tx;
void (*routine)(struct drchan *);
int ticks;
{
	hp->w[rx_or_tx].wcall = routine;
	hp->w[rx_or_tx].wakecnt = ticks;
}

/* Master interrupt handler.  One interrupt at a time is handled.
 * here. Service routines are called from here.
 */
INTERRUPT (far *(drint)(dev))()
int dev;
{
	register char st;
	register uint16 pcbase, i;
	struct drchan *hpa,*hpb;
	struct DRTAB *dp;

	dp = &Drsi[dev];
	dp->ints++;
	pcbase = dp->addr;
	hpa = &Drchan[2 * dev];
	hpb = &Drchan[(2 * dev)+1];

yuk:
	/* Check CTC for timer interrupt */
	st = read_ctc(pcbase, Z8536_CSR3);
	if(st & Z_IP){
		/* Reset interrupt pending */
		write_ctc(pcbase, Z8536_CSR3, Z_CIP|Z_GCB);
		for(i=0;i<=1;i++){
			if(hpa->w[i].wakecnt){
				if(--hpa->w[i].wakecnt == 0){
					(hpa->w[i].wcall)(hpa);
				}
			}
			if(hpb->w[i].wakecnt){
				if(--hpb->w[i].wakecnt == 0){
					(hpb->w[i].wcall)(hpb);
				}
			}
		}
	}
	/* Check the SIO for interrupts */

	/* Read interrupt status register from channel A */
	while((st = read_scc(pcbase+CHANA+CTL,R3)) != 0){
		/* Use IFs to process ALL interrupts pending
		 * because we need to check all interrupt conditions
		 */
		if(st & CHARxIP){
			/* Channel A Rcv Interrupt Pending */
			rx_fsm(hpa);
		}
		if(st & CHBRxIP){
			/* Channel B Rcv Interrupt Pending */
			rx_fsm(hpb);
		}
		if(st & CHATxIP){
			/* Channel A Transmit Int Pending */
			tx_fsm(hpa);
		}
		if(st & CHBTxIP){
			/* Channel B Transmit Int Pending */
			tx_fsm(hpb);
		}
		if(st & CHAEXT){
			/* Channel A External Status Int */
			drexint(hpa);
		}
		if(st & CHBEXT){
			/* Channel B External Status Int */
			drexint(hpb);
		}
		/* Reset highest interrupt under service */
		write_scc(hpa->base+CTL,R0,RES_H_IUS);

	} /* End of while loop on int processing */
	if(read_ctc(pcbase, Z8536_CSR3) & Z_IP)
		goto yuk;
	return dp->chain ? dp->oldvec : NULL;
}


/* DRSI SIO External/Status interrupts
 * This can be caused by a receiver abort, or a Tx UNDERRUN/EOM.
 * Receiver automatically goes to Hunt on an abort.
 *
 * If the Tx Underrun interrupt hits, change state and
 * issue a reset command for it, and return.
 */
static void
drexint(hp)
register struct drchan *hp;
{
	register int base = hp->base;
	char st;
	int i_state;

	i_state = dirps();
	hp->exints++;

	st = read_scc(base+CTL,R0);     /* Fetch status */

	/* Check for Tx UNDERRUN/EOM - only in Transmit Mode */
        /* Note that the TxEOM bit remains set once we go    */
	/* back to receive.  The following qualifications    */
	/* are necessary to prevent an aborted frame causing */
	/* a queued transmit frame to be tossed when in      */
	/* DEFER state on transmit.			     */
	if((hp->tstate != DEFER) && (hp->rstate==0) && (st & TxEOM)){
		if(hp->tstate != UNDERRUN){
			/* This is an unexpected underrun.  Discard the current
			 * frame (there's no way to rewind),  kill the transmitter
			 * and return to receive with a wakeup posted to get the
			 * next (if any) frame.  Any recovery will have to be done
			 * by higher level protocols (yuk).
			 */
			write_scc(base, R5, Tx8|DTR);	/* Tx off now */
			write_scc(base, R1, 0);		/* Prevent ext.status int */
			write_scc(base, R0, RES_Tx_P);  /* Reset Tx int pending */
			write_scc(base, R0, ERR_RES);
			write_scc(base, R0, RES_EOM_L); /* Reset underrun latch */
			free_p(&hp->sndbuf);
			hp->tstate = IDLE;
			hp->tx_state = drtx_idle;
			dr_wake(hp, TX, tx_fsm, hp->slotime);
			hp->rstate = ENABLE;
			hp->rx_state = drrx_enable;
			drrx_enable(hp);
		}
	}
	/* Receive Mode only
	 * This triggers when hunt mode is entered, & since an ABORT
	 * automatically enters hunt mode, we use that to clean up
	 * any waiting garbage
	 */
	if((hp->rstate != IDLE) && (st & BRK_ABRT)){
		if(hp->rcvbuf != NULL){
			hp->rcp = hp->rcvbuf->data;
			hp->rcvbuf->cnt = 0;
		}
		while(read_scc(base,R0) & Rx_CH_AV)
			(void) inportb(base+DATA);
		hp->aborts++;
		hp->rstate = ACTIVE;
		write_scc(base, R0, ERR_RES);
	}
	/* reset external status latch */
	write_scc(base,R0,RES_EXT_INT);
	restore(i_state);
}

/* Receive Finite State Machine - dispatcher */
static void
rx_fsm(hp)
struct drchan *hp;
{
	int i_state;

	i_state = dirps();
	hp->rxints++;
	(*hp->rx_state)(hp);
	restore(i_state);
}

/* drrx_enable
 * Receive ENABLE state processor
 */
static void
drrx_enable(hp)
struct drchan *hp;
{
	register uint16 base = hp->base;

	write_scc(base, R1, INT_ALL_Rx|EXT_INT_ENAB);
	write_scc(base, R15, BRKIE);	/* Allow ABORT Int */
	write_scc(base, R14, BRSRC|BRENABL|SEARCH);
	/* Turn on rx and enter hunt mode */
	write_scc(base, R3, ENT_HM|RxENABLE|RxCRC_ENAB|Rx8);

	if(hp->rcvbuf != NULL){
		hp->rcvbuf->cnt = 0;
		hp->rcp = hp->rcvbuf->data;
	}
	hp->rstate = ACTIVE;
	hp->rx_state = drrx_active;
}

/* drrx_active
 * Receive ACTIVE state processor
 */
static void
drrx_active(hp)
struct drchan *hp;
{
	register uint16 base = hp->base;
	unsigned char rse,st;
	struct mbuf *bp;

	/* Allocate a receive buffer if not already present */
	if(hp->rcvbuf == NULL){
		bp = hp->rcvbuf = alloc_mbuf(hp->bufsiz);
		if(bp == NULL){
			/* No buffer - abort the receiver */
			write_scc(base, R3, ENT_HM|RxENABLE|RxCRC_ENAB|Rx8);
			/* Clear character from rx buffer in SIO */
			(void) inportb(base+DATA);
			return;
		}
		hp->rcvbuf->cnt = 0; 
		hp->rcp = hp->rcvbuf->data;
	}

	st = read_scc(base, R0); /* get interrupt status from R0 */
	rse = read_scc(base,R1); /* get special status from R1 */

	if(st & Rx_CH_AV){
		/* there is a char to be stored
		 * read special condition bits before reading the data char
		 * (already read above)
		 */
		if(rse & Rx_OVR){
			/* Rx overrun - toss buffer */
			hp->rcp = hp->rcvbuf->data;	/* reset buffer pointers */
			hp->rcvbuf->cnt = 0;
			hp->rstate = RXERROR;	/* set error flag */
			hp->rovers++;		/* count overruns */
		} else if(hp->rcvbuf->cnt >= hp->bufsiz){
			/* Too large -- toss buffer */
			hp->toobig++;
			hp->rcp = hp->rcvbuf->data;	/* reset buffer pointers */
			hp->rcvbuf->cnt = 0;
			hp->rstate = TOOBIG;	/* when set, chars are not stored */
		}
		/* ok, we can store the received character now */
		if((hp->rstate == ACTIVE) && ((st & BRK_ABRT) == 0)){
			*hp->rcp++ = inportb(base+DATA); /* char to rcv buff */
			hp->rcvbuf->cnt++;		 /* bump count */
		} else {
			/* got to empty FIFO */
			(void) inportb(base+DATA);
			hp->rcp = hp->rcvbuf->data;	/* reset buffer pointers */
			hp->rcvbuf->cnt = 0;
			hp->rstate = RXABORT;
			write_scc(base,R0,ERR_RES);	/* reset err latch */
		}
	}
	/* The End of Frame bit is ALWAYS associated with a character,
	 * usually, it is the last CRC char.  Only when EOF is true can
	 * we look at the CRC byte to see if we have a valid frame
	 */
	if(rse & END_FR){
		hp->rxframes++;
		/* END OF FRAME -- Make sure Rx was active */
		if(hp->rcvbuf->cnt > 0){	/* any data to store */
			/* looks like a frame was received
			 * now is the only time we can check for CRC error
			 */
			if((rse & CRC_ERR) || (hp->rstate > ACTIVE) ||
			 (hp->rcvbuf->cnt < 10) || (st & BRK_ABRT)){
				/* error occurred; toss frame */
				if(rse & CRC_ERR)
					hp->crcerr++;	/* count CRC errs */
				hp->rcp = hp->rcvbuf->data;
				hp->rcvbuf->cnt = 0;
				hp->rstate = ACTIVE;   /* Clear error state */
			} else {
				/* Here we have a valid frame */
				hp->rcvbuf->cnt -= 2;    /* chuck FCS bytes */
				    /* queue it in */
				net_route(hp->iface,&hp->rcvbuf);
				hp->enqueued++;
				/* packet queued - reset buffer pointer */
				hp->rcvbuf = NULL;
			} /* end good frame queued */
		}  /* end check for active receive upon EOF */
	}
}

/*
 * TX finite state machine - dispatcher
 */
static void
tx_fsm(hp)
struct drchan *hp;
{
	int i_state;

	i_state = dirps();
	if(hp->tstate != DEFER && hp->tstate)
		hp->txints++;
	(*hp->tx_state)(hp);
	restore(i_state);
}

/* drtx_idle
 * Transmit IDLE transmit state processor
 */
static void
drtx_idle(hp)
struct drchan *hp;
{
	register uint16 base;

	/* Tx idle - is there a frame to transmit ? */
	if((hp->sndbuf = dequeue(&hp->sndq)) == NULL){
		/* Nothing to send - return to receive mode
		 * Turn Tx off - any trailing flag should have been sent
		 * by now
		 */
#ifdef DRSIDEBUG
		printf("Nothing to TX\n");
#endif
		base = hp->base;
		write_scc(base, R5, Tx8|DTR);   /* Tx off now */
		write_scc(base, R0, ERR_RES);	/* Reset error bits */

		/* Delay for squelch tail before enabling receiver */
		hp->rstate = ENABLE;
		hp->rx_state = drrx_enable;
		dr_wake(hp, RX, rx_fsm, hp->squeldelay);
	} else {
		/* Frame to transmit */
		hp->tstate = DEFER;
		hp->tx_state = drtx_defer;
		drtx_defer(hp);
	}
}

/* drtx_defer
 * Transmit DEFER state processor
 */
static void
drtx_defer(hp)
struct drchan *hp;
{
	register uint16 base = hp->base;

	/* We may have defered a previous tx attempt - in any event...
	 * Check DCD in case someone is already transmitting
	 * then check to see if we should defer due to P-PERSIST.
	 */

#ifdef DRSIDEBUG
	printf("drtx_defer - checking for DCD\n");
#endif
	if((read_scc(base+CTL, R0) & DCD) > 0){
		/* Carrier detected - defer */
		hp->txdefers++;
		dr_wake(hp, TX, tx_fsm, 10);	/* Defer for 100 mS */
#ifdef DRSIDEBUG
		printf("drtx_defer - TX deferred\n");
#endif
		return;
	}

#ifdef DRSIDEBUG
	printf("drtx_defer - checking for P-PERSIST backoff\n");
#endif
	/* P-PERSIST is checked against channel 3 of the 8536 which is
	 * the free running counter for the 10 mS tick; The counter
	 * goes through 0x6000 ticks per 10 mS or one tick every
	 * 407 nS - this is pretty random compared to the DOS time of
	 * day clock (0x40:0x6C) used by the other (EAGLE) drivers.
	 */
        if (hp->persist <= read_ctc(base,Z8536_CC3LSB)) {
#ifdef DRSIDEBUG
	    printf("drtx_defer - BACKOFF\n");
#endif
	    hp->txppersist++;
	    dr_wake (hp, TX, tx_fsm, hp->slotime);
	    return;
	}
	/* No backoff - set RTS and start to transmit frame */
	write_scc(base, R1, 0);		/* Prevent external status int */
	write_scc(base, R3, Rx8);	/* Turn Rx off */
	hp->rstate = IDLE;		/* Mark Rx as idle */
	hp->tstate = RRTS;
	hp->tx_state = drtx_rrts;
#ifdef DRSIDEBUG
	printf("drtx_defer - wake posted for drtx_rrts\n");
#endif
	write_scc(base, R9, 0);		/* Interrupts off */
	write_scc(base,R5,RTS|Tx8|DTR);	/* Turn tx on */
	dr_wake(hp, TX, tx_fsm, 10);
}

/* drtx_rrts
 * Transmit RRTS state processor
 */
static void
drtx_rrts(hp)
struct drchan *hp;
{
	register uint16 base = hp->base;

	write_scc(base, R9, 0);	/* Interrupts off */
	write_scc(base,R5,TxCRC_ENAB|RTS|TxENAB|Tx8|DTR);	/* Tx now on */
	hp->tstate = TFIRST;
	hp->tx_state = drtx_tfirst;
#ifdef DRSIDEBUG
	printf("8530 Int status %x\n", read_scc(base+CHANA,R3)); 
	printf("drtx_rrts - Wake posted for TXDELAY\n");
#endif
	dr_wake(hp, TX, tx_fsm, hp->txdelay);
}
    
/* drtx_tfirst
 * Transmit TFIRST state processor
 */
static void
drtx_tfirst(hp)
struct drchan *hp;
{
	register uint16 base = hp->base;
	char c;
	
	/* Copy data to a local buffer to save on mbuf overheads
	 * during transmit interrupt time.
	 */
	hp->drtx_cnt = len_p(hp->sndbuf);
	hp->drtx_tcp = hp->drtx_buffer;
	
	pullup(&hp->sndbuf, hp->drtx_tcp, hp->drtx_cnt);
	
	/* Transmit the first character in the buffer */
	c = *hp->drtx_tcp++;
	hp->drtx_cnt--;

	write_scc(base, R0, RES_Tx_CRC);	/* Reset CRC */
	write_scc(base, R0, RES_EOM_L);		/* Reset underrun latch */
	outportb(base+DATA, c);			/* Output first character */
	write_scc(base, R15, TxUIE);		/* Allow underrun ints only */
	write_scc(base, R1, TxINT_ENAB|EXT_INT_ENAB); /* Tx/Ext status ints on */
	write_scc(base, R9, MIE|NV);		/* master enable */
	hp->tstate = ACTIVE;
	hp->tx_state = drtx_active;
}

/* drtx_active
 * Transmit ACTIVE state processor
 */
static void
drtx_active(hp)
struct drchan *hp;
{
	if(hp->drtx_cnt-- > 0){
		/* Send next character */
		outportb(hp->base+DATA, *hp->drtx_tcp++);
	} else {
		/* No more to send - wait for underrun to hit */
		hp->tstate = UNDERRUN;
		hp->tx_state = drtx_flagout;
		free_p(&hp->sndbuf);
		write_scc(hp->base, R0, RES_EOM_L);  /* Send CRC on underrun */
		write_scc(hp->base, R0, RES_Tx_P);   /* Reset Tx Int pending */
	}
}

/* drtx_flagout
 * Transmit FLAGOUT state processor
 */
static void
drtx_flagout(hp)
struct drchan *hp;
{
	/* Arrive here after CRC sent and Tx interrupt fires.
	 * Post a wake for ENDDELAY
	 */

	hp->tstate = UNDERRUN;
	hp->tx_state = drtx_downtx;
	write_scc(hp->base, R9, 0);
	write_scc(hp->base, R0,  RES_Tx_P);
	dr_wake(hp, TX, tx_fsm, hp->enddelay);
}

/* drtx_downtx
 * Transmit DOWNTX state processor
 */
static void
drtx_downtx(hp)
struct drchan *hp;
{
	register int base = hp->base;

	/* See if theres anything left to send - if there is,  send it ! */
	if((hp->sndbuf = dequeue(&hp->sndq)) == NULL){
		/* Nothing left to send - return to receive */
		write_scc(base, R5, Tx8|DTR);   /* Tx off now */
		write_scc(base, R0, ERR_RES);   /* Reset error bits */
		hp->tstate = IDLE;
		hp->tx_state = drtx_idle;
		hp->rstate = ENABLE;
		hp->rx_state = drrx_enable;
		drrx_enable(hp);
	} else
		drtx_tfirst(hp);

}
    
/* Write CTC register */
static void
write_ctc(base, reg, val)
uint16 base;
uint8 reg,val;
{
	int i_state;
	
	i_state = dirps();
	/* Select register */
	outportb(base+Z8536_MASTER,reg);
	outportb(base+Z8536_MASTER,val);
	restore(i_state);
}

/* Read CTC register */
static char
read_ctc(base, reg)
uint16 base;
uint8 reg;
{
	uint8 c;
	uint16 i;
	int i_state;
	
	i_state = dirps();
	/* Select register */
        outportb(base+Z8536_MASTER,reg);
	/* Delay for a short time to allow 8536 to settle */
	for(i=0;i<100;i++)
		;
	c = inportb(base+Z8536_MASTER);
	restore(i_state);
	return(c);
}

/* Initialize dr controller parameters */
static int
drchanparam(hp)
register struct drchan *hp;
{
	uint16 tc;
	long br;
	register uint16 base;
	int i_state;

	/* Initialize 8530 channel for SDLC operation */
	base = hp->base;
	i_state = dirps();

	switch(base & 2){
	case 2:
		write_scc(base,R9,CHRA);	/* Reset channel A */
		break;
	case 0:
		write_scc(base,R9,CHRB);	/* Reset channel B */
		break;
	}
	/* Deselect all Rx and Tx interrupts */
	write_scc(base,R1,0);

	/* Turn off external interrupts (like CTS/CD) */
	write_scc(base,R15,0);

	/* X1 clock, SDLC mode */
	write_scc(base,R4,SDLC|X1CLK);	 /* SDLC mode and X1 clock */

	/* Now some misc Tx/Rx parameters */
	/* CRC PRESET 1, NRZI Mode */
	write_scc(base,R10,CRCPS|NRZI);

	/* Set up BRG and DPLL multiplexers */
	/* Tx Clk from RTxC. Rcv Clk from DPLL, TRxC pin outputs BRG */
	write_scc(base,R11,RCDPLL|TCRTxCP|TRxCOI|TRxCBR);

	/* Null out SDLC start address */
	write_scc(base,R6,0);

	/* SDLC flag */
	write_scc(base,R7,FLAG);

	/* Set up the Transmitter but don't enable it */
	/*  DTR, 8 bit TX chars only - TX NOT ENABLED */
	write_scc(base,R5,Tx8|DTR);

	/* Receiver - initial setup only - more later */
	write_scc(base,R3,Rx8);		 /* 8 bits/char */

	/* Setting up BRG now - turn it off first */
	write_scc(base,R14,BRSRC);     /* BRG off, but keep Pclk source */

	/* set the 32x time constant for the BRG */

	br = hp->speed;			/* get desired speed */
	tc = ((XTAL/32)/br)-2;		/* calc 32X BRG divisor */

	write_scc(base,R12,tc);      /* lower byte */
	write_scc(base,R13,(tc>>8)); /* upper bite */

	/* Time to set up clock control register for RECEIVE mode
	 * DRSI has xtal osc going to pclk at 4.9152 Mhz
	 * The BRG is sourced from that, and set to 32x clock
	 * The DPLL is sourced from the BRG.  BRG is fed to the TRxC pin
	 * Transmit clock is provided by the BRG externally divided by
	 * 32 in the CTC counter 1 and 2.
	 * Receive clock is from the DPLL
	 */

	/* Following subroutine sets up and ENABLES the receiver */
	drrx_enable(hp);
	
	/* DPLL from BRG, BRG source is PCLK */
	write_scc(hp->base,R14,BRSRC|SSBR);
	/* SEARCH mode, keep BRG source */
	write_scc(hp->base,R14,BRSRC|SEARCH);
	/* Enable the BRG */
	write_scc(hp->base,R14,BRSRC|BRENABL);

	/* enable the receive interrupts */

	write_scc(hp->base,R1,(INT_ALL_Rx|EXT_INT_ENAB));
	write_scc(hp->base,R15,BRKIE);	/* ABORT int */
	write_scc(hp->base,R9,MIE|NV);	/* master enable */


	/* Now, turn on the receiver and hunt for a flag */
	write_scc(hp->base,R3,RxENABLE|RxCRC_ENAB|Rx8);

	restore(i_state);
	return 0;
}

/*
 * Initialize the CTC (8536)
 * Only the counter/timers are used - the IO ports are un-comitted.
 * Channels 1 and 2 are linked to provide a /32 counter to convert
 * the SIO BRG to a real clock for Transmit clocking.
 * CTC 3 is left free running on a 10 mS period.  It is always polled
 * and therefore all interrupts from the chip are disabled.
 *
 * Updated 02/16/89 by N6TTO
 * Changed to support the use of the second channel on the 8530.
 * Added so that the driver works on the DRSI type 2 PC Adaptor
 * which has 2 1200 bps modems.
 *
 */
static void
drinitctc(port)
unsigned port;
{
	long i;

	/* Initialize 8536 CTC */

	/* Initialize 8536 */
	/* Start by forcing chip into known state */
	(void) read_ctc(port, Z8536_MICR);

	write_ctc(port, Z8536_MICR, 0x01);	/* Reset the CTC */
	for(i=0;i < 1000L; i++)		/* Loop to delay */
		;
	write_ctc(port, Z8536_MICR, 0x00);	/* Clear reset and start init seq. */

	/* Wait for chip to come ready */
	while((read_ctc(port, Z8536_MICR)) != 0x02)
		;

	write_ctc(port, Z8536_MICR, 0xa6);	/* MIE|NV|CT_VIS|RJA */
	write_ctc(port, Z8536_MCCR, 0xf4);	/* PBE|CT1E|CT2E|CT3E|PAE */

	write_ctc(port, Z8536_CTMS1, 0xe2);	/* Continuous,EOE,ECE, Pulse output */
	write_ctc(port, Z8536_CTMS2, 0xe2);	/* Continuous,EOE,ECE, Pulse output */
	write_ctc(port, Z8536_CTMS3, 0x80);	/* Continuous,Pulse output */
	write_ctc(port, Z8536_CT1MSB, 0x00);	/* Load time constant CTC #1 */
	write_ctc(port, Z8536_CT1LSB, 0x10);
	write_ctc(port, Z8536_CT2MSB, 0x00);	/* Load time constant CTC #2 */
	write_ctc(port, Z8536_CT2LSB, 0x10);
	write_ctc(port, Z8536_CT3MSB, 0x60);	/* Load time constant CTC #3 */
	write_ctc(port, Z8536_CT3LSB, 0x00);

	write_ctc(port, Z8536_IVR, 0x06);

	/* Set port direction bits in port A and B
	 * Data is input on bits d1 and d5, output on d0 and d4.
	 * The direction is set by 1 for input and 0 for output
	 */
	write_ctc(port, Z8536_PDCA, 0x22);
	write_ctc(port, Z8536_PDCB, 0x22);

	write_ctc(port, Z8536_CSR1, Z_GCB|Z_TCB);  /* Start CTC #1 running */
	write_ctc(port, Z8536_CSR2, Z_GCB|Z_TCB);  /* Start CTC #2 running */
	write_ctc(port, Z8536_CSR3, Z_IE|Z_GCB|Z_TCB); /* Start CTC #3 running */
}

/* Attach a DRSI interface to the system
 * argv[0]: hardware type, must be "drsi"
 * argv[1]: I/O address, e.g., "0x300"
 * argv[2]: vector, e.g., "2"
 * argv[3]: mode, must be "ax25"
 * argv[4]: iface label, e.g., "dr0"
 * argv[5]: receiver packet buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: iface speed for channel A
 * argv[8]: iface speed for channel B (defaults to same as A if absent)
 * argv[9]: First IP address, optional (defaults to Ip_addr)
 * argv[10]: Second IP address, optional (defaults to Ip_addr)
 */
int
dr_attach(argc,argv)
int argc;
char *argv[];
{
	register struct iface *if_pca,*if_pcb;
	struct drchan *hp;
	int dev;
	char *cp;

	/* Quick check to make sure args are good and mycall is set */
	if(setencap(NULL,argv[3]) == -1){
		printf("Mode %s unknown for interface %s\n",
			argv[3],argv[4]);
		return -1;
	}
	if(if_lookup(argv[4]) != NULL){
		printf("Interface %s already exists\n", argv[4]);
		return -1;
	}	
	if(Mycall[0] == '\0'){
		printf("set mycall first\n");
		return -1;
	}
	/* Note: More than one card can be supported if you give up a COM:
	 * port, thus freeing up an IRQ line and port address
	 */
	if(Drnbr >= DRMAX){
		printf("Only %d DRSI controller(s) supported right now!\n",DRMAX);
		return -1;
	}
	dev = Drnbr++;

	/* Initialize hardware-level control structure */
	Drsi[dev].addr = htoi(argv[1]);
	Drsi[dev].vec = atoi(argv[2]);
	if(strchr(argv[2],'c') != NULL)
		Drsi[dev].chain = 1;
	else
		Drsi[dev].chain = 0;

	/* Save original interrupt vector */
	Drsi[dev].oldvec = getirq(Drsi[dev].vec);

	/* Set new interrupt vector */
	if(setirq(Drsi[dev].vec,Drhandle[dev]) == -1){
		printf("IRQ %u out of range\n",Drsi[dev].vec);
		Drnbr--;
	}	
	/* Initialize the CTC */
	drinitctc(Drsi[dev].addr);
	
	/* Create iface structures and fill in details */
	if_pca = (struct iface *)callocw(1,sizeof(struct iface));
	if_pcb = (struct iface *)callocw(1,sizeof(struct iface));

	if_pca->addr = if_pcb->addr = Ip_addr;
	if(argc > 9)
		if_pca->addr = resolve(argv[9]);
	if(argc > 10)
		if_pcb->addr = resolve(argv[10]);
	if(if_pca->addr == 0 || if_pcb->addr == 0){
		printf(Noipaddr);
		free(if_pca);
		free(if_pcb);
		return -1;
	}
	/* Append "a" to iface associated with A channel */
	if_pca->name = mallocw((unsigned)strlen(argv[4])+2);
	strcpy(if_pca->name,argv[4]);
	strcat(if_pca->name,"a");

	/* Append "b" to iface associated with B channel */
	if_pcb->name = mallocw((unsigned)strlen(argv[4])+2);
	strcpy(if_pcb->name,argv[4]);
	strcat(if_pcb->name,"b");

	if_pcb->mtu = if_pca->mtu = atoi(argv[6]);
	if_pcb->ioctl = if_pca->ioctl = dr_ctl;
	if_pca->dev = 2*dev;			/* dr0a */
	if_pcb->dev = 2*dev + 1;		/* dr0b */
	if_pcb->stop = if_pca->stop = dr_stop;
	if_pcb->raw = if_pca->raw = dr_raw;

	setencap(if_pca,argv[3]);
	setencap(if_pcb,argv[3]);
	if(if_pcb->hwaddr == NULL)
		if_pcb->hwaddr = mallocw(sizeof(Mycall));
	memcpy(if_pcb->hwaddr,&Mycall,sizeof(Mycall));
	if(if_pca->hwaddr == NULL)
		if_pca->hwaddr = mallocw(sizeof(Mycall));
	memcpy(if_pca->hwaddr,&Mycall,sizeof(Mycall));
	/* Link em in to the iface chain */
	if_pca->next = if_pcb;
	if_pcb->next = Ifaces;
	Ifaces = if_pca;

	/* set params in drchan table for CHANNEL B */

	hp = &Drchan[2*dev+1];				/* dr1 is offset 1 */
	hp->iface = if_pcb;
	hp->stata = Drsi[dev].addr + CHANA + CTL;	/* permanent status */
	hp->statb = Drsi[dev].addr + CHANB + CTL;	/* addrs for CHANA/B*/
	if(argc > 8){
		/* Separate speed for channel B */
		hp->speed = (uint16)atoi(argv[8]);
	} else {
		/* Set speed to same as for channel A */
		hp->speed = (uint16)atoi(argv[7]);
	}
	hp->base = Drsi[dev].addr + CHANB;
	hp->bufsiz = atoi(argv[5]);
	hp->drtx_buffer = mallocw(if_pcb->mtu+100);
	hp->tstate = IDLE;
	hp->tx_state = drtx_idle;
	hp->w[RX].wcall = NULL;
	hp->w[RX].wakecnt = 0;
	hp->w[TX].wcall = NULL;
	hp->w[TX].wakecnt = 0;
	/* default KISS Params */
	hp->txdelay = 25;		/* 250 Ms */
	hp->persist = 64;		/* 25% persistence */
	hp->slotime = 10;		/* 100 Ms */
	hp->squeldelay = 20;		/* 200 Ms */
	hp->enddelay = 10;		/* 100 Ms */
	
	write_scc(hp->stata,R9,FHWRES);		/* Hardware reset */
	
	/* Disable interrupts with Master interrupt ctrl reg */
	write_scc(hp->stata,R9,0);

	drchanparam(hp); 

	/* Initialize buffer pointers */
	hp->rcvbuf = NULL;
	hp->rcvbuf->cnt = 0;
	hp->sndq = NULL;
	
	/* set params in drchan table for CHANNEL A */
	hp = &Drchan[2*dev];			/* dr0a is offset 0 */
	hp->iface = if_pca;
	hp->speed = (uint16)atoi(argv[7]);
	hp->base = Drsi[dev].addr + CHANA;
	hp->bufsiz = atoi(argv[5]);
	hp->drtx_buffer = mallocw(if_pca->mtu+100);
	hp->tstate = IDLE;
	hp->tx_state = drtx_idle;
	hp->w[RX].wcall = NULL;
	hp->w[RX].wakecnt = 0;
	hp->w[TX].wcall = NULL;
	hp->w[TX].wakecnt = 0;
	/* default KISS Params */
	hp->txdelay = 30;		/* 300 Ms */
	hp->persist = 64;		/* 25% persistence */
	hp->slotime = 10;		/* 100 Ms */
	hp->squeldelay = 20;		/* 200 Ms */
	hp->enddelay = 10;		/* 100 Ms */

	drchanparam(hp);

	/* Initialize buffer pointers */
	hp->rcvbuf = NULL;
	hp->rcvbuf->cnt = 0;
	hp->sndq = NULL;

	write_scc(hp->base,R9,MIE|NV);		/* master interrupt enable */

	/* Enable interrupt in 8259 interrupt controller */
	maskon(Drsi[dev].vec);
	
	cp = if_name(if_pca," tx");
	if_pca->txproc = newproc(cp,512,if_tx,0,if_pca,NULL,0);
	free(cp);
	cp = if_name(if_pcb," tx");
	if_pcb->txproc = newproc(cp,512,if_tx,0,if_pcb,NULL,0);
	free(cp);
	return 0;
}

/* Shut down iface */
static int
dr_stop(iface)
struct iface *iface;
{
	uint16 dev;

	dev = iface->dev;
	if(dev & 1)
		return 0;
	dev >>= 1;	/* Convert back into DRSI number */

	/* Set 8259 interrupt mask (turn off interrupts) */
	maskoff(Drsi[dev].vec);

	/* Restore original interrupt vector */
	setirq(Drsi[dev].vec, Drsi[dev].oldvec);
	Drnbr--;
	
	/* Force hardware reset */
	write_scc(Drsi[dev].addr + CHANA + CTL,R9,FHWRES);
	/* Reset the CTC */
	(void) read_ctc(Drsi[dev].addr, Z8536_MICR);
	write_ctc(Drsi[dev].addr, Z8536_MICR, 0x01);
	return 0;
}

/* Send raw packet on DRSI card */
static int
dr_raw(
struct iface *iface,
struct mbuf **bpp
){
	char kickflag;
	struct drchan *hp;
	int i_state;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();

	hp = &Drchan[iface->dev];
	i_state = dirps();
	kickflag = (hp->sndq == NULL) & (hp->sndbuf == NULL);
	/* clever! flag=1 if something in queue */
	enqueue(&hp->sndq,bpp);

	if(kickflag)			/* simulate interrupt to xmit */
		tx_fsm(hp);		/* process interrupt */

	restore(i_state);
	return 0;
}

/* display DRSI Channel stats */
int
dodrstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct drchan *hp0, *hp1;
	int i;

	for(i=0; i<DRMAX; i++){
		hp0 = &Drchan[i];
		hp1 = &Drchan[i+1];
		i = Drchan[i].base;
		printf("DRSI Board Statistics - N6TTO 112790.0\n");
		printf("--------------------------------------\n");
		printf("Channel - %s\n", hp0->iface->name);
		printf("Rxints  - %8ld  Txints  - %8ld  Exints  - %8ld\n",
			hp0->rxints, hp0->txints, hp0->exints);
		printf("Enqued  - %8ld  Crcerr  - %8ld  Aborts  - %8ld\n",
			hp0->enqueued, hp0->crcerr, hp0->aborts);
		printf("RFrames - %8ld  Rxovers - %8ld  TooBig  - %8ld\n",
			hp0->rxframes, hp0->rovers, hp0->toobig);
		printf("Txdefer - %8ld  Txppers - %8ld  Nomem   - %8ld\n",
			hp0->txdefers, hp0->txppersist, hp0->nomem);
		printf("Tx state  %8d  Rx state  %8d\n\n",hp0->tstate,hp0->rstate);
		printf("Channel - %s\n", hp1->iface->name);
		printf("Rxints  - %8ld  Txints  - %8ld  Exints  - %8ld\n",
			hp1->rxints, hp1->txints, hp1->exints);
		printf("Enqued  - %8ld  Crcerr  - %8ld  Aborts  - %8ld\n",
			hp1->enqueued, hp1->crcerr, hp1->aborts);
		printf("RFrames - %8ld  Rxovers - %8ld  TooBig  - %8ld\n",
			hp1->rxframes, hp1->rovers, hp1->toobig);
		printf("Txdefer - %8ld  Txppers - %8ld  Nomem   - %8ld\n",
			hp1->txdefers, hp1->txppersist, hp1->nomem);
		printf("Tx state  %8d  Rx state  %8d\n",hp1->tstate,hp1->rstate);
	}
	return 0;
}

/* Subroutine to set kiss params in channel tables */
static int32
dr_ctl(iface,cmd,set,val)
struct iface *iface;
int cmd;
int set;
int32 val;
{
	struct drchan *hp;
	hp = &Drchan[iface->dev];

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
	case PARAM_ENDDELAY:
		if(set)
			hp->enddelay = val;
		return hp->enddelay;
	case PARAM_SPEED:
		return hp->speed;
	}
	return -1;
}
