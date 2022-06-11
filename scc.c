/* Generic driver for Z8530 boards, modified from the PE1CHL
 * driver for use with NOS. This version also supports the NRS
 * mode when used as an asynch port. Device setup is similar to
 * that of the PE1CHL version, with the addition of user specification
 * of buffer size (bufsize). See the file "scc.txt" for general
 * information on the use of this driver and setup procedures.
 *
 * General differences between this driver and the original version:
 *
 * 1) Slip encoding and decoding is not done in the driver, but
 *    using the routines in slip.c, and these routines are supported
 *    in a manner similar to the asynch routines for the 8250. The
 *    input is handled via fifo buffer, while output is direct. The
 *    routines scc_send and get_scc are called via pointers in the
 *    Slip and Nrs structs for the parcticular channel.
 *
 * 2) The timer routine, scctim, is not installed directly in the
 *    timer interrupt chain, but is called through the systick routine
 *    in pc.c.
 *
 * 3) Facilities of nos are used whenever possible in place of direct
 *    structure or variable manipulation. Mbuf management is handled
 *    this way, along with interface initialization.
 *
 * 4) Nrs mode support is added in a manner similar to that of the
 *    Slip support. I have not had an opportunity to test this, but
 *    it is essentially identical to the way the 8250 version works.
 *
 * 5) Callsign specification on radio modes (kiss,nrs,ax25) is an
 *    option. If not supplied, the value of Mycall will be used.
 *
 * 6) Bufsize specification is now a parameter on setup of each channel.
 *    This is the size of the fifo on asynch input, and the size of
 *    mbuf buffers for sdlc mode. Since the fifo buffer can fill up,
 *    this value should be reasonably large for asynch mode. Mbufs
 *    are chained when they fill up, so having a small bufsize with
 *    sdlc modes (ax25) does not result in loss of characters.
 *
 * 7) Because slip and nrs decoding is handled outside the driver,
 *    sccstat cannot be used to report sent and receive packet counts
 *    in asynch mode, and these fields are blanked on display in asynch
 *    modes.
 *
 *
 * I am interested in setting up some default initializations for
 * the popular Z8530 boards, to minimize user problems in constructing
 * the proper attach init entries. These would allow for shortened
 * entries to use the defaults, such as "attach scc 1 init drsi" to
 * attach a DRSI board in standard configuration at its default address.
 * Since I do not have complete technical information on all such boards,
 * I would very much appreciate any information that users can provide
 * me regarding particular boards.
 *
 * 1/25/90
 *
 * Modifications:
 *
 * 2/17/90:
 *
 * 1) Added mods from PE1CHL which reflect relevent changes to the
 *    scc driver in his version of net between 10/89 and 1/90. Changes
 *    incorporated include additional delays in sccvec.asm, addition
 *    of external clock mode, and initialization for the 8536 as a
 *    clock divider on the DRSI board. "INLINE" is a slight delay
 *    for register access incorporated for use with the inline i/o
 *    code in MSC. This may not be useful or necessary with TURBO.
 *    Changes making "TPS" a variable were not added, since the
 *    scc timer does not install itself on the hardware interrupt
 *    in this version.
 * 
 *
 * Ken Mitchum, KY3B       km@cs.pitt.edu  km@dsl.pitt.edu
 *                             or mail to the tcpip group
 *
 * 5 Aug 91:
 * Support added for Sealevel Systems Inc's ACB-IV 8530 card (HWSEALEVEL)
 * <hdwe> == HWSEALEVEL (0x10) sets the control/status port at chipbase + 4
 * to the value in <param>. (Where the control bits for both side's DTR
 * lines are; the 8530-internal DTR/REQB is used for DMA...)
 *
 * Added a side-effect to ATTACH SCC's <speed> == 'ext'. Previously, all 
 * async modes set a 16X clock; now when external clock is selected, the 
 * baud rate divider in R4 is set to 1X.
 *
 * Tom Jennings, Cygnus Support		(tomj@cygnus.com) 
 *
 *
 * 9 Aug 91:
 * Allow 'v' to specify Van Jacobson TCP header compression (SLIP only).
 * Because the 8th arg is optional [<call>], this checks for 'v' in the
 * 9th arg if that exists, otherwise the 8th arg is used. Not pretty
 * but servicable.
 *
 * Tom Jennings, Cygnus Support		(tomj@cygnus.com) 
 */

/* Added ANSI-style prototypes, reformatted source, minor delinting.
 * Integrated into standard 900201 NOS by KA9Q.
 */

/*
 * Generic driver for Z8530 SCC chip in SLIP, KISS or AX.25 mode.
 *
 * Written by R.E. Janssen (PE1CHL) using material from earlier
 * EAGLE and PC100 drivers in this package.
 *
 * The driver has initially been written for my own Atari SCC interface
 * board, but it could eventually replace the other SCC drivers.
 *
 * Unfortunately, there is little consistency between the different interface
 * boards, as to the use of a clock source, the solution for the fullduplex
 * clocking problem, and most important of all: the generation of the INTACK
 * signal.	Most designs do not even support the generation of an INTACK and
 * the read of the interrupt vector provided by the chip.
 * This results in lots of configuration parameters, and a fuzzy
 * polltable to be able to support multiple chips connected at one interrupt
 * line...
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <dos.h>
#include "global.h"
#include "mbuf.h"
#include "config.h"
#include "netuser.h"
#include "proc.h"
#include "iface.h"
#include "pktdrvr.h"
#include "slip.h"
#include "nrs.h"
#include "n8250.h"
#include "scc.h"
#include "z8530.h"
#include "z8536.h"
#include "ax25.h"
#include "trace.h"
#include "nospc.h"
#include "kiss.h"
#include "devparam.h"

/* interrupt handlers */
extern INTERRUPT sccvec();
extern INTERRUPT sccnovec();

/* variables used by the SCC interrupt handler in sccvec.asm */
static INTERRUPT (*Orgivec)();	/* original interrupt vector */

struct sccinfo Sccinfo = {0};		/* global info about SCCs */
struct sccchan *Sccchan[2 * MAXSCC] = {0}; /* information per channel */
ioaddr Sccvecloc = {0};			/* location to access for SCC vector */
unsigned char Sccmaxvec = {0};		/* maximum legal vector from SCC */
ioaddr Sccpolltab[MAXSCC+1][2] = {0};	/* polling table when no vectoring */

#if defined(INLINE)
static unsigned scc_delay(unsigned v);

static unsigned scc_delay (v)		/* delay for about 5 PCLK cycles */
   unsigned v;				/* pass-through used for input */

{
   register int i,j;			/* it takes time to save them */

   return v;				/* return the passed parameter */
}
#endif

unsigned char Random = 0;		/* random number for p-persist */

static int scc_call(struct iface *ifp,char *call);
static int scc_init(int nchips,ioaddr iobase,int space,int aoff,
 int boff,int doff,ioaddr intack,int ivec,long clk,int pclk,int hwtype,
 int hwparam);
static int scc_raw(struct iface *ifp,struct mbuf **bpp);
static int scc_stop(struct iface *ifp);
static int get_scc(int dev);
static int scc_send(int dev,struct mbuf **bpp);
static int scc_async(struct sccchan *scc);

