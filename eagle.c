/*
 * Interface driver for the EAGLE board for KA9Q's TCP/IP on an IBM-PC ONLY!
 *
 *  Written by Art Goldman, WA3CVG - (c) Copyright 1987 All Rights Reserved
 *  Permission for non-commercial use is hereby granted provided this notice
 *  is retained.  For info call: (301) 997-3838.
 *
 *  10 Jan 88	ng6q	- Corrected IDLE comparison in doegstat.
 *   6 Apr 88	ng6q	- Changed eg_raw to prevent calling egtxint with a
 *			  packet in sndbuf.  Initialized sndq and rcvq in
 *			  eg_attach.  Added carrier detect check before
 *			  slot time delay in egtxint.  Should make major
 *			  changes to egtxint to avoid delay loops while
 *			  masked for receive interrupts.
 */

#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "eagle.h"
#include "z8530.h"
#include "ax25.h"
#include "trace.h"
#include "nospc.h"
#include "devparam.h"
#include <time.h>

static int32 eg_ctl(struct iface *iface,int cmd,int set,int32 val);
static int eg_raw(struct iface *iface,struct mbuf **bpp);
static int eg_stop(struct iface *iface);
static void egchanparam(struct egchan *hp);
static void egexint(struct egchan *hp);
static void egrxint(struct egchan *hp);
static void egtxint(struct egchan *hp);
static void rts(struct egchan *hp,uint16 x);
static void waitmsec(int n);

static struct EGTAB Eagle[EGMAX];	/* Device table - one entry per card */
static INTERRUPT (*eghandle[])() = {	/* handler interrupt vector table */
	eg0vec,	
};
static struct egchan Egchan[2*EGMAX];	/* channel table - 2 entries per card */
static uint16 Egnbr;

/* Master interrupt handler.  One interrupt at a time is handled.
 * here. Service routines are called from here.
 */
INTERRUPT (far *(egint)(dev))()
int dev;
{
	register char st;
	register uint16 pcbase;
	struct egchan *hp;
	struct EGTAB *ep;

	ep = &Eagle[dev];
	ep->ints++;
	pcbase = ep->addr;

	/* Read interrupt status register from channel A */
	while((st = read_scc(pcbase+CHANA+CTL,R3)) != 0) {
		/* Use IFs to process ALL interrupts pending
		 * because we need to check all interrupt conditions
		 */
		if (st & CHARxIP) {
			/* Channel A Rcv Interrupt Pending */
			hp = &Egchan[2 * dev];
			egrxint(hp);
		} else if (st & CHATxIP) {
			/* Channel A Transmit Int Pending */
			hp = &Egchan[2 * dev];
			egtxint(hp);
		} else if (st & CHAEXT) {
			/* Channel A External Status Int */
			hp = &Egchan[2 * dev];
			egexint(hp);
		} else if (st & CHBRxIP) {
			/* Channel B Rcv Interrupt Pending */
			hp = &Egchan[(2 * dev)+1];
			egrxint(hp);
		} else if (st & CHBTxIP) {
			/* Channel B Transmit Int Pending */
			hp = &Egchan[(2 * dev)+1];
			egtxint(hp);
		} else if (st & CHBEXT) {
			/* Channel B External Status Int */
			hp = &Egchan[(2 * dev)+1];
			egexint(hp);
		}
		/* Reset highest interrupt under service */
		write_scc(hp->base+CTL,R0,RES_H_IUS);
	} /* End of while loop on int processing */
	return ep->chain ? ep->oldvec : NULL;
}

/* Eagle SIO External/Status interrupts
 * This can be caused by a receiver abort, or a Tx UNDERRUN/EOM.
 * Receiver automatically goes to Hunt on an abort.
 *
 * If the Tx Underrun interrupt hits, change state and
 * issue a reset command for it, and return.
 */
