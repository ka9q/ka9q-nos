/* net/rom level 3 low level processing
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 */

#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "arp.h"
#include "slip.h"
#include "ax25.h"
#include "netrom.h"
#include "nr4.h"
#include "lapb.h"
#include "socket.h"
#include "trace.h"
#include "ip.h"
#include "commands.h"

static int accept_bc(uint8 *addr,unsigned ifnum);
static struct nr_bind *find_best(struct nr_bind *list,unsigned obso);
static struct nr_bind *find_binding(struct nr_bind *list,struct nrnbr_tab *neighbor);
static struct nrnbr_tab *find_nrnbr(uint8 *, unsigned);
static struct nrnf_tab *find_nrnf(uint8 *, unsigned);
static struct nr_bind *find_worst(struct nr_bind *list);
static int ismycall(uint8 *addr);
#ifdef	notdef
static uint8 *nr_getroute(uint8 *);
#endif
static struct raw_nr *Raw_nr;

/* Nodes message broadcast address: "NODES" in shifted ASCII */
uint8 Nr_nodebc[AXALEN] = {
	'N'<<1, 'O'<<1, 'D'<<1, 'E'<<1, 'S'<<1, ' '<<1,
	('0'<<1) | E
};

struct nriface Nrifaces[NRNUMIFACE];
unsigned Nr_numiface;
struct nrnbr_tab *Nrnbr_tab[NRNUMCHAINS];
struct nrroute_tab *Nrroute_tab[NRNUMCHAINS];
struct nrnf_tab *Nrnf_tab[NRNUMCHAINS];
unsigned Nr_nfmode = NRNF_NOFILTER;
unsigned short Nr_ttl = 64;
static unsigned Obso_init = 6;
static unsigned Obso_minbc = 5;
static unsigned Nr_maxroutes = 5;
static unsigned Nr_autofloor = 1;
int Nr_verbose = 0;
struct iface *Nr_iface;

/* send a NET/ROM layer 3 datagram */
void
nr3output(
uint8 *dest,
struct mbuf **data
){
	struct nr3hdr n3hdr;
	struct mbuf *n3b;

	memcpy(n3hdr.dest,dest,AXALEN);	/* copy destination field */
	n3hdr.ttl = Nr_ttl;	/* time to live from initializer parm */

	if((n3b = htonnr3(&n3hdr)) == NULL){
		free_p(data);
		return;
	}
	append(&n3b, data);
	/* The null interface indicates that the packet needs to have */
	/* an appropriate source address inserted by nr_route */
	nr_route(&n3b,NULL);
}

/* send IP datagrams across a net/rom network connection */
int
nr_send(
struct mbuf **bpp,
struct iface *iface,
int32 gateway,
uint8 tos
){
	struct arp_tab *arp;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();
	if((arp = arp_lookup(ARP_NETROM,gateway)) == NULL){
		free_p(bpp);	/* drop the packet if no route */
		return -1;
	}
	nr_sendraw(arp->hw_addr, NRPROTO_IP, NRPROTO_IP, bpp);
	return 0;
}

/* Send arbitrary protocol data on top of a NET/ROM connection */
void
nr_sendraw(
uint8 *dest,
unsigned family,
unsigned proto,
struct mbuf **data
){
	struct mbuf *pbp;
	struct nr4hdr n4hdr;

	/* Create a "network extension" transport header */
	n4hdr.opcode = NR4OPPID;
	n4hdr.u.pid.family = family;
	n4hdr.u.pid.proto = proto;

	if((pbp = htonnr4(&n4hdr)) == NULL){
		free_p(data);
		return;
	}
	append(&pbp,data);		/* Append the data to that */
	nr3output(dest, &pbp); /* and pass off to level 3 code */
}

