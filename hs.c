/* Interface driver for the DRSI PCPA or the Eagle 8530 boards for the IBM PC
 * connected to a WA4DSY 56kbps modem. Uses polling-loop transfers with
 * interrupts disabled for maximum speed.
 *
 * This driver is a bit of a kludge. A DMA-driven card and driver (e.g.,
 * the PI) is much better, but this is better than nothing if all you have
 * is a "dumb" 8530 card.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "hs.h"
#include "z8530.h"
#include "ax25.h"
#include "trace.h"
#include "nospc.h"
#include "proc.h"
#include "devparam.h"

static void flushrx(uint16 data);
static void hdlcparam(struct hdlc *hp);
static void hexint(struct hdlc *hp);
static void hrxint(struct hdlc *hp);
static int hs_stop(struct iface *iface);
static int hs_raw(struct iface *iface,struct mbuf **bpp);
static int32 hs_ctl(struct iface *,int cmd,int set,int32 val);
static void hstxoff(struct hdlc *hp);
static void hstxon(struct hdlc *hp);
static void htxint(struct hdlc *hp);
static void init_delay(void);
static void msdelay(void);

static struct hs Hs[NHS];
static INTERRUPT (*Hshandle[])() = { hs0vec };
static struct hdlc Hdlc[2*NHS];
static uint16 Nhs;

/* Master interrupt handler for the PC-100 card. All interrupts come
 * here first, then are switched out to the appropriate routine.
 */
INTERRUPT (far *(hsint)(dev))()
int dev;
{
	register char iv;
	uint16 hsbase;
	struct hs *hsp;
	register struct hdlc *hp;
	
	hsp = &Hs[dev];
	hsp->ints++;
	hsbase = hsp->addr;

#ifdef	foo
	outportb(hsbase+4,0x8+0x10);	/* HIT EAGLE INTACK */
	(void)inportb(hsbase+CHANA+CTL,R0);
	outportb(hsbase+4,0x8);		/***/
#endif

	/* Read interrupt status from channel A */
	while((iv = read_scc(hsbase+CHANA+CTL,R3)) != 0){
		if(iv & CHARxIP){
			/* Channel A Rcv Interrupt Pending */
			hp = &Hdlc[2*dev];
			hrxint(hp);
		} else if(iv & CHATxIP){
			/* Channel A Transmit Int Pending */
			hp = &Hdlc[2*dev];
			htxint(hp);
		} else if(iv & CHAEXT){
			/* Channel A External Status Int */
			hp = &Hdlc[2*dev];
			hexint(hp);
		} else if(iv & CHBRxIP){
			/* Channel B Rcv Interrupt Pending */
			hp = &Hdlc[(2*dev)+1];
			hrxint(hp);
		} else if(iv & CHBTxIP){
			/* Channel B Transmit Int Pending */
			hp = &Hdlc[(2*dev)+1];
			htxint(hp);
		} else if(iv & CHBEXT){
			/* Channel B External Status Int */
			hp = &Hdlc[(2*dev)+1];
			hexint(hp);
		}
		/* Reset interrupt pending state */
		write_scc(hp->ctl,R0,RES_H_IUS);
		outportb(hsbase+CHANA+CTL,0);	/* Restore pointer to 0 */
		outportb(hsbase+CHANB+CTL,0);	/* Restore pointer to 0 */
	}
	outportb(hsbase+CHANA+CTL,0);	/* Restore pointer to 0 */
	outportb(hsbase+CHANB+CTL,0);	/* Restore pointer to 0 */
	return hsp->chain ? hsp->save.vec : NULL;
}
/* HDLC SIO External/Status interrupts
 * The only one that can happen in this driver is a DCD change
 */