static void
egexint(hp)
register struct egchan *hp;
{
	char st;
	int i_state;

	i_state = dirps();
	hp->exints++;
	st = read_scc(hp->base+CTL,R0);     /* Fetch status */

	/* Check for Tx UNDERRUN/EOM - only in Transmit Mode */
	if((hp->rstate==0) && (st & TxEOM)) {
		/* if in UNDERRUN, go to FLAGOUT state
		 * see explanation under egtxint()
		 * CRC & FLAG now going out, so
		 * wait for Tx BUffer Empty int
		 */

		/* If we are not in underrun, this is an unexpected
		 * underrun.  EOM bit should be set, so the SCC will
		 * now send an abort
		 */

		if(hp->tstate == UNDERRUN)
		    hp->tstate = FLAGOUT;

		/* Tx Buff EMPTY interrupt occurs after CRC is sent */
	}

	/* Receive Mode only
	 * This triggers when hunt mode is entered, & since an ABORT
	 * automatically enters hunt mode, we use that to clean up
	 * any waiting garbage
	 */
	if((hp->rstate == ACTIVE) && (st & BRK_ABRT)) {
		hp->rcp = hp->rcvbuf->data;
		hp->rcvbuf->cnt = 0;	      /* rewind on DCD transition */
		hp->aborts++;		      /* nbr aborts * 2 */
	}

	/* reset external status latch */
	write_scc(CTL+hp->base,R0,RES_EXT_INT);

	restore(i_state);
}

/* EG receive interrupt handler. The first receive buffer is pre-allocated
 * in the init routine.  Thereafter, it is filled here, queued out, and a
 * new one acquired.  CRC, OVERRUN and TOOBIG errors merely 'rewind' the
 * pointers and reuse the same buffer.
 */
static void
egrxint(hp)
register struct egchan *hp;
{
	register uint16 base;
	char rse;
	int i_state;

	i_state = dirps();
	hp->rxints++;
	base = hp->base;

	if ((read_scc(base+CTL,R0)) & Rx_CH_AV) {
		/* there is a char to be stored
		 * read special condition bits before reading the data char
		 */
		rse = read_scc(hp->base+CTL,R1); /* get status byte from R1 */
		if(rse & Rx_OVR) {
			/* Rx overrun - toss buffer */
			hp->rcp = hp->rcvbuf->data;	/* reset buffer pointers */
			hp->rcvbuf->cnt = 0;
			hp->rstate = RXERROR;	/* set error flag */
			hp->rovers++;		/* count overruns */
		} else if(hp->rcvbuf->cnt >= hp->bufsiz) {
			/* Too large -- toss buffer */
			hp->toobig++;
			hp->rcp = hp->rcvbuf->data;	/* reset buffer pointers */
			hp->rcvbuf->cnt = 0;
			hp->rstate = TOOBIG;	/* when set, chars are not stored */
		}
		/* ok, we can store the received character now */
		if(hp->rstate == ACTIVE) {		/* If no errors... */
			*hp->rcp++ = inportb(base+DATA);    /* char to rcv buff */
			hp->rcvbuf->cnt++;		    /* bump count */
		} else {
			/* got to empty FIFO */
			(void) inportb(base+DATA);
			write_scc(hp->base+CTL,R0,ERR_RES);	/* reset err latch */
			hp->rstate = ACTIVE;
		}
	}
	/* char has been stored
	 * read special condition bits
	 */
	rse = read_scc(hp->base+CTL,R1);     /* get status byte from R1 */

	/* The End of Frame bit is ALWAYS associated with a character,
	 * usually, it is the last CRC char.  Only when EOF is true can
	 * we look at the CRC byte to see if we have a valid frame
	 */
	if(rse & END_FR) {
		hp->rxframes++;
		/* END OF FRAME -- Make sure Rx was active */
		if(hp->rcvbuf->cnt > 0) {		/* any data to store */
			/* looks like a frame was received
			 * now is the only time we can check for CRC error
			 */
			if((rse & CRC_ERR) || (hp->rstate > ACTIVE) || (hp->rcvbuf->cnt < 10)) {
				/* error occurred; toss frame */
				if(rse & CRC_ERR)
					hp->crcerr++;	/* count CRC errs */
				if(hp->rstate == RXERROR)
					hp->rovers++;
				/* don't throw away buffer -
				 * merely reset the pointers
				 */
				hp->rcp = hp->rcvbuf->data;
				hp->rcvbuf->cnt = 0;
			} else {
				/* Here we have a valid frame */
				hp->rcvbuf->cnt -= 2;	       /* Toss 2 crc bytes */
				net_route(hp->iface,&hp->rcvbuf);
				/* packet queued - get buffer for next frame */
				hp->rcvbuf = alloc_mbuf(hp->bufsiz);
				hp->rcp = hp->rcvbuf->data;
				hp->rcvbuf->cnt = 0;
				if(hp->rcvbuf == NULL) {
					/* No memory, abort receiver */
					restore(i_state);
					printf("DISASTER! Out of Memory for Receive!\n");
					write_scc(CTL+base,R3,Rx8);
					return;
				}
			} /* end good frame queued */
		}  /* end check for active receive upon EOF */
		hp->rstate = ACTIVE;	/* and clear error status */
	} /* end EOF check */
	restore(i_state);
}

