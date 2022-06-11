#ifndef	_SCC_H
#define	_SCC_H

/* Definitions for Z8530 SCC driver by PE1CHL
 * Adapted for NOS 1/23/90
 * Ken Mitchum KY3B
 * km@speedy.cs.pitt.edu
 * km@cadre.dsl.pitt.edu
 *
 * Support added for Sealevel Systems Inc's ACB-IV 8530 card (HWSEALEVEL)
 * 5 Aug 91, Tom Jennings, Cygnus Support (tomj@cygnus.com). See comments
 * in SCC.C
 *
 */

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#define INLINE 1

typedef uint16 ioaddr;		/* type definition for an 'io port address' */
#define MAXSCC		4	/* maximal number of SCC chips supported */
#define TPS		(1000/MSPTICK)	/* scctim() ticks per second  */

# if defined(INLINE)
/* special delay construction only necessary when inline IN/OUT is used */
#define D(v)		scc_delay(v)	/* delay for 5 PCLK cycles (or more) */
#define RDREG(a)	(D(inportb(a)))		/* read any input port */
#define WRREG(a,v)	{outportb(a,v); D(1);}	/* write any output port */
#define RDSCC(c,r)	(outportb(c,r), D(1), D(inportb(c))) /* read SCC reg */
#define WRSCC(c,r,v)	{outportb(c,r); D(1); outportb(c,v); D(1);} /* write SCC reg*/
# else
#define RDREG(a)	(inportb(a))		/* read any input port */
#define WRREG(a,v)	{outportb(a,v);}	/* write any output port */
#define RDSCC(c,r)	(outportb(c,r), inportb(c)) /* read SCC reg */
#define WRSCC(c,r,v)	{outportb(c,r); outportb(c,v);} /* write SCC reg */
# endif

#define HWEAGLE		0x01	/* hardware type for EAGLE card */
#define HWPC100		0x02	/* hardware type for PC100 card */
#define HWPRIMUS		0x04	/* hardware type for PRIMUS-PC (DG9BL) card */
#define HWDRSI		0x08	/* hardware type for DRSI PC*Packet card */
#define HWSEALEVEL	0x10	/* hardware type for Sealevel ACB-IV card */

#ifndef VOID
#define VOID(x)		(x)	/* not necessary for most compilers */
#endif

struct sccinfo {
	int init;		/* SCC driver initialized? */
	int nchips;		/* Number of SCC chips in system */
	int maxchan;		/* Highest valid channel number */
	ioaddr iobase;		/* Base address of first SCC */
	int space;		/* Spacing between subsequent SCCs */
	int off[2];		/* Offset to A and B channel control regs */
	int doff;		/* Offset from control to data register */
	int ivec;		/* System interrupt vector number */
	long clk;		/* PCLK/RTxC frequency in Hz */
	int pclk;		/* flag to use PCLK (instead of RTxC) */
	int hwtype;		/* special hardware type indicator */
	int hwparam;		/* special hardware parameter */
};
extern struct sccinfo sccinfo;

/* SCC channel control structure for AX.25 mode */
struct scca {
	unsigned int maxdefer;	/* Timer for CSMA defer time limit */

	unsigned int tstate;		/* Transmitter state */
#define IDLE		0	/* Transmitter off, no data pending */
#define DEFER		1	/* Receive Active - DEFER Transmit */
#define KEYUP		2	/* Permission to keyup the transmitter */
#define KEYWT		3	/* Transmitter switched on, waiting for CTS */
#define ACTIVE		4	/* Transmitter on, sending data */
#define FLUSH		5	/* CRC sent - attempt to start next frame */
#define TAIL		6	/* End of transmission, send tail */

	unsigned char txdelay;	/* Transmit Delay 10 ms/cnt */
	unsigned char persist;	/* Persistence (0-255) as a % */
	unsigned char slottime;	/* Delay to wait on persistence hit */
	unsigned char tailtime;	/* Delay after XMTR OFF */
	unsigned char fulldup;	/* Full Duplex mode 0=CSMA 1=DUP 2=ALWAYS KEYED */
	unsigned char waittime;	/* Waittime before any transmit attempt */
	unsigned char maxkeyup;	/* Maximum time to transmit (seconds) */
	unsigned char mintime;	/* Minimal offtime after MAXKEYUP timeout */
	unsigned char idletime;	/* Maximum idle time in ALWAYS KEYED mode (seconds) */
};

/* SCC channel structure. one is allocated for each attached SCC channel, */
/* so 2 of these are allocated for each fully utilized SCC chip */
struct sccchan {
	/* interrupt handlers for 4 different IP's */
	/* MUST BE first 4 elements of this structure, and MUST remain */
	/* in the sequence Transmit-Status-Receive-Special */
	void (*int_transmit)(); /* Transmit Buffer Empty interrupt handler */
	void (*int_extstat)();	/* External/Status Change interrupt handler */
	void (*int_receive)();	/* Receive Character Avail. interrupt handler */
	void (*int_special)();	/* Special Receive Condition interrupt handler */

	/* don't insert anything before "ctrl" (see assembly interrupt handler) */
	ioaddr ctrl;		/* I/O address of CONTROL register */
	ioaddr data;		/* I/O address of DATA register for this channel */

	unsigned char wreg[16]; /* Copy of last written value in WRx */
	unsigned char status;	/* Copy of R0 at last external interrupt */
	unsigned char txchar;	/* Char to transmit on next TX interrupt */

	struct fifo fifo;
	struct scca a;	/* control structure for AX.25 use */
	
	struct mbuf *rbp;	/* Head of mbuf chain being filled */
	struct mbuf *rbp1;	/* Pointer to mbuf currently being written */
	struct mbuf *sndq;	/* Encapsulated packets awaiting transmission */
	struct mbuf *tbp;	/* Transmit mbuf being sent */

	struct iface *iface; /* associated interface structure */

	int bufsiz;

	unsigned int timercount;/* 10ms timer for AX.25 use */
	int group;		/* group ID for AX.25 TX interlocking */
#define NOGROUP		0	/* not member of any group */
#define RXGROUP		0x100	/* if set, only tx when all channels clear */
#define TXGROUP		0x200	/* if set, don't transmit simultaneously */

	long speed;		/* Line speed, bps */
	char extclock;		/* External clock source on RTxC/TRxC */
	char fulldup;		/* External divider for fulldup available */
	char tx_inhibit;	/* Transmit is not allowed when set */
	char dum;		/* filler (keep addr even for speed) */

	/* statistic information on this channel */
	long rxints;		/* Receiver interrupts */
	long txints;		/* Transmitter interrupts */
	long exints;		/* External/status interrupts */
	long spints;		/* Special receiver interrupts */

	long enqueued;		/* Packets actually forwarded */
	long rxframes;		/* Number of Frames Actally Received */
	long rxerrs;		/* CRC Errors or KISS errors */
	unsigned int nospace;	/* "Out of buffers" */
	unsigned int rovers;	/* Receiver Overruns */
};
extern struct sccchan *sccchan[];

/* Z8530 SCC Register access macros */

#define rd(scc,reg)	RDSCC((scc)->ctrl,(reg))
#define wr(scc,reg,val) WRSCC((scc)->ctrl,(reg),((scc)->wreg[reg] = val))
#define or(scc,reg,val) WRSCC((scc)->ctrl,(reg),((scc)->wreg[reg] |= val))
#define cl(scc,reg,val) WRSCC((scc)->ctrl,(reg),((scc)->wreg[reg] &= ~(val)))

#endif	/* _SCC_H */
