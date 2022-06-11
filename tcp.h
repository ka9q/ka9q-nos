#ifndef	_TCP_H
#define	_TCP_H

/* TCP implementation. Follows RFC 793 as closely as possible */
#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#ifndef	_INTERNET_H
#include "internet.h"
#endif

#ifndef _IP_H
#include "ip.h"
#endif

#ifndef	_NETUSER_H
#include "netuser.h"
#endif

#ifndef	_TIMER_H
#include "timer.h"
#endif

#define	DEF_MSS	512	/* Default maximum segment size */
#define	DEF_WND	2048	/* Default receiver window */
#define	RTTCACHE 16	/* # of TCP round-trip-time cache entries */
#define	DEF_RTT	5000	/* Initial guess at round trip time (5 sec) */
#define	MSL2	30	/* Guess at two maximum-segment lifetimes */
#define	MIN_RTO	500L	/* Minimum timeout, milliseconds */
#define	TCP_HDR_PAD	70	/* mbuf size to preallocate for headers */
#define	DEF_WSCALE	0	/* Our window scale option */

#define	geniss()	((int32)msclock() << 12) /* Increment clock at 4 MB/sec */

/* Number of consecutive duplicate acks to trigger fast recovery */
#define	TCPDUPACKS	3

/* Round trip timing parameters */
#define	AGAIN	8	/* Average RTT gain = 1/8 */
#define	LAGAIN	3	/* Log2(AGAIN) */
#define	DGAIN	4	/* Mean deviation gain = 1/4 */
#define	LDGAIN	2	/* log2(DGAIN) */

/* TCP segment header -- internal representation
 * Note that this structure is NOT the actual header as it appears on the
 * network (in particular, the offset field is missing).
 * All that knowledge is in the functions ntohtcp() and htontcp() in tcpsubr.c
 */
#define TCPLEN		20	/* Minimum Header length, bytes */
#define	TCP_MAXOPT	40	/* Largest option field, bytes */
struct tcp {
	uint16 source;	/* Source port */
	uint16 dest;	/* Destination port */
	int32 seq;	/* Sequence number */
	int32 ack;	/* Acknowledgment number */
	uint16 wnd;			/* Receiver flow control window */
	uint16 checksum;		/* Checksum */
	uint16 up;			/* Urgent pointer */
	uint16 mss;			/* Optional max seg size */
	uint8 wsopt;			/* Optional window scale factor */
	uint32 tsval;			/* Outbound timestamp */
	uint32 tsecr;			/* Timestamp echo field */
	struct {
		unsigned int congest:1;	/* Echoed IP congestion experienced bit */
		unsigned int urg:1;
		unsigned int ack:1;
		unsigned int psh:1;
		unsigned int rst:1;
		unsigned int syn:1;
		unsigned int fin:1;
		unsigned int mss:1;	/* MSS option present */
		unsigned int wscale:1;	/* Window scale option present */
		unsigned int tstamp:1;	/* Timestamp option present */
	} flags;
};
/* TCP options */
#define	EOL_KIND	0
#define	NOOP_KIND	1
#define	MSS_KIND	2
#define	MSS_LENGTH	4
#define	WSCALE_KIND	3
#define	WSCALE_LENGTH	3
#define	TSTAMP_KIND	8
#define	TSTAMP_LENGTH	10

/* Resequencing queue entry */
struct reseq {
	struct reseq *next;	/* Linked-list pointer */
	struct tcp seg;		/* TCP header */
	struct mbuf *bp;	/* data */
	uint16 length;		/* data length */
	char tos;		/* Type of service */
};
/* These numbers match those defined in the MIB for TCP connection state */
enum tcp_state {
	TCP_CLOSED=1,
	TCP_LISTEN,
	TCP_SYN_SENT,
	TCP_SYN_RECEIVED,
	TCP_ESTABLISHED,
	TCP_FINWAIT1,
	TCP_FINWAIT2,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_CLOSING,
	TCP_TIME_WAIT,
};

/* TCP connection control block */
struct tcb {
	struct tcb *next;	/* Linked list pointer */

	struct connection conn;

	enum tcp_state state;	/* Connection state */