/* egchan transmit interrupt service routine
 *
 * The state variable tstate, along with some static pointers,
 * represents the state of the transmit "process".
 */
static void
egtxint(hp)
register struct egchan *hp;
{
	register uint16 base;
	int c;
	int i_state;

	i_state = dirps();

	if(hp->tstate != DEFER && hp->tstate)
		hp->txints++;
	base = hp->base;

	switch(hp->tstate) {
	case FLAGOUT:
		/* Here after CRC sent and Tx interrupt fires.
		 * To give the SCC a chance to get the FLAG
		 * out, we delay 100 Ms
		 */
		hp->tstate = IDLE;	/* fall thru to IDLE */
		waitmsec(10);		/* 100 msec wait for flag Tx */
		/* Note, it may be possible to stuff out a
		 * meaningless character, wait for the interrupt
		 * then go to idle.  A delay is absolutely necessary
		 * here else the packet gets truncated prematurely
		 * when no other packet is waiting to be sent.
		 * IDLE state indicates either we are starting a brand new packet
		 * as a result of its being queued for transmission (egtxint called
		 * from eg_raw), or after a frame has been transmitted (as a
		 * result of a Tx buffer empty interrupt after the CRC/FLAG
		 */
	case IDLE:
		/* Transmitter idle. Find a frame for transmission */
		if((hp->sndbuf = dequeue(&hp->sndq)) == NULL) {
			/* Nothing to send - return to receive mode
			 * Tx OFF now - flag should have gone
			 */
			rts(hp,OFF);
			restore(i_state);
			return;
		}
		/* If a buffer to send, we drop thru here */
	case DEFER:	    /* we may have deferred prev xmit attempt */
		/* PPERSIST CALCULATION: we use the lower byte of the
		 * 8253 timer 0 count, as a random number (0-255).
		 * If the persist value is higher, wait one slot time
		 */
		if(hp->persist >= peekb(0x40,0x6c))
			waitmsec(hp->slotime);

		/* Check DCD so we don't step on a frame being received */
		/* DCD is ACTIVE LOW on the SCC DCD pin, but the bit in R0 */
		/* is SET when DCD is ACTIVE!! */

		if((read_scc(base+CTL,R0) & DCD) > 0) { /* Carrier Detected? */
			hp->tstate = DEFER;	/* defer xmit */
			/* don't release dequeued buffer...*/
			restore(i_state);
			return;
		}

		rts(hp,ON);   /* Transmitter on */
		/* ints not enabled yet */

		/* Get next char to send */
		c = PULLCHAR(&hp->sndbuf);	    /* one char at a time */
		write_scc(CTL+base,R0,RES_Tx_CRC);	/* reset for next frame */
		outportb(base+DATA,c);	    /* First char out now */

		/* select transmit interrupts to enable */

		write_scc(CTL+base,R15,TxUIE);	    /* allow Underrun int only */
		write_scc(CTL+base,R1,TxINT_ENAB|EXT_INT_ENAB);  /* Tx/Extern ints on */
		write_scc(CTL+base,R9,MIE|NV);	    /* master enable */
		/* enable interrupt latch on board */
		outportb(hp->dmactrl,INTENABLE);

		hp->tstate = ACTIVE;	/* char going out now */
		restore(i_state);
		return;

	case ACTIVE:
		/* Here we are actively sending a frame */
		if((c = PULLCHAR(&hp->sndbuf)) != -1){
			outportb(hp->base+DATA,c);	/* next char is gone */
			/* stuffing a char satisfies Interrupt condition */
		} else {
			/* No more to send - just stop till underrun int */
			hp->tstate = UNDERRUN;
			free_p(&hp->sndbuf);
			/* now we reset the EOM latch & enable underrun int */
			write_scc(CTL+base,R0,RES_EOM_L);	/* send CRC at underrun */
			write_scc(CTL+hp->base,R0,RES_Tx_P); /* reset Tx Int Pend */
		}
		restore(i_state);
		return;     /* back to wait for interrupt */

	case UNDERRUN:
		/*
		 * This state is handled by an UNDERRUN interrupt, which
		 * is an External Status interrupt.  At UNDERRUN, the
		 * UNDERRUN/EOM latch in R0 will be 0, so the SCC will send
		 * CRC and ending flag.  After the CRC clears the Tx buffer,
		 * a TX BUFF EMPTY interrupt will fire.  At that time, we
		 * should be in FLAGOUT state, ready to send another frame
		 * if one is there to send.
		 */
		break;
	} /* end switch */
	restore(i_state);
}

