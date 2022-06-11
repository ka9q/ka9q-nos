/* OS- and machine-dependent stuff for the 8250 asynch chip on a IBM-PC
 * Copyright 1991 Phil Karn, KA9Q
 *
 * 16550A support plus some statistics added mah@hpuviea.at 15/7/89
 *
 * CTS hardware flow control from dkstevens@ucdavis,
 * additional stats from delaroca@oac.ucla.edu added by karn 4/17/90
 * Feb '91      RLSD line control reorganized by Bill_Simpson@um.cc.umich.edu
 * Sep '91      All control signals reorganized by Bill Simpson
 * Apr '92	Control signals redone again by Phil Karn
 */
#include <stdio.h>
#include <dos.h>
#include <errno.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "n8250.h"
#include "asy.h"
#include "devparam.h"
#include "nospc.h"
#include "dialer.h"

static int asyrxint(struct asy *asyp);
static void asytxint(struct asy *asyp);
static void asymsint(struct asy *asyp);
static void pasy(struct asy *asyp);
static INTERRUPT (far *(asycom)(struct asy *))(void);

struct asy Asy[ASY_MAX];

struct fport Fport[FPORT_MAX];

static INTERRUPT (*Fphand[FPORT_MAX])() = {
fp0vec,
};

/* ASY interrupt handlers */
static INTERRUPT (*Handle[ASY_MAX])() = {
asy0vec,asy1vec,asy2vec,asy3vec,asy4vec,asy5vec
};

/* Initialize asynch port "dev" */
int
asy_init(dev,ifp,base,irq,bufsize,trigchar,speed,cts,rlsd,chain)
int dev;
struct iface *ifp;
int base;
int irq;
uint16 bufsize;
int trigchar;
long speed;
int cts;		/* Use CTS flow control */
int rlsd;		/* Use Received Line Signal Detect (aka CD) */
int chain;		/* Chain interrupts */
{
	register struct fifo *fp;
	register struct asy *ap;
	int i_state;

	ap = &Asy[dev];
	ap->iface = ifp;
	ap->addr = base;
	ap->vec = irq;
	ap->chain = chain;

	/* Set up receiver FIFO */
	fp = &ap->fifo;
	fp->buf = mallocw(bufsize);
	fp->bufsize = bufsize;
	fp->wp = fp->rp = fp->buf;
	fp->cnt = 0;
	fp->hiwat = 0;
	fp->overrun = 0;
	base = ap->addr;
	ap->trigchar = trigchar;

	/* Purge the receive data buffer */
	(void)inportb(base+RBR);

	i_state = dirps();

	/* Save original interrupt vector, mask state, control bits */
	if(ap->vec != -1){
		ap->save.vec = getirq(ap->vec);
		ap->save.mask = getmask(ap->vec);
	}
	ap->save.lcr = inportb(base+LCR);
	ap->save.ier = inportb(base+IER);
	ap->save.mcr = inportb(base+MCR);
	ap->msr = ap->save.msr = inportb(base+MSR);
	ap->save.iir = inportb(base+IIR);

	/* save speed bytes */
	setbit(base+LCR,LCR_DLAB);
	ap->save.divl = inportb(base+DLL);
	ap->save.divh = inportb(base+DLM);
	clrbit(base+LCR,LCR_DLAB);

	/* save modem control flags */
	ap->cts = cts;
	ap->rlsd = rlsd;

	/* Set interrupt vector to SIO handler */
	if(ap->vec != -1)
		setirq(ap->vec,Handle[dev]);

	/* Set line control register: 8 bits, no parity */
	outportb(base+LCR,LCR_8BITS);
	
	/* determine if 16550A, turn on FIFO mode and clear RX and TX FIFOs */
	outportb(base+FCR,FIFO_ENABLE);

	/* According to National ap note AN-493, the FIFO in the 16550 chip
	 * is broken and must not be used. To determine if this is a 16550A
	 * (which has a good FIFO implementation) check that both bits 7
	 * and 6 of the IIR are 1 after setting the fifo enable bit. If
	 * not, don't try to use the FIFO.
	 */
	if ((inportb(base+IIR) & IIR_FIFO_ENABLED) == IIR_FIFO_ENABLED) {
		ap->is_16550a = TRUE;
		outportb(base+FCR,FIFO_SETUP);
	} else {
		/* Chip is not a 16550A. In case it's a 16550 (which has a
		 * broken FIFO), turn off the FIFO bit.
		 */
		outportb(base+FCR,0);
		ap->is_16550a = FALSE;
	}

	/* Turn on receive interrupts and optionally modem interrupts;
	 * leave transmit interrupts off until we actually send data.
	 */
	if(ap->rlsd || ap->cts)
		outportb(base+IER,IER_MS|IER_DAV);
	else
		outportb(base+IER,IER_DAV);

	/* Turn on 8250 master interrupt enable (connected to OUT2) */
	setbit(base+MCR,MCR_OUT2);

	/* Enable interrupt */
	if(ap->vec != -1)
		maskon(ap->vec);
	restore(i_state);

	asy_speed(dev,speed);

	return 0;
}


