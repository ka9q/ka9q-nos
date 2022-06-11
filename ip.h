#ifndef	_IP_H
#define	_IP_H

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

#ifndef	_TIMER_H
#include "timer.h"
#endif

#define TLB		30	/* Default reassembly timeout, sec */
#define	IPVERSION	4
#define IP_CS_OLD	1	/* use saved checksum */
#define IP_CS_NEW	0	/* calculate checksum */

extern char Hashtab[];	/* Modulus lookup table */

/* SNMP MIB variables, used for statistics and control. See RFC 1066 */
extern struct mib_entry Ip_mib[];
#define	ipForwarding		Ip_mib[1].value.integer
#define	ipDefaultTTL		Ip_mib[2].value.integer
#define	ipInReceives		Ip_mib[3].value.integer
#define	ipInHdrErrors		Ip_mib[4].value.integer
#define	ipInAddrErrors		Ip_mib[5].value.integer
#define	ipForwDatagrams		Ip_mib[6].value.integer
#define	ipInUnknownProtos	Ip_mib[7].value.integer
#define	ipInDiscards		Ip_mib[8].value.integer
#define	ipInDelivers		Ip_mib[9].value.integer
#define	ipOutRequests		Ip_mib[10].value.integer
#define	ipOutDiscards		Ip_mib[11].value.integer
#define	ipOutNoRoutes		Ip_mib[12].value.integer
#define	ipReasmTimeout		Ip_mib[13].value.integer
#define	ipReasmReqds		Ip_mib[14].value.integer
#define	ipReasmOKs		Ip_mib[15].value.integer
#define	ipReasmFails		Ip_mib[16].value.integer
#define	ipFragOKs		Ip_mib[17].value.integer
#define	ipFragFails		Ip_mib[18].value.integer
#define	ipFragCreates		Ip_mib[19].value.integer

#define	NUMIPMIB	19

/* IP header, INTERNAL representation */
#define IPLEN		20	/* Length of standard IP header */
#define IP_MAXOPT	40	/* Largest option field, bytes */
struct ip {
	int32 source;		/* Source address */
	int32 dest;		/* Destination address */
	uint16 length;		/* Total length */
	uint16 id;		/* Identification */
	uint16 offset;		/* Fragment offset in bytes */
	uint16 checksum;		/* Header checksum */

	struct {
		unsigned int congest:1;	/* Congestion experienced bit (exp) */
		unsigned int df:1;	/* Don't fragment flag */
		unsigned int mf:1;	/* More Fragments flag */
	} flags;

	uint8 version;		/* IP version number */
	uint8 tos;		/* Type of service */
	uint8 ttl;		/* Time to live */
	uint8 protocol;		/* Protocol */
	uint8 optlen;		/* Length of options field, bytes */
	uint8 options[IP_MAXOPT];/* Options field */
};
/* Fields in option type byte */
#define	OPT_COPIED	0x80	/* Copied-on-fragmentation flag */
#define	OPT_CLASS	0x60	/* Option class */
#define	OPT_NUMBER	0x1f	/* Option number */

/* IP option numbers */
#define	IP_EOL		0	/* End of options list */
#define	IP_NOOP		1	/* No Operation */
#define	IP_SECURITY	2	/* Security parameters */
#define	IP_LSROUTE	3	/* Loose Source Routing */
#define	IP_TIMESTAMP	4	/* Internet Timestamp */
#define	IP_RROUTE	7	/* Record Route */
#define	IP_STREAMID	8	/* Stream ID */
#define	IP_SSROUTE	9	/* Strict Source Routing */

/* Timestamp option flags */
#define	TS_ONLY		0	/* Time stamps only */
#define	TS_ADDRESS	1	/* Addresses + Time stamps */
#define	TS_PRESPEC	3	/* Prespecified addresses only */

/* IP routing table entry */
struct route {
	struct route *prev;	/* Linked list pointers */
	struct route *next;
	int32 target;		/* Target IP address */
	unsigned int bits;	/* Number of significant bits in target */
	int32 gateway;		/* IP address of local gateway for this target */
	int32 metric;		/* Hop count or whatever */
	struct iface *iface;	/* Device interface structure */
	struct {
		unsigned int rtprivate:1; /* Don't advertise this route */
		unsigned int rttrig:1;	/* Trigger is pending for this route */
	} flags;
	struct timer timer;	/* Time until aging of this entry */
	int32 uses;		/* Usage count */
};
extern struct route *Routes[32][HASHMOD];	/* Routing table */
extern struct route R_default;			/* Default route entry */