static void
hexint(hp)
register struct hdlc *hp;
{
	struct mbuf *rcvbuf;
	char *cp;
	int cnt,data;
	register int ctl;

	ctl = hp->ctl;
	data = hp->data;
	hp->exints++;

	/* Allocate a receive buffer */
	if((rcvbuf = alloc_mbuf(hp->bufsiz+sizeof(struct iface *))) == NULL){
		/* Alloc failed; refuse to proceed */
		hp->nomem++;
		write_scc(ctl,R3,ENT_HM|RxENABLE|RxCRC_ENAB|Rx8);
		write_scc(ctl,R0,RES_EXT_INT);
		return;
	}
	/* Allow space for descriptor on front */
	rcvbuf->data += sizeof(struct iface *);
	cnt = 0;

	/* Disable DCDIE bit so we can track changes in DCD */
	write_scc(ctl,R15,0);

	write_scc(ctl,R3,ENT_HM|RxENABLE|RxCRC_ENAB|Rx8);
	flushrx(data);
	while((cnt = rx8530(ctl,data,cp,hp->bufsiz)) != -1){
		if(cnt > 4){
			/* Good frame */
			hp->good++;
			/* Toss crc */
			rcvbuf->cnt = cnt - 1;
			net_route(hp->iface,&rcvbuf);
			/* Replenish buffer */
			rcvbuf = alloc_mbuf(hp->bufsiz + sizeof(struct iface *));
		}
		/* Start new buffer */
		if(rcvbuf == NULL)
			break;	/* alloc failed */
		rcvbuf->data +=  sizeof(struct iface *);
	}	
	write_scc(ctl,R0,RES_EXT_INT);
	write_scc(ctl,R15,DCDIE);	/* Re-enable DCD */
	write_scc(ctl,R3,ENT_HM|RxENABLE|RxCRC_ENAB|Rx8);

	/* Get rid of fragmentary buffer */
	free_p(&rcvbuf);
}
static void
flushrx(data)
register uint16 data;
{
	register int i = 5;
	while(i-- != 0)
		(void)inportb(data);
}
/* HDLC receiver interrupt handler.
 * Not used in this driver
 */
static void
hrxint(hp)
register struct hdlc *hp;
{
}
/* HDLC transmit interrupt service routine
 * Not used in this driver
 */
static void
htxint(hp)
register struct hdlc *hp;
{
}

/* (re)Initialize HDLC controller parameters */
static void
hdlcparam(hp)
register struct hdlc *hp;
{
	register uint16 ctl;
	int i_state;

	/* Initialize 8530 channel for SDLC operation */
	ctl = hp->ctl;
	i_state = dirps();

#ifdef	foo
	switch(ctl & 2){
	case CHANA:
		write_scc(ctl,R9,CHRA);	/* Reset channel A */
		break;
	case CHANB:
		write_scc(ctl,R9,CHRB);	/* Reset channel B */
		break;
	}
	ppause(1L);	/* Allow plenty of time for resetting */
#endif

	/* Deselect interrupts for now */
	write_scc(ctl,R1,0);
	write_scc(ctl,R15,0);

	/* X1 clock, SDLC mode, Sync modes enable, parity disable */
	write_scc(ctl,R4,X1CLK | SDLC | SYNC_ENAB);

	/* CRC preset 1, NRZ encoding, no active on poll, flag idle,
	 * flag on underrun, no loop mode, 8 bit sync
	 */
	write_scc(ctl,R10,CRCPS|NRZ);

	/* 8530 gets both tx and rx clock from modem.
	 * By default, TRxC = transmit clock, RTxC = receive clock
	 * (swapped 11 Feb 1990 to use new DRSI wiring) UNLESS
	 * the 'r' parameter is specified
	 */
	if(!hp->clkrev)
		write_scc(ctl,R11,RCRTxCP | TCTRxCP);
	else
		write_scc(ctl,R11,RCTRxCP | TCRTxCP);

	/* Note: baud rate generator not used */

	/* Null out SDLC start address */
	write_scc(ctl,R6,0);

	/* SDLC flag */
	write_scc(ctl,R7,FLAG);

	/* DTR On, 8 bit TX chars, no break, TX enable, SDLC CRC,
	 * RTS off, TxCRC enable
	 */
	write_scc(ctl,R5,DTR|Tx8|TxENAB|TxCRC_ENAB);

	/* 8 bit RX chars, auto enables off, no hunt mode, RxCRC enable,
	 * no address search, no inhibit sync chars, disable RX. Rx is
	 * started only by an actual DCD interrupt
	 */
	write_scc(ctl,R3,RxENABLE|RxCRC_ENAB|Rx8);

	/* Dummy interrupt vector
	 * (This probably isn't necessary)
	 */
	write_scc(ctl,R2,0);

	/* Enable only the external interrupts (modem interrupts) since
	 * polling is used for all actual tx/rx operations
	 */
	write_scc(ctl,R1,EXT_INT_ENAB);

	/* Enable only DCD interrupts */
	write_scc(ctl,R15,DCDIE);

	/* No reset, status low, master int enable, enable lower chain,
	 * no vector
	 */
	write_scc(ctl,R9,MIE|NV);

	restore(i_state);
}
/* Attach a high speed iterface to the system
 * argv[0]: hardware type, must be "hs"
 * argv[1]: I/O address, e.g., "0x380"
 * argv[2]: vector, e.g., "2"
 * argv[3]: mode, must be "ax25"
 * argv[4]: interface base label, e.g., "drsi0". Driver appends "a" and "b".
 * argv[5]: receiver packet buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: keyup delay, milliseconds
 * argv[8]: persistence value, 0-255
 * argv[9]: "r" to reverse sense of clock leads (optional)
 */