int
asy_stop(ifp)
struct iface *ifp;
{
	register unsigned base;
	register struct asy *ap;
	struct asydialer *dialer;
	int i_state;

	ap = &Asy[ifp->dev];

	if(ap->iface == NULL)
		return -1;	/* Not allocated */		
	ap->iface = NULL;

	base = ap->addr;

	if(ifp->dstate != NULL){
		dialer = (struct asydialer *)ifp->dstate;
		stop_timer(&dialer->idle);	/* Stop the idle timer, if running */		
		free(dialer);
		ifp->dstate = NULL;
	}
	(void)inportb(base+RBR); /* Purge the receive data buffer */

	if(ap->is_16550a){
		/* Purge hardware FIFOs and disable if we weren't already
		 * in FIFO mode when we entered. Apparently some
		 * other comm programs can't handle 16550s in
		 * FIFO mode; they expect 16450 compatibility mode.
		 */
		outportb(base+FCR,FIFO_SETUP);
		if((ap->save.iir & IIR_FIFO_ENABLED) != IIR_FIFO_ENABLED)
			outportb(base+FCR,0);
	}
	/* Restore original interrupt vector and 8259 mask state */
	i_state = dirps();
	if(ap->vec != -1){
		setirq(ap->vec,ap->save.vec);
		if(ap->save.mask)
			maskon(ap->vec);
		else
			maskoff(ap->vec);
	}

	/* Restore speed regs */
	setbit(base+LCR,LCR_DLAB);
	outportb(base+DLL,ap->save.divl);	/* Low byte */
	outportb(base+DLM,ap->save.divh);	/* Hi byte */
	clrbit(base+LCR,LCR_DLAB);

	/* Restore control regs */
	outportb(base+LCR,ap->save.lcr);
	outportb(base+IER,ap->save.ier);
	outportb(base+MCR,ap->save.mcr);

	restore(i_state);
	free(ap->fifo.buf);
	return 0;
}


/* Set asynch line speed */
int
asy_speed(dev,bps)
int dev;
long bps;
{
	register unsigned base;
	register long divisor;
	struct asy *asyp;
	int i_state;

	if(bps <= 0 || dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];
	if(asyp->iface == NULL)
		return -1;

	if(bps == 0)
		return -1;
	asyp->speed = bps;

	base = asyp->addr;
	divisor = BAUDCLK / bps;

	i_state = dirps();

	/* Purge the receive data buffer */
	(void)inportb(base+RBR);
	if (asyp->is_16550a)		/* clear tx+rx fifos */
		outportb(base+FCR,FIFO_SETUP);

	/* Turn on divisor latch access bit */
	setbit(base+LCR,LCR_DLAB);

	/* Load the two bytes of the register */
	outportb(base+DLL,divisor);		/* Low byte */
	outportb(base+DLM,divisor >> 8);	/* Hi byte */

	/* Turn off divisor latch access bit */
	clrbit(base+LCR,LCR_DLAB);

	restore(i_state);
	return 0;
}