/* Arrange for receipt of raw NET/ROM datagrams */
struct raw_nr *
raw_nr(protocol)
uint8 protocol;
{
	register struct raw_nr *rp;

	rp = (struct raw_nr *)callocw(1,sizeof(struct raw_nr));
	rp->protocol = protocol;
	rp->next = Raw_nr;
	if(rp->next != NULL)
		rp->next->prev = rp;
	Raw_nr = rp;
	return rp;
}
/* Free a raw NET/ROM descriptor */
void
del_rnr(rpp)
struct raw_nr *rpp;
{
	register struct raw_nr *rp;

	/* Do sanity check on arg */
	for(rp = Raw_nr;rp != NULL;rp = rp->next)
		if(rp == rpp)
			break;
	if(rp == NULL)
		return;	/* Doesn't exist */

	/* Unlink */
	if(rp->prev != NULL)
		rp->prev->next = rp->next;
	else
		Raw_nr = rp->next;
	if(rp->next != NULL)
		rp->next->prev = rp->prev;
	/* Free resources */
	free_q(&rp->rcvq);
	free(rp);
}

/* Figure out if a call is assigned to one of my net/rom
 * interfaces.
 */
static int
ismycall(addr)
uint8 *addr;
{
	register int i;
	int found = 0;
	
	for(i = 0; i < Nr_numiface; i++)
		if(addreq(Nrifaces[i].iface->hwaddr,addr)){
			found = 1;
			break;
		}

	return found;
}


/* Route net/rom network layer packets.
 */
void
nr_route(bpp, iaxp)
struct mbuf **bpp;			/* network packet */
struct ax25_cb *iaxp;			/* incoming ax25 control block */
{
	struct nr3hdr n3hdr;
	struct nr4hdr n4hdr;
	struct ax25_cb *axp;
	struct mbuf *hbp, *pbp;
	struct raw_nr *rnr;
	register struct nrnbr_tab *np;
	register struct nrroute_tab *rp;
	register struct nr_bind *bindp;
	struct iface *iface;
	unsigned ifnum;
	
	if(ntohnr3(&n3hdr,bpp) == -1){
		free_p(bpp);
		return;
	}
	/* If this isn't an internally generated network packet,
	 * give the router a chance to record a route back to the
	 * sender, in case they aren't in the local node's routing
	 * table yet.
	 */
	if(iaxp != NULL && ax_lookup(iaxp->remote) != NULL){
			
		/* find the interface number */
		for(ifnum = 0; ifnum < Nr_numiface; ifnum++)
			if(iaxp->iface == Nrifaces[ifnum].iface)
				break;

		if(ifnum == Nr_numiface){	/* Not a net/rom interface! */
			free_p(bpp);
			return;
		}
		/* Add (possibly) a zero-quality recorded route via */
		/* the neighbor from which this packet was received */
		/* Note that this doesn't work with digipeated neighbors. */
		
		(void) nr_routeadd("      ",n3hdr.source,ifnum,0,iaxp->remote,0,1);
	}

	/* A packet from me, to me, can only be one thing:
	 * a horrible routing loop.  This will probably result
	 * from a bad manual ARP entry, but we should fix these
	 * obscure errors as we find them.
	 */
	if(ismycall(n3hdr.dest)){
		/* Toss if from me, or if we can't read the header */
		if(iaxp == NULL || ntohnr4(&n4hdr,bpp) == -1){
			free_p(bpp);
		} else if((n4hdr.opcode & NR4OPCODE) == NR4OPPID){
			for(rnr = Raw_nr;rnr!=NULL;rnr = rnr->next){
				if(rnr->protocol!=n4hdr.u.pid.family ||
				 rnr->protocol != n4hdr.u.pid.proto)
					continue;
				/* Duplicate the data portion, and put the
				 * level 3 header back on
				 */
				dup_p(&pbp,*bpp,0,len_p(*bpp));
				if(pbp != NULL &&
				 (hbp = htonnr3(&n3hdr)) != NULL){
					append(&hbp,&pbp);
					enqueue(&rnr->rcvq,&hbp);
				} else {
					free_p(&pbp);
					free_p(&hbp);
				}
			}
			/* IP does not use a NET/ROM level 3 socket */
			if(n4hdr.u.pid.family == NRPROTO_IP
			 && n4hdr.u.pid.proto == NRPROTO_IP)
				ip_route(iaxp->iface,bpp,0);
			else		/* we don't do this proto */
				free_p(bpp);
		} else {
			/* Must be net/rom transport: */
			nr4input(&n4hdr,bpp);
		}
		return;
	}
	if((rp = find_nrroute(n3hdr.dest)) == NULL){
		/* no route, drop the packet */
		free_p(bpp);
		return;
	}
	if((bindp = find_best(rp->routes,1)) == NULL){
		/* This shouldn't happen yet, but might if we add */
		/* dead route detection */
		free_p(bpp);
		return;
	}

