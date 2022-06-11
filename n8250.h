/* Various I/O definitions specific to asynch I/O on the IBM PC */
#ifndef	_N8250_H
#define	_N8250_H

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

/* Output pseudo-dma control structure */
struct dma {
	uint8 *data;		/* current output pointer */
	unsigned short cnt;	/* byte count remaining */
	volatile uint8 busy;	/* transmitter active */
};

/* Read fifo control structure */
struct fifo {
	uint8 *buf;		/* Ring buffer */
	unsigned bufsize;	/* Size of ring buffer */
	uint8 *wp;		/* Write pointer */
	uint8 *rp;		/* Read pointer */
	volatile unsigned short cnt;	/* count of characters in buffer */
	unsigned short hiwat;	/* High water mark */
	long overrun;		/* count of sw fifo buffer overruns */
};

/* Asynch controller control block */
struct asy {
	struct iface *iface;
	struct fifo fifo;
	int trigchar;		/* Fifo trigger character */

	struct dma dma;

	unsigned addr;		/* Base I/O address */
	int vec;		/* Interrupt vector */
	long speed;		/* Line speed in bits per second */

	struct {		/* Previous configuration saved at startup */
		INTERRUPT (*vec)(void);
				/* Original interrupt vector [cs:pc] */
		uint8 mask;	/* 8259 mask */
		uint8 divh,divl;	/* baud rate divisor */
		uint8 lcr;	/* line control reg */
		uint8 ier;	/* Interrupt enable register */
		uint8 mcr;	/* modem control bits */
		uint8 msr;	/* modem status bits */
		uint8 iir;	/* Interrupt ind register (for fifo bits) */
	} save;

	uint8 is_16550a;		/* 16550A detected */
	uint8 msr;		/* Copy of current modem status register */
	uint8 cts;		/* obey CTS lead for xmit flow control */
	uint8 rlsd;		/* RLSD (CD) indicates line state */
	uint8 chain;		/* Do interrupt vector chaining */

	long fifotimeouts;	/* hw fifo character timeout int's */
	long rxints;		/* receive interrupts */
	long txints;		/* transmit interrupts */
	long rxchar;		/* Received characters */
	long overrun;		/* Receiver hardware overrun errors */
	long txchar;		/* Transmitted characters */
	long rxhiwat;		/* High water mark on hardware rx fifo */
	long msint_count;	/* Count of modem status interrupts */
	long txto;		/* Count of TX interrupt timeouts */
	long cdchanges;		/* Count of CD changes */
};

extern int Nasy;		/* Actual number of asynch lines */
extern struct asy Asy[];

struct fport {
	int base;
	int irq;
	struct asy *asy[4];		/* Pointers to regular asy entries */
	int iv;			/* Interrupt demux port */
};
extern int Nfport;
extern struct fport Fport[];

#define	BAUDCLK	115200L		/* 1.8432 Mhz / 16 */

/* National 8250 asynch UART definitions */
/* Control/status register offsets from base address */
#define	THR	0		/* Transmitter holding register */
#define	RBR	0		/* Receiver buffer register */
#define	DLL	0		/* Divisor latch LSB */
#define	DLM	1		/* Divisor latch MSB */
#define	IER	1		/* Interrupt enable register */
#define	IIR	2		/* Interrupt ident register */
#define	FCR	2		/* FIFO control register (16550A only) */
#define	LCR	3		/* Line control register */
#define	MCR	4		/* Modem control register */
#define	LSR	5		/* Line status register */
#define	MSR	6		/* Modem status register */

/* 8250 Line Control Register */
#define	LCR_5BITS	0x00	/* 5 bit words */
#define	LCR_6BITS	0x01	/* 6 bit words */
#define	LCR_7BITS	0x02	/* 7 bit words */
#define	LCR_8BITS	0x03	/* 8 bit words */
#define LCR_BIT_MASK	0x03	/* mask of bit size */
#define	LCR_NSB		0x04	/* Number of stop bits */
#define	LCR_PEN		0x08	/* Parity enable */
#define	LCR_EPS		0x10	/* Even parity select */
#define	LCR_SP		0x20	/* Stick parity */
#define	LCR_SB		0x40	/* Set break */
#define	LCR_DLAB	0x80	/* Divisor Latch Access Bit */