/* Asynchronous line I/O control */
int32
asy_ioctl(ifp,cmd,set,val)
struct iface *ifp;
int cmd;
int set;
int32 val;
{
	struct asy *ap = &Asy[ifp->dev];
	uint16 base = ap->addr;

	switch(cmd){
	case PARAM_SPEED:
		if(set)
			asy_speed(ifp->dev,val);
		return ap->speed;
	case PARAM_DTR:
		if(set) {
			writebit(base+MCR,MCR_DTR,(int)val);
		}
		return (inportb(base+MCR) & MCR_DTR) ? TRUE : FALSE;
	case PARAM_RTS:
		if(set) {
			writebit(base+MCR,MCR_RTS,(int)val);
		}
		return (inportb(base+MCR) & MCR_RTS) ? TRUE : FALSE;
	case PARAM_DOWN:
		clrbit(base+MCR,MCR_RTS);
		clrbit(base+MCR,MCR_DTR);
		return FALSE;
	case PARAM_UP:
		setbit(base+MCR,MCR_RTS);
		setbit(base+MCR,MCR_DTR);
		return TRUE;
	}
	return -1;
}
/* Open an asynch port for direct I/O, temporarily suspending any
 * packet-mode operations. Returns device number for asy_write and get_asy
 */
int
asy_open(name)
char *name;
{
	struct iface *ifp;
	int dev;

	if((ifp = if_lookup(name)) == NULL){
		errno = ENODEV;
		return -1;
	}
	if((dev = ifp->dev) >= ASY_MAX || Asy[dev].iface != ifp){
		errno = EINVAL;
		return -1;
	}
	/* Suspend the packet drivers */
	suspend(ifp->rxproc);
	suspend(ifp->txproc);

	/* bring the line up (just in case) */
	if(ifp->ioctl != NULL)
		(*ifp->ioctl)(ifp,PARAM_UP,TRUE,0L);
	return dev;
}
int
asy_close(dev)
int dev;
{
	struct iface *ifp;

	if(dev < 0 || dev >= ASY_MAX){
		errno = EINVAL;
		return -1;
	}
	/* Resume the packet drivers */
	if((ifp = Asy[dev].iface) == NULL){
		errno = EINVAL;
		return -1;
	}
	resume(ifp->rxproc);
	resume(ifp->txproc);
	return 0;
}

/* Send a buffer on the serial transmitter and wait for completion */
int
asy_write(dev,buf,cnt)
int dev;
void *buf;
unsigned short cnt;
{
	register struct dma *dp;
	unsigned base;
	struct asy *asyp;
	int tmp;
	int i_state;
	struct iface *ifp;

	if(dev < 0 || dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];
	if((ifp = asyp->iface) == NULL)
		return -1;

	base = asyp->addr;
	dp = &asyp->dma;

	if(dp->busy)
		return -1;	/* Already busy */

	dp->data = buf;
	dp->cnt = cnt;
	dp->busy = 1;

	/* If CTS flow control is disabled or CTS is true,
	 * enable transmit interrupts here so we'll take an immediate
	 * interrupt to get things going. Otherwise let the
	 * modem control interrupt enable transmit interrupts
	 * when CTS comes up. If we do turn on TxE,
	 * "kick start" the transmitter interrupt routine, in case just
	 * setting the interrupt enable bit doesn't cause an interrupt
	 */

	if(!asyp->cts || (asyp->msr & MSR_CTS)){
		setbit(base+IER,IER_TxE);
		asytxint(asyp);
	}
	/* Wait for completion */
	for(;;){
		i_state = dirps();
		tmp = dp->busy;
		restore(i_state);
		if(tmp == 0)
			break;
		kwait(&asyp->dma);
	}
	ifp->lastsent = secclock();
	return cnt;
}

/* Read data from asynch line
 * Blocks until at least 1 byte of data is available.
 * returns number of bytes read, up to 'cnt' max
 */
