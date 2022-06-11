/*
 * Center for Information Technology Integration
 *           The University of Michigan
 *                    Ann Arbor
 *
 * Dedicated to the public domain.
 * Send questions to info@citi.umich.edu
 *
 * BOOTP is documented in RFC 951 and RFC 1048
 * Delinted, ANSIfied and reformatted - 5/30/91 P. Karn
 */


/*
 * BOOTP (bootstrap protocol) server daemon.
 *
 */


#include <stdio.h>
#include <sys\types.h>
#include <sys\stat.h>
#include <ctype.h>
#include <time.h>

#include "global.h"
#include "config.h"
#include "bootp.h"
#include "cmdparse.h"
#include "iface.h"
#include "mbuf.h"
#include "proc.h"
#include "socket.h"
#include "bootpd.h"
#include "udp.h"
#include "ip.h"
#include "arp.h"
#include "netuser.h"

void bootpd(struct iface *iface, struct udp_cb *sock, int cnt); 

struct udp_cb *Bootpd_cb = NULL;

char homedir[64] = "";      		/* bootfile homedirectory */
char defaultboot[64] = "";  		/* default file to boot */

struct host hosts[MHOSTS+1];
int Nhosts = 0;  	 /* current number of hosts */

int32 bp_DefaultDomainNS[BP_MAXDNS] =  {0};

struct bootpd_stat Bootpd_stat;

char *ArpNames[] = {
	"Netrom",
	"Ethernet",
	"AX25",
	"Pronet",
	"Chaos",
	"IEEE 802",
	"ArcNet",
	"AppleTalk" 
};


/* Routine declarations */
static int bootpd_recv(struct udp_cb *sock,struct bootp *bootp);
static void sendreply(struct bootp *bp,struct iface *iface);
static void vend_fill(uint8 *vend,struct iface *iface,struct host *hp);
static void vend_print(uint8 *vend);
static void bootpd_request(struct bootp *rq,struct iface *iface);

/* The bootp server. */
void 
bootpd(iface,sock,cnt)
struct iface *iface;
struct udp_cb *sock;
int cnt;
{
	struct bootp bp_packet;

	while(bootpd_recv(sock, &bp_packet) != -1) {

       		if(readtab() == -1) /* maybe re-read bootptab */
			return;

		switch(bp_packet.op) {
	    	case BOOTREQUEST:
			bootpd_request(&bp_packet, iface); 
			break;
		case BOOTREPLY: 
			/* Replies are not forwarded, left to the gateway */ 
			break;
		}

	}

}

/* A packet has been received, read it into a bootp structure. */
static int
bootpd_recv(sock, bootp)
struct udp_cb *sock;
struct bootp *bootp;
{
	struct socket fsock;
	struct mbuf *bp;
	int len;

	/* increment the rcvd cnt */
	Bootpd_stat.rcvd++;

	/* receive the packet */
	len = recv_udp(sock,&fsock,&bp);
	if(len == -1)
		return -1;


	/* check length of packet */
	if(len < sizeof(struct bootp)) {
		Bootpd_stat.bad_size++;
		bootp->op = 0;
		free_p(&bp);
		return -1;
	}
		
	/* parse the packet */
	pullup (&bp, bootp, sizeof (struct bootp));
	free_p(&bp);	

	if(bootp->op != BOOTREPLY  &&  bootp->op != BOOTREQUEST) {
		Bootpd_stat.bad_op++;
		bootp->op = 0;
		return -1;
	}

	bootp->ciaddr.s_addr = (unsigned long) get32 ((uint8 *)&(bootp->ciaddr));
	bootp->giaddr.s_addr = (unsigned long) get32 ((uint8 *)&(bootp->giaddr));
	return 0;
};


/*
 * Process BOOTREQUEST packet.
 *
 * (Note, this version of the bootp.c server never forwards 
 * the request to another server.  In our environment the 
 * stand-alone gateways perform that function.)
 *
 * (Also this version does not interpret the hostname field of
 * the request packet;  it COULD do a name->address lookup and
 * forward the request there.)
 */

static void
bootpd_request (rq, iface)
struct bootp *rq;
struct iface *iface;
{

	struct bootp *rp;
	char path[64], file[64];
	struct host *hp;
	int n;
	time_t tloc;
	int32 ipaddr;
	struct arp_type *at;

	time(&tloc);
	bp_log ("\nBootpd request packet received %s", ctime(&tloc));
	/* Forwarding not done here. */
	if(rq->giaddr.s_addr)  {
		bp_log ("     Dropped, giaddr specifies to be forwarded;\n");
		return;
	}

	/* Is a specific host requested? */
	if((strlen(rq->sname) != 0) && (strcmp (Hostname, rq->sname) != 0)) {
		bp_log ("     Dropped, sname specifies server '%s'\n", rq->sname);  
		return;
	}

	/* allocate the reply */
	rp = (struct bootp *) calloc (1, sizeof (struct bootp));