static void scc_sdlc(struct sccchan *scc);
static void scc_tossb(struct sccchan *scc);
static void scc_txon(struct sccchan *scc);
static void scc_txoff(struct sccchan *scc);
static int32 scc_aioctl(struct iface *ifp,int cmd,int set,int32 val);
static int32 scc_sioctl(struct iface *ifp,int cmd,int set,int32 val);
static void scc_sstart(struct sccchan *scc);
static unsigned int scc_speed(struct sccchan *scc,
  unsigned int clkmode,long speed);
static void scc_asytx(struct sccchan *scc);
static void scc_asyex(struct sccchan *scc);
static void scc_asyrx(struct sccchan *scc);
static void scc_asysp(struct sccchan *scc);
static void scc_sdlctx(struct sccchan *scc);
static void scc_sdlcex(struct sccchan *scc);
static void scc_sdlcrx(struct sccchan *scc);
static void scc_sdlcsp(struct sccchan *scc);

/* Attach an SCC channel to the system, or initialize SCC driver.
 * operation depends on argv[2]:
 * when "init", the SCC driver is initialized, and global information about
 * the hardware is set up.
 * argv[0]: hardware type, must be "scc"
 * argv[1]: number of SCC chips we will support
 * argv[2]: mode, must be: "init" in this case
 * argv[3]: base address of SCC chip #0 (hex)
 * argv[4]: spacing between SCC chip base addresses
 * argv[5]: offset from chip base address to channel A control register
 * argv[6]: offset from chip base address to channel B control register
 * argv[7]: offset from each channel's control register to data register
 * argv[8]: address of INTACK/Read Vector port. 0 to read from RR3A/RR2B
 * argv[9]: CPU interrupt vector number for all connected SCCs
 * argv[10]: clock frequency (PCLK/RTxC) of all SCCs in cycles per second
 *		 prefix with "p" for PCLK, "r" for RTxC clock (for baudrate gen)
 * argv[11]: optional hardware type (for special features)
 * argv[12]: optional extra parameter for special hardware
 *
 * otherwise, a single channel is attached using the specified parameters:
 * argv[0]: hardware type, must be "scc"
 * argv[1]: SCC channel number to attach, 0/1 for first chip A/B, 2/3 for 2nd...
 * argv[2]: mode, can be:
 *		"slip", "kiss", "ax25"
 * argv[3]: interface label, e.g., "sl0"
 * argv[4]: maximum transmission unit, bytes
 * argv[5]: interface speed, e.g, "1200". prefix with "d" when an external
 *		divider is available to generate the TX clock.	When the clock
 *		source is PCLK, this can be a /32 divider between TRxC and RTxC.
 *		When the clock is at RTxC, the TX rate must be supplied at TRxC.
 *		This is needed only for AX.25 fullduplex.
 *	    When this arg is given as "ext", the transmit and receive clock
 *	    are external, and the BRG and DPLL are not used.
 * argv[6]: buffer size
 * argv[7]: callsign used on the radio channels (optional) or 'v' to 
 *          specify Van Jacobson TCP header compression. See argv[8] below.
 * argv[8]: 'v' here specifies Van Jacobson TCP header compression iff
 *          there is a 9th arg (ie. [7] is a callsign.)
 */

int
scc_attach(argc,argv)
int argc;
char *argv[];
{
	register struct iface *ifp;
	struct sccchan *scc;
	unsigned int chan,brgrate;
	int pclk = 0,hwtype = 0,hwparam = 0;
	int xdev;
	char *cp;

	/* first handle the special "init" mode, to initialize global stuff */

	if(!strcmp(argv[2],"init")){
	if(argc < 11)			/* need at least argv[1]..argv[10] */
		return -1;

	if(isupper(argv[10][0]))
		argv[10][0] = tolower(argv[10][0]);

	if(argv[10][0] == 'p'){	/* wants to use PCLK as clock? */
		pclk = 1;
		argv[10]++;
	} else {
		if(argv[10][0] == 'r')	/* wants to use RTxC? */
		argv[10]++;		/* that's the default */
	}
	if(argc > 11)			/* optional hardware type */
		hwtype = htoi(argv[11]);	/* it is given in hex */

	if(argc > 12)			/* optional hardware param */
		hwparam = htoi(argv[12]);	/* also in hex */

	return scc_init(atoi(argv[1]),(ioaddr) htol(argv[3]),atoi(argv[4]),
		atoi(argv[5]),atoi(argv[6]),atoi(argv[7]),
		(ioaddr) htol(argv[8]),atoi(argv[9]),
		atol(argv[10]),pclk,hwtype,hwparam);
	}
	/* not "init", so it must be a valid mode to attach a channel */
	if(strcmp(argv[2],"ax25") && strcmp(argv[2],"kiss") &&
	 strcmp(argv[2],"slip")){
		printf("Mode %s unknown for SCC\n",argv[2]);
		return -1;
	}
	if(strcmp(argv[2],"slip") == 0 || strcmp(argv[2],"kiss") == 0){
		for(xdev = 0;xdev < SLIP_MAX;xdev++)
			if(Slip[xdev].iface == NULL)
				break;
		if(xdev >= SLIP_MAX){
			printf("Too many slip devices\n");
			return -1;
		}
	}
	if(strcmp(argv[2],"nrs") == 0){
		for(xdev = 0;xdev < NRS_MAX;xdev++)
			if(Nrs[xdev].iface == NULL)
				break;

		if(xdev >= NRS_MAX){
			printf("Too many nrs devices\n");
			return -1;
		}
	}
	if(!Sccinfo.init){
		printf("First init SCC driver\n");
		return -1;
	}
	if((chan = atoi(argv[1])) > Sccinfo.maxchan){
		printf("SCC channel %d out of range\n",chan);
		return -1;
	}
	if(Sccchan[chan] != NULL){
		printf("SCC channel %d already attached\n",chan);
		return -1;
	}
	/* create interface structure and fill in details */
	ifp = (struct iface *) callocw(1,sizeof(struct iface));
	ifp->name = mallocw(strlen(argv[3]) + 1);
	strcpy(ifp->name,argv[3]);

	ifp->mtu = atoi(argv[4]);
	ifp->dev = chan;
	ifp->stop = scc_stop;

	scc = (struct sccchan *) callocw(1,sizeof(struct sccchan));
	scc->ctrl = Sccinfo.iobase + (chan / 2) * Sccinfo.space + Sccinfo.off[chan % 2];
	scc->data = scc->ctrl + Sccinfo.doff;
	scc->iface = ifp;

	if(isupper(argv[5][0]))
		argv[5][0] = tolower(argv[5][0]);

    switch (argv[5][0]) {
    case 'd':				/* fulldup divider installed? */
		scc->fulldup = 1;		/* set appropriate flag */
		argv[5]++;			/* skip the 'd' */
		break;

    case 'e':				/* external clocking? */
		scc->extclock = 1;		/* set the flag */
		break;
    }

	scc->bufsiz = atoi(argv[6]);
	ifp->addr = Ip_addr; 
	Sccchan[chan] = scc;		/* put addr in table for interrupts */

	switch(argv[2][0]){ 			/* mode already checked above */
#ifdef AX25
	case 'a':	/* AX.25 */
		scc_sdlc(scc);				/* init SCC in SDLC mode */

		if (!scc->extclock) {
			brgrate = scc_speed(scc,32,atol(argv[5]));/* init SCC speed */
			scc->speed = Sccinfo.clk / (64L * (brgrate + 2));/* calc real speed */
		}

		brgrate = scc_speed(scc,32,atol(argv[5]));/* init SCC speed */
		scc->speed = Sccinfo.clk / (64L * (brgrate + 2));/* calc real speed */
		setencap(ifp,"AX25UI");
		scc_call(ifp,argc > 7 ? argv[7] : (char *) 0);	/* set the callsign */
			ifp->ioctl = scc_sioctl;
		ifp->raw = scc_raw;

		/* default KISS Params */
		scc->a.txdelay = 36*TPS/100;	/* 360 ms */
		scc->a.persist = 25;		/* 10% persistence */
		scc->a.slottime = 16*TPS/100; /* 160 ms */
#if TPS > 67
		scc->a.tailtime = 3*TPS/100;	/* 30 ms */
#else
		scc->a.tailtime = 2;		/* minimal reasonable value */
#endif
		scc->a.fulldup = 0;		/* CSMA */
		scc->a.waittime = 50*TPS/100; /* 500 ms */
		scc->a.maxkeyup = 7;		/* 7 s */
		scc->a.mintime = 3;		/* 3 s */
		scc->a.idletime = 120;	/* 120 s */
		break;
	case 'k':	/* kiss */
		scc_async(scc);				/* init SCC in async mode */
		brgrate = scc_speed(scc,16,atol(argv[5]));
		scc->speed = Sccinfo.clk / (32L * (brgrate + 2));

		setencap(ifp,"AX25UI");
		scc_call(ifp,argc > 7 ? argv[7] : (char *) 0);	/* set the callsign */

		ifp->ioctl = kiss_ioctl;
		ifp->raw = kiss_raw;

		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			if(Slip[xdev].iface == NULL)
				break;
		}
		ifp->xdev = xdev;
		Slip[xdev].iface = ifp;
		Slip[xdev].type = CL_KISS;
		Slip[xdev].send = scc_send;
		Slip[xdev].get = get_scc;
		cp = if_name(ifp," rx");
		ifp->rxproc = newproc(cp,256,slip_rx,xdev,NULL,NULL,0);
		free(cp);
		break;
#endif
#ifdef SLIP
	case 's':	/* slip */
		scc_async(scc);				/* init SCC in async mode */
		brgrate = scc_speed(scc,16,atol(argv[5]));
		scc->speed = Sccinfo.clk / (32L * (brgrate + 2));
		setencap(ifp,"SLIP");
		ifp->ioctl = scc_aioctl;
		ifp->raw = slip_raw;
		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			if(Slip[xdev].iface == NULL)
				break;
		}
		ifp->xdev = xdev;
		Slip[xdev].iface = ifp;
		Slip[xdev].type = CL_SERIAL_LINE;
		Slip[xdev].send = scc_send;
		Slip[xdev].get = get_scc;
		cp = if_name(ifp," rx");