int
hs_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *if_hsa,*if_hsb;
	struct hdlc *hp;
	int dev;
	char *cp;

	if(Nhs >= NHS){
		printf("Too many hs controllers\n");
		return -1;
	}
	if(if_lookup(argv[4]) != NULL){
		printf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	if(setencap(NULL,argv[3]) == -1){
		printf("Unknown encapsulation %s\n",argv[3]);
		return -1;
	}
	if(Mycall[0] == '\0'){
		printf("set mycall first\n");
		return -1;
	}		
	dev = Nhs++;

	/* Initialize hardware-level control structure */
	Hs[dev].addr = htoi(argv[1]);
	Hs[dev].vec = atoi(argv[2]);
	if(strchr(argv[2],'c') != NULL)
		Hs[dev].chain = 1;
	else
		Hs[dev].chain = 0;

	/* Save original interrupt vector */
	Hs[dev].save.vec = getirq(Hs[dev].vec);
	/* Set new interrupt vector */
	if(setirq(Hs[dev].vec,Hshandle[dev]) == -1){
		printf("IRQ %u out of range\n",Hs[dev].vec);
		Nhs--;
		return -1;
	}
	/* Create interface structures and fill in details */
	if_hsa = (struct iface *)callocw(1,sizeof(struct iface));
	if_hsb = (struct iface *)callocw(1,sizeof(struct iface));

	if_hsa->addr = if_hsb->addr = Ip_addr;
	if_hsa->name = mallocw(strlen(argv[4])+2);
	strcpy(if_hsa->name,argv[4]);
	strcat(if_hsa->name,"a");
	if_hsb->name = mallocw(strlen(argv[4])+2);
	strcpy(if_hsb->name,argv[4]);
	strcat(if_hsb->name,"b");
	if_hsb->mtu = if_hsa->mtu = atoi(argv[6]);
	if_hsa->dev = 2*dev;
	if_hsb->dev = 2*dev + 1;
	if_hsb->stop = if_hsa->stop = hs_stop;
	if_hsb->raw = if_hsa->raw = hs_raw;
	if_hsa->ioctl = if_hsb->ioctl = hs_ctl;

	setencap(if_hsa,argv[3]);
	setencap(if_hsb,argv[3]);
	if(if_hsb->hwaddr == NULL)
		if_hsb->hwaddr = mallocw(AXALEN);
	memcpy(if_hsb->hwaddr,Mycall,AXALEN);
	if(if_hsa->hwaddr == NULL)
		if_hsa->hwaddr = mallocw(AXALEN);
	memcpy(if_hsa->hwaddr,Mycall,AXALEN);
	if_hsa->next = if_hsb;
	if_hsb->next = Ifaces;
	Ifaces = if_hsa;

	write_scc(Hs[dev].addr+CHANA+CTL,R9,FHWRES);
	hp = &Hdlc[2*dev+1];
	hp->ctl = Hs[dev].addr + CHANB + CTL;
	hp->data = Hs[dev].addr + CHANB + DATA;
	hp->bufsiz = atoi(argv[5]);
	if(argc > 7)
		hp->txdelay = atol(argv[7]);
	else
		hp->txdelay = 15L;
	if(argc > 8)
		hp->p = atoi(argv[8]);
	else
		hp->p = 64;
	if(argc > 9 && argv[9][0] == 'r')
		hp->clkrev = 1;
	else
		hp->clkrev = 0;
	hp->iface = if_hsb;
	hdlcparam(hp);

	hp = &Hdlc[2*dev];
	hp->ctl = Hs[dev].addr + CHANA + CTL;
	hp->data = Hs[dev].addr + CHANA + DATA;
	hp->bufsiz = atoi(argv[5]);
	hp->txdelay = Hdlc[2*dev+1].txdelay;
	hp->p = Hdlc[2*dev+1].p;
	if(argc > 9 && argv[9][0] == 'r')
		hp->clkrev = 1;
	else
		hp->clkrev = 0;
	hp->iface = if_hsa;
	hdlcparam(hp);

	outportb(Hs[dev].addr + 4,0x08);	/*EAGLE INT GATE */
	/* Clear mask (enable interrupt) in 8259 interrupt controller */
	maskon(Hs[dev].vec);

	/* Initialize timing delay loop */
	init_delay();
	cp = if_name(if_hsa," tx");
	if_hsa->txproc = newproc(cp,512,if_tx,0,if_hsa,NULL,0);
	free(cp);
	cp = if_name(if_hsb," tx");
	if_hsb->txproc = newproc(cp,512,if_tx,0,if_hsb,NULL,0);
	free(cp);
	return 0;
}
static int
hs_stop(iface)
struct iface *iface;
{
	int dev;