	np = bindp->via;
	iface = Nrifaces[np->iface].iface;

	/* Now check to see if iaxp is null.  That is */
	/* a signal that the packet originates here, */
	/* so we need to insert the callsign of the appropriate  */
	/* interface */
	if(iaxp == NULL)
		memcpy(n3hdr.source,iface->hwaddr,AXALEN);
	
	/* Make sure there is a connection to the neighbor */
	if((axp = find_ax25(np->call)) == NULL ||
		(axp->state != LAPB_CONNECTED && axp->state != LAPB_RECOVERY)){
		/* Open a new connection or reinitialize old one */
		/* hwaddr has been advanced to point to neighbor + digis */
		axp = open_ax25(iface,iface->hwaddr,np->call, AX_ACTIVE, Axwindow, s_arcall, s_atcall, s_ascall,-1);
		if(axp == NULL){
			free_p(bpp);
			return;
		}
	}
		
	if(--n3hdr.ttl == 0){	/* the packet's time to live is over! */
		free_p(bpp);
		return;
	}
	/* now format network header */
	if((pbp = htonnr3(&n3hdr)) == NULL){
		free_p(bpp);
		return;
	}
	append(&pbp,bpp);		/* append data to header */

	/* put AX.25 PID on front */
	pushdown(&pbp,NULL,1);
	pbp->data[0] = PID_NETROM;

	if((pbp = segmenter(&pbp,axp->paclen)) == NULL){
		free_p(&pbp);
		return;
	}
	send_ax25(axp,&pbp,-1);	/* pass it off to ax25 code */
}
	

/* Perform a nodes broadcast on interface # ifno in the net/rom
 * interface table.
 */
