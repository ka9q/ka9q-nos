/*  Driver for HAPN-1 8273 card on PC
 *  Jon Bloom, KE3Z; adapted from KA9Q's PC-100 driver
 *  Modified Rx interrupt routine to prevent lockup
 *  John Tanner VK2ZXQ 6th Feb 1988
 *  Adapted back into 871225.9 by KA9Q 15 Feb 1988
 */
#include <stdio.h>
#include <dos.h>
#include "global.h"
#include <stdarg.h>
#include "timer.h"
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "hapn.h"
#include "ax25.h"
#include "trace.h"
#include "nospc.h"
#include "proc.h"

static void cmd_8273(uint16 base,uint8 cmd,int np,...);
static int hapn_init(struct hapn *hp);
static int hapn_raw(struct iface *iface,struct mbuf **bpp);
static int hapn_stop(struct iface *iface);
static int hcdchk(uint16 base);
static void hrxint(struct hapn *hp);
static void hrxgo(struct hapn *hp);
static void htxint(void *p);

static struct hapn Hapn[NHAPN];
static INTERRUPT (*H_handle[])() = { ha0vec };
static uint16 Nhapn;

/*  send command to the 8273
 *  "base" = base port of 8273
 *  "cmd"  = command byte
 *  "np"   = number of parameter bytes
 *  "p1"   = first parameter (parameters are int)
 */
static void
cmd_8273(uint16 base, uint8 cmd, int np, ...)
{
	int p;
	va_list ap;

	while(inportb(base+STA) & CBSY)
		;
	outportb(base+CMD, cmd);

	va_start(ap,np);
	while(np--){
		while(inportb(base+STA) & CPBF)
			;
		p = va_arg(ap,int);
		outportb(base+PAR, p);
	}
	va_end(ap);
}
/*  Start receiver of 8273 */
static void
hrxgo(hp)
register struct hapn *hp;
{
	cmd_8273(hp->base, GENERAL_RX, 2, hp->bufsiz & 0xff, hp->bufsiz >> 8);
}

/*  Interrupt service function.  Entered with hapn index
 *  The "flag" variable is used in this routine to indicate a
 *  valid TX or RX interrupt. If an invalid interrupt is detected
 *  the 8273 is reset.
 */
INTERRUPT (far *(haint)(dev))()
int dev;
{
	register struct hapn *hp;
	register uint16 base;
	char flag = 0;

	hp = &Hapn[dev];
	base = hp->base;

	/*  Check for TX interrupt  */
	if(inportb(base+STA) & TXINT){
		flag = 1;	/* Valid interrupt, set flag */
		htxint(hp);
	}
	/*  Check for RX interrupt  */
	if(inportb(base+STA) & RXINT){
		flag = 1;	/* Valid interrupt, set flag */
		hrxint(hp);
	}
	/* Check for unknown interrupt  */
	if(!flag){
		hp->badint++;	/* Increment error counter */
		hapn_init(hp);	/* Reinitialise the 8273 */
	}
	return hp->chain ? hp->oldvec : NULL;
}
/*  RX interrupt service
 *  if status register bit "RXIRA" is set, interrupt is final,
 *  otherwise, interrupt is data request
 */
static void
hrxint(hp)
register struct hapn *hp;
{
	struct mbuf *bp;
	register uint16 base;
	unsigned char results[10];

	hp->rxints++;
	base = hp->base;

	if(inportb(base+STA) & RXIRA){
		/* RX result interrupt
		 * If the result is a good frame 3 bytes need to be read
		 * If an error has occurred only one byte need to be read
		 */

		/* Read first result byte and test for good data */
		if((results[0]=(inportb(base + RXI))) == 0xe0){
			/* Good result; read two more result bytes */
			while((inportb(base + STA) & RXIRA) == 0)
				;
			/* Read second result byte */
			results[1] = inportb(base + RXI);
			/* Wait for third result byte  */
			while((inportb(base + STA) & RXIRA) == 0)
				;  
			results[2] = inportb(base + RXI);/* Read it */

			/* Since this frame is ok put it on the queue */
			net_route(hp->iface,&hp->rcvbuf);
			hp->rframes++;
		} else {
			/* Error termination
			 * Parse RIC and act accordingly
			 * Only one result byte returned on error
			 */
			switch(results[0]){
			case CRCERR:
				hp->crcerr++;
				break;
			case ABORT_DET:
				hp->aborts++;
				break;
			case DMA_OVRN:
				hp->dmaorun++;
				break;
			case MEM_OVFL:
				hp->toobig++;
				break;
			case CD_LOSS:
				hp->cdloss++;
				hapn_init(hp);	/* 8273 reset on cd error */
				break;
			case RX_ORUN:
				hp->rxorun++;
				break;
			}
			/* Throw rx buffer contents away to start over */
			hp->rcp = hp->rcvbuf->data;
			hp->rcvbuf->cnt = 0;
		}
		/* Restart the receiver */
		cmd_8273(base,RX_DISABLE,0);
		hrxgo(hp);
	} else {
		/* RX data interrupt; allocate new rx buffer if none present */
		if((bp = hp->rcvbuf) == NULL){
			bp = hp->rcvbuf = alloc_mbuf(hp->bufsiz);
			if(bp == NULL){
				/* No memory available */
				hp->nomem++;
				cmd_8273(base, RX_DISABLE, 0);
				hrxgo(hp);
				return;
			}
			/* Init buffer pointer */
			hp->rcp = hp->rcvbuf->data;
		}
		/*  Barf if rx data is more than buffer can hold (should never
		 *  happen since 8273 is also counting bytes).
		 */
		if(bp->cnt++ >= hp->bufsiz){
			hp->toobig++;
			cmd_8273(base, RX_DISABLE, 0);
			hrxgo(hp);
			free_p(&bp);
			hp->rcvbuf = NULL;
			return;
		}
		/* Store the received byte */
		*hp->rcp++ = inportb(base+RXD);
	}
}