	dev = iface->dev;
	if(dev & 1)
		return -1;	/* Valid only for the first device */
	dev >>= 1;	/* Convert back into hs number */

	/* Turn off interrupts */
	maskoff(Hs[dev].vec);

	/* Restore original interrupt vector */
	setirq(Hs[dev].vec,Hs[dev].save.vec);

	/* Force hardware reset */
	write_scc(Hs[dev].addr + CHANA+CTL,R9,FHWRES);
	return 0;
}
/* Send raw packet */
static int
hs_raw(
struct iface *iface,
struct mbuf **bpp
){

	struct hdlc *hp;
	struct mbuf *nbp;
	register uint16 cnt;
	register uint8 *cp;
	uint16 ctl,data;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();
	hp = &Hdlc[iface->dev];
	hp->txpkts++;

	ctl = hp->ctl;
	data = hp->data;

	cnt = len_p(*bpp);
	/* If buffer isn't contiguous (which is almost always
	 * the case) copy it to a new buffer for speed
	 */
	if((*bpp)->next != NULL){
		if((nbp = copy_p(*bpp,cnt)) == NULL){
			hp->nomem++;
			free_p(bpp);
			return -1;
		}
		free_p(bpp);
		*bpp = nbp;
	}
	cp = (*bpp)->data;
	/* Turn transmitter on */
	hstxon(hp);
	/* Initialize transmitter CRC */
	write_scc(ctl,R0,RES_Tx_CRC);
	for(;;){
		/* Wait for the transmitter to become ready */
		while(!(inportb(ctl) & Tx_BUF_EMP))
			;
		if(cnt-- == 0)
			break;
		outportb(data,*cp++); /* Send the character */
	}
	write_scc(ctl,R0,RES_EOM_L);	/* Allow CRC generation */
	/* End of frame. Wait for TxEOM to go high, indicating start of
	 * CRC transmission. Note that we don't reset the transmit
	 * interrupt pending flag as one ordinarily would, since we're
	 * not using tx interrupts.
	 */
	while(!(inportb(ctl) & TxEOM))
		;

	free_p(bpp);
	hstxoff(hp);	/* Shut down tx */
	/* Hold off to give other guy a chance to
	 * respond
	 */
	hp->deftime = msclock() + hp->txdelay + 500;
	return 0;
}

/* Turn on high speed transmitter. Does p-persistence, then sends a dummy
 * frame to allow for keyup delay. Returns with transmitter on and interrupts
 * disabled
 */
static void
hstxon(hp)
struct hdlc *hp;
{
	uint16 ctl;
	int i;
	long ca;
	int32 t;

	ctl = hp->ctl;

	/* Defer logic. Wait until deftime is in the past (so we
	 * defer to any overheard CTS messages) AND the p-persistence
	 * dice roll succeeds. The computation of ca allows for clock
	 * rollover (which happens every 49+ days).
	 */
	for(;;){
		t = msclock();
		ca = hp->deftime - t;
		if(ca > 0){
			ppause(ca);
			continue;
		}
		hp->deftime = t;	/* Keep from getting too old */
		if((rand() & 0xff) > hp->p){
			ppause((long)MSPTICK);
			continue;
		}
		break;
	}
	/* Prevent distractions. In particular, block off the DCD interrupt
	 * so we don't hear our own carrier and hang in the interrupt handler!
	 * Note that simply disabling CPU interrupts isn't enough since
	 * the call to ppause will block and the kernel will re-enable
	 * them.
	 */
	write_scc(ctl,R9,0);	/* Disable all SCC interrupts */
	disable();

	/* Turn on carrier, enable transmitter */
	write_scc(ctl,R5,TxCRC_ENAB | RTS | TxENAB | Tx8 | DTR);