	char reason;		/* Reason for closing */
#define	NORMAL		0	/* Normal close */
#define	RESET		1	/* Reset by other end */
#define	TIMEOUT		2	/* Excessive retransmissions */
#define	NETWORK		3	/* Network problem (ICMP message) */

/* If reason == NETWORK, the ICMP type and code values are stored here */
	uint8 type;
	uint8 code;

	/* Send sequence variables */
	struct {
		int32 una;	/* First unacknowledged sequence number */
		int32 nxt;	/* Next sequence num to be sent for the first time */
		int32 ptr;	/* Working transmission pointer */
		int32 wl1;	/* Sequence number used for last window update */
		int32 wl2;	/* Ack number used for last window update */
		int32 wnd;	/* Other end's offered receive window */
		uint16 up;	/* Send urgent pointer */
		uint8 wind_scale;/* Send window scale */
	} snd;
	int32 iss;		/* Initial send sequence number */
	int32 resent;		/* Count of bytes retransmitted */
	int32 cwind;		/* Congestion window */
	int32 ssthresh;		/* Slow-start threshold */
	int dupacks;		/* Count of duplicate (do-nothing) ACKs */

	/* Receive sequence variables */
	struct {
		int32 nxt;	/* Incoming sequence number expected next */
		int32 wnd;	/* Our offered receive window */
		uint16 up;	/* Receive urgent pointer */
		uint8 wind_scale;/* Recv window scale */
	} rcv;
	int32 last_ack_sent;	/* Last ack sent for timestamp purposes */
	int32 ts_recent;	/* Most recent incoming timestamp */

	int32 irs;		/* Initial receive sequence number */
	int32 rerecv;		/* Count of duplicate bytes received */
	int32 mss;		/* Maximum segment size */

	int32 window;		/* Receiver window and send queue limit */
	int32 limit;		/* Send queue limit */

	void (*r_upcall)(struct tcb *tcb,int32 cnt);
		/* Call when "significant" amount of data arrives */
	void (*t_upcall)(struct tcb *tcb,int32 cnt);
		/* Call when ok to send more data */
	void (*s_upcall)(struct tcb *tcb,int old,int new);
		/* Call when connection state changes */
	struct {	/* Control flags */
		unsigned int force:1;	/* We owe the other end an ACK or window update */
		unsigned int clone:1;	/* Server-type TCB, cloned on incoming SYN */
		unsigned int retran:1;	/* A retransmission has occurred */
		unsigned int active:1;	/* TCB created with an active open */
		unsigned int synack:1;	/* Our SYN has been acked */
		unsigned int rtt_run:1;	/* We're timing a segment */
		unsigned int congest:1;	/* Copy of last IP congest bit received */
		int ts_ok:1;	/* We're using timestamps */
		int ws_ok:1;		/* We're using window scaling */
	} flags;
	char tos;		/* Type of service (for IP) */
	int backoff;		/* Backoff interval */

	struct mbuf *rcvq;	/* Receive queue */
	struct mbuf *sndq;	/* Send queue */
	int32 rcvcnt;		/* Count of items on rcvq */
	int32 sndcnt;		/* Number of unacknowledged sequence numbers on
				 * sndq. NB: includes SYN and FIN, which don't
				 * actually appear on sndq!
				 */

	struct reseq *reseq;	/* Out-of-order segment queue */
	struct timer timer;	/* Retransmission timer */
	int32 rtt_time;		/* Stored clock values for RTT */
	int32 rttseq;		/* Sequence number being timed */
	int32 rttack;		/* Ack at start of timing (for txbw calc) */
	int32 srtt;		/* Smoothed round trip time, milliseconds */
	int32 mdev;		/* Mean deviation, milliseconds */
	int32 rtt;		/* Last received RTT (for debugging) */

	int user;		/* User parameter (e.g., for mapping to an
				 * application control block
				 */
	int32 quench;		/* Count of incoming ICMP source quenches */
	int32 unreach;		/* Count of incoming ICMP unreachables */
	int32 timeouts;		/* Count of retransmission timeouts */
	int32 lastack;		/* Time of last received ack */
	int32 txbw;		/* Estimate of transmit bandwidth */
	int32 lastrx;		/* Time of last received data */
	int32 rxbw;		/* Estimate of receive bandwidth */
};
/* TCP round-trip time cache */
struct tcp_rtt {
	int32 addr;		/* Destination IP address */
	int32 srtt;		/* Most recent SRTT */
	int32 mdev;		/* Most recent mean deviation */
};
extern struct tcp_rtt Tcp_rtt[];
extern int (*Kicklist[])();