void
nr_bcnodes(ifno)
unsigned ifno;
{
	struct mbuf *hbp, *dbp, *savehdr;
	struct nrroute_tab *rp;
	struct nrnbr_tab *np;
	struct nr_bind * bp;
	struct nr3dest nrdest;
	int i, didsend = 0, numdest = 0;
	register uint8 *cp;
	struct iface *axif = Nrifaces[ifno].iface;
	
	/* prepare the header */
	if((hbp = alloc_mbuf(NR3NODEHL)) == NULL)
		return;
		
	hbp->cnt = NR3NODEHL;	
	
	*hbp->data = NR3NODESIG;
	memcpy(hbp->data+1,Nrifaces[ifno].alias,ALEN);

	/* Some people don't want to advertise any routes; they
	 * just want to be a terminal node.  In that case we just
	 * want to send our call and alias and be done with it.
	 */

	if(!Nr_verbose){
		(*axif->output)(axif, Nr_nodebc, axif->hwaddr,
				PID_NETROM, &hbp);	/* send it */
		return;
	}

	/* make a copy of the header in case we need to send more than */
	/* one packet */
	savehdr = copy_p(hbp,NR3NODEHL);

	/* now scan through the routing table, finding the best routes */
	/* and their neighbors.  create destination subpackets and append */
	/* them to the header */
	for(i = 0; i < NRNUMCHAINS; i++){
		for(rp = Nrroute_tab[i]; rp != NULL; rp = rp->next){
			/* look for best, non-obsolescent route */
			if((bp = find_best(rp->routes,0)) == NULL)
				continue;	/* no non-obsolescent routes found */
			if(bp->quality == 0)	/* this is a loopback route */
				continue;	/* we never broadcast these */
			np = bp->via;
			/* insert best neighbor */
			memcpy(nrdest.neighbor,np->call,AXALEN);
			/* insert destination from route table */
			memcpy(nrdest.dest,rp->call,AXALEN);
			/* insert alias from route table */
			strcpy(nrdest.alias,rp->alias);
			/* insert quality from binding */
			nrdest.quality = bp->quality;
			/* create a network format destination subpacket */
			if((dbp = htonnrdest(&nrdest)) == NULL){
				free_p(&hbp);	/* drop the whole idea ... */
				free_p(&savehdr);
				return;
			}
			/* we now have a partially filled packet */
			didsend = 0;	
			append(&hbp,&dbp);/* append to header and others */
			/* see if we have appended as many destinations
			 * as we can fit into a single broadcast.  If we
			 * have, go ahead and send them out.
			 */
			if(++numdest == NRDESTPERPACK){	/* filled it up */
				/* indicate that we did broadcast */
				didsend = 1;
				/* reset the destination counter */
				numdest = 0;
				(*axif->output)(axif, Nr_nodebc, axif->hwaddr,
				 PID_NETROM,&hbp);	/* send it */
				/* new header */
				hbp = copy_p(savehdr,NR3NODEHL);
			}
		}
	}

	/* Now, here is something totally weird.  If our interfaces */
	/* have different callsigns than this one, advertise a very */
	/* high quality route to them.  Is this a good idea?  I don't */
	/* know.  However, it allows us to simulate a bunch of net/roms */
	/* hooked together with a diode matrix coupler. */
	for(i = 0; i < Nr_numiface; i++){
		if(i == ifno)
			continue;		/* don't bother with ours */
		cp = Nrifaces[i].iface->hwaddr;
		if(!addreq((uint8 *)axif->hwaddr,cp)){
			/* both destination and neighbor address */
			memcpy(nrdest.dest,cp,AXALEN);
			memcpy(nrdest.neighbor,cp,AXALEN);
			/* alias of the interface */
			strcpy(nrdest.alias,Nrifaces[i].alias);
			/* and the very highest quality */
			nrdest.quality = 255;
			/* create a network format destination subpacket */
			if((dbp = htonnrdest(&nrdest)) == NULL){
				free_p(&hbp);	/* drop the whole idea ... */
				free_p(&savehdr);
				return;
			}
			/* we now have a partially filled packet */
			didsend = 0;	
			/* append to header and others */
			append(&hbp,&dbp);
			if(++numdest == NRDESTPERPACK){	/* filled it up */
				/* indicate that we did broadcast */
				didsend = 1;
				/* reset the destination counter */
				numdest = 0;
				(*axif->output)(axif, Nr_nodebc, axif->hwaddr,
				 PID_NETROM,&hbp);	/* send it */
				/* new header */
				hbp = copy_p(savehdr,NR3NODEHL);
			}
		}
	}
			
	/* If we have a partly filled packet left over, or we never */
	/* sent one at all, we broadcast: */
	if(!didsend || numdest > 0)
		(*axif->output)(axif, Nr_nodebc, axif->hwaddr,PID_NETROM, &hbp);

	/* free the header copies */
	if(numdest == 0)
		free_p(&hbp);
	free_p(&savehdr);
}

