/* Interface driver for the PACCOMM PC-100 board for the IBM PC */
/* UNFINISHED, DOESN'T WORK YET - work in progress by Bdale */
/* currently only attempting to use the AMD7910 on Channel A */

#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "pc100.h"
#include "z8530.h"
#include "ax25.h"
#include "trace.h"
#include "nospc.h"

static void hspint(struct hdlc *hp);
static void hexint(struct hdlc *hp);
static void hrxint(struct hdlc *hp);
static void htxint(register struct hdlc *hp);
static void rts(uint16 base,int x);
static void hdlcparam(struct hdlc *hp);
static int pc_raw(struct iface *iface,struct mbuf **bpp);
static int pc_stop(struct iface *iface);

static struct pc100 Pc100[NPC];
static INTERRUPT (*Pchandle[])() = { pc0vec };
static struct hdlc Hdlc[2*NPC];
static uint16 Npc;

/* Branch table for interrupt handler */
static void (*Svec[])(struct hdlc *hp) = {
	htxint, hexint, hrxint, hspint
};

/* Master interrupt handler for the PC-100 card. All interrupts come
 * here first, then are switched out to the appropriate routine.
 */
INTERRUPT (far *(pcint)(dev))()
int dev;
{
	register char iv;
	register uint16 pcbase;
	struct hdlc *hp;
	struct pc100 *pcp;

	pcp = &Pc100[dev];
	pcp->ints++;
	pcbase = pcp->addr;

	/* Read interrupt vector, including status, from channel B */
	iv = read_scc(CTL+pcbase+CHANB,R2);

	hp = &Hdlc[2 * dev + ((iv & 0x80)? 0 : 1)];

	/* Now switch to appropriate routine */
	(*Svec[(iv>>1) & 0x3])(hp);

	/* Reset interrupt pending state (register A only) */
	write_scc(CTL+pcbase+CHANA,R0,RES_H_IUS);

	/* Wang the 8530 hardware interrupt acknowledge line - Bdale */
	inportb(pcbase+INTACK);

	return pcp->chain ? pcp->oldvec : NULL;
}
/* HDLC Special Receive Condition interrupt
 * The most common event that triggers this interrupt is the
 * end of a frame; it can also be caused by a receiver overflow.
 */
static void
hspint(hp)
register struct hdlc *hp;
{
	register char c;

	hp->spints++;
	c = read_scc(CTL+hp->base,R1);	/* Fetch latched bits */

	if((c & (END_FR|CRC_ERR)) == END_FR && hp->rcvbuf != NULL
		&& hp->rcvbuf->cnt > 1){
		/* End of valid frame */
		hp->rcvbuf->cnt--;	/* Toss 1st crc byte */
		enqueue(&hp->rcvq,&hp->rcvbuf);
		hp->rcvbuf = NULL;
		hp->rcvcnt++;
	} else {
		/* An overflow or CRC error occurred; restart receiver */
		hp->crcerr++;
		if(hp->rcvbuf != NULL){
			hp->rcp = hp->rcvbuf->data;
			hp->rcvbuf->cnt = 0;
		}
	}
	write_scc(CTL+hp->base,R0,ERR_RES);
}
/* HDLC SIO External/Status interrupts
 * The only one of direct interest is a receiver abort; the other
 * usual cause is a change in the modem control leads, so kick the
 * transmit interrupt routine.
 */
static void
hexint(hp)
register struct hdlc *hp;
{
	hp->exints++;
	hp->status = read_scc(CTL+hp->base,R0);	/* Fetch status */
	if((hp->status & BRK_ABRT) && hp->rcvbuf != NULL){
		hp->aborts++;
		/* Restart receiver */
		hp->rcp = hp->rcvbuf->data;
		hp->rcvbuf->cnt = 0;
	}
	write_scc(CTL+hp->base,R0,RES_EXT_INT);
	write_scc(CTL+hp->base,R0,RES_H_IUS);
	/* Kick the transmit interrupt routine for a possible modem change */
	htxint(hp);
}
/* HDLC receiver interrupt handler. Allocates buffers off the freelist,
 * fills them with receive data, and puts them on the receive queue.
 */