#ifdef VJCOMPRESS
		if((argc > 8) && (strchr(argv[8],'v') != NULL)) {
			Slip[xdev].escaped |= SLIP_VJCOMPR;
			Slip[xdev].slcomp = slhc_init(16,16);

		} else if((argc > 7) && (strchr(argv[7],'v') != NULL)) {
			Slip[xdev].escaped |= SLIP_VJCOMPR;
			Slip[xdev].slcomp = slhc_init(16,16);
		}
#else
		Slip[xdev].slcomp = NULL;
#endif	/* VJCOMPRESS */
		ifp->rxproc = newproc(cp,256,slip_rx,xdev,NULL,NULL,0);
		free(cp);
		break;
#endif
#ifdef NRS
	case 'n':	/* nrs */
		scc_async(scc);				/* init SCC in async mode */
		brgrate = scc_speed(scc,16,atol(argv[5]));
		scc->speed = Sccinfo.clk / (32L * (brgrate + 2));
		setencap(ifp,"AX25UI");
		scc_call(ifp,argc > 7 ? argv[7] : (char *) 0);	/* set the callsign */
		ifp->ioctl = scc_aioctl;
		ifp->raw = nrs_raw;
	
		for(xdev = 0;xdev < NRS_MAX;xdev++)
			if(Nrs[xdev].iface == NULL)
				break;

		ifp->xdev = xdev;
		Nrs[xdev].iface = ifp;
		Nrs[xdev].send = scc_send;
		Nrs[xdev].get = get_scc;
		cp = if_name(ifp," rx");
		ifp->rxproc = newproc(cp,256,nrs_recv,xdev,NULL,NULL,0);
		free(cp);
		break;
#endif
	}
	ifp->next = Ifaces;			/* link interface in list */
	Ifaces = ifp;
	cp = if_name(ifp," tx");
	ifp->txproc = newproc(cp,512,if_tx,0,ifp,NULL,0);
	free(cp);
	return 0;
}

