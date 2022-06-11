#ifndef	_PC100_H
#define	_PC100_H

/* UNFINISHED! */
/* Hardware-dependent routines for the PACCOMM PC-100 card for the PC
 * This card contains a Zilog 8530 plus two modems, one of which is an
 * AMD 7910, and one of which is a TCM3105.
 *
 * Originally written by Phil Karn KA9Q for an early rev of the board which
 * did interrupt acks wrong.  Hacked by Bdale Garbee N3EUA to work with beta
 * copies of the second major revision of the card.
 */
#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#define	NPC	1		/* One card max */
#define	AX_MTU	512
#define	INTMASK	0x21		/* Intel 8259 interrupt controller mask */

struct pc100 {
	INTERRUPT (*oldvec)(void);/* Original interrupt vector contents */
	uint16 addr;	/* Base I/O address */
	unsigned vec;	/* Vector */
	long ints;	/* Interrupt count */
	uint8 chain;	/* Interrupt chaining enable */	
};
extern struct pc100 Pc100[];

/* Register offset info, specific to the PC-100
 * E.g., to read the data port on channel A, use
 *      inportb(hdlc[dev].base + CHANA + DATA)
 */
#define	MODEM_CTL	0	/* modem control port, write only */
/* #define	CHANB		4	/* Base of channel B regs */
/* #define	CHANA		6	/* Base of channel A regs */
#define	CHANB		6	/* Base of channel B regs */
#define	CHANA		4	/* Base of channel A regs */
#define INTACK		8	/* access this to wang 8530's INTA.L */

/* 8530 ports on each channel */
/* #define	CTL	0 */
/* #define	DATA	1 */
#define	CTL	1
#define	DATA	0

struct hdlc {
	long rxints;		/* Receiver interrupts */
	long txints;		/* Transmitter interrupts */
	long exints;		/* External/status interrupts */
	long spints;		/* Special receiver interrupts */

	int nomem;		/* Buffer allocate failures */
	int toobig;		/* Giant receiver packets */
	int crcerr;		/* CRC Errors */
	int aborts;		/* Receiver aborts */

	uint8 status;		/* Copy of R0 at last external interrupt */
	struct mbuf *rcvq;	/* Receive queue */
	uint16 rcvcnt;		/* Number of packets on rcvq */
	struct mbuf *rcvbuf;	/* Buffer for current rx packet */
	uint16 bufsiz;		/* Size of rcvbuf */
	uint8 *rcp;		/* Pointer into rcvbuf */

	struct mbuf *sndq;	/* Packets awaiting transmission */
	uint16 sndcnt;		/* Number of packets on sndq */
	struct mbuf *sndbuf;	/* Current buffer being transmitted */
	int tstate;		/* Tranmsitter state */
#define	IDLE	0		/* Transmitter off, no data pending */
#define	DEFER	1		/* Transmitter off, waiting for channel clear*/
#define	KEYUP	2		/* Transmitter on, waiting for CTS */
#define	ACTIVE	3		/* Transmitter on, sending data */
#define	FLUSH	4		/* Transmitter on, flushing tx buffers */
#define	FIN2	5		/* Ditto, second char */

	int mode;		/* Transmitter mode */
#define	CSMA	0		/* Carrier-sense multiple access */
#define	FULLDUP	1		/* Full duplex */

	int dev;		/* Device number */
	uint16 base;		/* Base of I/O registers */
	uint16 speed;		/* Line speed, bps */
};

#define	OFF	0
#define	ON	1

/* 8530 clock speed */
/* #define	XTAL	((unsigned)38400)	/* 2.4576 Mhz / (2*32) */
#define	XTAL	((unsigned)76797)	/* 4.915 Mhz / (2*32) */

/* Baud rate generator definitions */
struct baudrate {
	uint16 speed;
	uint8 val;
};

/* In pc100.c: */
INTERRUPT (far *(pcint)(int dev))();

/* In pc100vec.asm: */
INTERRUPT pc0vec(void);

#endif	/* _PC100_H */
