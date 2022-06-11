#ifndef	_NRS_H
#define	_NRS_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#define NRS_MAX 5		/* Maximum number of Nrs channels */

/* SLIP definitions */
#define	NRS_ALLOC	40	/* Receiver allocation increment */

#define STX	0x02		/* frame start */
#define ETX 0x03		/* frame end */
#define DLE	0x10		/* data link escape */
#define NUL 0x0			/* null character */

/* packet unstuffing state machine */
#define NRS_INTER	0		/* in between packets */
#define NRS_INPACK	1		/* we've seen STX, and are in a the packet */
#define NRS_ESCAPE	2		/* we've seen a DLE while in NRS_INPACK */
#define NRS_CSUM	3		/* we've seen an ETX, and are waiting for the checksum */

/* net/rom serial protocol control structure */
struct nrs {
	uint8 state;		/* Receiver State control flag */
	unsigned char csum;	/* Accumulating checksum */
	struct mbuf *rbp;	/* Head of mbuf chain being filled */
	struct mbuf *rbp1;	/* Pointer to mbuf currently being written */
	uint8 *rcp;		/* Write pointer */
	uint16 rcnt;		/* Length of mbuf chain */
	struct mbuf *tbp;	/* Transmit mbuf being sent */
	long errors;		/* Checksum errors detected */
	long packets ;		/* Number of packets received successfully */
	struct iface *iface ;	/* Associated interface structure */
	int (*send)(int,struct mbuf **);/* Routine to send mbufs */
	int (*get)(int);/* Routine to fetch input chars */
};

extern struct nrs Nrs[];
/* In nrs.c: */
int nrs_free(struct iface *ifp);
int nrs_init(struct iface *ifp);
int nrs_raw(struct iface *iface,struct mbuf **bpp);
void nrs_recv(int dev,void *v1,void *v2);

#endif	/* _NRS_H */
