#ifndef	_DRSI_H
#define _DRSI_H

/* Hardware-dependent routines for the DRSI card for the PC
 * This card contains a Zilog 8530, ZILOG 8536 and a modem.
 */
#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#define DRMAX	1		/* One card max */
#define AX_MTU	512
#define INTMASK 0x21		/* Intel 8259 interrupt controller mask */

struct DRTAB {
		/* Original interrupt vector contents */
	INTERRUPT (*oldvec)(void);
	uint16 addr;		/* Base I/O address */
	uint16 vec;		/* Vector */
	long ints;		/* Interrupt count */
	uint8 chain;		/* Interrupt chaining enable */
};
extern struct DRTAB Drsi[];
extern uint16 Drnbr;

/* Register offset info, specific to the DRSI
 * E.g., to read the data port on channel A, use
 *	inportb(drchan[dev].base + CHANA + DATA)
 */
#define CHANB		0	/* Base of channel B regs */
#define CHANA		2	/* Base of channel A regs */

/* 8530 ports on each channel */
#define CTL	0
#define DATA	1
#define DMACTRL 	4	/* Base of channel + 4 */

/* 8536 ports */
#define Z8536_MASTER	7	/* 8536 master register offset from BASE */

/* Struct used to contain wakeup data */

struct dr_wakeup {
    void (*wcall)(struct drchan *hp);
    int wakecnt;
};

#define RX	0		/* Index into wake array */
#define TX	1		/* ----------""--------- */
struct drchan {
	long rxints;		/* Receiver interrupts */
	long txints;		/* Transmitter interrupts */
	long exints;		/* External/status interrupts */
	long spints;		/* Special receiver interrupts */

	long enqueued;		/* Packets actually forwarded */
	long rxframes;		/* Number of Frames Actally Received */
	long toobig;		/* Giant receiver packets */
	long crcerr;		/* CRC Errors */
	long aborts;		/* Receiver aborts */
	long rovers;		/* Receiver Overruns */
	long txdefers;		/* Number of defered transmit attempts */
	long txppersist;	/* Tx attempts defered due to p-persist */
	long nomem;		/* No TX buffer available for local buffer */
	uint8 status;		/* Copy of R0 at last external interrupt */
	struct mbuf *rcvbuf;	/* Buffer for current rx packet */
	uint16 bufsiz;		/* Size of rcvbuf */
	uint8 *rcp;		/* Pointer into rcvbuf */

	struct mbuf *sndq;	/* Packets awaiting transmission */
	uint16 sndcnt;		/* Number of packets on sndq */
	struct mbuf *sndbuf;	/* Current buffer being transmitted */
	uint16 drtx_cnt;		/* Number of chars left in local buffer */
	uint8 *drtx_buffer;	/* Local Tx buffer (copy of data buffer) */
	uint8 *drtx_tcp;		/* Pointer into local buffer */
	struct dr_wakeup w[2];	/* One wakeup for Rx and one for Tx */
		/* Tx state machine - state processor */
	void (*tx_state)(struct drchan *hp);
		/* Rx state machine - state processor */
	void (*rx_state)(struct drchan *hp);
	uint8 tstate;		/* Tranmsitter state */
#define IDLE	0		/* Transmitter off, no data pending */
#define ACTIVE	1		/* Transmitter on, sending data */
#define UNDERRUN 2		/* Transmitter on, flushing CRC */
#define FLAGOUT 3		/* CRC sent - attempt to start next frame */
#define DEFER 4 		/* Receive Active - DEFER Transmit */
#define RRTS  5			/* Raise RTS */
#define TFIRST 6		/* Transmit first character */
#define DOWNTX 7		/* Return to receive after ENDDELAY */
	uint8 rstate;		/* Set when !DCD goes to 0 (TRUE) */
/* Normal state is ACTIVE if Receive enabled */
#define RXERROR 2		/* Error -- Aborting current Frame */
#define RXABORT 3		/* ABORT sequence detected */
#define TOOBIG 4		/* too large a frame to store */
#define ENABLE 5		/* Enable RX */
	uint16 dev;		/* Device number */
	uint16 base;		/* Base of I/O registers */
	uint16 stata;		/* address of Channel A status regs */
	uint16 statb;		/* address of Channel B status regs */
	uint16 dmactrl;		/* address of DMA/INTERRUPT reg on card */
	uint16 speed;		/* Line speed, bps */
	int txdelay;		/* Transmit Delay 10 ms/cnt */
	int persist;		/* Persistence (0-255) as a % */
	int slotime;		/* Delay to wait on persistence hit */
	int squeldelay;		/* Delay after XMTR OFF for seuelch tail */
	int enddelay;		/* Hold up after sending last char in frame */
	struct iface *iface;	/* Associated interface */
};
extern struct drchan drchan[];

/* In drsi.c: */
INTERRUPT (far *(drint)(int dev))();

/* Interrupt vector handlers (assembler) in drsivec.asm */
INTERRUPT dr0vec(void);


#define OFF	0
#define ON	1
#define INIT	2

/* 8530 clock speed */

#define XTAL	((long) 4915200/2)	 /* 32X clock constant */

/*************************************************************/
/* TEMP FOR DEBUG ONLY - eliminates Divide by zero interrupt */
/*		       - preset for 1200 BAUD !!!!!!!!!!!!!! */
/*************************************************************/
#define TXCONST 2046	/* (XTAL/1200L)-2 */
#define RXCONST 62	/* ((XTAL/32)/1200L)-2 */


/* Baud rate generator definitions */
struct baudrate {
	uint16 speed;
	uint8 val;
};


/* 8536 register definitions */

#define Z8536_MICR  0x00    /* Master interrupt control register */
#define Z8536_MCCR  0x01    /* Master configuration control register */
#define Z8536_CTMS1 0x1c    /* Counter/timer mode specification #1 */
#define Z8536_CTMS2 0x1d    /* Counter/timer mode specification #2 */
#define Z8536_CTMS3 0x1e    /* Counter/timer mode specification #3 */
#define Z8536_IVR   0x04    /* Interrupt vector register */

#define Z8536_CSR1  0x0a    /* Command and status register CTC #1 */
#define Z8536_CSR2  0x0b    /* Command and status register CTC #2 */
#define Z8536_CSR3  0x0c    /* Command and status register CTC #3 */

#define Z8536_CC3LSB 0x15    /* CTC #3 Current count - LSB  */

#define	Z8536_CT1MSB 0x16    /* CTC #1 Timer constant - MSB */
#define	Z8536_CT1LSB 0x17    /* CTC #1 Timer constant - LSB */
#define	Z8536_CT2MSB 0x18    /* CTC #2 Timer constant - MSB */
#define	Z8536_CT2LSB 0x19    /* CTC #2 Timer constant - LSB */
#define	Z8536_CT3MSB 0x1a    /* CTC #3 Timer constant - MSB */
#define	Z8536_CT3LSB 0x1b    /* CTC #3 Timer constant - LSB */
#define Z8536_PDCA   0x23    /* Port A data direction control */
#define Z8536_PDCB   0x2b    /* Port B data direction control */

#define Z_GCB	    0x04    /* CTC Gate command bit */
#define Z_TCB	    0x02    /* CTC Trigger command bit */
#define Z_IE	    0xc0    /* CTC Interrupt enable (set) */
#define Z_CIP	    0x20    /* CTC Clear interrupt pending */
#define Z_IP	    0x20    /* CTC Interrupt pending */

#endif	/* _DRSI_H */