	/* Delay for keyup interval */
	for(i=hp->txdelay;i != 0;i--)
		msdelay();

}
/* Turn transmitter off at the end of a series of frames */
static void
hstxoff(hp)
struct hdlc *hp;
{
	int cnt;
	uint16 ctl,data;

	ctl = hp->ctl;
	data = hp->data;
	/* To allow the SCC buffering to drain, we begin a dummy frame,
	 * then abort it
	 */
	for(cnt=5;cnt != 0;cnt--){
		while(!(inportb(ctl) & Tx_BUF_EMP))
			;
		outportb(data,0);
	}
	write_scc(ctl,R0,SEND_ABORT);

	/* Turn off carrier and disable transmitter */
	write_scc(ctl,R5,TxCRC_ENAB | Tx8 | DTR);
	/* Re-Enable SCC interrupts */
	write_scc(ctl,R9,MIE|NV);		
	enable();	/* Turn interrupts back on */
}

int
dohs(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register int i;
	register struct hdlc *hp;

	for(i=0;i<2*Nhs;i++){
		hp = &Hdlc[i];
		printf("port %d: txpkts %lu ints %lu rxpkts %lu rxbytes %lu nomem %lu toobig %lu crcerr %lu aborts %lu overrun %lu\n",
		 i,hp->txpkts,hp->exints,hp->good,hp->rxbytes,
		 hp->nomem,hp->toobig,hp->crcerr,hp->aborts,
		 hp->overrun);
	}
	return 0;
}
static int32
hs_ctl(iface,cmd,set,val)
struct iface *iface;
int cmd;
int set;
int32 val;
{
	register struct hdlc *hp;
	int32 t,ca;

	hp = &Hdlc[iface->dev];
	switch(cmd){
	case PARAM_TXDELAY:	/* Tx keyup delay */
		if(set)
			hp->txdelay = val;
		return hp->txdelay;
	case PARAM_PERSIST:
		if(set)
			hp->p = val;
		return hp->p;
	case PARAM_MUTE:
		/* Mute transmitter for specified # of ms */
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
#ifdef	notdef		/* replaced with assembler in 8530.asm */
/* Read data from the 8530 receiver.
 * Returns when either a good frame is received, or when carrier drops.
 * If a good frame is received, the length is returned; otherwise -1.
 */
int
rx8530(ctl,data,buf,bufsize)
uint16 ctl,data;
char *buf;
uint16 bufsize;
{
	int cnt = 0;
	register char status;
	char error;
	register char *cp = buf;

	for(;;){
		status = inportb(ctl);
		if(!(status & DCD)){
			cnt = -1;
			break;
		} else if(status & BRK_ABRT){
			cp = buf;
			cnt = 0;
		} else if(status & Rx_CH_AV){
			/* Receive character is ready, get it */
			*cp++ = inportb(data);
			if(++cnt > bufsize){
				/* Buffer overflow, start again */
				write_scc(ctl,R3,ENT_HM|RxENABLE|RxCRC_ENAB|Rx8);
				cp = buf;
				cnt = 0;
			}
		} else if((error = read_scc(ctl,R1)) & END_FR){
			if(!(error & CRC_ERR))
				break;	/* Good frame! */
			/* Bad frame, start again */
			cp = buf;
			cnt = 0;
		}
	}
	return cnt;
}
#endif

static int32 Del_const;

/* Find the value of Del_const that will cause one execution of mloop()
 * to take one millisecond
 */
static void
init_delay()
{
	int32 start,delay;
	register int i,j;
	int success = 0;

	/* Start with small value to make things tolerable on slow machines */
	Del_const = 10;
	printf("Del_const = %lu\n",Del_const);
	/* Limit the number of iterations in case we don't converge */
	for(i=0;i<5;i++){
		start = msclock();
		for(j=0;j<1000;j++)
			msdelay();
		delay = msclock()-start;
		printf("delay %lu\n",delay);
		if(delay == 0){
			/* Too fast for accurate measurement on coarse clk */	
			Del_const *= 10;
			printf("Del_const = %lu\n",Del_const);
			continue;
		}
		Del_const = (Del_const * 1000)/delay;
		printf("Del_const = %lu\n",Del_const);
		if(delay > 950 && delay < 1050){
			success = 1;
			break;	/* Within 1 tick - Close enough */
		}
	}
	if(!success)
		printf("HS: Warning: auto delay set failed\n");
}
/* Delay for one millisecond (once calibrated by init_delay()) */
static void
msdelay()
{
	int32 i;

	for(i=Del_const;i !=0;i--)
		;
}