int
asy_read(dev,buf,cnt)
int dev;
void *buf;
unsigned short cnt;
{
	struct fifo *fp;
	int i_state,tmp;
	uint8 c,*obp;

	if(cnt == 0)
		return 0;

	if(dev < 0 || dev >= ASY_MAX){
		errno = EINVAL;
		return -1;
	}
	fp = &Asy[dev].fifo;
	obp = (uint8 *)buf;
	for(;;){
		/* Atomic read of and subtract from fp->cnt */
		i_state = dirps();
		tmp = fp->cnt;
		if(tmp != 0){
			if(cnt > tmp)
				cnt = tmp;	/* Limit to data on hand */
			fp->cnt -= cnt;
			restore(i_state);
			break;
		}
		restore(i_state);
		if((errno = kwait(fp)) != 0)
			return -1;
	}		
	tmp = cnt;
	while(tmp-- != 0){
		/* This can be optimized later if necessary */
		c = *fp->rp++;
		if(fp->rp >= &fp->buf[fp->bufsize])
			fp->rp = fp->buf;
		*obp++ = c;
	}
	return cnt;
}
/* Blocking read from asynch line
 * Returns character or -1 if aborting
 */
int
get_asy(dev)
int dev;
{
	uint8 c;
	int tmp;

	if((tmp = asy_read(dev,&c,1)) == 1)
		return c;
	else
		return tmp;
}

/* Interrupt handler for 8250 asynch chip (called from asyvec.asm) */
INTERRUPT (far *(asyint)(dev))()
int dev;
{
	return asycom(&Asy[dev]);
}
/* Interrupt handler for AST 4-port board (called from fourport.asm) */
INTERRUPT (far *(fpint)(dev))()
int dev;
{
	int iv;
	struct fport *fport;
	int i;

	fport = &Fport[dev];
	/* Read special interrupt demux register to see which port is active */
	while(((iv = inportb(fport->iv)) & 0xf) != 0xf){
		for(i=0;i<4;i++){
			if((iv & (1 << i)) == 0 && fport->asy[i] != NULL)
				asycom(fport->asy[i]);
		}
	}
	return NULL;
}
/* Common interrupt handler code for 8250/16550 port */
static INTERRUPT (far *(asycom)(asyp))(void)
struct asy *asyp;
{
	unsigned base;
	char iir;

	base = asyp->addr;
	while(((iir = inportb(base+IIR)) & IIR_IP) == 0){
		switch(iir & IIR_ID_MASK){
		case IIR_RDA:	/* Receiver interrupt */
			asyrxint(asyp);
			break;
		case IIR_THRE:	/* Transmit interrupt */
			asytxint(asyp);
			break;
		case IIR_MSTAT:	/* Modem status change */
			asymsint(asyp);
			asyp->msint_count++;
			break;
		}
		/* should happen at end of a single packet */
		if(iir & IIR_FIFO_TIMEOUT)
			asyp->fifotimeouts++;
	}
	return asyp->chain ? asyp->save.vec : NULL;
}


/* Process 8250 receiver interrupts */
static int
asyrxint(asyp)
struct asy *asyp;
{
	register struct fifo *fp;
	unsigned base;
	uint8 c,lsr;
	int cnt = 0;
	int trigseen = FALSE;

	asyp->rxints++;
	base = asyp->addr;
	fp = &asyp->fifo;
	for(;;){
		lsr = inportb(base+LSR);
		if(lsr & LSR_OE)
			asyp->overrun++;

		if(lsr & LSR_DR){
			asyp->rxchar++;
			c = inportb(base+RBR);
			if(asyp->trigchar == -1 || asyp->trigchar == c)
				trigseen = TRUE;
			/* If buffer is full, we have no choice but
			 * to drop the character
			 */
			if(fp->cnt != fp->bufsize){
				*fp->wp++ = c;
				if(fp->wp >= &fp->buf[fp->bufsize])
					/* Wrap around */
					fp->wp = fp->buf;
				fp->cnt++;
				if(fp->cnt > fp->hiwat)
					fp->hiwat = fp->cnt;
				cnt++;
			} else
				fp->overrun++;
		} else
			break;
	}
	if(cnt > asyp->rxhiwat)
		asyp->rxhiwat = cnt;
	if(trigseen)
		ksignal(fp,1);
	return cnt;
}


