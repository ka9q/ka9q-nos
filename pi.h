#ifndef	PIMAX

/* Hardware-dependent routines for the VE3IFB interface card for the PC
 */

#include "global.h"
#define PIMAX	3		/* 3 cards max */
#define AX_MTU	512
#define INTMASK 0x21		/* Intel 8259 interrupt controller mask */

#define	DMABASE	0	/* Base I/O address of 1st (8-bit) DMA controller */
struct PITAB {
	INTERRUPT (*oldvec)(void);	/* Original interrupt vector contents */
	uint16 addr;				/* Base I/O address */
	unsigned vec;				/* Vector */
	long ints;				/* Interrupt count */
	uint8 chain;				/* Enable interrupt chaining */
};
extern struct PITAB Pi[];

/* Register offset info, specific to the PI
 * E.g., to read the data port on channel A, use
 *	inportb(pichan[dev].base + CHANA + DATA)
 */
#define CHANB	0	/* Base of channel B regs */
#define CHANA	2	/* Base of channel A regs */

/* 8530 ports on each channel */
#define CTL	0
#define DATA	1

#define DMAEN	0x4 /* Offset off DMA Enable register */

/* Timer chip offsets */
#define TMR0	0x8 /* Offset of timer 0 register */
#define TMR1	0x9 /* Offset of timer 1 register */
#define TMR2	0xA /* Offset of timer 2 register */
#define TMRCMD	0xB /* Offset of timer command register */

/* Timer chip equates */
#define SC0	0x00 /* Select counter 0 */
#define SC1	0x40 /* Select counter 1 */
#define SC2	0x80 /* Select counter 2 */
#define CLATCH	0x00 /* Counter latching operation */
#define MSB	0x20 /* Read/load MSB only */
#define LSB	0x10 /* Read/load LSB only */
#define LSB_MSB	0x30 /* Read/load LSB, then MSB */
#define MODE0	0x00 /* Interrupt on terminal count */
#define MODE1	0x02 /* Programmable one shot */
#define MODE2	0x04 /* Rate generator */
#define MODE3	0x06 /* Square wave rate generator */
#define MODE4	0x08 /* Software triggered strobe */
#define MODE5	0x0a /* Hardware triggered strobe */
#define BCD	0x01 /* BCD counter */

/* DMA controller registers */
#define DMA_STAT	8	/* DMA controller status register */
#define DMA_MASK        10	/* DMA controller mask register	*/
#define DMA_MODE        11	/* DMA controller mode register	*/
#define DMA_RESETFF	12	/* DMA controller first/last flip flop	*/
/* DMA data */
#define DMA_DISABLE (0x04)	/* Disable channel n */
#define DMA_ENABLE	(0x00)	/* Enable channel n */
/* Single transfers, incr. address, auto init, writes, ch. n */
#define DMA_RX_MODE	(0x54)
/* Single transfers, incr. address, no auto init, reads, ch. n */
#define DMA_TX_MODE (0x48)

struct pichan {
	long rxints;		/* Receiver interrupts */
	long txints;		/* Transmitter interrupts */
	long exints;		/* External/status interrupts */

	int enqueued;		/* Packets enqueued for transmit */
	int rxframes;		/* Packets received */
	int crcerr;		/* CRC Errors */
	int rovers;		/* Receiver Overruns */
	int tunders;		/* Tranmitter underruns */

	uint8 *rcvbuf;		/* Buffer for current rx packet */
	int32 rcvphys;		/* Physical address of same, for DMA */
	uint8 *rcp;		/* Pointer into rcvbuf for non-dma */
	uint16 bufsiz;		/* Size of rcvbuf */
	uint16 rxcnt;		/* Running count (non-DMA) */

	struct mbuf *sndq;	/* Packets awaiting transmission */
	uint16 sndcnt;		/* Number of packets on sndq */
	uint8 *sndbuf;		/* Current buffer being transmitted */
	uint8 *tcp;		/* Pointer into sndbuf for non-DMA */
	uint16 txcnt;		/* Chars remaining to be sent (non-DMA) */
	int32 sndphys;		/* Physical address of sndbuf, for DMA */
	uint8 tstate;		/* Transmitter state */
#define IDLE	0		/* Transmitter off, no data pending */
#define ACTIVE	1		/* Transmitter on, sending data */
#define UNDERRUN 2		/* Transmitter on, flushing CRC */
#define FLAGOUT 3		/* CRC sent - attempt to start next frame */
#define DEFER 4 		/* Receive Active - DEFER Transmit */
#define ST_TXDELAY 5		/* Sending leading flags */
#define CRCOUT 6
	uint8 rstate;		/* Set when !DCD goes to 0 (TRUE) */
/* Normal state is ACTIVE if Receive enabled */
#define RXERROR 2		/* Error -- Aborting current Frame */
#define RXABORT 3		/* ABORT sequence detected */
#define TOOBIG 4		/* too large a frame to store */
	uint16 dev;		/* Device number */
	uint16 base;		/* Base of I/O registers */
	uint16 cardbase;		/* Base address of card */
	uint16 stata;		/* address of Channel A status regs */
	uint16 statb;		/* address of Channel B status regs */
	uint16 speed;		/* Line speed, bps */
	uint16 txdelay;		/* Transmit Delay 10 ms/cnt */
	uint8 persist;		/* Persistence (0-255) as a % */
	uint16 slotime;		/* Delay to wait on persistence hit */
	uint16 squeldelay;	/* Delay after XMTR OFF for squelch tail */
	struct iface *iface;	/* Associated interface */
	uint8 dmachan;		/* DMA channel for this port */
	int32 deftime;		/* Time when xmit is enabled */
};
extern struct pichan Pichan[];

#define OFF	0
#define ON	1

/* 8530 clock speed */
#define XTAL	((long)3686400/2)	 /* 32X clock constant */

/* In pi.c: */
INTERRUPT (far *(piint)(int dev))();

/* In pivec.asm: */
void mloop(void);
void wrtscc(uint16 cbase,uint16 ctl,uint16 reg,uint16 word);
uint8 rdscc(uint16 cbase,uint16 word,uint8 byte);
INTERRUPT pi0vec(void);
INTERRUPT pi1vec(void);
INTERRUPT pi2vec(void);

#endif	/* PIMAX */