/* Cache for the last-used routing entry, speeds up the common case where
 * we handle a burst of packets to the same destination
 */
struct rt_cache {
	int32 target;
	struct route *route;
};
extern struct rt_cache Rt_cache[];
extern int32 Rtlookups;	/* Count of calls to rt_lookup() */
extern int32 Rtchits;		/* Count of cache hits in rt_lookup() */

extern uint16 Id_cntr;		/* Datagram serial number */

/* Reassembly descriptor */
struct reasm {
	struct reasm *next;	/* Linked list pointer */
	struct timer timer;	/* Reassembly timeout timer */
	struct frag *fraglist;	/* Head of data fragment chain */
	uint16 length;		/* Entire datagram length, if known */
	int32 source;		/* src/dest/id/protocol uniquely describe a datagram */
	int32 dest;
	uint16 id;
	char protocol;
};

/* Fragment descriptor in a reassembly list */
struct frag {
	struct frag *prev;	/* Previous fragment on list */
	struct frag *next;	/* Next fragment */
	struct mbuf *buf;	/* Actual fragment data */
	uint16 offset;		/* Starting offset of fragment */
	uint16 last;		/* Ending offset of fragment */
};

extern struct reasm *Reasmq;	/* The list of reassembly descriptors */

/* Structure for handling raw IP user sockets */
struct raw_ip {
	struct raw_ip *next;	/* Linked list pointer */

	struct mbuf *rcvq;	/* receive queue */
	void (*r_upcall)(struct raw_ip *);
	int protocol;		/* Protocol */
	int user;		/* User linkage */
};

extern struct raw_ip *Raw_ip;

/* Transport protocol link table */
struct iplink {
	char proto;
	char *name;
	void (*funct)(struct iface *,struct ip *,struct mbuf **,int,int32);
	void (*dump)(FILE *,struct mbuf **,int32,int32,int);
};
extern struct iplink Iplink[];

/* List of TCP port numbers to be given priority queuing */
extern int Tcp_interact[];

extern int Ip_trace;

/* In ip.c: */
void ip_garbage(int drastic);
void ip_recv(struct iface *iface,struct ip *ip,struct mbuf **bpp,
	int rxbroadcast, int32 said);
void ipip_recv(struct iface *iface,struct ip *ip,struct mbuf **bp,
	int rxbroadcast,int32 said);
int ip_send(int32 source,int32 dest,char protocol,char tos,char ttl,
	struct mbuf **bpp,uint16 length,uint16 id,char df);
struct raw_ip *raw_ip(int protocol,void (*r_upcall)(struct raw_ip *) );
void del_ip(struct raw_ip *rrp);
void rquench(struct iface *ifp,int drop);

/* In ipdump.c */
void dumpip(struct iface *iface,struct ip *ip,struct mbuf *bp,int32 spi);

/* In iproute.c: */
void ipinit(void);
uint16 ip_mtu(int32 addr);
void encap_tx(int dev,void *arg1,void *unused);
int ip_encap(struct mbuf **bpp,struct iface *iface,int32 gateway,uint8 tos);
void ip_proc(struct iface *iface,struct mbuf **bpp);
int ip_route(struct iface *i_iface,struct mbuf **bpp,int rxbroadcast);
int32 locaddr(int32 addr);
void rt_merge(int trace);
struct route *rt_add(int32 target,unsigned int bits,int32 gateway,
	struct iface *iface,int32 metric,int32 ttl,uint8 private);
int rt_drop(int32 target,unsigned int bits);
struct route *rt_lookup(int32 target);
struct route *rt_blookup(int32 target,unsigned int bits);

/* In iphdr.c: */
uint16 cksum(struct pseudo_header *ph,struct mbuf *m,uint16 len);
uint16 eac(int32 sum);
void htonip(struct ip *ip,struct mbuf **data,int cflag);
int ntohip(struct ip *ip,struct mbuf **bpp);

/* In either lcsum.c or pcgen.asm: */
uint16 lcsum(uint16 *wp,uint16 len);

/* In sim.c: */
void net_sim(struct mbuf *bp);

#endif /* _IP_H */