/* SET Transmit or Receive Mode
 * Set RTS (request-to-send) to modem on Transmit
 */
static void
rts(hp,x)
register struct egchan *hp;
uint16 x;
{
uint16 tc;
long br;

	/* Reprogram BRG and turn on transmitter to send flags */
	if(x == ON) {				/* Turn Tx ON and Receive OFF */
		write_scc(CTL+hp->base,R3,Rx8);	/* Rx off */
		waitmsec(50);			/* 500 msec delay before on */
		hp->rstate = IDLE;
		write_scc(CTL+hp->base,R9,0);	/* Interrupts off */
		br = hp->speed; 		/* get desired speed */
		tc = (XTAL/br)-2;		/* calc 1X BRG divisor */
		write_scc(CTL+hp->base,R12,tc&0xFF);      /* lower byte */
		write_scc(CTL+hp->base,R13,(tc>>8)&0xFF); /* upper bite */

		write_scc(CTL+hp->base,R5,TxCRC_ENAB|RTS|TxENAB|Tx8|DTR);
		/* Transmitter now on */
		write_scc(CTL+hp->base,R0,RES_Tx_CRC);/* CRC reset */
		waitmsec(hp->txdelay);	  /* Delay after Tx on */
	} else {	/* Tx OFF and Rx ON */
		hp->tstate = IDLE;
		write_scc(CTL+hp->base,R5,Tx8|DTR); 	/* TX off now */
		write_scc(CTL+hp->base,R0,ERR_RES); 	/* reset error bits */

		write_scc(CTL+hp->base,R1,(INT_ALL_Rx|EXT_INT_ENAB));
		write_scc(CTL+hp->base,R15,BRKIE);		/* allow ABORT int */

		/* delay for squelch tail before enable of Rx */
		waitmsec(hp->squeldelay);	/* keep it up  */

		/* Reprogram BRG for 32x clock for receive DPLL */
		write_scc(CTL+hp->base,R14,BRSRC);	     /* BRG off, but keep Pclk source */
		br = hp->speed; 			/* get desired speed */
		tc = ((XTAL/32)/br)-2;			/* calc 32X BRG divisor */
		write_scc(CTL+hp->base,R12,tc&0xFF);	/* lower byte */
		write_scc(CTL+hp->base,R13,(tc>>8)&0xFF);	/* upper bite */
		write_scc(CTL+hp->base,R14,BRSRC|SEARCH);	/* SEARCH mode, keep BRG source */
		write_scc(CTL+hp->base,R14,BRSRC|BRENABL);	/* Enable the BRG */

		/* Now, turn on the receiver and hunt for a flag */
		write_scc(CTL+hp->base,R3,RxENABLE|RxCRC_ENAB|Rx8);
		hp->rstate = ACTIVE;			/* Normal state */
	}
}

