#ifndef	_HS_H
#define	_HS_H
/* Hardware-dependent routines for the DRSI or Eagle cards for the PC
 * driving a high speed modem. These cards both contain Zilog 8530s.
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

#define	NHS	1		/* One card max */

struct hs {
	struct {
		INTERRUPT (*vec)(void);
	} save;

	uint16 addr;	/* Base I/O adHsess */
	uint16 vec;	/* Vector */
	long ints;	/* Interrupt count */
	uint8 chain;	/* Interrupt chaining enable */
};
extern struct hs Hs[];

/* Register offset info, specific to the DRSI PCPA and Eagle cards
 * E.g., to read the data port on channel A, use
 *      inportb(hdlc[dev].base + CHANA + DATA)
 */
#define	CHANB		0	/* Base of channel B regs */
#define	CHANA		2	/* Base of channel A regs */

/* 8530 ports on each channel */
#define	CTL	0
#define	DATA	1

struct hdlc {
	long rxints;		/* Receiver interrupts */
	long txints;		/* Transmitter interrupts */
	long exints;		/* External/status interrupts */
	long spints;		/* Special receiver interrupts */
	long rxbytes;		/* Total receive bytes */
	long nomem;		/* Buffer allocate failures */
	long toobig;		/* Giant receiver packets */
	long crcerr;		/* CRC Errors */
	long aborts;		/* Receiver aborts */
	long good;		/* Valid frames */
	long txpkts;
	long overrun;		/* Receiver overruns */

	uint16 bufsiz;		/* Size of rcvbuf */

	int dev;		/* Device number */
	int clkrev;		/* Clock pins swapped */
	uint16 ctl;		/* Control register */
	uint16 data;		/* Data register */
	uint16 speed;		/* Line speed, bps */
	long txdelay;		/* Keyup delay, ticks */ 
	uint8 p;			/* P-persistence value */
	struct mbuf *txq;	/* Transmit queue */

	struct iface *iface;	/* Associated interface */
	int32 deftime;		/* Time when we can next transmit */
};

#define	OFF	0
#define	ON	1

/* Baud rate generator definitions */
struct baudrate {
	uint16 speed;
	uint8 val;
};
/* In hs.c: */
INTERRUPT (far *(hsint)(int dev))();

/* In hsvec.asm: */
INTERRUPT hs0vec(int dev);

#endif /* _HS_H */