static void
hrxint(hp)
register struct hdlc *hp;
{
	struct mbuf *bp;
	register uint16 base;

	hp->rxints++;
	base = hp->base;
	/* Allocate a receive buffer if not already present */
	if((bp = hp->rcvbuf) == NULL){
		bp = hp->rcvbuf = alloc_mbuf(hp->bufsiz);
		if(bp == NULL){
			/* No memory, abort receiver */
			hp->nomem++;
			write_scc(CTL+base,R3,ENT_HM|RxENABLE|RxCRC_ENAB|Rx8);
			(void) inportb(base+DATA);
			return;
		}
		hp->rcp = hp->rcvbuf->data;
	}
	while(read_scc(CTL+base,R0) & Rx_CH_AV){
		if(bp->cnt++ >= hp->bufsiz){
			/* Too large; abort the receiver, toss buffer */
			hp->toobig++;
			write_scc(CTL+base,R3,ENT_HM|RxENABLE|RxCRC_ENAB|Rx8);
			(void) inportb(base+DATA);
			free_p(&bp);
			hp->rcvbuf = NULL;
			break;
		}
		/* Normal save */
		*hp->rcp++ = inportb(base+DATA);
	}
}
static int ctswait;
/* HDLC transmit interrupt service routine
 *
 * The state variable tstate, along with some static pointers,
 * represents the state of the transmit "process".
 */
static void
htxint(hp)
register struct hdlc *hp;
{
	register uint16 base;
	int c;
	int i_state;

	hp->txints++;
	base = hp->base;
	i_state = dirps();
	while(read_scc(CTL+base,R0) & Tx_BUF_EMP){
		switch(hp->tstate){
		/* First here for efficiency */
		case ACTIVE:		/* Sending frame */
			if((c = PULLCHAR(&hp->sndbuf)) != -1){
				outportb(base+DATA,c);
			} else {
				/* Do this after sending the last byte */
				write_scc(CTL+base,R0,RES_Tx_P);
				if((hp->sndbuf = dequeue(&hp->sndq)) == NULL){
					switch(hp->mode){
					case CSMA:
						/* Begin transmitter shutdown */
						hp->tstate = FLUSH;
						break;
					case FULLDUP:
						hp->tstate = IDLE;
						break;
					}
				}
			}
			continue;
		case IDLE:
			/* Transmitter idle. Find a frame for transmission */
			if((hp->sndbuf = dequeue(&hp->sndq)) == NULL)
				goto ret;

		case DEFER:	/* note fall-thru */
			if(hp->mode == CSMA && (hp->status & DCD)){
				hp->tstate = DEFER;
				goto ret;
			}
			rts(base,ON);	/* Transmitter on */
		case KEYUP:	/* note fall-thru */
			if((hp->status & CTS) == 0){
				ctswait++;
				hp->tstate = KEYUP;
				goto ret;
			}
			write_scc(CTL+base,R0,RES_Tx_CRC);
			c = PULLCHAR(&hp->sndbuf);
			outportb(hp->base+DATA,c);
			hp->tstate = ACTIVE;
			write_scc(CTL+base,R0,RES_EOM_L);
			continue;
		case FLUSH:	/* Sending flush character */
			outportb(hp->base+DATA,0);
			hp->tstate = FIN2;
			continue;
		case FIN2:
			write_scc(CTL+base,R0,SEND_ABORT);
			hp->tstate = IDLE;
			rts(base,OFF);
			write_scc(CTL+base,R0,RES_Tx_P);
			continue;
		}
	}
ret:	restore(i_state);
}