/* attach the net/rom interface.  no parms for now. */
int
nr_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(Nr_iface != (struct iface *)0){
		printf("netrom interface already attached\n");
		return -1;
	}
	Nr_iface = (struct iface *)callocw(1,sizeof(struct iface));
	Nr_iface->addr = Ip_addr;

	/* The strdup is needed to keep the detach routine happy (it'll
	 * free the allocated memory)
	 */
	Nr_iface->name = strdup("netrom");
	if(Nr_iface->hwaddr == NULL){
		Nr_iface->hwaddr = mallocw(AXALEN);
		memcpy(Nr_iface->hwaddr,Mycall,AXALEN);
	}
	Nr_iface->mtu = NR4MAXINFO;
	setencap(Nr_iface,"NETROM");
	Nr_iface->next = Ifaces;
	Ifaces = Nr_iface;
	memcpy(Nr4user,Mycall,AXALEN);
	Nr_iface->txproc = newproc("nr tx",512,if_tx,0,Nr_iface,NULL,0);
	return 0;
}

/* This function checks an ax.25 address and interface number against
 * the filter table and mode, and returns 1 if the address is to be
 * accepted, and 0 if it is to be filtered out.
 */
static int
accept_bc(addr,ifnum)
uint8 *addr;
unsigned ifnum;
{
	struct nrnf_tab *fp;

	if(Nr_nfmode == NRNF_NOFILTER)		/* no filtering in effect */
		return 1;

	fp = find_nrnf(addr,ifnum);		/* look it up */
	
	if((fp != NULL && Nr_nfmode == NRNF_ACCEPT)
		|| (fp == NULL && Nr_nfmode == NRNF_REJECT))
		return 1;
	else
		return 0;
}


/* receive and process node broadcasts. */
void
nr_nodercv(
struct iface *iface,
uint8 *source,
struct mbuf **bpp
){
	register int ifnum;
	char bcalias[AXALEN];
	struct nr3dest ds;
	
	/* First, see if this is even a net/rom interface: */
	for(ifnum = 0; ifnum < Nr_numiface; ifnum++)
		if(iface == Nrifaces[ifnum].iface)
			break;
			
	if(ifnum == Nr_numiface){	/* not in the interface table */
		free_p(bpp);
		return;
	}

	if(!accept_bc(source,ifnum)){	/* check against filter */
		free_p(bpp);
		return;
	}
	
	/* See if it has a routing broadcast signature: */
	if(PULLCHAR(bpp) != NR3NODESIG){
		free_p(bpp);
		return;
	}

	/* now try to get the alias */
	if(pullup(bpp,bcalias,ALEN) < ALEN){
		free_p(bpp);
		return;
	}

	bcalias[ALEN] = '\0';		/* null terminate */

	/* enter the neighbor into our routing table */
	if(nr_routeadd(bcalias,source,ifnum,Nrifaces[ifnum].quality,
	 source, 0, 0) == -1){
		free_p(bpp);
		return;
	}
	
	/* we've digested the header; now digest the actual */
	/* routing information */
	while(ntohnrdest(&ds,bpp) != -1){
		/* ignore routes to me! */
		if(ismycall(ds.dest))
			continue;
		/* ignore routes below the minimum quality threshhold */
		if(ds.quality < Nr_autofloor)
			continue;
		/* set loopback paths to 0 quality */
		if(ismycall(ds.neighbor))
			ds.quality = 0;
		else
			ds.quality = ((ds.quality * Nrifaces[ifnum].quality + 128)
			 / 256) & 0xff;
		if(nr_routeadd(ds.alias,ds.dest,ifnum,ds.quality,source,0,0)
			== -1)
			break;
	}
			
	free_p(bpp);	/* This will free the mbuf if anything fails above */
}


/* The following are utilities for manipulating the routing table */

/* hash function for callsigns.  Look familiar? */
uint16
nrhash(s)
uint8 *s;
{
	register uint8 x;
	register int i;

	x = 0;
	for(i = ALEN; i !=0; i--)
		x ^= *s++ & 0xfe;
	x ^= *s & SSID;
	return (uint16)(x % NRNUMCHAINS);
}