/* Initialize eg controller parameters */
static void
egchanparam(hp)
register struct egchan *hp;
{
	uint16 tc;
	long br;
	register uint16 base;
	int i_state;

	/* Initialize 8530 channel for SDLC operation */

	base = hp->base;
#ifdef	notdef
	printf("Initializing Channel %c - Base = %x\n",base&2?'A':'B',base);
#endif
	i_state = dirps();

	switch(base & 2){
	case 2:
		write_scc(CTL+base,R9,CHRA);	/* Reset channel A */
		break;
	case 0:
		write_scc(CTL+base,R9,CHRB);	/* Reset channel B */
		break;
	}

	/* Deselect all Rx and Tx interrupts */
	write_scc(CTL+base,R1,0);

	/* Turn off external interrupts (like CTS/CD) */
	write_scc(CTL+base,R15,0);

	/* X1 clock, SDLC mode */
	write_scc(CTL+base,R4,SDLC|X1CLK);	       /* SDLC mode and X1 clock */

	/* Now some misc Tx/Rx parameters */
	/* CRC PRESET 1, NRZI Mode */
	write_scc(CTL+base,R10,CRCPS|NRZI);

	/* Set up BRG and DPLL multiplexers */
	/* Tx Clk from BRG. Rcv Clk from DPLL, TRxC pin outputs DPLL */
	write_scc(CTL+base,R11,TCBR|RCDPLL|TRxCDP|TRxCOI);

	/* Null out SDLC start address */
	write_scc(CTL+base,R6,0);

	/* SDLC flag */
	write_scc(CTL+base,R7,FLAG);

	/* Set up the Transmitter but don't enable it */
	/*  DTR, 8 bit TX chars only - TX NOT ENABLED */
	write_scc(CTL+base,R5,Tx8|DTR);

	/* Receiver - intial setup only - more later */
	write_scc(CTL+base,R3,Rx8); 	       /* 8 bits/char */

	/* Setting up BRG now - turn it off first */
	write_scc(CTL+hp->base,R14,BRSRC);	     /* BRG off, but keep Pclk source */

	/* set the 32x time constant for the BRG in Receive mode */

	br = hp->speed; 			/* get desired speed */
	tc = ((XTAL/32)/br)-2;			/* calc 32X BRG divisor */

	write_scc(CTL+hp->base,R12,tc&0xFF);      /* lower byte */
	write_scc(CTL+hp->base,R13,(tc>>8)&0xFF); /* upper bite */

	/* Time to set up clock control register for RECEIVE mode
	 * Eagle has xtal osc going to pclk at 3.6864 Mhz
	 * The BRG is sourced from that, and set to 32x clock
	 * The DPLL is sourced from the BRG, and feeds the TRxC pin
	 * Transmit clock & Receive clock come from DPLL
	 */

	/* Following subroutine sets up and ENABLES the receiver */
	rts(hp,OFF);		   /* TX OFF and RX ON */

	write_scc(CTL+hp->base,R14,BRSRC|SSBR);	   /* DPLL from BRG, BRG source is PCLK */
	write_scc(CTL+hp->base,R14,BRSRC|SEARCH);	   /* SEARCH mode, keep BRG source */

	write_scc(CTL+hp->base,R14,BRSRC|BRENABL);	/* Enable the BRG */

	/* enable the receive interrupts */

	write_scc(CTL+hp->base,R1,(INT_ALL_Rx|EXT_INT_ENAB));
	write_scc(CTL+hp->base,R15,BRKIE);	    /* ABORT int */
	write_scc(CTL+hp->base,R9,MIE|NV);	/* master enable */

	/* enable interrupt latch on board */
	outportb(hp->dmactrl,INTENABLE);

	/* Now, turn on the receiver and hunt for a flag */
	write_scc(CTL+hp->base,R3,RxENABLE|RxCRC_ENAB|Rx8);

	restore(i_state);
}

