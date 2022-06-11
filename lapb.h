#ifndef	_LAPB_H
#define	_LAPB_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#ifndef	_TIMER_H
#include "timer.h"
#endif

#ifndef	_AX25_H
#include "ax25.h"
#endif

/* Upper sub-layer (LAPB) definitions */

/* Control field templates */
#define	I	0x00	/* Information frames */
#define	S	0x01	/* Supervisory frames */
#define	RR	0x01	/* Receiver ready */
#define	RNR	0x05	/* Receiver not ready */
#define	REJ	0x09	/* Reject */
#define	U	0x03	/* Unnumbered frames */
#define	SABM	0x2f	/* Set Asynchronous Balanced Mode */
#define	DISC	0x43	/* Disconnect */
#define	DM	0x0f	/* Disconnected mode */
#define	UA	0x63	/* Unnumbered acknowledge */
#define	FRMR	0x87	/* Frame reject */
#define	UI	0x03	/* Unnumbered information */
#define	PF	0x10	/* Poll/final bit */

#define	MMASK	7	/* Mask for modulo-8 sequence numbers */

/* FRMR reason bits */
#define	W	1	/* Invalid control field */
#define	X	2	/* Unallowed I-field */
#define	Y	4	/* Too-long I-field */
#define	Z	8	/* Invalid sequence number */

#define	SEG_FIRST	0x80	/* First segment of a sequence */
#define	SEG_REM		0x7f	/* Mask for # segments remaining */

/* Per-connection link control block
 * These are created and destroyed dynamically,
 * and are indexed through a hash table.
 * One exists for each logical AX.25 Level 2 connection
 */
struct ax25_cb {
	struct ax25_cb *next;		/* Linked list pointer */

	struct iface *iface;		/* Interface */

	struct mbuf *txq;		/* Transmit queue */
	struct mbuf *rxasm;		/* Receive reassembly buffer */
	struct mbuf *rxq;		/* Receive queue */

	uint8 local[AXALEN];		/* Addresses */
	uint8 remote[AXALEN];

	struct {
		unsigned int rejsent:1;		/* REJ frame has been sent */
		unsigned int remotebusy:1;	/* Remote sent RNR */
		unsigned int rtt_run:1;		/* Round trip "timer" is running */
		unsigned int retrans:1;		/* A retransmission has occurred */
		unsigned int clone:1;		/* Server-type cb, will be cloned */
	} flags;

	uint8 reason;			/* Reason for connection closing */
#define	LB_NORMAL	0		/* Normal close */
#define	LB_DM		1		/* Received DM from other end */
#define	LB_TIMEOUT	2		/* Excessive retries */

	uint8 response;			/* Response owed to other end */
	uint8 vs;			/* Our send state variable */
	uint8 vr;			/* Our receive state variable */
	uint8 unack;			/* Number of unacked frames */
	int maxframe;			/* Transmit flow control level, frames */
	uint16 paclen;			/* Maximum outbound packet size, bytes */
	uint16 window;			/* Local flow control limit, bytes */
	enum {
		V1=1,			/* AX.25 Version 1 */
		V2			/* AX.25 Version 2 */
	} proto;			/* Protocol version */
	uint16 pthresh;			/* Poll threshold, bytes */
	unsigned retries;		/* Retry counter */
	unsigned n2;			/* Retry limit */
	enum {
		LAPB_DISCONNECTED=1,
		LAPB_LISTEN,
		LAPB_SETUP,
		LAPB_DISCPENDING,
		LAPB_CONNECTED,
		LAPB_RECOVERY
	} state;			/* Link state */
	struct timer t1;		/* Retry timer */
	struct timer t3;		/* Keep-alive poll timer */
	int32 rtt_time;			/* Stored clock values for RTT, ticks */
	int rtt_seq;			/* Sequence number being timed */
	int32 srt;			/* Smoothed round-trip time, ms */
	int32 mdev;			/* Mean rtt deviation, ms */

	void (*r_upcall)(struct ax25_cb *,int);	/* Receiver upcall */
	void (*t_upcall)(struct ax25_cb *,int);	/* Transmit upcall */
	void (*s_upcall)(struct ax25_cb *,int,int);	/* State change upcall */

	int user;			/* User pointer */

	int segremain;			/* Segmenter state */
};
/* Linkage to network protocols atop ax25 */
struct axlink {
	int pid;
	void (*funct)(struct iface *,struct ax25_cb *,uint8 *, uint8 *,
	 struct mbuf **,int);
};
extern struct axlink Axlink[];

/* Codes for the open_ax25 call */
#define	AX_PASSIVE	0
#define	AX_ACTIVE	1
#define	AX_SERVER	2	/* Passive, clone on opening */

extern struct ax25_cb Ax25default,*Ax25_cb;
extern char *Ax25states[],*Axreasons[];
extern int32 Axirtt,T3init,Blimit;
extern uint16 N2,Maxframe,Paclen,Pthresh,Axwindow,Axversion;

/* In ax25cmd.c: */
void st_ax25(struct ax25_cb *axp);

/* In ax25subr.c: */
struct ax25_cb *cr_ax25(uint8 *addr);
void del_ax25(struct ax25_cb *axp);
struct ax25_cb *find_ax25(uint8 *);

/* In ax25user.c: */
int ax25val(struct ax25_cb *axp);
int disc_ax25(struct ax25_cb *axp);
int kick_ax25(struct ax25_cb *axp);
struct ax25_cb *open_ax25(struct iface *,uint8 *,uint8 *,
	int,uint16,
	void (*)(struct ax25_cb *,int),
	void (*)(struct ax25_cb *,int),
	void (*)(struct ax25_cb *,int,int),
	int user);
struct mbuf *recv_ax25(struct ax25_cb *axp,uint16 cnt);
int reset_ax25(struct ax25_cb *axp);
int send_ax25(struct ax25_cb *axp,struct mbuf **bp,int pid);

/* In lapb.c: */
void est_link(struct ax25_cb *axp);
void lapbstate(struct ax25_cb *axp,int s);
int lapb_input(struct ax25_cb *axp,int cmdrsp,struct mbuf **bp);
int lapb_output(struct ax25_cb *axp);
struct mbuf *segmenter(struct mbuf **bp,uint16 ssize);
int sendctl(struct ax25_cb *axp,int cmdrsp,int cmd);
int sendframe(struct ax25_cb *axp,int cmdrsp,int ctl,struct mbuf **data);
void axnl3(struct iface *iface,struct ax25_cb *axp,uint8 *src,
	uint8 *dest,struct mbuf **bp,int mcast);

/* In lapbtimer.c: */
void pollthem(void *p);
void recover(void *p);

/* In ax25subr.c: */
uint16 ftype(int control);
void lapb_garbage(int drastic);

/* In axsock.c: */
void s_arcall(struct ax25_cb *axp,int cnt);
void s_ascall(struct ax25_cb *axp,int old,int new);
void s_atcall(struct ax25_cb *axp,int cnt);

#endif	/* _LAPB_H */