/* Find a neighbor table entry.  Neighbors are determined by
 * their callsign and the interface number.  This takes care
 * of the case where the same switch or hosts uses the same
 * callsign on two different channels.  This isn't done by
 * net/rom, but it might be done by stations running *our*
 * software.
 */
static struct nrnbr_tab *
find_nrnbr(addr,ifnum)
register uint8 *addr;
unsigned ifnum;
{
	uint16 hashval;
	register struct nrnbr_tab *np;

	/* Find appropriate hash chain */
	hashval = nrhash(addr);

	/* search hash chain */
	for(np = Nrnbr_tab[hashval]; np != NULL; np = np->next){
		/* convert first in  list to ax25 address format */
		if(addreq(np->call,addr) && np->iface == ifnum){
			return np;
		}
	}
	return NULL;
}


/* Find a route table entry */
struct nrroute_tab *
find_nrroute(addr)
register uint8 *addr;
{
	uint16 hashval;
	register struct nrroute_tab *rp;

	/* Find appropriate hash chain */
	hashval = nrhash(addr);

	/* search hash chain */
	for(rp = Nrroute_tab[hashval]; rp != NULL; rp = rp->next){
		if(addreq(rp->call,addr)){
			return rp;
		}
	}
	return NULL;
}

/* Try to find the AX.25 address of a node with the given alias.  Return */
/* a pointer to the AX.25 address if found, otherwise NULL.  The alias */
/* should be a six character, blank-padded, upper-case string. */

uint8 *
find_nralias(alias)
char *alias;
{
	int i;
	register struct nrroute_tab *rp;

	/* Since the route entries are hashed by ax.25 address, we'll */
	/* have to search all the chains */
	
	for(i = 0; i < NRNUMCHAINS; i++)
		for(rp = Nrroute_tab[i]; rp != NULL; rp = rp->next)
			if(strncmp(alias, rp->alias, 6) == 0)
				return rp->call;

	/* If we get to here, we're out of luck */

	return NULL;
}


/* Find a binding in a list by its neighbor structure's address */
static struct nr_bind *
find_binding(list,neighbor)
struct nr_bind *list;
register struct nrnbr_tab *neighbor;
{
	register struct nr_bind *bp;

	for(bp = list; bp != NULL; bp = bp->next)
		if(bp->via == neighbor)
			return bp;

	return NULL;
}

/* Find the worst quality non-permanent binding in a list */
static
struct nr_bind *
find_worst(list)
struct nr_bind *list;
{
	register struct nr_bind *bp;
	struct nr_bind *worst = NULL;
	unsigned minqual = 1000;	/* infinity */

	for(bp = list; bp != NULL; bp = bp->next)
		if(!(bp->flags & NRB_PERMANENT) && bp->quality < minqual){
			worst = bp;
			minqual = bp->quality;
		}

	return worst;
}

/* Find the best binding of any sort in a list.  If obso is 1,
 * include entries below the obsolescence threshhold in the
 * search (used when this is called for routing broadcasts).
 * If it is 0, routes below the threshhold are treated as
 * though they don't exist.
 */
static
struct nr_bind *
find_best(list,obso)
struct nr_bind *list;
unsigned obso;
{
	register struct nr_bind *bp;
	struct nr_bind *best = NULL;
	int maxqual = -1;	/* negative infinity */

	for(bp = list; bp != NULL; bp = bp->next)
		if((int)bp->quality > maxqual)
			if(obso || bp->obsocnt >= Obso_minbc){
				best = bp;
				maxqual = bp->quality;
			}

	return best;
}

/* Add a route to the net/rom routing table */
int
nr_routeadd(alias,dest,ifnum,quality,neighbor,permanent,record)
char *alias;		/* net/rom node alias, blank-padded and */
			/* null-terminated */