/* Set request-to-send on modem */
static void
rts(base,x)
uint16 base;
int x;
{
	uint16 cmd;

	if(x)
		cmd = TxCRC_ENAB | RTS | TxENAB | Tx8 | DTR;
	else
		cmd = TxCRC_ENAB | TxENAB | Tx8 | DTR;
	write_scc(CTL+base,R5,cmd);
}
/* (re)Initialize HDLC controller parameters */
static void
hdlcparam(hp)
register struct hdlc *hp;
{
	uint16 tc;
	register uint16 base;
	int i_state;

	/* Initialize 8530 channel for SDLC operation */
	base = hp->base;
	i_state = dirps();

	switch(base & 2){
	case 0:
		write_scc(CTL+base,R9,CHRA);	/* Reset channel A */
		break;
	case 2:
		write_scc(CTL+base,R9,CHRB);	/* Reset channel B */
		break;
	}
	/* Wait/DMA disable, Int on all Rx chars + spec condition,
	 * parity NOT spec condition, TxINT enable, Ext Int enable
	 */
	write_scc(CTL+base,R1,INT_ALL_Rx | TxINT_ENAB | EXT_INT_ENAB);

	/* Dummy interrupt vector, will be modified by interrupt type
	 * (This probably isn't necessary)
	 */
	write_scc(CTL+base,R2,0);

	/* 8 bit RX chars, auto enables off, no hunt mode, RxCRC enable,
	 * no address search, no inhibit sync chars, enable RX
	 */
	write_scc(CTL+base,R3,Rx8|RxCRC_ENAB|RxENABLE);

	/* X1 clock, SDLC mode, Sync modes enable, parity disable
	 * (Note: the DPLL does a by-32 clock division, so it's not necessary
	 * to divide here).
	 */
	write_scc(CTL+base,R4,X1CLK | SDLC | SYNC_ENAB);

	/* DTR On, 8 bit TX chars, no break, TX enable, SDLC CRC,
	 * RTS off, TxCRC enable
	 */
	write_scc(CTL+base,R5,DTR|Tx8|TxENAB|TxCRC_ENAB);

	/* SDLC flag */
	write_scc(CTL+base,R7,FLAG);

	/* No reset, status low, master int enable, enable lower chain,
	 * no vector, vector includes status
	 */
	write_scc(CTL+base,R9,MIE|NV|VIS);
	/* CRC preset 1, NRZI encoding, no active on poll, flag idle,
	 * flag on underrun, no loop mode, 8 bit sync
	 */
	write_scc(CTL+base,R10,CRCPS|NRZI);

	/* Board no longer channel-specific for clk.  The board should be set
	 * up to run from the 4.9152Mhz onboard crystal connected to PCLK.
	 * Both channels get receive clock at 32x from PCLK via the DPLL,
	 * with TRxC as an output, via a 4040 div by 32 counter to RTxC set
	 * us as an input to provide the transmit clock.
	 */

	/*            TRxC = BR Generator Output, TRxC O/I,
	 *	      transmit clock = RTxC pin, 
	 *	      receive clock = DPLL output
	 */
	write_scc(CTL+base,R11,TRxCBR|TRxCOI|TCRTxCP|RCDPLL);

	/* Compute and load baud rate generator time constant
	 * DPLL needs x32 clock
	 * XTAL is defined in pc100.h to be the crystal clock / (2 * 32)
	 */
	tc = XTAL/(hp->speed) - 2;
	write_scc(CTL+base,R12,tc);
	write_scc(CTL+base,R13,tc >> 8);

	write_scc(CTL+base,R14,SNRZI);	/* Set NRZI mode */
	write_scc(CTL+base,R14,SSBR);	/* Set DPLL source = BR generator */
	write_scc(CTL+base,R14,SEARCH);	/* Enter search mode */
	/* Set baud rate gen source = PCLK, enable baud rate gen */
	write_scc(CTL+base,R14,BRENABL|BRSRC);

	/* Break/abort IE, TX EOM IE, CTS IE, no SYNC/HUNT IE, DCD IE,
	 * no Zero Count IE
	 */
	write_scc(CTL+base,R15,BRKIE|TxUIE|CTSIE|DCDIE);

	restore(i_state);
	if(hp->mode == FULLDUP){
		rts(base,ON);
	} else if(hp->tstate == IDLE){
		rts(base,OFF);
	}
}
/* Attach a PC-100 interface to the system
 * argv[0]: hardware type, must be "pc100"
 * argv[1]: I/O address, e.g., "0x380"
 * argv[2]: vector, e.g., "2"
 * argv[3]: mode, must be:
 *	    "ax25ui" (AX.25 UI frame format)
 *	    "ax25i" (AX.25 I frame format)
 * argv[4]: interface label, e.g., "pc0"
 * argv[5]: receiver packet buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "9600"
 * argv[8]: First IP address, optional (defaults to Ip_addr)
 * argv[9]: Second IP address, optional (defaults to Ip_addr)
 */
