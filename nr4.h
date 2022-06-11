#ifndef	_NR4_H
#define	_NR4_H
/* nr4.h:  defines for netrom layer 4 (transport) support */

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_TIMER_H
#include "timer.h"
#endif

#ifndef	_AX25_H
#include "ax25.h"
#endif

/* compile-time limitations */

#define	NR4MAXCIRC	20		/* maximum number of open circuits */
#define NR4MAXWIN	127		/* maximum window size, send and receive */

/* protocol limitation: */

#define	NR4MAXINFO	236		/* maximum data in an info packet */

/* sequence number wraparound mask */

#define NR4SEQMASK	0xff	/* eight-bit sequence numbers */

/* flags in high nybble of opcode byte */

#define	NR4CHOKE	0x80
#define	NR4NAK		0x40
#define	NR4MORE		0x20	/* The "more follows" flag for */
				/* pointless packet reassembly */

/* mask for opcode nybble */

#define	NR4OPCODE	0x0f

/* opcodes */

#define NR4OPPID	0		/* protocol ID extension to network layer */
#define	NR4OPCONRQ	1		/* connect request */
#define	NR4OPCONAK	2		/* connect acknowledge */
#define	NR4OPDISRQ	3		/* disconnect request */
#define	NR4OPDISAK	4		/* disconnect acknowledge */
#define	NR4OPINFO	5		/* information packet */
#define	NR4OPACK	6		/* information ACK */
#define NR4NUMOPS	7		/* number of transport opcodes */

/* minimum length of NET/ROM transport header */

#define	NR4MINHDR	5

/* host format net/rom transport header */

struct nr4hdr {
	uint8 opcode ;		/* opcode and flags */
	uint8 yourindex ;	/* receipient's circuit index */
	uint8 yourid ;		/* receipient's circuit ID */

	union {

		struct {				/* network extension */
			uint8 family ;	/* protocol family */
			uint8 proto ;	/* protocol within family */
		} pid ;

		struct {				/* connect request */
			uint8 myindex ;	/* sender's circuit index */
			uint8 myid ;	/* sender's circuit ID */
			uint8 window ;	/* sender's proposed window size */
			uint8 user[AXALEN] ;	/* callsign of originating user */
			uint8 node[AXALEN] ;	/* callsign of originating node */
		} conreq ;

		struct {				/* connect acknowledge */
			uint8 myindex ;	/* sender's circuit index */
			uint8 myid ;	/* sender's circuit ID */
			uint8 window ; 	/* accepted window size */
		} conack ;

		struct {				/* information */
			uint8 txseq ;	/* sender's tx sequence number */
			uint8 rxseq ;	/* sender's rx sequence number */
		} info ;

		struct {				/* information acknowledge */
			uint8 rxseq ;	/* sender's rx sequence number */
		} ack ;

	} u ;	/* End of union */

} ;

/* A netrom send buffer structure */

struct nr4txbuf {
	struct timer tretry ;		/* retry timer */
	unsigned retries ;			/* number of retries */
	struct mbuf *data ;			/* data sent but not acknowledged */
} ;

/* A netrom receive buffer structure */

struct nr4rxbuf {
	uint8 occupied ;	/* flag: buffer in use */
	struct mbuf *data ; 		/* data received out of sequence */
} ;

/* address structure */
struct nr4_addr {
	uint8 user[AXALEN];
	uint8 node[AXALEN];
};

struct sockaddr_nr {
	short nr_family;
	struct nr4_addr nr_addr;
};

/* The netrom circuit control block */

struct nr4cb {
	unsigned mynum ;			/* my circuit number */
	unsigned myid ;				/* my circuit ID */
	unsigned yournum ;			/* remote circuit number */
	unsigned yourid ;			/* remote circuit ID */
	struct nr4_addr remote ;		/* address of remote node */
	struct nr4_addr local ;			/* our own address */

	unsigned window ;			/* negotiated window size */

	/* Data for round trip timer calculation and setting */

	long srtt ;					/* Smoothed round trip time */
	long mdev ;					/* Mean deviation in round trip time */
	unsigned blevel ;			/* Backoff level */
	unsigned txmax ;			/* The maximum number of retries among */
								/* the frames in the window.  This is 0 */
								/* if there are no frames in the window. */
								/* It is used as a baseline to determine */
								/* when to increment the backoff level. */

	/* flags */

	char clone ;				/* clone this cb upon connect */
	char choked ;				/* choke received from remote */
	char qfull ;				/* receive queue is full, and we have */
								/* choked the other end */
	char naksent ;				/* a NAK has already been sent */

	/* transmit buffers and window variables */