/* Attach a EAGLE interface to the system
 * argv[0]: hardware type, must be "eagle"
 * argv[1]: I/O address, e.g., "0x300"
 * argv[2]: vector, e.g., "2"
 * argv[3]: mode, must be:
 *	    "ax25" (AX.25 UI frame format)
 * argv[4]: interface label, e.g., "eg0"
 * argv[5]: receiver packet buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "1200"
 * argv[8]: First IP address, optional (defaults to Ip_addr);
 * argv[9]: Second IP address, optional (defaults to Ip_addr);
 */
int
eg_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *if_pca,*if_pcb;
	struct egchan *hp;
	int dev;
	char *cp;

	/* Quick check to make sure args are good and mycall is set */
	if(strcmp(argv[3],"ax25") != 0){
		printf("Mode %s unknown for interface %s\n",
			argv[3],argv[4]);
		return -1;
	}
	if(if_lookup(argv[4]) != NULL){
		printf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	if(Mycall[0] == '\0'){
		printf("set mycall first\n");
		return -1;
	}
	/* Note: More than one card can be supported if you give up a COM:
	 * port, thus freeing up an IRQ line and port address
	 */

	if(Egnbr >= EGMAX) {
		printf("Only 1 EAGLE controller supported right now!\n");
		return -1;
	}
	if(setencap(NULL,argv[3]) == -1){
		printf("Unknown encapsulation %s\n",argv[3]);
		return -1;
	}
	dev = Egnbr++;

	/* Initialize hardware-level control structure */
	Eagle[dev].addr = htoi(argv[1]);
	Eagle[dev].vec = atoi(argv[2]);
	if(strchr(argv[2],'c') != NULL)
		Eagle[dev].chain = 1;
	else
		Eagle[dev].chain = 0;


	/* Save original interrupt vector */
	Eagle[dev].oldvec = getirq(Eagle[dev].vec);

	/* Set new interrupt vector */
	if(setirq(Eagle[dev].vec,eghandle[dev]) == -1){
		printf("IRQ %u out of range\n",Eagle[dev].vec);
		Egnbr--;
		return -1;
	}
	/* Create interface structures and fill in details */
	if_pca = (struct iface *)callocw(1,sizeof(struct iface));
	if_pcb = (struct iface *)callocw(1,sizeof(struct iface));

	if_pca->addr = if_pcb->addr = Ip_addr;
	if(argc > 8)
		if_pca->addr = resolve(argv[8]);
	if(argc > 9)
		if_pcb->addr = resolve(argv[9]);

	if(if_pca->addr == 0 || if_pcb->addr == 0){
		printf(Noipaddr);
		free(if_pca);
		free(if_pcb);
		return -1;
	}
	/* Append "a" to interface associated with A channel */
	if_pca->name = mallocw((unsigned)strlen(argv[4])+2);
	strcpy(if_pca->name,argv[4]);
	strcat(if_pca->name,"a");
	/* Append "b" to iface associated with B channel */
	if_pcb->name = mallocw((unsigned)strlen(argv[4])+2);
	strcpy(if_pcb->name,argv[4]);
	strcat(if_pcb->name,"b");

	if_pcb->mtu = if_pca->mtu = atoi(argv[6]);
	if_pcb->ioctl = if_pca->ioctl = eg_ctl;
	if_pca->dev = 2*dev;			/* eg0a */
	if_pcb->dev = 2*dev + 1;		/* eg0b */
	if_pcb->stop = if_pca->stop = eg_stop;
	if_pcb->raw = if_pca->raw = eg_raw;

	/* Must work, was checked at top */
	setencap(if_pca,argv[3]);
	setencap(if_pcb,argv[3]);
	if(if_pcb->hwaddr == NULL)
		if_pcb->hwaddr = mallocw(AXALEN);
	memcpy(if_pcb->hwaddr,Mycall,AXALEN);
	if(if_pca->hwaddr == NULL)
		if_pca->hwaddr = mallocw(AXALEN);
	memcpy(if_pca->hwaddr,Mycall,AXALEN);

	/* Link em in to the interface chain */
	if_pca->next = if_pcb;
	if_pcb->next = Ifaces;
	Ifaces = if_pca;

	/* set params in egchan table for CHANNEL B */

	hp = &Egchan[2*dev+1];				/* eg1 is offset 1 */
	hp->iface = if_pcb;
	hp->stata = Eagle[dev].addr + CHANA + CTL;	/* permanent status */
	hp->statb = Eagle[dev].addr + CHANB + CTL;	/* addrs for CHANA/B*/
	hp->dmactrl = Eagle[dev].addr + DMACTRL;	/* Eagle control reg */
	hp->speed = (uint16)atoi(argv[7]);
	hp->base = Eagle[dev].addr + CHANB;
	hp->bufsiz = atoi(argv[5]);
	hp->tstate = IDLE;
	/* default KISS Params */
	hp->txdelay = 25;		/* 250 Ms */
	hp->persist = 64;		/* 25% persistence */
	hp->slotime = 10;		/* 100 Ms */
	hp->squeldelay = 20;		/* 200 Ms */

	write_scc(CTL+hp->stata,R9,FHWRES); 	/* Hardware reset */
						/* one time only */
	/* Disable interrupts with Master interrupt ctrl reg */
	write_scc(CTL+hp->stata,R9,0);

	egchanparam(hp);

	/* Pre-allocate a receive buffer */
	hp->rcvbuf = alloc_mbuf(hp->bufsiz);
	if(hp->rcvbuf == NULL) {
	    /* No memory, abort receiver */
	    printf("EGATTACH: No memory available for Receive buffers\n");
	    /* Restore original interrupt vector */
	    setirq(Eagle[dev].vec,Eagle[dev].oldvec);
	    Egnbr--;
	    return(-1);
	}
	hp->rcp = hp->rcvbuf->data;
	hp->rcvbuf->cnt = 0;
	hp->sndq = NULL;

	/* set params in egchan table for CHANNEL A */
	hp = &Egchan[2*dev];			/* eg0a is offset 0 */
	hp->iface = if_pca;
	hp->speed = (uint16)atoi(argv[7]);
	hp->base = Eagle[dev].addr + CHANA;
	hp->bufsiz = atoi(argv[5]);
	hp->tstate = IDLE;
	/* default KISS Params */
	hp->txdelay = 25;		/* 250 Ms */
	hp->persist = 64;		/* 25% persistence */
	hp->slotime = 10;		/* 100 Ms */
	hp->squeldelay = 20;		/* 200 Ms */

	egchanparam(hp);

	/* Pre-allocate a receive buffer */
	hp->rcvbuf = alloc_mbuf(hp->bufsiz);
	if(hp->rcvbuf == NULL) {
		/* No memory, abort receiver */
		printf("EGATTACH: No memory available for Receive buffers\n");
		/* Restore original interrupt vector */
		setirq(Eagle[dev].vec,Eagle[dev].oldvec);
		Egnbr--;
		return -1;
	}
	hp->rcp = hp->rcvbuf->data;
	hp->rcvbuf->cnt = 0;
	hp->sndq = NULL;

	write_scc(CTL+hp->base,R9,MIE|NV);		/* master interrupt enable */

	/* Enable interrupts on the EAGLE card itself */
	outportb(hp->dmactrl,INTENABLE);

	/* Enable interrupt */
	maskon(Eagle[dev].vec);

	cp = if_name(if_pca," tx");
	if_pca->txproc = newproc(cp,512,if_tx,0,if_pca,NULL,0);
	free(cp);
	cp = if_name(if_pcb," tx");
	if_pcb->txproc = newproc(cp,512,if_tx,0,if_pcb,NULL,0);
	free(cp);

	return 0;
}