/* 8250 Line Status Register */
#define	LSR_DR		0x01	/* Data ready */
#define	LSR_OE		0x02	/* Overrun error */
#define	LSR_PE		0x04	/* Parity error */
#define	LSR_FE		0x08	/* Framing error */
#define	LSR_BI		0x10	/* Break interrupt */
#define LSR_THRE	0x20	/* Transmitter line holding register empty */
#define LSR_TSRE	0x40	/* Transmitter shift register empty */

/* 8250 Interrupt Identification Register */
#define	IIR_IP		0x01	/* 0 if interrupt pending */

#define	IIR_MSTAT	0x00	/* Modem status interrupt */
#define	IIR_THRE	0x02	/* Transmitter holding register empty int */
#define	IIR_RDA		0x04	/* Receiver data available interrupt */
#define	IIR_RLS		0x06	/* Receiver Line Status interrupt */
#define IIR_ID_MASK	0x06	/* Mask for interrupt ID */

#define IIR_FIFO_TIMEOUT 0x08	/* FIFO timeout interrupt pending - 16550A */
#define IIR_FIFO_ENABLED 0xc0	/* FIFO enabled (FCR0,1 = 1) - 16550A only */


/* 8250 interrupt enable register bits */
#define	IER_DAV		0x01	/* Data available interrupt */
#define	IER_TxE		0x02	/* Tx buffer empty interrupt */
#define	IER_RLS		0x04	/* Receive line status interrupt */
#define	IER_MS		0x08	/* Modem status interrupt */

/* 8250 Modem control register */
#define	MCR_DTR		0x01	/* Data Terminal Ready */
#define	MCR_RTS		0x02	/* Request to Send */
#define	MCR_RFR		MCR_RTS	/* Ready for Receiving (same as RTS) */
#define	MCR_OUT1	0x04	/* Out 1 (not used) */
#define	MCR_OUT2	0x08	/* Master interrupt enable (actually OUT 2) */
#define	MCR_LOOP	0x10	/* Loopback test mode */

/* 8250 Modem Status Register */
#define	MSR_DCTS	0x01	/* Delta Clear-to-Send */
#define	MSR_DDSR	0x02	/* Delta Data Set Ready */
#define	MSR_TERI	0x04	/* Trailing edge ring indicator */
#define	MSR_DRLSD	0x08	/* Delta Rx Line Signal Detect */
#define	MSR_CTS		0x10	/* Clear to send */
#define	MSR_DSR		0x20	/* Data set ready */
#define	MSR_RI		0x40	/* Ring indicator */
#define MSR_RLSD	0x80	/* Rx line signal detect */

/* 16550A FIFO control register values */
#define	FIFO_ENABLE	0x01	/* enable TX & RX fifo */
#define	FIFO_CLR_RX	0x02	/* clear RX fifo */
#define	FIFO_CLR_TX	0x04	/* clear TX fifo */
#define	FIFO_START_DMA	0x08	/* enable TXRDY/RXRDY pin DMA handshake */
#define FIFO_SIZE_1	0x00	/* RX fifo trigger levels */
#define FIFO_SIZE_4	0x40
#define FIFO_SIZE_8	0x80
#define FIFO_SIZE_14	0xC0
#define FIFO_SIZE_MASK	0xC0

#define FIFO_TRIGGER_LEVEL	FIFO_SIZE_4
#define FIFO_SETUP	(FIFO_ENABLE|FIFO_CLR_RX|FIFO_CLR_TX|FIFO_TRIGGER_LEVEL)

#define OUTPUT_FIFO_SIZE	16

/* In asyvec.asm: */
INTERRUPT asy0vec(void);
INTERRUPT asy1vec(void);
INTERRUPT asy2vec(void);
INTERRUPT asy3vec(void);
INTERRUPT asy4vec(void);
INTERRUPT asy5vec(void);

#endif	/* _N8250_H */