/*  test for busy channel (CD active)
 *  returns TRUE if channel busy
 */
static int
hcdchk(base)
uint16 base;
{
	int i_state;

	i_state = dirps();
	cmd_8273(base, READ_A, 0);
	while(!(inportb(base+STA) & CRBF))
		;
	restore(i_state);
	return((inportb(base+RES) & CD) != 0);
}

/*  TX interrupt service
 *  if status register bit "TXIRA" is set, interrupt is final,
 *  otherwise, interrupt is data request
 */
static void
htxint(p)
void *p;
{
	register struct hapn *hp;
	register uint16 base;
	uint16 len;
	int c;
	int i_state;

	hp = (struct hapn *)p;
	hp->txints++;
	base = hp->base;
	
	c = 0;
	i_state = dirps();
	if(inportb(base+STA) & TXIRA){		/* TX result interupt */
		hp->tstate = IDLE;
		free_p(&hp->sndbuf);
		hp->sndbuf = NULL;

		/*  Read result  */
		while((inportb(base+STA) & (TXINT | TXIRA)) != (TXINT | TXIRA))
			;
		c = inportb(base+TXI);

		/*  Test for tx abort  */
		switch(c & 0x1f){
		case DMA_URUN:
			hp->t_urun++;
			break;
		case CTS_LOSS:
			hp->ctsloss++;
			break;
		case ABORT_CMPLT:
			hp->taborts++;
			break;
		}
	}
	switch(hp->tstate){
	case IDLE:	/*  See if a buffer is ready to be sent  */
		if((hp->sndbuf = dequeue(&hp->sndq)) == NULL)
			break;

	case DEFER:	/*  Busy-channel check  */
		if(hp->mode == CSMA && (c & 0x1f) != EARLY_TXI){
			if(hcdchk(base)){
				hp->tstate = DEFER;
				start_timer(&hp->defer);
				break;
			}
		}
		/*  Start transmitter  */
		stop_timer(&hp->defer);
		len = len_p(hp->sndbuf);
		cmd_8273(base, TX_FRAME, 2, len & 0xff, len >> 8);
		hp->tstate = ACTIVE;
		hp->tframes++;
		break;
	case ACTIVE:	/*  Get next byte to send  */
		if((c = PULLCHAR(&hp->sndbuf)) == -1){
			cmd_8273(base, ABORT_TXF, 0);
			hp->tstate = IDLE;
		} else
			outportb(base+TXD, c);
		break;
	}
	restore(i_state);
}

/*  Attach a HAPN adaptor to the system
 *  argv[0]:  hardware type, must be "hapn"
 *  argv[1]:  I/O address, e.g. "0x310"
 *  argv[2]:  vector, e.g. "2"
 *  argv[3]:  mode, must be "ax25i" or "ax25ui"
 *  argv[4]:  interface name, e.g. "ha0"
 *  argv[5]:  rx packet buffer size in bytes
 *  argv[6]:  maximum transmission unit in bytes
 *  argv[7]:  channel-access mechanism, "csma" or "full"
 *  argv[8]: IP address, optional (defaults to Ip_addr)
 */
