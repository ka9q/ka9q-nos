#ifndef	_NETROM_H
#define	_NETROM_H

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#ifndef	_AX25_H
#include "ax25.h"
#endif

#ifndef	_LAPB_H
#include "lapb.h"
#endif

#ifndef _NR4_H
#include "nr4.h"
#endif

/* net/rom support definitions
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 */

#define NR3HLEN		15	/* length of a net/rom level 3 hdr, */
#define NR3DLEN		241	/* max data size in net/rom l3 packet */
#define NR3NODESIG	0xff	/* signature for nodes broadcast */
#define NR3NODEHL	7	/* nodes bc header length */

#define NRNUMIFACE	10	/* number of interfaces associated */
				/* with net/rom network layer      */
#define NRNUMCHAINS	17	/* number of chains in the */
				/* neighbor and route hash tables */
#define NRRTDESTLEN	21	/* length of destination entry in */
				/* nodes broadcast */
#define NRDESTPERPACK	11	/* maximum number of destinations per */
				/* nodes packet */

/* NET/ROM protocol numbers */
#define NRPROTO_IP	0x0c

/* Internal representation of net/rom network layer header */
struct nr3hdr {
	uint8 source[AXALEN] ;	/* callsign of origin node */
	uint8 dest[AXALEN] ;	/* callsign of destination node */
	unsigned ttl ;		/* time-to-live */
} ;

/* Internal representation of net/rom routing broadcast destination */
/* entry */
struct nr3dest {
	uint8 dest[AXALEN] ;		/* destination callsign */
	char alias[AXALEN] ;		/* ident, upper case ASCII, blank-filled */
	uint8 neighbor[AXALEN] ;	/* best-quality neighbor */
	unsigned quality ;		/* quality of route for this neighbor */
} ;


/* net/rom interface table entry */
struct nriface {
	struct iface *iface ;		/* pointer to ax.25 interface */
	char alias[AXALEN] ;		/* alias for this interface's node */
					/* broadcasts */
	unsigned quality ;		/* net/rom link quality estimate */
} ;

/* net/rom neighbor table structure */
struct nrnbr_tab {
	struct nrnbr_tab *next ;	/* doubly linked list pointers */
	struct nrnbr_tab *prev ;
	uint8 call[AXALEN] ;		/* call of neighbor + 2 digis max */
	unsigned iface ;		/* offset of neighbor's port in */
					/* interface table */
	unsigned refcnt ;		/* how many routes for this neighbor? */
} ;

/* A list of these structures is provided for each route table */
/* entry.  They bind a destination to a neighbor node.  If the */
/* list of bindings becomes empty, the route table entry is    */
/* automatically deleted.									   */

struct nr_bind {
	struct nr_bind *next ;		/* doubly linked list */
	struct nr_bind *prev ;
	unsigned quality ;		/* quality estimate */
	unsigned obsocnt ;		/* obsolescence count */
	unsigned flags ;
#define	NRB_PERMANENT	0x01		/* entry never times out */
#define NRB_RECORDED	0x02		/* a "record route" entry */
	struct nrnbr_tab *via ;		/* route goes via this neighbor */
} ;

/* net/rom routing table entry */

struct nrroute_tab {
	struct nrroute_tab *next ;	/* doubly linked list pointers */
	struct nrroute_tab *prev ;
	char alias[AXALEN] ;		/* alias of node */
	uint8 call[AXALEN] ;		/* callsign of node */
	unsigned num_routes ;		/* how many routes in bindings list? */
	struct nr_bind *routes ;	/* list of neighbors */

} ;

/* The net/rom nodes broadcast filter structure */
struct nrnf_tab {
	struct nrnf_tab *next ;		/* doubly linked list */
	struct nrnf_tab *prev ;
	uint8 neighbor[AXALEN] ;	/* call of neighbor to filter */
	unsigned iface ;		/* filter on this interface */
} ;

/* Structure for handling raw NET/ROM user sockets */
struct raw_nr {
	struct raw_nr *prev;
	struct raw_nr *next;

	struct mbuf *rcvq;	/* receive queue */
	uint8 protocol;		/* Protocol */
};

/* The interface table */
extern struct nriface Nrifaces[NRNUMIFACE] ;

/* How many interfaces are in use */
extern unsigned Nr_numiface ;

/* The neighbor hash table (hashed on neighbor callsign) */
extern struct nrnbr_tab *Nrnbr_tab[NRNUMCHAINS] ;

/* The routes hash table (hashed on destination callsign) */
extern struct nrroute_tab *Nrroute_tab[NRNUMCHAINS] ;

/* The nodes broadcast filter table */
extern struct nrnf_tab *Nrnf_tab[NRNUMCHAINS] ;

extern uint8 Nr_nodebc[AXALEN];

/* filter modes: */
#define	NRNF_NOFILTER	0	/* don't filter */
#define	NRNF_ACCEPT	1	/* accept broadcasts from stations in list */
#define	NRNF_REJECT	2	/* reject broadcasts from stations in list */

/* The filter mode */
extern unsigned Nr_nfmode ;

/* The time-to-live for net/rom network layer packets */
extern unsigned short Nr_ttl ;

/* The obsolescence count initializer */
extern unsigned Obso_init ;

/* The threshhold at which routes becoming obsolete are not broadcast */
extern unsigned Obso_minbc ;

/* The quality threshhold below which routes in a broadcast will */
/* be ignored */
extern unsigned Nr_autofloor ;

/* Whether we want to broadcast the contents of our routing
 * table, or just our own callsign and alias:
 */
extern int Nr_verbose ;

/* The maximum number of routes maintained for a destination. */
/* If the list fills up, only the highest quality routes are  */
/* kept.  This limiting is done to avoid possible over-use of */
/* memory for routing tables in closely spaced net/rom networks. */
extern unsigned Nr_maxroutes ;

/* The netrom pseudo-interface */
extern struct iface *Nr_iface ;

/* Functions */

/* In nr3.c: */
void del_rnr(struct raw_nr *rpp);
uint8 *find_nralias(char *);
struct nrroute_tab *find_nrroute(uint8 *);
void nr_bcnodes(unsigned ifno);
void nr_nodercv(struct iface *iface,uint8 *source,struct mbuf **bpp);
int nr_nfadd(uint8 *, unsigned);
int nr_nfdrop(uint8 *, unsigned);
void nr_route(struct mbuf **bp,struct ax25_cb *iaxp);
int nr_routeadd(char *, uint8 *, unsigned,
	unsigned, uint8 *, unsigned, unsigned);
int nr_routedrop(uint8 *, uint8 *, unsigned);
int nr_send(struct mbuf **bp,struct iface *iface,int32 gateway,uint8 tos);
void nr_sendraw(uint8 *dest,unsigned family,unsigned proto,
	struct mbuf **data);
void nr3output(uint8 *dest,struct mbuf **data);
uint16 nrhash(uint8 *s);
struct raw_nr *raw_nr(uint8);

/* In nrcmd.c: */
void donrdump(struct nr4cb *cb);
int doroutedump(void);
int dorouteinfo(int argc,char *argv[],void *p);
int putalias(char *to, char *from,int complain);

/* In nrhdr.c: */
struct mbuf *htonnr3(struct nr3hdr *);
struct mbuf *htonnrdest(struct nr3dest *);
int ntohnr3(struct nr3hdr *, struct mbuf **);
int ntohnrdest(struct nr3dest *ds,struct mbuf **bpp);

#endif	/* _NETROM_H */