/* Shut down interface */
static int
eg_stop(iface)
struct iface *iface;
{
	int dev;

	dev = iface->dev;
	if(dev & 1)
		return 0;
	dev >>= 1;	/* Convert back into eagle number */

	/* Turn off interrupts */
	maskoff(Eagle[dev].vec);

	/* Restore original interrupt vector */
	setirq(Eagle[dev].vec,Eagle[dev].oldvec);

	/* Force hardware reset */
	write_scc(CTL+Eagle[dev].addr + CHANA,R9,FHWRES);

	/* resets interrupt enable on eagle card itself */
	outportb(Eagle[dev].addr+DMACTRL,0);
	return 0;
}

/* Send raw packet on eagle card */
static int
eg_raw(
struct iface *iface,
struct mbuf **bpp
){
	char kickflag;
	struct egchan *hp;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();
	hp = &Egchan[iface->dev];
	kickflag = (hp->sndq == NULL) & (hp->sndbuf == NULL);	/* clever! flag=1 if something in queue */
	enqueue(&hp->sndq,bpp);
	if(kickflag)			/* simulate interrupt to xmit */
		egtxint(hp);		/* process interrupt */
	return 0;
}
/* routine to delay n increments of 10 milliseconds
 * about right on a turbo XT - will be slow on 4.77
 * Still looking for a way to use the 8253 timer...
 */