int
hapn_attach(argc, argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *if_h;
	struct hapn *hp;
	int dev, i;
	static struct {
		char *str;
		char type;
	} ch_access [] = { "csma", 0, "full", 1 };
	char *cp;
	int i_state;

	if(Nhapn >= NHAPN){
		printf("Too many HAPN adaptors\n");
		return -1;
	}
	if(if_lookup(argv[4]) != NULL){
		printf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	if(setencap(NULL,argv[3]) == -1){
		printf("Mode %s unknown for interface %s\n", argv[3], argv[4]);
		return -1;
	}
	if(Mycall[0] == '\0'){
		printf("set mycall first\n");
		return -1;
	}
	/*  Create new interface structure  */
	if_h = (struct iface *) callocw(1,sizeof(struct iface));

	/*  Set interface address  */
	if_h->addr = Ip_addr;
	if(argc > 8)
		if_h->addr = resolve(argv[8]);
	if(if_h->addr == 0){
		printf(Noipaddr);
		free(if_h);
		return -1;
	}
	dev = Nhapn++;
	hp = &Hapn[dev];

	/*  Initialize hardware constants */
	hp->base = htoi(argv[1]);
	hp->vec = atoi(argv[2]);
	if(strchr(argv[2],'c') != NULL)
		hp->chain = 1;
	else
		hp->chain = 0;

	/*  Save original interrupt vector  */
	hp->oldvec = getirq(Hapn[dev].vec);

	/*  Set new interrupt vector  */
	setirq(hp->vec, H_handle[dev]);

	/* Continue filling interface structure */
	if_h->name = strdup(argv[4]);
	if_h->mtu = atoi(argv[6]);
	if_h->dev = dev;
	if_h->stop = hapn_stop;
	if_h->raw = hapn_raw;
	hp->iface = if_h;

	setencap(if_h,argv[3]);
	if(if_h->hwaddr == NULL)
		if_h->hwaddr = mallocw(AXALEN);
	memcpy(if_h->hwaddr,Mycall,AXALEN);
	/*  Link the interface into the interface list  */
	if_h->next = Ifaces;
	Ifaces = if_h;

	/*  Fill the local data structure  */
	hp->bufsiz = atoi(argv[5]);
	for(i = 0; i < (sizeof ch_access / sizeof ch_access[0]); i++)
		if(!strcmp(argv[7], ch_access[i].str))
			hp->mode = ch_access[i].type;

	/*  Initialize the hardware  */
	i_state = dirps();
	hapn_init(hp);

	/* Initialize the defer timer */
	set_timer(&hp->defer,MSPTICK);
	hp->defer.func = htxint;
	hp->defer.arg = hp;

	/*  Enable the interrupt  */
	maskon(hp->vec);

	restore(i_state);
	cp = if_name(if_h," tx");
	if_h->txproc = newproc(cp,512,if_tx,0,if_h,NULL,0);
	free(cp);
	return 0;
}

/*  initialize the HAPN adaptor */
static int
hapn_init(hp)
register struct hapn *hp;
{
	register uint16 base;
	int i_state;

	i_state = dirps();
	base = hp->base;

	/*  Reset the 8273 */
	outportb(base+RST, 1);
	outportb(base+RST, 0);
	inportb(base+TXI);		/* Clear any old IR contents */
	inportb(base+RXI);

	/*  Select the operating modes  */
	cmd_8273(base, SET_XFER, 1, 1);
	cmd_8273(base, SET_MODE, 1, HDLC | EARLY | PREFRM | FLG_STM);
	cmd_8273(base, SET_SERIAL, 1, NRZI);
	cmd_8273(base, SET_B, 1, IRQ_ENB | RTS);
	cmd_8273(base, RST_B, 1, 0xff ^ RTS);
	hrxgo(hp);
	restore(i_state);
	return 0;
}

/*  shut down the HAPN adaptor */
static int
hapn_stop(iface)
struct iface *iface;
{
	int dev;
	uint16 base;
	struct hapn *hp;

	dev = iface->dev;
	hp = &Hapn[dev];
	base = hp->base;

	/*  Mask off interrupt input  */
	maskoff(hp->vec);

	/*  Restore original interrupt vector  */
	setirq(hp->vec,hp->oldvec);

	/*  Reset the 8273  */
	outportb(base+RST, 1);
	outportb(base+RST, 0);
	return 0;
}

/* Display adaptor statistics */
int
dohapnstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct hapn *hp;
	int i;

	if(Nhapn == 0){
		printf("No HAPN adaptor attached\n");
		return 1;
	}
	for(i = 0; i < Nhapn; i++){
		hp = &Hapn[i];
		printf("HAPN %d:   rxints: %ld   txints: %ld   badint: %-5d\n", i,
		 hp->rxints,hp->txints,hp->badint);
		printf(" receive  - frames:  %-5d  crcerrs: %-5d  aborts: %-5d  dmaorun: %-5d\n",
		 hp->rframes,hp->crcerr, hp->aborts, hp->dmaorun);
		printf("          - toobig:  %-5d  dcdloss: %-5d  rxorun: %-5d\n",
		 hp->toobig,hp->cdloss,hp->rxorun);
		printf(" transmit - frames:  %-5d  aborts : %-5d  uruns : %-5d  ctsloss: %-5d\n",
		 hp->tframes,hp->taborts, hp->t_urun, hp->ctsloss);
	}
	return 0;
}

/* Send raw packet on HAPN interface */
static int
hapn_raw(
struct iface *iface,
struct mbuf **bpp
){
	struct hapn *hp;

	hp = &Hapn[iface->dev];
	enqueue(&hp->sndq, bpp);

	/*  See if anything being transmitted  */
	if(hp->tstate == IDLE)
		htxint(hp);
	return 0;
}