/* SCC driver initialisation. called on "attach scc <num> init ..." */
static int
scc_init(nchips,iobase,space,aoff,boff,doff,intack,ivec,clk,pclk,hwtype,hwparam)
int nchips;			/* number of chips */
ioaddr iobase;			/* base of first chip */
int space,aoff,boff,doff;
ioaddr intack;			/* INTACK ioport or 0 for no INTACK */
int ivec;			/* interrupt vector number */
long clk;			/* clock frequency */
int pclk;			/* PCLK or RTxC for clock */
int hwtype;			/* selection of special hardware types */
int hwparam;			/* extra parameter for special hardware */
{
	int chip,chan;
	ioaddr chipbase;
	register ioaddr ctrl;
    int d;
    int dum = 1;
	int i_state;

#define z 0

	if(Sccinfo.init){
		printf("SCC driver already initialized - nothing done\n");
		return 1;
	}
	Sccinfo.init = 1;
	Sccinfo.nchips = nchips;
	Sccinfo.maxchan = (2 * nchips) - 1;
	Sccinfo.iobase = iobase;
	Sccinfo.space = space;
	Sccinfo.off[0] = aoff;
	Sccinfo.off[1] = boff;
	Sccinfo.doff = doff;
	Sccinfo.ivec = ivec;
	Sccinfo.clk = clk;
	Sccinfo.pclk = pclk;
	Sccinfo.hwtype = hwtype;
	Sccinfo.hwparam = hwparam;

	/* reset and pre-init all chips in the system */
	for(chip = 0; chip < nchips; chip++){
		chipbase = iobase + chip * space;
		ctrl = chipbase + Sccinfo.off[0];
		i_state = dirps();		/* because of 2-step accesses */
		VOID(RDREG(ctrl));		/* make sure pointer is written */
		WRSCC(ctrl,R9,FHWRES);		/* force hardware reset */
		for (d = 0; d < 1000; d++)	/* wait a while to be sure */
			dum *= 10;
		for(chan = 0; chan < 2; chan++){
			ctrl = chipbase + Sccinfo.off[chan];

			/* initialize a single channel to no-op */
			VOID(RDREG(ctrl));		/* make sure pointer is written */
			WRSCC(ctrl,R4,z);		/* no mode selected yet */
			WRSCC(ctrl,R1,z);		/* no W/REQ operation */
			WRSCC(ctrl,R2,16 * chip);	/* chip# in upper 4 bits of vector */
			WRSCC(ctrl,R3,z);		/* disable rx */
			WRSCC(ctrl,R5,z);		/* disable tx */
			WRSCC(ctrl,R9,VIS);		/* vector includes status, MIE off */
			Sccpolltab[chip][chan] = ctrl; /* store ctrl addr for polling */
		}

		if (hwtype & HWSEALEVEL)		/* if a SEALEVEL card, */
			WRREG(chipbase + 4,hwparam);	/* set control/status (DTR) */

		if(hwtype & HWEAGLE)		/* this is an EAGLE card */
			WRREG(chipbase + 4,0x08);	/* enable interrupt on the board */

		if(hwtype & HWPC100)		/* this is a PC100 card */
			WRREG(chipbase,hwparam);	/* set the MODEM mode (22H normally) */

		if(hwtype & HWPRIMUS)		/* this is a PRIMUS-PC */
			WRREG(chipbase + 4,hwparam); /* set the MODEM mode (02H normally) */

		if (hwtype & HWDRSI) {		/* this is a DRSI PC*Packet card */
			ioaddr z8536 = chipbase + 7; /* point to 8536 master ctrl reg */

			/* Initialize 8536 to perform its divide-by-32 function */
			/* This part copied from N6TTO DRSI-driver */

			/* Start by forcing chip into known state */

			VOID(RDREG(z8536));		/* make sure pointer is written */
			WRSCC(z8536,CIO_MICR,0x01); /* force hardware reset */

			for (d = 0; d < 1000; d++)	/* wait a while to be sure */
				dum *= 10;

			WRSCC(z8536,CIO_MICR,0x00); /* Clear reset and start */

			/* Wait for chip to come ready */

			while (RDSCC(z8536,CIO_MICR) != 0x02)
				dum *= 10;

			WRSCC(z8536,CIO_MICR,0x26); /* NV|CT_VIS|RJA */
			WRSCC(z8536,CIO_MCCR,0xf4); /* PBE|CT1E|CT2E|CT3E|PAE */

			WRSCC(z8536,CIO_CTMS1,0xe2);/* Continuous, EOE, ECE, Pulse output */
			WRSCC(z8536,CIO_CTMS2,0xe2);/* Continuous, EOE, ECE, Pulse output */
			
	    WRSCC(z8536,CIO_CT1MSB,0x00); /* Load time constant CTC #1 */
			WRSCC(z8536,CIO_CT1LSB,0x10);
			WRSCC(z8536,CIO_CT2MSB,0x00); /* Load time constant CTC #2 */
			WRSCC(z8536,CIO_CT2LSB,0x10);

			WRSCC(z8536,CIO_IVR,0x06);

			/* Set port direction bits in port A and B		     */
			/* Data is input on bits d1 and d5, output on d0 and d4. */
			/* The direction is set by 1 for input and 0 for output  */

			WRSCC(z8536,CIO_PDCA,0x22);
			WRSCC(z8536,CIO_PDCB,0x22);

			WRSCC(z8536,CIO_CSR1,CIO_GCB|CIO_TCB); /* Start CTC #1 running */
			WRSCC(z8536,CIO_CSR2,CIO_GCB|CIO_TCB); /* Start CTC #2 running */
		}

		restore(i_state);
	}
	Sccpolltab[chip][0] = 0;	/* terminate the polling table */
	Sccvecloc = intack;		/* location of INTACK/vector read */
	Sccmaxvec = 16 * nchips;	/* upper limit on valid vector */
	/* save original interrupt vector */
	Orgivec = getirq(ivec);

	if(intack){	/* INTACK method selected? */
		/* set interrupt vector to INTACK-generating routine  */
		setirq(ivec,sccvec);
	} else {
		/* set interrupt vector to polling routine */
		setirq(ivec,sccnovec);
	}
	/* enable the interrupt  */
	maskon(ivec);
	return 0;
}

