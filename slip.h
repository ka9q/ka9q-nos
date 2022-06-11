#ifndef	_SLIP_H
#define	_SLIP_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _SLHC_H
#include "slhc.h"
#endif

#define SLIP_MAX 6		/* Maximum number of slip channels */

/* SLIP definitions */

/* Receiver allocation increment
 * Make this match the medium mbuf size in mbuf.h for best performance
 */
#define	SLIP_ALLOC	128

#define	FR_END		0300	/* Frame End */
#define	FR_ESC		0333	/* Frame Escape */
#define	T_FR_END	0334	/* Transposed frame end */
#define	T_FR_ESC	0335	/* Transposed frame escape */

/* Slip protocol control structure */
struct slip {
	struct iface *iface;
	uint8 escaped;		/* Receiver State control flag */
#define SLIP_FLAG	0x01		/* Last char was a frame escape */
#define SLIP_VJCOMPR	0x02		/* TCP header compression enabled */
	struct mbuf *rbp_head;	/* Head of mbuf chain being filled */
	struct mbuf *rbp_tail;	/* Pointer to mbuf currently being written */
	uint8 *rcp;		/* Write pointer */
	uint16 rcnt;		/* Length of mbuf chain */
	struct mbuf *tbp;	/* Transmit mbuf being sent */
	uint16 errors;		/* Receiver input errors */
	int type;		/* Protocol of input */
	int (*send)(int,struct mbuf **);	/* send mbufs to device */
	int (*get)(int);	/* fetch input chars from device */
	struct slcompress *slcomp;	/* TCP header compression table */
};

/* In slip.c: */
extern struct slip Slip[];

void asytxdone(int dev);
int slip_free(struct iface *ifp);
int slip_init(struct iface *ifp);
int slip_raw(struct iface *iface,struct mbuf **data);
void slip_rx(int xdev,void *p1,void *p2);
int slip_send(struct mbuf **bp,struct iface *iface,int32 gateway,uint8 tos);
int vjslip_send(struct mbuf **bp,struct iface *iface,int32 gateway,uint8 tos);
void slip_status(struct iface *iface);

#endif	/* _SLIP_H */
