#ifndef	_ARP_H
#define	_ARP_H

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

/* Lifetime of a valid ARP entry */
#define	ARPLIFE		900	/* 15 minutes */
/* Lifetime of a pending ARP entry */
#define	PENDTIME	15	/* 15 seconds */

/* ARP definitions (see RFC 826) */

#define	ARPLEN	16		/* Size of ARP hdr, minus hardware addresses */

/* Address size definitions */
#define	IPALEN	4		/* Length in bytes of an IP address */
#define	MAXHWALEN	20	/* Maximum length of a hardware address */

/* ARP opcodes */
enum arp_opcode {
	ARP_REQUEST=1,
	ARP_REPLY,
	REVARP_REQUEST,
	REVARP_REPLY
};
/* Hardware types */
enum arp_hwtype {
	ARP_NETROM=0,	/* Fake for NET/ROM (never actually sent) */
	ARP_ETHER,	/* Assigned to 10 megabit Ethernet */
	ARP_EETHER,	/* Assigned to experimental Ethernet */
	ARP_AX25,	/* Assigned to AX.25 Level 2 */
	ARP_PRONET,	/* Assigned to PROnet token ring */
	ARP_CHAOS,	/* Assigned to Chaosnet */
	ARP_IEEE802,	/* Who uses this? */
	ARP_ARCNET,
	ARP_APPLETALK
};
extern char *Arptypes[];	/* Type fields in ASCII, defined in arpcmd */
#define	NHWTYPES 9

/* Table of hardware types known to ARP */
struct arp_type {
	uint16 hwalen;		/* Hardware length */
	uint16 iptype;		/* Hardware type field for IP */
	uint16 arptype;		/* Hardware type field for ARP */
	uint16 pendtime;	/* # secs to wait pending response */
	uint8 *bdcst;		/* Hardware broadcast address */
	char *(*format)(char *,uint8 *);
				/* Function that formats addresses */
	int (*scan)(uint8 *,char *);
				/* Reverse of format */
};
extern struct arp_type Arp_type[];

/* Format of an ARP request or reply packet. From p. 3 */
struct arp {
	enum arp_hwtype hardware;	/* Hardware type */
	uint16 protocol;		/* Protocol type */
	uint8 hwalen;			/* Hardware address length, bytes */
	uint8 pralen;			/* Length of protocol address */
	enum arp_opcode opcode;		/* ARP opcode (request/reply) */
	uint8 shwaddr[MAXHWALEN];	/* Sender hardware address field */
	int32 sprotaddr;		/* Sender Protocol address field */
	uint8 thwaddr[MAXHWALEN];	/* Target hardware address field */
	int32 tprotaddr;		/* Target protocol address field */
};
		
/* Format of ARP table */
struct arp_tab {
	struct arp_tab *next;		/* Doubly-linked list pointers */
	struct arp_tab *prev;	
	struct timer timer;		/* Time until aging this entry */
	struct mbuf *pending;		/* Queue of datagrams awaiting resolution */
	int32 ip_addr;			/* IP Address, host order */
	enum arp_hwtype hardware;	/* Hardware type */
	enum {
		ARP_PENDING,	/* Incomplete */
		ARP_VALID	/* Complete */
	} state;
	uint8 *hw_addr;		/* Hardware address */
	unsigned int pub:1;	/* Respond to requests for this entry? */
};
extern struct arp_tab *Arp_tab[];

struct arp_stat {
	unsigned recv;		/* Total number of ARP packets received */
	unsigned badtype;	/* Incoming requests for unsupported hardware */
	unsigned badlen;	/* Incoming length field(s) didn't match types */
	unsigned badaddr;	/* Bogus incoming addresses */
	unsigned inreq;		/* Incoming requests for us */
	unsigned replies;	/* Replies sent */
	unsigned outreq;	/* Outoging requests sent */
};
extern struct arp_stat Arp_stat;

/* In arp.c: */
struct arp_tab *arp_add(int32 ipaddr,enum arp_hwtype hardware,uint8 *hw_addr,
	int pub);
void arp_drop(void *p);
int arp_init(unsigned int hwtype,int hwalen,int iptype,int arptype,
	int pendtime,uint8 *bdcst,char *(*format)(char *,uint8 *),
	int  (*scan)(uint8 *,char *) );
void arp_input(struct iface *iface,struct mbuf **bpp);
struct arp_tab *arp_lookup(enum arp_hwtype hardware,int32 ipaddr);
uint8 *res_arp(struct iface *iface,enum arp_hwtype hardware,int32 target,struct mbuf **bpp);

/* In arphdr.c: */
struct mbuf *htonarp(struct arp *arp);
int ntoharp(struct arp *arp,struct mbuf **bpp);

#endif /* _ARP_H */