/* Handle 8250 transmitter interrupts */
static void
asytxint(asyp)
struct asy *asyp;
{
	register struct dma *dp;
	register unsigned base;
	register int count;

	base = asyp->addr;
	dp = &asyp->dma;
	asyp->txints++;
	if(!dp->busy || (asyp->cts && !(asyp->msr & MSR_CTS))){
		/* These events "shouldn't happen". Either the
		 * transmitter is idle, in which case the transmit
		 * interrupts should have been disabled, or flow control
		 * is enabled but CTS is low, and interrupts should also
		 * have been disabled.
		 */
		clrbit(base+IER,IER_TxE);
		return;	/* Nothing to send */
	}
	if(!(inportb(base+LSR) & LSR_THRE))
		return;	/* Not really ready */

	/* If it's a 16550A, load up to 16 chars into the tx hw fifo
	 * at once. With an 8250, it can be one char at most.
	 */
	if(asyp->is_16550a){
		count = min(dp->cnt,OUTPUT_FIFO_SIZE);

		/* 16550A: LSR_THRE will drop after the first char loaded
		 * so we can't look at this bit to determine if the hw fifo is
		 * full. There seems to be no way to determine if the tx fifo
		 * is full (any clues?). So we should never get here while the
		 * fifo isn't empty yet.
		 */
		asyp->txchar += count;
		dp->cnt -= count;
#ifdef	notdef	/* This is apparently too fast for some chips */
		dp->data = outbuf(base+THR,dp->data,count);
#else
		while(count-- != 0)
			outportb(base+THR,*dp->data++);
#endif
	} else {	/* 8250 */
		do {
			asyp->txchar++;
			outportb(base+THR,*dp->data++);
		} while(--dp->cnt != 0 && (inportb(base+LSR) & LSR_THRE));
	}
	if(dp->cnt == 0){
		dp->busy = 0;
		/* Disable further transmit interrupts */
		clrbit(base+IER,IER_TxE);
		ksignal(&asyp->dma,1);
	}
}

/* Handle 8250 modem status change interrupt */
static void
asymsint(asyp)
struct asy *asyp;
{
	unsigned base = asyp->addr;

	asyp->msr = inportb(base+MSR);

	if(asyp->cts && (asyp->msr & MSR_DCTS)){
		/* CTS has changed and we care */
		if(asyp->msr & MSR_CTS){
			/* CTS went up */
			if(asyp->dma.busy){
				/* enable transmit interrupts and kick */
				setbit(base+IER,IER_TxE);
				asytxint(asyp);
			}
		} else {
			/* CTS now dropped, disable Transmit interrupts */
			clrbit(base+IER,IER_TxE);
		}
	}
	if(asyp->rlsd && (asyp->msr & MSR_DRLSD)){
		/* RLSD just changed and we care, signal it */
		ksignal( &(asyp->rlsd), 1 );
		/* Keep count */
		asyp->cdchanges++;
	}
	ksignal(&asyp->msr,0);
}

/* Wait for a signal that the RLSD modem status has changed */
int
get_rlsd_asy(dev, new_rlsd)
int dev;
int new_rlsd;
{
	struct asy *ap = &Asy[dev];

	if(ap->rlsd == 0)
		return -1;

	for(;;){
		if(new_rlsd && (ap->msr & MSR_RLSD))
			return 1;
		if(!new_rlsd && !(ap->msr & MSR_RLSD))
			return 0;

		/* Wait for state change to requested value */
		ppause(2L);
		kwait( &(ap->rlsd) );
	}
}

/* Poll the asynch input queues; called on every clock tick.
 * This helps limit the interrupt ring buffer occupancy when long
 * packets are being received.
 */