static void
waitmsec(n)
int n;
{
	long i;

	for(i=0L; i < (200L*n); i++)
		;  /* simple loop delay */
}

/* display EAGLE Channel stats */
int
doegstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct egchan *hp0, *hp1;
	int i;

	for(i=0; i<EGMAX; i++) {
		hp0 = &Egchan[i];
		hp1 = &Egchan[i+1];

		printf("EAGLE Board Statistics:\n\n");
		printf("Base Addr\tRxints\tTxints\tExints\tEnqued\tCrcerr\tAborts\tRxOvers\tRFrames\n");
		printf("---------\t------\t------\t------\t------\t------\t------\t-------\t-------\n");
		printf("0x%x\t\t%ld\t%ld\t%ld\t%d\t%d\t%d\t%d\t%d\nRcv State=%s\n", hp0->base, hp0->rxints,
		    hp0->txints, hp0->exints, hp0->enqueued, hp0->crcerr, hp0->aborts,
		    hp0->rovers,hp0->rxframes,
		    hp0->rstate==0?"IDLE":hp0->rstate==1?"ACTIVE":hp0->rstate==2?"RXERROR":hp0->rstate==3?"RXABORT":"TOOBIG");

		printf("0x%x\t\t%ld\t%ld\t%ld\t%d\t%d\t%d\t%d\t%d\nRcv State=%s\n\n", hp1->base, hp1->rxints,
		    hp1->txints, hp1->exints, hp1->enqueued, hp1->crcerr, hp1->aborts,
		    hp1->rovers,hp1->rxframes,
		    hp1->rstate==0?"IDLE":hp1->rstate==1?"ACTIVE":hp1->rstate==2?"RXERROR":hp1->rstate==3?"RXABORT":"TOOBIG");
	}
	return 0;
}

/* Subroutine to set kiss params in channel tables */
static int32
eg_ctl(iface,cmd,set,val)
struct iface *iface;
int cmd;
int set;
int32 val;
{
	struct egchan *hp;

	hp = &Egchan[iface->dev];	    /* point to channel table */
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
	}
	return -1;
}