	/* copy for construction */
	memcpy (rp, rq, sizeof (struct bootp) - sizeof (rp->vend));

	rp->op = BOOTREPLY;

	hp = NULL;


	/* If the client doesn't know it's ip address, find one. */
	if(rq->ciaddr.s_addr == 0) { 

		/*
		 * client doesn't know his IP address, 
		 * search by hardware address.
		 */
		at = &Arp_type[rq->htype];
		bp_log ("     Resolved by %s addr %s\n", ArpNames[rq->htype], 
			(*at->format)(bp_ascii, rq->chaddr));

		for(hp = &hosts[0], n = 0 ; n < Nhosts ; n++,hp++)
			if((rq->htype == hp->htype)
			   && (memcmp(rq->chaddr, hp->haddr, rq->hlen) == 0))
				break;

		/* If the client wasn't found, assign an IP address */
		if(n == Nhosts) {

			hp = NULL;
			if(da_assign(iface, rq->chaddr, &ipaddr) != 0) {
				free (rp);
				bp_log ("     No dynamic addresses available.\n");
				return;
			} else {		
				put32 ((uint8 *) &(rp->yiaddr), ipaddr);
				bp_log ("     Dynamic address assigned: %s\n", 
					inet_ntoa (ipaddr));
			}
		} else {
			bp_log ("     Static address assigned: %s\n", 
				inet_ntoa (hp->iaddr.s_addr));
			put32 ((uint8 *) &(rp->yiaddr), hp->iaddr.s_addr);
		}

	} else {
		/* search by IP address */
		bp_log ("     Resolve by IP addr %s\n", inet_ntoa (rq->ciaddr.s_addr));
		for(hp = &hosts[0], n = 0 ; n < Nhosts ; n++,hp++)
			if(rq->ciaddr.s_addr == hp->iaddr.s_addr)
				break;
		if(n == Nhosts) {
			hp = NULL;
			bp_log ("     Host not found, default values used.\n");
		} else
			bp_log ("     Lookup successful.\n");
		put32 ((uint8 *) &(rp->ciaddr), rq->ciaddr.s_addr);
	}

	put32 ((uint8 *) &(rp->siaddr), iface->addr);

	/* Determine the bootp file */
	file[0] = 0;
	if(rq->file[0] == 0) { 		/* if client didn't specify file */
		/* Use the host record file, else the default file */
		if((hp == NULL) || (hp->bootfile[0] == 0))
			strcpy(file, defaultboot);
		else
			strcpy(file, hp->bootfile);
	} else {
		/* use client specified file */
		strcpy(file, rq->file);
	}

	/* If a file is specified, specify the path to the bootp file */
	path[0] = 0;
	if((*homedir != 0) && (*file != 0)) {
		strcpy(path, homedir);
		strcat(path, "/");
	}

	if(file[0] == '/')     /* if absolute pathname */
		strcpy(path, file);
	else
		strcat(path, file);

	/* No files are provided here, just return a path. */
	strcpy(rp->file, path);

	/* Fill in the vendor information */
	vend_fill (rp->vend, iface, hp);

	sendreply (rp, iface);
	free (rp); 

};


/* Print the bootp structure. */
void
bootp_print_packet  (bp)
struct bootp *bp;
{
	bp_log ("Packet op code........................%d\n", bp->op);
	bp_log ("hardware address type.................%d\n", bp->htype);
	bp_log ("hardware address length...............%d\n", bp->hlen);
	bp_log ("client sets to zero...................%d\n", bp->hops);
	bp_log ("transaction ID........................%ld\n", bp->xid);
	bp_log ("seconds elapsed since client booted...%d\n", bp->secs);
	bp_log ("unused................................%d\n", bp->unused);
	bp_log ("Client IP address, if known...........%s\n",
			inet_ntoa ((int32)bp->ciaddr.s_addr));
	bp_log ("Server supplied IP address............%s\n", 
			inet_ntoa ((int32)bp->yiaddr.s_addr));
	bp_log ("Server IP address.....................%s\n", 
			inet_ntoa ((int32)bp->siaddr.s_addr));
	bp_log ("Gateway IP address....................%s\n", 
			inet_ntoa ((int32)bp->giaddr.s_addr));
	bp_log ("Client hardware address...............%x:%x:%x:%x:%x:%x\n",
		bp->chaddr[0], bp->chaddr[1], bp->chaddr[2],
		bp->chaddr[3], bp->chaddr[4], bp->chaddr[5]);
	bp_log ("Server host name......................'%s'\n", bp->sname);
	bp_log ("Boot file name........................'%s'\n", bp->file);
	
	vend_print (bp->vend);	
}