uint8 *dest;		/* destination node callsign */
unsigned ifnum;	/* net/rom interface number */
unsigned quality;	/* route quality */
uint8 *neighbor;	/* neighbor node + 2 digis (max) in arp format */
unsigned permanent;	/* 1 if route is permanent (hand-entered) */
unsigned record;	/* 1 if route is a "record route" */
{
	struct nrroute_tab *rp;
	struct nr_bind *bp;
	struct nrnbr_tab *np;
	uint16 rhash, nhash;

	/* See if a routing table entry exists for this destination */
	if((rp = find_nrroute(dest)) == NULL){
		rp = (struct nrroute_tab *)callocw(1,sizeof(struct nrroute_tab));
		/* create a new route table entry */
		strncpy(rp->alias,alias,6);
		memcpy(rp->call,dest,AXALEN);
		rhash = nrhash(dest);
		rp->next = Nrroute_tab[rhash];
		if(rp->next != NULL)
			rp->next->prev = rp;
		Nrroute_tab[rhash] = rp;	/* link at head of hash chain */
	} else if(!record){
		strncpy(rp->alias,alias,6);	/* update the alias */
	}

	/* See if an entry exists for this neighbor */
	if((np = find_nrnbr(neighbor,ifnum)) == NULL){
		np = (struct nrnbr_tab *)callocw(1,sizeof(struct nrnbr_tab));
		/* create a new neighbor entry */
		memcpy(np->call,neighbor,AXALEN);
		np->iface = ifnum;
		nhash = nrhash(neighbor);
		np->next = Nrnbr_tab[nhash];
		if(np->next != NULL)
			np->next->prev = np;
		Nrnbr_tab[nhash] = np;
	} else if(permanent){		/* force this path to the neighbor */
		memcpy(np->call,neighbor,AXALEN);
	}
		
	/* See if there is a binding between the dest and neighbor */
	if((bp = find_binding(rp->routes,np)) == NULL){
		bp = (struct nr_bind *)callocw(1,sizeof(struct nr_bind));
		/* create a new binding and link it in */
		bp->via = np;	/* goes via this neighbor */
		bp->next = rp->routes;	/* link into binding chain */
		if(bp->next != NULL)
			bp->next->prev = bp;
		rp->routes = bp;
		rp->num_routes++;	/* bump route count */
		np->refcnt++;		/* bump neighbor ref count */
		bp->quality = quality;
		bp->obsocnt = Obso_init;	/* use initial value */
		if(permanent)
			bp->flags |= NRB_PERMANENT;
		else if(record)	/* notice permanent overrides record! */
			bp->flags |= NRB_RECORDED;
	} else {
		if(permanent){	/* permanent request trumps all */
			bp->quality = quality;
			bp->obsocnt = Obso_init;
			bp->flags |= NRB_PERMANENT;
			bp->flags &= ~NRB_RECORDED;	/* perm is not recorded */
		} else if(!(bp->flags & NRB_PERMANENT)){	/* not permanent */
			if(record){	/* came from nr_route */
				if(bp->flags & NRB_RECORDED){ /* no mod non-rec bindings */
					bp->quality = quality;
					bp->obsocnt = Obso_init; /* freshen recorded routes */
				}
			} else {		/* came from a routing broadcast */
				bp->quality = quality;
				bp->obsocnt = Obso_init;
				bp->flags &= ~NRB_RECORDED; /* no longer a recorded route */
			}
		}
	}

	/* Now, check to see if we have too many bindings, and drop */
	/* the worst if we do */
	if(rp->num_routes > Nr_maxroutes){
		/* since find_worst never returns permanent entries, the */
		/* limitation on number of routes is circumvented for    */
		/* permanent routes */
		if((bp = find_worst(rp->routes)) != NULL){
			nr_routedrop(dest,bp->via->call,bp->via->iface);
		}
	}

	return 0;
}