/* initialize an SCC channel in asynchronous mode */
static int
scc_async(scc)
register struct sccchan *scc;
{
	register struct fifo *fp = &(scc->fifo);
	int i_state;
	
	if((fp->buf = malloc(scc->bufsiz)) == NULL){
		printf("scc%d: No space for rx buffer\n",scc->iface->dev);
		return -1;
	}
	fp->bufsize = scc->bufsiz;
	fp->wp = fp->rp = fp->buf;
	fp->cnt = 0;

	scc->int_transmit = scc_asytx;	/* set interrupt handlers */
	scc->int_extstat = scc_asyex;
	scc->int_receive = scc_asyrx;
	scc->int_special = scc_asysp;

	i_state = dirps();

	if (scc->extclock) {
		wr(scc,R4,X1CLK|SB1);		/* *1 clock, 1 stopbit, no parity */
		wr(scc,R11,RCRTxCP|TCTRxCP);	/* RXclk RTxC, TXclk TRxC. */
		wr(scc,R14,z);			/* No BRG options */
	wr(scc,R4,X16CLK|SB1);		/* *16 clock, 1 stopbit, no parity */
		wr(scc,R11,RCBR|TCBR);		/* clocks are BR generator */
		wr(scc,R14,Sccinfo.pclk? BRSRC:z); /* brg source = PCLK/RTxC */
	}
	wr(scc,R1,z);			/* no W/REQ operation */
	wr(scc,R3,Rx8);			/* RX 8 bits/char, disabled */
	wr(scc,R5,Tx8|DTR|RTS);		/* TX 8 bits/char, disabled, DTR RTS */
	wr(scc,R9,VIS);			/* vector includes status */
	wr(scc,R10,NRZ|z);			/* select NRZ */
	wr(scc,R15,BRKIE);			/* enable BREAK ext/status int */

	or(scc,R3,RxENABLE);		/* enable receiver */
	or(scc,R5,TxENAB);			/* enable transmitter */

	WRREG(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	WRREG(scc->ctrl,RES_EXT_INT);	/* must be done twice */
	scc->status = RDREG(scc->ctrl);	/* read initial status */

	or(scc,R1,INT_ALL_Rx|TxINT_ENAB|EXT_INT_ENAB); /* enable interrupts */
	or(scc,R9,MIE);			/* master interrupt enable */

	restore(i_state);
	return 0;
}

/* initialize an SCC channel in SDLC mode */
static void
scc_sdlc(scc)
register struct sccchan *scc;
{
	int i_state;

	scc->int_transmit = scc_sdlctx;	/* set interrupt handlers */
	scc->int_extstat = scc_sdlcex;
	scc->int_receive = scc_sdlcrx;
	scc->int_special = scc_sdlcsp;

	i_state = dirps();

	wr(scc,R4,X1CLK|SDLC);		/* *1 clock, SDLC mode */
	wr(scc,R1,z);			/* no W/REQ operation */
	wr(scc,R3,Rx8|RxCRC_ENAB);	/* RX 8 bits/char, CRC, disabled */
	wr(scc,R5,Tx8|DTR|TxCRC_ENAB);	/* TX 8 bits/char, disabled, DTR */
	wr(scc,R6,z);			/* SDLC address zero (not used) */
	wr(scc,R7,FLAG);		/* SDLC flag value */
	wr(scc,R9,VIS);			/* vector includes status */
	wr(scc,R10,CRCPS|NRZI|ABUNDER);	/* CRC preset 1, select NRZI, ABORT on underrun */

	if (scc->extclock){			/* when using external clocks */
		/* RXclk RTxC, TXclk TRxC. */
		wr(scc,R11,RCRTxCP|TCTRxCP);
		wr(scc,R14,z);			/* No BRG options */
		WRSCC(scc->ctrl,R14,DISDPLL|scc->wreg[R14]); /* No DPLL operation */
	} else {
		if(scc->fulldup){		/* when external clock divider */
			if(Sccinfo.pclk){	/* when using PCLK as clock source */
				/* RXclk DPLL, TXclk RTxC, out=BRG.	 external /32 TRxC->RTxC */
				wr(scc,R11,RCDPLL|TCRTxCP|TRxCOI|TRxCBR);
			} else {
				/* RXclk DPLL, TXclk TRxC.	external TX clock to TRxC */
				wr(scc,R11,RCDPLL|TCTRxCP);
			}
		} else {		/* only half-duplex operation */
			/* RXclk DPLL, TXclk BRG. BRG reprogrammed at every TX/RX switch */
#ifdef	notdef	/* KA9Q - for PSK modem */
			wr(scc,R11,RCDPLL|TCBR);
#else
			/* DPLL -> Rx clk, DPLL -> Tx CLK, TxCLK -> TRxC pin */
			wr(scc,R11,RCDPLL|TCDPLL|TRxCOI|TRxCDP);
#endif
		}
		wr(scc,R14,Sccinfo.pclk? BRSRC:z);	/* BRG source = PCLK/RTxC */
		WRSCC(scc->ctrl,R14,SSBR|scc->wreg[R14]); /* DPLL source = BRG */
		WRSCC(scc->ctrl,R14,SNRZI|scc->wreg[R14]); /* DPLL NRZI mode */
	}
	wr(scc,R15,BRKIE|CTSIE|DCDIE);	/* enable ABORT, CTS & DCD interrupts */

	if(RDREG(scc->ctrl) & DCD){	/* DCD is now ON */
		if (!scc->extclock)
			WRSCC(scc->ctrl,R14,SEARCH|scc->wreg[R14]); /* DPLL: enter search mode */
		or(scc,R3,ENT_HM|RxENABLE);	/* enable the receiver, hunt mode */
	}
	WRREG(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	WRREG(scc->ctrl,RES_EXT_INT);	/* must be done twice */
	scc->status = RDREG(scc->ctrl);	/* read initial status */

	or(scc,R1,INT_ALL_Rx|TxINT_ENAB|EXT_INT_ENAB); /* enable interrupts */
	or(scc,R9,MIE);			/* master interrupt enable */

	restore(i_state);
}

/* set SCC channel speed
 * clkmode specifies the division rate (1,16,32) inside the SCC
 * returns the selected brgrate for "real speed" calculation
 */
static unsigned int
scc_speed(scc,clkmode,speed)
register struct sccchan *scc;
unsigned int clkmode;
long speed;				/* the desired baudrate */
{
	unsigned int brgrate;
	long spdclkm;
	int i_state;

	/* calculate baudrate generator value */

    if ((spdclkm = speed * clkmode) == 0)
	return 65000U;			/* avoid divide-by-zero */

	brgrate = (unsigned) ((Sccinfo.clk + spdclkm) / (spdclkm * 2)) - 2;

	i_state = dirps();		/* 2-step register accesses... */

	cl(scc,R14,BRENABL);		/* disable baudrate generator */
	wr(scc,R12,brgrate);		/* brg rate LOW */
	wr(scc,R13,brgrate >> 8);		/* brg rate HIGH */
	or(scc,R14,BRENABL);		/* enable baudrate generator */

	restore(i_state);
	return brgrate;
}

/* de-activate SCC channel */
static int
scc_stop(ifp)
struct iface *ifp;
{
	struct sccchan *scc = Sccchan[ifp->dev];
	int i_state;

	i_state = dirps();

	VOID(RDREG(scc->ctrl));		/* make sure pointer is written */
	wr(scc,R9,(ifp->dev % 2)? CHRB : CHRA); /* reset the channel */

	switch(ifp->iftype->type){
	case CL_SERIAL_LINE:
	case CL_KISS:
		free(scc->fifo.buf);
	default:
		break;
	}
	free(scc);
	Sccchan[ifp->dev] = NULL;
	restore(i_state);
	return 0;
}

/* de-activate SCC driver on program exit */
void
sccstop()
{
	if(Sccinfo.init){			/* was it initialized? */
		maskoff(Sccinfo.ivec);		/* disable the interrupt */
		setirq(Sccinfo.ivec,Orgivec);	/* restore original interrupt vector */
	}
}

/* perform ioctl on SCC (async) channel
 * this is used for SLIP mode only, and will read/set the line speed
 */
static int32
scc_aioctl(ifp,cmd,set,val)
struct iface *ifp;
int cmd;
int set;
int32 val;
{
	struct sccchan *scc;
	unsigned int brgrate;

	scc = Sccchan[ifp->dev];

	switch(cmd){
	case PARAM_SPEED:
		if(set){
			brgrate = scc_speed(scc,16,val);
			scc->speed = Sccinfo.clk / (32L * (brgrate + 2));
		}
		return scc->speed;
	}
	return 0;
}

/* perform ioctl on SCC (sdlc) channel
 * this is used for AX.25 mode only, and will set the "kiss" parameters
 */
static int32
scc_sioctl(ifp,cmd,set,val)
struct iface *ifp;
int cmd;
int set;
int32 val;
{
	struct sccchan *scc;
	unsigned int brgrate;
	int i_state;
	
	scc = Sccchan[ifp->dev];

	switch(cmd){
	case PARAM_SPEED:
		if(set){
			if(val == 0)
				scc->extclock = 1;
			else {
			    brgrate = scc_speed(scc,32,val);/* init SCC speed */
			    scc->speed = Sccinfo.clk / (64L * (brgrate + 2));/* calc real speed */
			}
		}
		return scc->speed;
	case PARAM_TXDELAY:
		if(set)
			scc->a.txdelay = val;
		return scc->a.txdelay;
	case PARAM_PERSIST:
		if(set)
			scc->a.persist = val;
		return scc->a.persist;
	case PARAM_SLOTTIME:
		if(set)
			scc->a.slottime = val;
		return scc->a.slottime;
	case PARAM_TXTAIL:
		if(set)
			scc->a.tailtime = val;
		return scc->a.tailtime;
	case PARAM_FULLDUP:
		if(set)
			scc->a.fulldup = val;
		return scc->a.fulldup;
	case PARAM_WAIT:
		if(set)
			scc->a.waittime = val;
		return scc->a.waittime;
	case PARAM_MAXKEY:
		if(set)
			scc->a.maxkeyup = val;
		return scc->a.maxkeyup;
	case PARAM_MIN:
		if(set)
			scc->a.mintime = val;
		return scc->a.mintime;
	case PARAM_IDLE:
		if(set)
			scc->a.idletime = val;
		return scc->a.idletime;
	case PARAM_DTR:
		if(set){
			if(val)
				scc->wreg[R5] |= DTR;
			else
				scc->wreg[R5] &= ~DTR;

			i_state = dirps();
			if(scc->a.tstate == IDLE && scc->timercount == 0)
				scc->timercount = 1;	/* force an update */
			restore(i_state);
		}
		return (scc->wreg[R5] & DTR) ? 1 : 0;
	case PARAM_GROUP:
		if(set)
			scc->group = val;
		return scc->group;
	}
	return -1;
}

/* start SCC transmitter when it is idle (SLIP/KISS mode only) */
static void
scc_sstart(scc)
register struct sccchan *scc;
{
	if(scc->tbp != NULL ||	/* busy */
	 scc->sndq == NULL)	/* no work */
		return;

	scc->tbp = dequeue(&scc->sndq);
	WRREG(scc->data,FR_END);
}

/* show SCC status */
int
dosccstat()
{
	register struct sccchan *scc;
	int i;

	if(!Sccinfo.init){
		printf("SCC driver not initialized\n");
		return 0;
	}
	printf("Ch Iface    Sent   Rcvd   Error Space Overr   Rxints   Txints   Exints   Spints\n");

	for(i = 0; i <= Sccinfo.maxchan; i++){
		if((scc = Sccchan[i]) == NULL)
			continue;

		if(scc->int_receive == scc_asyrx)
			printf("%2d %-6s  ** asynch ** %7lu %5u %5u %8lu %8lu %8lu %8lu\n",i,scc->iface->name,
			    scc->rxerrs,scc->nospace,scc->rovers,
			    scc->rxints,scc->txints,scc->exints,scc->spints);
		else
			printf("%2d %-6s %6lu %6lu %7lu %5u %5u %8lu %8lu %8lu %8lu\n",i,scc->iface->name,
			    scc->enqueued,scc->rxframes,scc->rxerrs,scc->nospace,scc->rovers,
			    scc->rxints,scc->txints,scc->exints,scc->spints);
	}
	return 0;
}

/* send raw frame to SCC. used for AX.25 */
static int
scc_raw(
struct iface *ifp,
struct mbuf **bpp
){
	struct sccchan *scc;
	int i_state;

	dump(ifp,IF_TRACE_OUT,*bpp);
	ifp->rawsndcnt++;
	ifp->lastsent = secclock();

	scc = Sccchan[ifp->dev];

	if (scc->tx_inhibit){		/* transmitter inhibit */
		free_p(bpp);
		return -1;
    }

	enqueue(&scc->sndq,bpp);		/* enqueue packet */
	scc->enqueued++;

	i_state = dirps();

	if(scc->a.tstate == IDLE){	/* when transmitter is idle */
		scc->a.tstate = DEFER;	/* start the key-up sequence */
		scc->a.maxdefer = TPS * scc->a.idletime /
			scc->a.slottime;
		scc->timercount = scc->a.waittime;
	}
	restore(i_state);
	return 0;
}

static int
scc_send(
int dev,
struct mbuf **bpp
){
	struct sccchan *scc;

	scc = Sccchan[dev];
	enqueue(&scc->sndq,bpp);

	if(scc->tbp == NULL)
		scc_sstart(scc);
	return(0);
}

/* initialize interface for AX.25 use */
static int
scc_call(ifp,call)
register struct iface *ifp;
char *call;
{
	uint8 out[AXALEN];

	ifp->hwaddr = mallocw(AXALEN);
	if(setcall(out,call) == 0)
		memcpy(ifp->hwaddr,out,AXALEN);
	else
		memcpy(ifp->hwaddr,Mycall,AXALEN);
	return 0;
}

/* Interrupt handlers for asynchronous modes (kiss, slip) */

/* Transmitter interrupt handler */
/* This routine sends data from mbufs in SLIP format */
static void
scc_asytx(scc)
register struct sccchan *scc;
{
	register struct mbuf *bp;

	scc->txints++;

	if(scc->txchar != 0){		/* a character pending for transmit? */
		WRREG(scc->data,scc->txchar);	/* send it now */
		scc->txchar = 0;		/* next time, ignore it */
		return;
	}

	if(scc->tbp == NULL){	/* nothing to send? */
		if((scc->tbp = scc->sndq) != NULL){ /* dequeue next frame */
			scc->sndq = scc->sndq->anext;
			WRREG(scc->data,FR_END);	/* send FR_END to flush line garbage */
		} else {
			WRREG(scc->ctrl,RES_Tx_P);	/* else only reset pending int */
		}
		return;
	}
	while ((bp = scc->tbp)->cnt == 0){ /* nothing left in this mbuf? */
		bp = bp->next;			/* save link to next */

		free_mbuf(&scc->tbp);

		if((scc->tbp = bp) == NULL){ /* see if more mbufs follow */
			WRREG(scc->data,FR_END);	/* frame complete, send FR_END */
			return;
		}
	}
	/* now bp = scc->tbp (either from while or from if stmt above) */

	WRREG(scc->data,*(bp->data));	/* just send the character */
	bp->cnt--;				/* decrease mbuf byte count */
	bp->data++;				/* and increment the data pointer */
}

/* External/Status interrupt handler */
static void
scc_asyex(scc)
register struct sccchan *scc;
{
	register unsigned char status,changes;

	scc->exints++;
	status = RDREG(scc->ctrl);
	changes = status ^ scc->status;

	if(changes & BRK_ABRT){		/* BREAK? */
		if((status & BRK_ABRT) == 0)	/* BREAK now over? */
			VOID(RDREG(scc->data));	/* read the NUL character */
	}
	scc->status = status;
	WRREG(scc->ctrl,RES_EXT_INT);
}

/* Receiver interrupt handler under NOS.
 * Since the higher serial protocol routines are all written to work
 * well with the routines in 8250.c, it makes sense to handle
 * asynch i/o with the 8530 in a similar manner. Therefore, these
 * routines are as close to their counterparts in 8250.c as possible.
 */
 
static void
scc_asyrx(scc)
register struct sccchan *scc;
{
	register struct fifo *fp;
	char c;

	scc->rxints++;

	fp = &(scc->fifo);
	do {
		c = RDREG(scc->data);
		if(fp->cnt != fp->bufsize){
			*fp->wp++ = c;
			if(fp->wp >= &fp->buf[fp->bufsize])
				fp->wp = fp->buf;
			fp->cnt++;
		} else
			scc->nospace++;
	} while(RDREG(scc->ctrl) & Rx_CH_AV);
	ksignal(fp,1);	/* eventually move this to timer routine */
}

/* Blocking read from asynch input.
 * Essentially the same as get_asy() in 8250.c
 * See comments in asyrxint().
 */
static int
get_scc(dev)
int dev;
{
	register struct fifo *fp;
	uint8 c;
	int tmp;
	int i_state;

	fp = &(Sccchan[dev]->fifo);

	for(;;){
		i_state = dirps();
		tmp = fp->cnt;
		if(tmp != 0){
			fp->cnt--;
			restore(i_state);
			break;
		}
		restore(i_state);
		kwait(fp);
	}
	c = *fp->rp++;
	if(fp->rp >= &fp->buf[fp->bufsize])
		fp->rp = fp->buf;

	return c;
}

int
scc_frameup(dev)
int dev;
{
	Sccchan[dev]->rxframes++;
	return 0;
}

/* Receive Special Condition interrupt handler */
static void
scc_asysp(scc)
register struct sccchan *scc;
{
	register unsigned char status;

	scc->spints++;

	status = rd(scc,R1);		/* read receiver status */
	VOID(RDREG(scc->data));		/* flush offending character */

	if(status & (CRC_ERR | Rx_OVR))		/* did a framing error or overrun occur ? */
		scc->rovers++;			/* report as overrun */

	WRREG(scc->ctrl,ERR_RES);
}

/* Interrupt handlers for sdlc mode (AX.25) */

/* Transmitter interrupt handler */
static void
scc_sdlctx(scc)
register struct sccchan *scc;
{
	register struct mbuf *bp;

	scc->txints++;

	switch(scc->a.tstate){		/* look at transmitter state */
	case ACTIVE:			/* busy sending data bytes */
		while ((bp = scc->tbp)->cnt == 0){	/* nothing left in this mbuf? */
			bp = bp->next;			/* save link to next */
			free_mbuf(&scc->tbp);	/*KM*/
			if((scc->tbp = bp) == NULL){/* see if more mbufs follow */
				if(RDREG(scc->ctrl) & TxEOM){	/* check tx underrun status */
					scc->rovers++;		/* oops, an underrun! count them */
					WRREG(scc->ctrl,SEND_ABORT);/* send an abort to be sure */
					scc->a.tstate = TAIL;	/* key down tx after TAILTIME */
					scc->timercount = scc->a.tailtime;
					return;
				}
				cl(scc,R10,ABUNDER);		/* frame complete, allow CRC transmit */
				scc->a.tstate = FLUSH;
				WRREG(scc->ctrl,RES_Tx_P);	/* reset pending int */
				return;
			}
		}
		/* now bp = scc->tbp (either from while or from if stmt above) */
		WRREG(scc->data,*(bp->data++)); /* send the character */
		bp->cnt--;			/* decrease mbuf byte count */
		return;
	case FLUSH:	/* CRC just went out, more to send? */
		or(scc,R10,ABUNDER);		/* re-install underrun protection */
		/* verify that we are not exeeding max tx time (if defined) */
		if((scc->timercount != 0 || scc->a.maxkeyup == 0) &&
		 (scc->tbp = scc->sndq) != NULL){ /* dequeue a frame */
			scc->sndq = scc->sndq->anext;
			WRREG(scc->ctrl,RES_Tx_CRC); /* reset the TX CRC generator */
			scc->a.tstate = ACTIVE;
			scc_sdlctx(scc);		/* write 1st byte */
			WRREG(scc->ctrl,RES_EOM_L); /* reset the EOM latch */
			return;
		}
		scc->a.tstate = TAIL;		/* no more, key down tx after TAILTIME */
		scc->timercount = scc->a.tailtime;
		WRREG(scc->ctrl,RES_Tx_P);
		return;
	default:				/* another state */
		WRREG(scc->ctrl,RES_Tx_P);	/* then don't send anything */
		return;
	}
}

/* External/Status interrupt handler */
static void
scc_sdlcex(scc)
register struct sccchan *scc;
{
	register unsigned char status,changes;

	scc->exints++;
	status = RDREG(scc->ctrl);
	changes = status ^ scc->status;

	if(changes & BRK_ABRT){		/* Received an ABORT */
		if(status & BRK_ABRT){		/* is this the beginning? */
			if(scc->rbp != NULL){/* did we receive something? */
				/* check if a significant amount of data came in */
				/* this is because the drop of DCD tends to generate an ABORT */
				if(scc->rbp->next != NULL || scc->rbp->cnt > 0)
				scc->rxerrs++;	/* then count it as an error */
				scc_tossb(scc);		/* throw away buffer */
			}
			VOID(RDREG(scc->data));	/* flush the FIFO */
			VOID(RDREG(scc->data));
			VOID(RDREG(scc->data));
		}
	}
	if(changes & CTS){			/* CTS input changed state */
		if(status & CTS){		/* CTS is now ON */
			if(scc->a.tstate == KEYWT &&
				scc->a.txdelay == 0) /* zero TXDELAY = wait for CTS */
			scc->timercount = 1;	/* it will start within 10 ms */
		}
	}
	if(changes & DCD){			/* DCD input changed state */
		if(status & DCD){		/* DCD is now ON */
			if (!scc->extclock)
				WRSCC(scc->ctrl,R14,SEARCH|scc->wreg[R14]); /* DPLL: enter search mode */
			or(scc,R3,ENT_HM|RxENABLE); /* enable the receiver, hunt mode */
		} else {			/* DCD is now OFF */
			cl(scc,R3,ENT_HM|RxENABLE); /* disable the receiver */
			VOID(RDREG(scc->data));	/* flush the FIFO */
			VOID(RDREG(scc->data));
			VOID(RDREG(scc->data));
			if(scc->rbp != NULL){/* did we receive something? */
				/* check if a significant amount of data came in */
				/* this is because some characters precede the drop of DCD */
				if(scc->rbp->next != NULL || scc->rbp->cnt > 0)
				scc->rxerrs++;	/* then count it as an error */
				scc_tossb(scc);		/* throw away buffer */
			}
		}
	}
	scc->status = status;
	WRREG(scc->ctrl,RES_EXT_INT);
}

/* Receiver interrupt handler */
static void
scc_sdlcrx(scc)
register struct sccchan *scc;
{
	register struct mbuf *bp;

	scc->rxints++;

	if((bp = scc->rbp1) == NULL){ /* no buffer available now */
		if(scc->rbp == NULL){
			if((bp = alloc_mbuf(scc->bufsiz+sizeof(struct iface *))) != NULL){
				scc->rbp = scc->rbp1 = bp;
				bp->cnt = 0;
			}
		} else if((bp = alloc_mbuf(scc->bufsiz)) != NULL){
			scc->rbp1 = bp;
			for(bp = scc->rbp; bp->next != NULL; bp = bp->next)
				;
			bp->next = scc->rbp1;
			bp = scc->rbp1;
		}
		if(bp == NULL){
			VOID(RDREG(scc->data));	/* so we have to discard the char */
			or(scc,R3,ENT_HM);		/* enter hunt mode for next flag */
			scc_tossb(scc);		/* put buffers back on pool */
			scc->nospace++;		/* count these events */
			return;
		}
	}

	/* now, we have a buffer (at bp). read character and store it */
	bp->data[bp->cnt++] = RDREG(scc->data);

	if(bp->cnt == bp->size)		/* buffer full? */
		scc->rbp1 = NULL;	/* acquire a new one next time */
}

/* Receive Special Condition interrupt handler */
static void
scc_sdlcsp(scc)
register struct sccchan *scc;
{
	register unsigned char status;
	register struct mbuf *bp;

	scc->spints++;

	status = rd(scc,R1);		/* read receiver status */
	VOID(RDREG(scc->data));		/* flush offending character */

	if(status & Rx_OVR){		/* receiver overrun */
		scc->rovers++;			/* count them */
		or(scc,R3,ENT_HM);		/* enter hunt mode for next flag */
		scc_tossb(scc);			/* rewind the buffer and toss */
	}
	if(status & END_FR &&		/* end of frame */
	scc->rbp != NULL){	/* at least received something */
		if((status & CRC_ERR) == 0 &&	/* no CRC error is indicated */
		(status & 0xe) == RES8 &&	/* 8 bits in last byte */
		scc->rbp->cnt > 0){

			/* we seem to have a good frame. but the last byte received */
			/* from rx interrupt is in fact a CRC byte, so discard it */
			if(scc->rbp1 != NULL){
				scc->rbp1->cnt--;	/* current mbuf was not full */
			} else {
				for(bp = scc->rbp; bp->next != NULL; bp = bp->next);
					/* find last mbuf */

				bp->cnt--;		/* last byte is first CRC byte */
			}

			net_route(scc->iface,&scc->rbp);

			scc->rbp = scc->rbp1 = NULL;
			scc->rxframes++;
		} else {			/* a bad frame */
			scc_tossb(scc);		/* throw away frame */
			scc->rxerrs++;
		}
	}
	WRREG(scc->ctrl,ERR_RES);
}

/* Throw away receive mbuf(s) when an error occurred */
static void
scc_tossb (scc)
register struct sccchan *scc;
{
	register struct mbuf *bp;
	
	if((bp = scc->rbp) != NULL){
		free_p(&bp->next);
		free_p(&bp->dup);	/* Should be NULL */
		bp->next = NULL;
		scc->rbp1 = bp;		/* Don't throw this one away */
		bp->cnt = 0;	/* Simply rewind it */
	}
}

/* Switch the SCC to "transmit" mode */
/* Only to be called from an interrupt handler, while in AX.25 mode */
static void
scc_txon(scc)
register struct sccchan *scc;
{
    if (!scc->fulldup && !scc->extclock){ /* no fulldup divider? */
		cl(scc,R3,RxENABLE);		/* then switch off receiver */
		cl(scc,R5,TxENAB);		/* transmitter off during switch */
		scc_speed(scc,1,scc->speed);	/* reprogram baudrate generator */
	}
	or(scc,R5,RTS|TxENAB);		/* set the RTS line and enable TX */
	if(Sccinfo.hwtype & HWPRIMUS)	/* PRIMUS has another PTT bit... */
		WRREG(scc->ctrl + 4,Sccinfo.hwparam | 0x80); /* set that bit! */
}

/* Switch the SCC to "receive" mode (or: switch off transmitter)
 * Only to be called from an interrupt handler, while in AX.25 mode
 */
static void
scc_txoff(scc)
register struct sccchan *scc;
{
	cl(scc,R5,RTS);			/* turn off RTS line */
	if(Sccinfo.hwtype & HWPRIMUS)	/* PRIMUS has another PTT bit... */
		WRREG(scc->ctrl + 4,Sccinfo.hwparam); /* clear that bit! */

    if (!scc->fulldup && !scc->extclock){ /* no fulldup divider? */
		cl(scc,R5,TxENAB);		/* then disable the transmitter */
		scc_speed(scc,32,scc->speed);	/* back to receiver baudrate */
	}
}

/* SCC timer interrupt handler. Will be called every 1/TPS s by the 
 * routine systick in pc.c
 */
void scctimer()
{
	register struct sccchan *scc;
	register struct sccchan **sccp;
	int i_state;

	i_state = dirps();
	for(sccp = Sccchan + Sccinfo.maxchan; sccp >= Sccchan; sccp--){
		if((scc = *sccp) != NULL &&
		  scc->timercount != 0 &&
		  --(scc->timercount) == 0){
			/* handle an SCC timer event for this SCC channel
			 * this can only happen when the channel is AX.25 type
			 * (the SLIP/KISS driver does not use timers)
			 */
			switch(scc->a.tstate){
			case IDLE:			/* it was idle, this is FULLDUP2 timeout */
				scc_txoff(scc);		/* switch-off the transmitter */
				break;
			case DEFER:			/* trying to get the channel */
				/* operation is as follows:
				 * CSMA: when channel clear AND persistence randomgenerator
				 *	 wins, AND group restrictions allow it:
				 *		keyup the transmitter
				 *	 if not, delay one SLOTTIME and try again
				 * FULL: always keyup the transmitter
				 */
				if(scc->a.fulldup == 0){
					Random = 21 * Random + 53;
					if(scc->status & DCD || scc->a.persist < Random){
						/* defer transmission again. check for limit */
defer_it:					if(--(scc->a.maxdefer) == 0){
							/* deferred too long. choice is to:
							 * - throw away pending frames, or
							 * - smash-on the transmitter and send them.
							 * the first would be the choice in a clean
							 * environment, but in the amateur radio world
							 * a distant faulty station could tie us up
							 * forever, so the second may be better...
							*/
#ifdef THROW_AWAY_AFTER_DEFER_TIMEOUT
							struct mbuf *bp,*bp1;

							while ((bp = scc->sndq) != NULL){
								scc->sndq = scc->sndq->anext;
								free_p(&bp);
							}
#else
							goto keyup; /* just keyup the transmitter... */
#endif
						}
						scc->timercount = scc->a.slottime;
						break;
					}
					if(scc->group != NOGROUP){
						int i;
						struct sccchan *scc2;

						for(i = 0; i <= Sccinfo.maxchan; i++)
							if((scc2 = Sccchan[i]) != NULL &&
							 scc2 != scc &&
							 scc2->group & scc->group &&
							 ((scc->group & TXGROUP && scc2->wreg[R5] & RTS) ||
							 (scc->group & RXGROUP && scc2->status & DCD))){
								goto defer_it;
							}
					}
				}
			case KEYUP:			/* keyup transmitter (note fallthrough) */
keyup:				if((scc->wreg[R5] & RTS) == 0){ /* when not yet keyed */
					scc->a.tstate = KEYWT;
					scc->timercount = scc->a.txdelay; /* 0 if CTSwait */
					scc_txon(scc);
					break;
				}
				/* when already keyed, directly fall through */
			case KEYWT:			/* waited for CTS or TXDELAY */
				/* when a frame is available (it should be...):
				 * - dequeue it from the send queue
				 * - reset the transmitter CRC generator
				 * - set a timeout on transmission length, if defined
				 * - send the first byte of the frame
				 * - reset the EOM latch
				 * when no frame available, proceed to TAIL handling
				 */
				if((scc->tbp = scc->sndq) != NULL){
					scc->sndq = scc->sndq->anext;
					WRREG(scc->ctrl,RES_Tx_CRC);
					scc->a.tstate = ACTIVE;
					scc->timercount = TPS * scc->a.maxkeyup;
					scc_sdlctx(scc);
					WRREG(scc->ctrl,RES_EOM_L);
					break;
				}
				/* when no frame queued, fall through to TAIL case */
			case TAIL:			/* at end of frame */
				/* when fulldup is 0 or 1, switch off the transmitter.
				 * when frames are still queued (because of transmit time limit),
				 * restart the procedure to get the channel after MINTIME.
				 * when fulldup is 2, the transmitter remains keyed and we
				 * continue sending.	IDLETIME is an idle timeout in this case.
				 */	
				if(scc->a.fulldup < 2){
					scc->a.tstate = IDLE;
					scc_txoff(scc);

					if(scc->sndq != NULL){
						scc->a.tstate = DEFER;
						scc->a.maxdefer = TPS * scc->a.idletime /
						 scc->a.slottime;
						scc->timercount = TPS * scc->a.mintime;
					}
					break;
				}
				if(scc->sndq != NULL){ /* still frames on the queue? */
					scc->a.tstate = KEYWT; /* continue sending */
					scc->timercount = TPS * scc->a.mintime; /* after mintime */
				} else {
					scc->a.tstate = IDLE;
					scc->timercount = TPS * scc->a.idletime;
				}
				break;
			case ACTIVE:	/* max keyup time expired */
			case FLUSH:	/* same while in flush mode */
				break;	/* no action required yet */
			default:			/* unexpected state */
				scc->a.tstate = IDLE; /* that should not happen, but... */
				scc_txoff(scc);		/* at least stop the transmitter */
				break;
			}
		}
	}
	restore(i_state);
}