/* TCP statistics counters */
struct tcp_stat {
	uint16 runt;		/* Smaller than minimum size */
	uint16 checksum;		/* TCP header checksum errors */
	uint16 conout;		/* Outgoing connection attempts */
	uint16 conin;		/* Incoming connection attempts */
	uint16 resets;		/* Resets generated */
	uint16 bdcsts;		/* Bogus broadcast packets */
};
extern struct mib_entry Tcp_mib[];
#define	tcpRtoAlgorithm	Tcp_mib[1].value.integer
#define	tcpRtoMin	Tcp_mib[2].value.integer
#define	tcpRtoMax	Tcp_mib[3].value.integer
#define	tcpMaxConn	Tcp_mib[4].value.integer
#define	tcpActiveOpens	Tcp_mib[5].value.integer
#define tcpPassiveOpens	Tcp_mib[6].value.integer
#define	tcpAttemptFails	Tcp_mib[7].value.integer
#define	tcpEstabResets	Tcp_mib[8].value.integer
#define	tcpCurrEstab	Tcp_mib[9].value.integer
#define	tcpInSegs	Tcp_mib[10].value.integer
#define	tcpOutSegs	Tcp_mib[11].value.integer
#define	tcpRetransSegs	Tcp_mib[12].value.integer
#define	tcpInErrs	Tcp_mib[14].value.integer
#define	tcpOutRsts	Tcp_mib[15].value.integer
#define	NUMTCPMIB	15

extern struct tcb *Tcbs;
extern char *Tcpstates[];
extern char *Tcpreasons[];

/* In tcpcmd.c: */
extern int Tcp_tstamps;
extern int32 Tcp_irtt;
extern uint16 Tcp_limit;
extern uint16 Tcp_mss;
extern int Tcp_syndata;
extern int Tcp_trace;
extern uint16 Tcp_window;

void st_tcp(struct tcb *tcb);

/* In tcphdr.c: */
void htontcp(struct tcp *tcph,struct mbuf **data,
	int32 ipsrc,int32 ipdest);
int ntohtcp(struct tcp *tcph,struct mbuf **bpp);

/* In tcpin.c: */
void reset(struct ip *ip,struct tcp *seg);
void send_syn(struct tcb *tcb);
void tcp_input(struct iface *iface,struct ip *ip,struct mbuf **bpp,
	int rxbroadcast,int32 said);
void tcp_icmp(int32 icsource,int32 source,int32 dest,
	uint8 type,uint8 code,struct mbuf **bpp);

/* In tcpsubr.c: */
void close_self(struct tcb *tcb,int reason);
struct tcb *create_tcb(struct connection *conn);
struct tcb *lookup_tcb(struct connection *conn);
void rtt_add(int32 addr,int32 rtt);
struct tcp_rtt *rtt_get(int32 addr);
int seq_ge(int32 x,int32 y);
int seq_gt(int32 x,int32 y);
int seq_le(int32 x,int32 y);
int seq_lt(int32 x,int32 y);
int seq_within(int32 x,int32 low,int32 high);
void settcpstate(struct tcb *tcb,enum tcp_state newstate);
void tcp_garbage(int red);

/* In tcpout.c: */
void tcp_output(struct tcb *tcb);

/* In tcptimer.c: */
int32 backoff(int n);
void tcp_timeout(void *p);

/* In tcpuser.c: */
int close_tcp(struct tcb *tcb);
int del_tcp(struct tcb *tcb);
int kick(int32 addr);
int kick_tcp(struct tcb *tcb);
struct tcb *open_tcp(struct socket *lsocket,struct socket *fsocket,
	int mode,uint16 window,
	void (*r_upcall)(struct tcb *tcb,int32 cnt),
	void (*t_upcall)(struct tcb *tcb,int32 cnt),
	void (*s_upcall)(struct tcb *tcb,int old,int new),
	int tos,int user);
int32 recv_tcp(struct tcb *tcb,struct mbuf **bpp,int32 cnt);
void reset_all(void);
void reset_tcp(struct tcb *tcb);
long send_tcp(struct tcb *tcb,struct mbuf **bpp);
char *tcp_port(uint16 n);
int tcpval(struct tcb *tcb);

#endif	/* _TCP_H */
