#ifndef	_IFACE_H
#define	_IFACE_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif


/* Interface encapsulation mode table entry. An array of these structures
 * are initialized in config.c with all of the information necessary
 * to attach a device.
 */
struct iface;	/* Defined later */
struct iftype {
	char *name;		/* Name of encapsulation technique */
	int (*send)(struct mbuf **,struct iface *,int32,uint8);
				/* Routine to send an IP datagram */
	int (*output)(struct iface *,uint8 *,uint8 *,uint16,struct mbuf **);
				/* Routine to send link packet */
	char *(*format)(char *,uint8 *);
				/* Function that formats addresses */
	int (*scan)(uint8 *,char *);
				/* Reverse of format */
	int type;		/* Type field for network process */
	int hwalen;		/* Length of hardware address, if any */
	void (*rcvf)(struct iface *,struct mbuf **);
				/* Function that handles incoming packets */
	int (*addrtest)(struct iface *,struct mbuf *);
				/* Function that tests incoming addresses */
	void (*trace)(FILE *,struct mbuf **,int);
				/* Function that decodes protocol headers */
	int (*dinit)(struct iface *,int32,int,char **);
				/* Function to initialize demand dialing */
	int (*dstat)(struct iface *);
				/* Function to display dialer status */
};
extern struct iftype Iftypes[];


/* Interface control structure */
struct iface {
	struct iface *next;	/* Linked list pointer */
	char *name;		/* Ascii string with interface name */

	int32 addr;		/* IP address */
	int32 broadcast;	/* Broadcast address */
	int32 netmask;		/* Network mask */

	uint16 mtu;		/* Maximum transmission unit size */

	uint16 trace;		/* Trace flags */
#define	IF_TRACE_OUT	0x01	/* Output packets */
#define	IF_TRACE_IN	0x10	/* Packets to me except broadcast */
#define	IF_TRACE_ASCII	0x100	/* Dump packets in ascii */
#define	IF_TRACE_HEX	0x200	/* Dump packets in hex/ascii */
#define	IF_TRACE_NOBC	0x1000	/* Suppress broadcasts */
#define	IF_TRACE_RAW	0x2000	/* Raw dump, if supported */
	FILE *trfp;		/* Stream to trace to */

	struct iface *forw;	/* Forwarding interface for output, if rx only */

	struct proc *rxproc;	/* Receiver process, if any */
	struct proc *txproc;	/* IP send process */
	struct proc *supv;	/* Supervisory process, if any */

	struct mbuf *outq;	/* IP datagram transmission queue */
	int outlim;		/* Limit on outq length */
	int txbusy;		/* Transmitter is busy */

	void *dstate;		/* Demand dialer link state, if any */
	int (*dtickle)(struct iface *);
				/* Function to tickle dialer, if any */
	void (*dstatus)(struct iface *);	
				/* Function to display dialer state, if any */

	/* Device dependent */
	int dev;		/* Subdevice number to pass to send */
				/* To device -- control */
	int32 (*ioctl)(struct iface *,int cmd,int set,int32 val);
				/* From device -- when status changes */
	int (*iostatus)(struct iface *,int cmd,int32 val);
				/* Call before detaching */
	int (*stop)(struct iface *);
	uint8 *hwaddr;		/* Device hardware address, if any */

	/* Encapsulation dependent */
	void *edv;		/* Pointer to protocol extension block, if any */
	int xdev;		/* Associated Slip or Nrs channel, if any */
	struct iftype *iftype;	/* Pointer to appropriate iftype entry */

				/* Routine to send an IP datagram */
	int (*send)(struct mbuf **,struct iface *,int32,uint8);
			/* Encapsulate any link packet */
	int (*output)(struct iface *,uint8 *,uint8 *,uint16,struct mbuf **);
			/* Send raw packet */
	int (*raw)(struct iface *,struct mbuf **);
			/* Display status */
	void (*show)(struct iface *);

	int (*discard)(struct iface *,struct mbuf **);
	int (*echo)(struct iface *,struct mbuf **);

	/* Counters */
	int32 ipsndcnt; 	/* IP datagrams sent */
	int32 rawsndcnt;	/* Raw packets sent */
	int32 iprecvcnt;	/* IP datagrams received */
	int32 rawrecvcnt;	/* Raw packets received */
	int32 lastsent;		/* Clock time of last send */
	int32 lastrecv;		/* Clock time of last receive */
};
extern struct iface *Ifaces;	/* Head of interface list */
extern struct iface  Loopback;	/* Optional loopback interface */
extern struct iface  Encap;	/* IP-in-IP pseudo interface */

/* Header put on front of each packet sent to an interface */
struct qhdr {
	uint8 tos;
	int32 gateway;
};

extern char Noipaddr[];
extern struct mbuf *Hopper;

/* In iface.c: */
int bitbucket(struct iface *ifp,struct mbuf **bp);
int if_detach(struct iface *ifp);
struct iface *if_lookup(char *name);
char *if_name(struct iface *ifp,char *comment);
void if_tx(int dev,void *arg1,void *unused);
struct iface *ismyaddr(int32 addr);
void network(int i,void *v1,void *v2);
int nu_send(struct mbuf **bpp,struct iface *ifp,int32 gateway,uint8 tos);
int nu_output(struct iface *,uint8 *,uint8 *,uint16,struct mbuf **);
int setencap(struct iface *ifp,char *mode);

/* In config.c: */
int net_route(struct iface *ifp,struct mbuf **bpp);

#endif	/* _IFACE_H */