static void
vend_print (vend)
uint8 *vend; 
{
	uint8 ch;
	int size;
	uint8 *start;
	int32 *ipaddr;
	int i;

	start = vend;

	bp_log ("Magic Cookie..........................x%02x%02x%02x%02x\n", 
		(int) vend[0], (int) vend[1], (int) vend[2], (int) vend[3]);
	vend = vend + 4;

	while(((ch = *vend++) != BOOTP_END) && (vend - start <= 64)) 
		switch(ch) {

		case BOOTP_PAD:	 /* They're just padding */
			continue;
		case BOOTP_SUBNET:      /* fixed length, 4 octets */
			size = (int) *vend++;
			ipaddr = (int32 *) vend;
			bp_log ("Vend Subnet...........................%s\n", inet_ntoa
(*ipaddr));
			vend += size;
			break;
		case BOOTP_HOSTNAME:
			size = (int) *vend++;
			bp_log ("Vend Hostname.........................%s\n", vend);
			vend += size;
			break;
		case BOOTP_DNS:
			size = (int) *vend++;
			for(i = 0; i < (size / 4); i++) {
				ipaddr = (int32 *) vend;
				bp_log ("Vend DomainNS.........................%s\n", 
					inet_ntoa (*ipaddr));
				vend += 4;
			}
			break;
		case BOOTP_GATEWAY:
			size = (int) *vend++;
			for(i = 0; i < (size / 4); i++) {
				ipaddr = (int32 *) vend;
				bp_log ("Vend Gateway..........................%s\n", 
					inet_ntoa (*ipaddr));
				vend += 4;
			}
			break;
			
			
		default:		/* variable field we don't know about */
			size = *vend++;
			vend += size;
			break;
	}

}



static void
vend_fill (vend, iface, hp)
uint8 *vend;
struct iface *iface;
struct host *hp;
{
	uint8 	cookie[5] = {99, 130, 83, 99, 0};
	int 	len;
	int	mod;
	int	i;
	uint8	*sizep;

	/* Magic cookie */
	memcpy(vend,cookie,4);
	vend += 4;

	/* Send the iface subnet */
	/* Pad so number falls on word boundry */
	
	vend++;
	vend++;

	*vend = BOOTP_SUBNET;
	vend++;
	*vend = 4;
	vend++;
	put32 (vend, iface->netmask);
	vend += 4;

	
	/* Send the DNS */
	if(bp_DefaultDomainNS[0] != 0) {
		/* Pad for allignment */
		vend++;
		vend++;

		*vend = BOOTP_DNS;
		vend++;
		sizep = vend;
		vend++;
		for(i = 0; (i < BP_MAXDNS) && (bp_DefaultDomainNS[i] != 0); i++) {
			put32 (vend, bp_DefaultDomainNS[i]);
			*sizep = *sizep + 4;
			vend += 4;
		}
	}

	/* Send the default gateway */
	if(R_default.iface == iface) {
		vend++;
		vend++;

		*vend = BOOTP_GATEWAY; 
		vend++;	
		*vend = 4;	
		vend++;
		put32 (vend, R_default.gateway);
		vend +=  4;
	}
	
	/* Send the hostname */
	if(hp != NULL) {
		/* Pad so name begins on a word boundry */
		vend++;
		vend++;

		*vend = BOOTP_HOSTNAME;
		vend++;
		*vend = len = strlen (hp->name) + 1;
		vend++;
		strcpy ((char *)vend, hp->name);
		vend += len;

		/* Pad to a word. */
		mod = 4 - (len % 4);
		for(i = 0; i < mod; i++) {
			*vend = BOOTP_PAD;
			vend++;
		}
	}

	/* Mark the end of the data */
	*vend = BOOTP_END;
}



/*
 * Send a reply packet to the client.  'forward' flag is set if we are
 * not the originator of this reply packet.
 */
static void
sendreply(bp, iface)
struct bootp *bp;
struct iface *iface;
{
	struct mbuf *buf;
	int32 faddr;
	uint16 length;
	uint8 *cp;

	/*
	 * If the client IP address is specified, use that
	 * else if gateway IP address is specified, use that
	 * else make a temporary arp cache entry for the client's NEW 
	 * IP/hardware address and use that.
	 */
	if(bp->ciaddr.s_addr) {
		faddr = get32 ((uint8 *) &(bp->ciaddr));
	} else {
		faddr = get32 ((uint8 *) &(bp->yiaddr));
		arp_add (faddr, (uint16) bp->htype, bp->chaddr, 0);
	}

	if((buf = qdata (bp, sizeof (struct bootp))) == NULL)
		return;
	pushdown (&buf, NULL,UDPHDR);

	length = sizeof (struct bootp) + UDPHDR;

	cp = buf->data;
	cp = put16 (cp, IPPORT_BOOTPS);		/* Source */
	cp = put16 (cp, IPPORT_BOOTPC);		/* Dest */
	cp = put16 (cp, length);
	*cp++ = 0;
	*cp = 0;

	ip_send (iface->addr, faddr, UDP_PTCL, 0, 0, &buf, length, 0, 0);
};





