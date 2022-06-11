#ifndef	_EAGLE_H
#define	_EAGLE_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

/* Hardware-dependent routines for the EAGLE card for the PC
 * This card contains a Zilog 8530 only - no modem!
 */
#define EGMAX	1		/* One card max */
#define AX_MTU	512
#define INTMASK 0x21		/* Intel 8259 interrupt controller mask */

struct EGTAB {
	INTERRUPT (*oldvec)(void);	/* Original interrupt vector contents */
	uint16 addr;		/* Base I/O address */
	unsigned vec;		/* Vector */
	long ints;		/* Interrupt count */
	uint8 chain;		/* Interrupt chaining enable */	
};
extern struct EGTAB Eagle[];

/* Register offset info, specific to the EAGLE
 * E.g., to read the data port on channel A, use
 *	inportb(egchan[dev].base + CHANA + DATA)
 */
#define CHANB		0	/* Base of channel B regs */
#define CHANA		2	/* Base of channel A regs */

/* 8530 ports on each channel */
#define CTL	0
#define DATA	1
#define DMACTRL 	4	/* Base of channel + 4 */

/* EAGLE DMA/INTERRUPT CONTROL REGISTER */
#define DMAPORT 	0	/* 0 = Data Port */
#define INTPORT 	1	/* 1 = Interrupt Port */
#define DMACHANA	0	/* 0 = DMA on CHANNEL A */
#define DMACHANB	2	/* 1 = DMA on Channel B */
#define DMADISABLE	0	/* 0 = DMA disabled */
#define DMAENABLE	4	/* 1 = DMA enabled */
#define INTDISABLE	0	/* 0 = Interrupts disabled */
#define INTENABLE	8	/* 1 = Interrupts enabled */
#define INTACKTOG	10	/* 1 = INT ACK TOGGLE */


struct egchan {
	long rxints;		/* Receiver interrupts */
	long txints;		/* Transmitter interrupts */
	long exints;		/* External/status interrupts */
	long spints;		/* Special receiver interrupts */

	int enqueued;		/* Packets actually forwarded */
	int rxframes;		/* Number of Frames Actally Received */
	int toobig;		/* Giant receiver packets */
	int crcerr;		/* CRC Errors */
	int aborts;		/* Receiver aborts */
	int rovers;		/* Receiver Overruns */

	uint8 status;		/* Copy of R0 at last external interrupt */
	struct mbuf *rcvbuf;	/* Buffer for current rx packet */
	uint16 bufsiz;		/* Size of rcvbuf */
	uint8 *rcp;		/* Pointer into rcvbuf */

	struct mbuf *sndq;	/* Packets awaiting transmission */
	uint16 sndcnt;		/* Number of packets on sndq */
	struct mbuf *sndbuf;	/* Current buffer being transmitted */
	uint8 tstate;		/* Tranmsitter state */
#define IDLE	0		/* Transmitter off, no data pending */
#define ACTIVE	1		/* Transmitter on, sending data */
#define UNDERRUN 2		/* Transmitter on, flushing CRC */
#define FLAGOUT 3		/* CRC sent - attempt to start next frame */
#define DEFER 4 		/* Receive Active - DEFER Transmit */
	uint8 rstate;		/* Set when !DCD goes to 0 (TRUE) */
/* Normal state is ACTIVE if Receive enabled */
#define RXERROR 2		/* Error -- Aborting current Frame */
#define RXABORT 3		/* ABORT sequence detected */
#define TOOBIG 4		/* too large a frame to store */
	int dev;		/* Device number */
	uint16 base;		/* Base of I/O registers */
	uint16 stata;		/* address of Channel A status regs */
	uint16 statb;		/* address of Channel B status regs */
	uint16 dmactrl;		/* address of DMA/INTERRUPT reg on card */
	uint16 speed;		/* Line speed, bps */
	uint8 txdelay;		/* Transmit Delay 10 ms/cnt */
	uint8 persist;		/* Persistence (0-255) as a % */
	uint8 slotime;		/* Delay to wait on persistence hit */
	uint8 squeldelay;	/* Delay after XMTR OFF for squelch tail */
	struct iface *iface;	/* Associated interface */
};
extern struct egchan Egchan[];


#define OFF	0
#define ON	1
#define INIT	2

/* 8530 clock speed */

#define XTAL	((long) 3686400/2)	 /* 32X clock constant */

/*************************************************************/
/* TEMP FOR DEBUG ONLY - eliminates Divide by zero interrupt */
/*		       - preset for 1200 BAUD !!!!!!!!!!!!!! */
/*************************************************************/
#define TXCONST 1534			 /* (XTAL/1200L)-2 */
#define RXCONST 46			 /* ((XTAL/32)/1200L)-2 */

/* Baud rate generator definitions */
struct baudrate {
	uint16 speed;
	uint8 val;
};

/* In eagle.c: */
INTERRUPT (far *(egint)(int dev))();

/* In eaglevec.asm: */
INTERRUPT eg0vec(void);

#endif	/* _EAGLE_H */