	struct nr4txbuf *txbufs ;	/* pointer to array[windowsize] of bufs */
	uint8 nextosend ;	/* sequence # of next frame to send */
	uint8 ackxpected ;	/* sequence number of next expected ACK */
	unsigned nbuffered ;		/* number of buffered TX frames */
	struct mbuf *txq ;			/* queue of unsent data */

	/* receive buffers and window variables */

	struct nr4rxbuf *rxbufs ;	/* pointer to array[windowsize] of bufs */
	uint8 rxpected ;	/* # of next receive frame expected */
	uint8 rxpastwin ;	/* top of RX window + 1 */
	struct mbuf *rxq ;			/* "fully" received data queue */

	/* Connection state */

	int state ;					/* connection state */
#define NR4STDISC	0			/* disconnected */
#define NR4STCPEND	1			/* connection pending */
#define NR4STCON	2			/* connected */
#define	NR4STDPEND	3			/* disconnect requested locally */
#define NR4STLISTEN	4			/* listening for incoming connections */

	int dreason ;				/* Reason for disconnect */
#define NR4RNORMAL	0			/* Normal, requested disconnect */
#define NR4RREMOTE	1			/* Remote requested */
#define	NR4RTIMEOUT	2			/* Connection timed out */
#define	NR4RRESET	3			/* Connection reset locally */
#define NR4RREFUSED	4			/* Connect request refused */

	/* Per-connection timers */

	struct timer tchoke ;		/* choke timeout */
	struct timer tack ;		/* ack delay timer */

	struct timer tcd ;		/* connect/disconnect timer */
	unsigned cdtries ;		/* Number of connect/disconnect tries */

	void (*r_upcall)(struct nr4cb *,uint16);
					/* receive upcall */
	void (*t_upcall)(struct nr4cb *,uint16);
					/* transmit upcall */
	void (*s_upcall)(struct nr4cb *,int,int);
					/* state change upcall */
	int user ;			/* user linkage area */
} ;

/* The netrom circuit pointer structure */

struct nr4circp {
	uint8 cid ;			/* circuit ID; incremented each time*/
						/* this circuit is used */
	struct nr4cb *ccb ;		/* pointer to circuit control block, */
						/*  NULL if not in use */
} ;

/* The circuit table: */

extern struct nr4circp Nr4circuits[NR4MAXCIRC] ;

/* Some globals */

extern unsigned short Nr4window ;	/* The advertised window size, in frames */
extern long Nr4irtt ;			/* The initial round trip time */
extern unsigned short Nr4retries ;	/* The number of times to retry */
extern long Nr4acktime ;		/* How long to wait until ACK'ing */
extern char *Nr4states[] ;		/* NET/ROM state names */
extern char *Nr4reasons[] ;		/* Disconnect reason names */
extern unsigned short Nr4qlimit ;		/* max receive queue length before CHOKE */
extern long Nr4choketime ;		/* CHOKEd state timeout */
extern uint8 Nr4user[AXALEN];	/* User callsign in outgoing connects */

/* function definitions */

/* In nr4hdr.c: */
int ntohnr4(struct nr4hdr *, struct mbuf **);
struct mbuf *htonnr4(struct nr4hdr *);

/* In nr4subr.c: */
void free_n4circ(struct nr4cb *);
struct nr4cb *get_n4circ(int, int);
int init_nr4window(struct nr4cb *, unsigned);
int nr4between(unsigned, unsigned, unsigned);
struct nr4cb *match_n4circ(int, int,uint8 *,uint8 *);
struct nr4cb *new_n4circ(void);
void nr4defaults(struct nr4cb *);
int nr4valcb(struct nr4cb *);
void nr_garbage(int red);

/* In nr4.c: */
void nr4input(struct nr4hdr *hdr,struct mbuf **bp);
int nr4output(struct nr4cb *);
void nr4sbuf(struct nr4cb *, unsigned);
void nr4sframe(uint8 *, struct nr4hdr *, struct mbuf **);
void nr4state(struct nr4cb *, int);

/* In nr4timer.c */
void nr4ackit(void *);
void nr4cdtimeout(void *);
void nr4txtimeout(void *);
void nr4unchoke(void *);

/* In nr4user.c: */
void disc_nr4(struct nr4cb *);
int kick_nr4(struct nr4cb *);
struct nr4cb *open_nr4(struct nr4_addr *, struct nr4_addr *, int,
  void (*)(struct nr4cb *,uint16),
  void (*)(struct nr4cb *,uint16),
  void (*)(struct nr4cb *,int,int),int);
struct mbuf *recv_nr4(struct nr4cb *, uint16);
void reset_nr4(struct nr4cb *);
int send_nr4(struct nr4cb *, struct mbuf **);

/* In nrcmd.c: */
void nr4_state(struct nr4cb *, int, int);

#endif	/* _NR4_H */