int
pc_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *if_pca,*if_pcb;
	struct hdlc *hp;
	int dev;
	char *cp;

	if(Npc >= NPC){
		printf("Too many pc100 controllers\n");
		return -1;
	}
	if(if_lookup(argv[4]) != NULL){
		printf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	if(setencap(NULL,argv[3]) == -1){
		printf("Mode %s unknown for interface %s\n",
			argv[3],argv[4]);
		return -1;
	}
	if(Mycall[0] == '\0'){
		printf("set mycall first\n");
		return -1;
	}
	dev = Npc++;

	/* Initialize hardware-level control structure */
	Pc100[dev].addr = htoi(argv[1]);
	Pc100[dev].vec = atoi(argv[2]);
	if(strchr(argv[2],'c') != NULL)
		Pc100[dev].chain = 1;
	else
		Pc100[dev].chain = 0;

	/* Initialize modems */
	outportb(Pc100[dev].addr + MODEM_CTL,0x22);

	/* Save original interrupt vector */
	Pc100[dev].oldvec = getirq(Pc100[dev].vec);
	/* Set new interrupt vector */
	if(setirq(Pc100[dev].vec,Pchandle[dev]) == -1){
		printf("IRQ %u out of range\n",Pc100[dev].vec);
		Npc--;
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
	if_pca->name = strdup(argv[4]);
	if_pcb->name = strdup(argv[4]);
	if_pcb->name[strlen(argv[4]) - 1]++;	/* kludge */
	if_pcb->mtu = if_pca->mtu = atoi(argv[6]);
	if_pca->dev = 2*dev;
	if_pcb->dev = 2*dev + 1;
	if_pcb->stop = if_pca->stop = pc_stop;
	if_pcb->raw = pc_raw;

	setencap(if_pca,argv[3]);
	setencap(if_pcb,argv[3]);
	if(if_pcb->hwaddr == NULL)
		if_pcb->hwaddr = mallocw(AXALEN);
	memcpy(if_pcb->hwaddr,Mycall,AXALEN);

	if_pca->next = if_pcb;
	if_pcb->next = Ifaces;
	Ifaces = if_pca;

	hp = &Hdlc[2*dev+1];
	hp->speed = (uint16)atoi(argv[7]);
	hp->base = Pc100[dev].addr + CHANB;
	hp->bufsiz = atoi(argv[5]);
	hdlcparam(hp);

	hp = &Hdlc[2*dev];
	hp->speed = (uint16)atoi(argv[7]);
	hp->base = Pc100[dev].addr + CHANA;
	hp->bufsiz = atoi(argv[5]);
	hdlcparam(hp);

	/* Clear mask (enable interrupt) in 8259 interrupt controller */
	clrbit(INTMASK,(char)(1<<Pc100[dev].vec));

	cp = if_name(if_pca," tx");
	if_pca->txproc = newproc(cp,512,if_tx,0,if_pca,NULL,0);
	free(cp);
	cp = if_name(if_pcb," tx");
	if_pcb->txproc = newproc(cp,512,if_tx,0,if_pcb,NULL,0);
	free(cp);
	
	return 0;
}
static int
pc_stop(iface)
struct iface *iface;
{
	int dev;

	dev = iface->dev;
	if(dev & 1)
		return 0;
	dev >>= 1;	/* Convert back into PC100 number */
	/* Turn off interrupts */
	maskoff(Pc100[dev].vec);

	/* Restore original interrupt vector */
	setirq(Pc100[dev].vec,Pc100[dev].oldvec);

	/* Force hardware reset */
	write_scc(CTL+Pc100[dev].addr + CHANA,R9,FHWRES);
	return 0;
}
	
/* Send raw packet on PC-100 */
static int
pc_raw(
struct iface *iface,
struct mbuf **bpp
){
	char kickflag;
	struct hdlc *hp;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();
	hp = &Hdlc[iface->dev];
	kickflag = (hp->sndq == NULL);
	enqueue(&hp->sndq,bpp);
	if(kickflag)
		htxint(&Hdlc[iface->dev]);
	return 0;
}