void
asytimer()
{
	register struct asy *asyp;
	register struct fifo *fp;
	register int i;
	int i_state;

	for(i=0;i<ASY_MAX;i++){
		asyp = &Asy[i];
		fp = &asyp->fifo;
		if(fp->cnt != 0)
			ksignal(fp,1);
		if(asyp->dma.busy
		 && (inportb(asyp->addr+LSR) & LSR_THRE)
		 && (!asyp->cts || (asyp->msr & MSR_CTS))){
			asyp->txto++;
			i_state = dirps();
			asytxint(asyp);
			restore(i_state);
		}
	}
}
int
doasystat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct asy *asyp;
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface != NULL)
				pasy(asyp);
		}
		return 0;
	}
	for(i=1;i<argc;i++){
		if((ifp = if_lookup(argv[i])) == NULL){
			printf("Interface %s unknown\n",argv[i]);
			continue;
		}
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface == ifp){
				pasy(asyp);
				break;
			}
		}
		if(asyp == &Asy[ASY_MAX])
			printf("Interface %s not asy\n",argv[i]);
	}

	return 0;
}

static void
pasy(asyp)
struct asy *asyp;
{
	int mcr;

	printf("%s:",asyp->iface->name);
	if(asyp->is_16550a)
		printf(" [NS16550A]");
	if(asyp->trigchar != -1)
		printf(" [trigger 0x%02x]",asyp->trigchar);
	if(asyp->cts)
		printf(" [cts flow control]");
	if(asyp->rlsd)
		printf(" [rlsd line control]");

	printf(" %lu bps\n",asyp->speed);

	mcr = inportb(asyp->addr+MCR);
	printf(" MC: int %lu DTR %s  RTS %s  CTS %s  DSR %s  RI %s  CD %s\n",
	 asyp->msint_count,
	 (mcr & MCR_DTR) ? "On" : "Off",
	 (mcr & MCR_RTS) ? "On" : "Off",
	 (asyp->msr & MSR_CTS) ? "On" : "Off",
	 (asyp->msr & MSR_DSR) ? "On" : "Off",
	 (asyp->msr & MSR_RI) ? "On" : "Off",
	 (asyp->msr & MSR_RLSD) ? "On" : "Off");
	
	printf(" RX: int %lu chars %lu hw over %lu hw hi %lu",
	 asyp->rxints,asyp->rxchar,asyp->overrun,asyp->rxhiwat);
	asyp->rxhiwat = 0;
	if(asyp->is_16550a)
		printf(" fifo TO %lu",asyp->fifotimeouts);
	printf(" sw over %lu sw hi %u\n",
	 asyp->fifo.overrun,asyp->fifo.hiwat);
	asyp->fifo.hiwat = 0;

	printf(" TX: int %lu chars %lu THRE TO %lu%s\n",
	 asyp->txints,asyp->txchar,asyp->txto,
	 asyp->dma.busy ? " BUSY" : "");
}
/* Send a message on the specified serial line */
int
asy_send(dev,bpp)
int dev;
struct mbuf **bpp;
{
	if(dev < 0 || dev >= ASY_MAX){
		free_p(bpp);
		return -1;
	}
	while(*bpp != NULL){
		/* Send the buffer */
		asy_write(dev,(*bpp)->data,(*bpp)->cnt);
		/* Now do next buffer on chain */
		*bpp = free_mbuf(bpp);
	}
	return 0;
}
/* Attach an AST 4-port serial interface (or clone) to the system
 * argv[0]: hardware type, must be "4port"
 * argv[1]: I/O address, e.g., "0x2a0"
 * argv[2]: vector, e.g., "5",
 */
int
fp_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int i;
	struct fport *fp;

	for(i=0;i<FPORT_MAX;i++){
		if(Fport[i].base == 0)
			break;
	}
	if(i == FPORT_MAX){
		printf("Too many 4port devices\n");
		return 1;
	}
	fp = &Fport[i];
	fp->base = htoi(argv[1]);
	fp->irq = atoi(argv[2]);
	fp->iv = fp->base + 0x1f;
	setirq(fp->irq,Fphand[i]);
	maskon(fp->irq);
	outportb(fp->iv,0x80);	/* Enable global interrupts */
	return 0;
}
void
fp_stop()
{
	int i;
	struct fport *fp;

	for(i=0;i<FPORT_MAX;i++){
		if(Fport[i].base == 0)
			continue;
		fp = &Fport[i];
		outportb(fp->iv,0);	/* Disable global interrupts */
		maskoff(fp->irq);
	}
}