/* Drop a route to dest via neighbor */
int
nr_routedrop(dest,neighbor,ifnum)
uint8 *dest, *neighbor;
unsigned ifnum;
{
	register struct nrroute_tab *rp;
	register struct nrnbr_tab *np;
	register struct nr_bind *bp;

	if((rp = find_nrroute(dest)) == NULL)
		return -1;

	if((np = find_nrnbr(neighbor,ifnum)) == NULL)
		return -1;

	if((bp = find_binding(rp->routes,np)) == NULL)
		return -1;

	/* drop the binding first */
	if(bp->next != NULL)
		bp->next->prev = bp->prev;
	if(bp->prev != NULL)
		bp->prev->next = bp->next;
	else
		rp->routes = bp->next;

	free(bp);
	rp->num_routes--;		/* decrement the number of bindings */
	np->refcnt--;			/* and the number of neighbor references */
	
	/* now see if we should drop the route table entry */
	if(rp->num_routes == 0){
		if(rp->next != NULL)
			rp->next->prev = rp->prev;
		if(rp->prev != NULL)
			rp->prev->next = rp->next;
		else
			Nrroute_tab[nrhash(dest)] = rp->next;

		free(rp);
	}

	/* and check to see if this neighbor can be dropped */
	if(np->refcnt == 0){
		if(np->next != NULL)
			np->next->prev = np->prev;
		if(np->prev != NULL)
			np->prev->next = np->next;
		else
			Nrnbr_tab[nrhash(neighbor)] = np->next;

		free(np);
	}
	
	return 0;
}

#ifdef	notused
/* Find the best neighbor for destination dest, in arp format */
static uint8 *
nr_getroute(dest)
uint8 *dest;
{
	register struct nrroute_tab *rp;
	register struct nr_bind *bp;

	if((rp = find_nrroute(dest)) == NULL)
		return NULL;

	if((bp = find_best(rp->routes,1)) == NULL)	/* shouldn't happen! */
		return NULL;

	return bp->via->call;
}
#endif	/* notused */

/* Find an entry in the filter table */
static struct nrnf_tab *
find_nrnf(addr,ifnum)
register uint8 *addr;
unsigned ifnum;
{
	uint16 hashval;
	register struct nrnf_tab *fp;

	/* Find appropriate hash chain */
	hashval = nrhash(addr);

	/* search hash chain */
	for(fp = Nrnf_tab[hashval]; fp != NULL; fp = fp->next){
		if(addreq(fp->neighbor,addr) && fp->iface == ifnum){
			return fp;
		}
	}

	return NULL;
}

/* Add an entry to the filter table.  Return 0 on success,
 * -1 on failure
 */
int
nr_nfadd(addr,ifnum)
uint8 *addr;
unsigned ifnum;
{
	struct nrnf_tab *fp;
	uint16 hashval;
	
	if(find_nrnf(addr,ifnum) != NULL)
		return 0;	/* already there; it's a no-op */

	fp = (struct nrnf_tab *)callocw(1,sizeof(struct nrnf_tab));

	hashval = nrhash(addr);
	memcpy(fp->neighbor,addr,AXALEN);
	fp->iface = ifnum;
	fp->next = Nrnf_tab[hashval];
	if(fp->next != NULL)
		fp->next->prev = fp;
	Nrnf_tab[hashval] = fp;

	return 0;
}

/* Drop a neighbor from the filter table.  Returns 0 on success, -1
 * on failure.
 */
int
nr_nfdrop(addr,ifnum)
uint8 *addr;
unsigned ifnum;
{
	struct nrnf_tab *fp;

	if((fp = find_nrnf(addr,ifnum)) == NULL)
		return -1;	/* not in the table */

	if(fp->next != NULL)
		fp->next->prev = fp->prev;
	if(fp->prev != NULL)
		fp->prev->next = fp->next;
	else
		Nrnf_tab[nrhash(addr)] = fp->next;

	free(fp);

	return 0;
}
