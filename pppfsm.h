#ifndef _PPPFSM_H
#define _PPPFSM_H

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_PROC_H
#include "proc.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#ifndef	_TIMER_H
#include "timer.h"
#endif

				/* 00: serious internal problems */
				/* 01: interoperability problems */
				/* 02: state machine messages */
#define PPP_DEBUG_RAW
#define PPP_DEBUG_OPTIONS	0x08
#define PPP_DEBUG_CHECKS(x)	if(PPPtrace & 0x40) trace_log(PPPiface,x);
#define PPP_DEBUG_ROUTINES(x)	if(PPPtrace & 0x80) trace_log(PPPiface,x);

/* config packet header */
struct config_hdr {
	byte_t code;
#define CONFIG_REQ	 1
#define CONFIG_ACK	 2
#define CONFIG_NAK	 3
#define CONFIG_REJ	 4
#define TERM_REQ	 5
#define TERM_ACK	 6
#define CODE_REJ	 7
#define PROT_REJ	 8
#define ECHO_REQ	 9
#define ECHO_REPLY	10
#define DISCARD_REQ	11
#define QUALITY_REPORT	12

	byte_t id;
	uint16 len;
};
#define CONFIG_HDR_LEN	4	/* Length of config packet header */


/* config option header */
struct option_hdr {
	byte_t type;		/* protocol dependant types */
	byte_t len;
};
#define OPTION_HDR_LEN	2	/* Length of option header */


/* Supported Configuration Protocol index */
enum {
	Lcp,
	Pap,
	IPcp,
	fsmi_Size
};

struct fsm_s;		/* forward declaration */

/* Protocol Constants needed by State Machine */
struct fsm_constant_s {
	char *name;			/* Name of protocol */
	uint16 protocol;			/* Protocol number */
	uint16 recognize;		/* Config codes to use (bits) */

	byte_t fsmi;			/* Finite State Machine index */
	byte_t try_req;			/* # tries for request */
	byte_t try_nak;			/* # tries for nak substitutes */
	byte_t try_terminate;		/* # tries for terminate */
	int32 timeout;			/* Time for timeouts (milliseconds)*/

	/* To free structure */
	void (*free)(struct fsm_s *fsm_p);

	/* Set negotiation to initial values */
	void (*reset)(struct fsm_s *fsm_p);
	/* When leaving Closed or Listen */
	void (*starting)(struct fsm_s *fsm_p);
	/* When entering Opened */
	void (*opening)(struct fsm_s *fsm_p);
	/* When leaving Opened */
	void (*closing)(struct fsm_s *fsm_p);
	/* When entering Closed or Listen (after termination) */
	void (*stopping)(struct fsm_s *fsm_p);

	struct mbuf *(*makereq)(struct fsm_s *fsm_p);

	int (*request)(struct fsm_s *fsm_p,
					struct config_hdr *hdr,
					struct mbuf **bpp);
	int (*ack)(struct fsm_s *fsm_p,
					struct config_hdr *hdr,
					struct mbuf **bpp);
	int (*nak)(struct fsm_s *fsm_p,
					struct config_hdr *hdr,
					struct mbuf **bpp);
	int (*reject)(struct fsm_s *fsm_p,
					struct config_hdr *hdr,
					struct mbuf **bpp);
};

/* FSM states */
enum {
	fsmCLOSED,
	fsmLISTEN,
	fsmREQ_Sent,
	fsmACK_Rcvd,
	fsmACK_Sent,
	fsmOPENED,
	fsmTERM_Sent,
	fsmState_Size
};

/* State Machine Control Block */
struct fsm_s {
	byte_t state;			/* FSM state */
	byte_t lastid;			/* ID of last REQ we sent */

	byte_t flags;
#define PPP_ESCAPED	0x01
#define PPP_TOSS	0x02
#define FSM_PASSIVE	0x40	/* opened passive */
#define FSM_ACTIVE	0x80	/* opened active */

	byte_t retry;			/* counter for timeouts */
	byte_t try_req;			/* # tries for request */
	byte_t try_terminate;		/* # tries for terminate */

	byte_t retry_nak;		/* counter for naks of requests */
	byte_t try_nak;			/* # tries for nak substitutes */

	struct ppp_s *ppp_p;		/* the ppp we belong to */
	struct timer timer;
	struct fsm_constant_s *pdc;	/* protocol dependent constants */
	void *pdv;			/* protocol dependent variables */
};


/* Link Phases */
enum {
	pppDEAD,		/* Waiting for physical layer */
	pppLCP,			/* Link Control Phase */
	pppAP,			/* Authentication Phase */
	pppREADY,		/* Link ready for traffic */
	pppTERMINATE,		/* Termination Phase */
	pppPhase_Size
};

/* PPP control block */
struct ppp_s {
	struct iface *iface;		/* pointer to interface block */

	byte_t phase;			/* phase of link initialization */
	byte_t id;			/* id counter for connection */

	byte_t flags;
#define PPP_AP_LOCAL	0x10	/* local authentication */
#define PPP_AP_REMOTE	0x20	/* remote authentication */

	byte_t trace;			/* trace flags for connection */

	struct fsm_s fsm[fsmi_Size];	/* finite state machines */

	int32 upsince;			/* Timestamp when Link Opened */
	char *peername;			/* Peername from remote (if any) */

	int32 OutTxOctetCount;		/* # octets sent */
	int32 OutOpenFlag;		/* # of open flags sent */
	uint16 OutNCP[fsmi_Size];	/* # NCP packets sent by protocol */
	uint16 OutError;			/* # packets with error on send */
	uint16 OutMemory;		/* # alloc failures on send */

	int32 InRxOctetCount;		/* # octets received */
	int32 InOpenFlag;		/* # of open flags */
	uint16 InNCP[fsmi_Size];		/* # NCP packets by protocol */
	uint16 InUnknown;		/* # unknown packets received */
	uint16 InChecksum;		/* # packets with bad checksum */
	uint16 InFrame;			/* # packets with frame error */
	uint16 InError;			/* # packets with other error */
	uint16 InMemory; 		/* # alloc failures */
};

extern char *fsmStates[];
extern char *fsmCodes[];

void htoncnf(struct config_hdr *cnf, struct mbuf **data);
int ntohcnf(struct config_hdr *cnf, struct mbuf **bpp);
int ntohopt(struct option_hdr *opt, struct mbuf **bpp);

void fsm_no_action(struct fsm_s *fsm_p);
int fsm_no_check(struct fsm_s *fsm_p,
				struct config_hdr *hdr,
				struct mbuf **bp);

void fsm_log(struct fsm_s *fsm_p, char *comment);
void fsm_timer(struct fsm_s *fsm_p);

int fsm_send(struct fsm_s *fsm_p, byte_t code,
			byte_t id, struct mbuf **data);
int fsm_sendreq(struct fsm_s *fsm_p);

void fsm_proc(struct fsm_s *fsm_p, struct mbuf **bp);

void fsm_start(struct fsm_s *fsm_p);
void fsm_down(struct fsm_s *fsm_p);
void fsm_close(struct fsm_s *fsm_p);

void fsm_init(struct fsm_s *fsm_p);
void fsm_free(struct fsm_s *fsm_p);

#endif /* _PPPFSM_H */
