#include <errno.h>
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "netrom.h"
#include "nr4.h"
#include "socket.h"
#include "usock.h"

static void autobind(struct usock *up);
static void s_nrcall(struct nr4cb *cb,uint16 cnt);
static void s_nscall(struct nr4cb *cb,int old,int new);
static void s_ntcall(struct nr4cb *cb,uint16 cnt);

int
so_n3_sock(up,protocol)
struct usock *up;
int protocol;
{
	up->cb.rnr = raw_nr((char)protocol);	
	return 0;
}
int
so_n4_sock(up,protocol)
struct usock *up;
int protocol;
{
	return 0;
}
int
so_n4_listen(up,backlog)
struct usock *up;
int backlog;
{
	struct sockaddr_nr *local;
	int s;

	s = up->index;
	if(up->name == NULL)
		autobind(up);
	local = (struct sockaddr_nr *)up->name;
	up->cb.nr4 = open_nr4(&local->nr_addr,NULL,
	 backlog ? AX_SERVER:AX_PASSIVE,s_nrcall,s_ntcall,s_nscall,s);
	return 0;
}
int
so_n3_conn(up)
struct usock *up;
{
	if(up->name != NULL)
		autobind(up);
	return 0;
}
int
so_n4_conn(up)
struct usock *up;
{
	struct sockaddr_nr *local,*remote;
	struct nr4cb *nr4;
	int s;
	
	s = up->index;
	if(up->name != NULL)
		autobind(up);
	local = (struct sockaddr_nr *)up->name;
	remote = (struct sockaddr_nr *)up->peername;
	up->cb.nr4 = open_nr4(&local->nr_addr,&remote->nr_addr,
	 AX_ACTIVE,s_nrcall,s_ntcall,s_nscall,s);

	/* Wait for the connection to complete */
	while((nr4 = up->cb.nr4) != NULL && nr4->state != NR4STCON){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(nr4 == NULL){
		/* Connection probably already exists */
		free(up->peername);
		up->peername = NULL;
		errno = ECONNREFUSED;
		return -1;
	}
	return 0;
}
int
so_n3_recv(up,bpp,from,fromlen)
struct usock *up;
struct mbuf **bpp;
struct sockaddr *from;
int *fromlen;
{
	int cnt;
	struct raw_nr *rnr;
	struct sockaddr_nr *remote;
	struct nr3hdr n3hdr;

	while((rnr = up->cb.rnr) != NULL
	 && rnr->rcvq == NULL){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(rnr == NULL){
		/* Connection went away */
		errno = ENOTCONN;
		return -1;
	}
	*bpp = dequeue(&rnr->rcvq);
	ntohnr3(&n3hdr,bpp);
	cnt = len_p(*bpp);
	if(from != NULL && fromlen != NULL
	   && *fromlen >= sizeof(struct sockaddr_nr)){
		remote = (struct sockaddr_nr *)from;
		remote->nr_family = AF_NETROM;
		/* The callsign of the local user is not part of
		   NET/ROM level 3, so that field is not used here */
		memcpy(remote->nr_addr.node,n3hdr.source,AXALEN);
		*fromlen = sizeof(struct sockaddr_nr);
	}
	return cnt;
}
int
so_n4_recv(up,bpp,from,fromlen)
struct usock *up;
struct mbuf **bpp;
struct sockaddr *from;
int *fromlen;
{
	struct nr4cb *nr4;

	while((nr4 = up->cb.nr4) != NULL
	 && (*bpp = recv_nr4(nr4,(uint16)0)) == NULL){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(nr4 == NULL){
		/* Connection went away */
		errno = ENOTCONN;
		return -1;
	}
	return (*bpp)->cnt;
}
int
so_n3_send(
struct usock *up,
struct mbuf **bpp,
struct sockaddr *to
){
	struct sockaddr_nr *remote;

	if(len_p(*bpp) > NR4MAXINFO) {
		free_p(bpp);
		errno = EMSGSIZE;
		return -1;
	}
	if(to != NULL) {
		remote = (struct sockaddr_nr *)to;
	} else if(up->peername != NULL) {
		remote = (struct sockaddr_nr *)up->peername;
	} else {
		free_p(bpp);
		errno = ENOTCONN;
		return -1;
	}	
	/* The NETROM username is always ignored in outgoing traffic */
	nr_sendraw(remote->nr_addr.node,up->cb.rnr->protocol,
	 up->cb.rnr->protocol,bpp);
	return 0;
}
int
so_n4_send(
struct usock *up,
struct mbuf **bpp,
struct sockaddr *to
){
	struct nr4cb *nr4;

	if((nr4 = up->cb.nr4) == NULL) {
		free_p(bpp);
		errno = ENOTCONN;
		return -1;
	}
	if(len_p(*bpp) > NR4MAXINFO){ /* reject big packets */
		free_p(bpp);
		errno = EMSGSIZE;
		return -1;
	}
	send_nr4(nr4,bpp);

	while((nr4 = up->cb.nr4) != NULL && nr4->nbuffered >= nr4->window){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(nr4 == NULL){
		errno = EBADF;
		return -1;
	}
	return 0;
}

int
so_n3_qlen(up,rtx)
struct usock *up;
int rtx;
{
	int len;

	switch(rtx){	
	case 0:
		len = len_q(up->cb.rnr->rcvq);
		break;
	case 1:
		len = 0;		
	}
	return len;
}
int
so_n4_qlen(up,rtx)
struct usock *up;
int rtx;
{
	int len;

	switch(rtx){
	case 0:
		len = len_p(up->cb.nr4->rxq);
		break;
	case 1:	/* Number of packets, not bytes */
		len = len_q(up->cb.nr4->txq);
		break;
	}
	return len;
}
int
so_n4_kick(up)
struct usock *up;
{
	if(up->cb.nr4 == NULL){
		errno = ENOTCONN;
		return -1;
	}
	kick_nr4(up->cb.nr4);
	return 0;
}
int
so_n4_shut(up,how)
struct usock *up;
int how;
{
	switch(how){
	case 0:
	case 1:	/* Attempt regular disconnect */
		disc_nr4(up->cb.nr4);
		break;
	case 2: /* Blow it away */
		reset_nr4(up->cb.nr4);
		up->cb.nr4 = NULL;
		break;
	}
	return 0;
}
int
so_n3_close(up)
struct usock *up;
{
	del_rnr(up->cb.rnr);
	return 0;
}
int
so_n4_close(up)
struct usock *up;
{
	if(up->cb.nr4 != NULL){
		/* Tell the TCP_CLOSED upcall there's no more socket */
		up->cb.nr4->user = -1;
		disc_nr4(up->cb.nr4);
	}
	return 0;
}


/* Issue an automatic bind of a local NETROM address */
static void
autobind(up)
struct usock *up;
{
	struct sockaddr_nr local;
	int s;

	s = up->index;
	local.nr_family = AF_NETROM;
	memcpy(local.nr_addr.user,Mycall,AXALEN);
	memcpy(local.nr_addr.node,Mycall,AXALEN);
	bind(s,(struct sockaddr *)&local,sizeof(struct sockaddr_nr));
}

/* NET/ROM receive upcall routine */
static void
s_nrcall(cb,cnt)
struct nr4cb *cb;
uint16 cnt;
{
	/* Wake up anybody waiting for data, and let them run */
	ksignal(itop(cb->user),1);
	kwait(NULL);
}
/* NET/ROM transmit upcall routine */
static void
s_ntcall(cb,cnt)
struct nr4cb *cb;
uint16 cnt;
{
	/* Wake up anybody waiting to send data, and let them run */
	ksignal(itop(cb->user),1);
	kwait(NULL);
}
/* NET/ROM state change upcall routine */
static void
s_nscall(cb,old,new)
struct nr4cb *cb;
int old,new;
{
	int s,ns;
	struct usock *up,*nup,*oup;
	union sp sp;

	s = cb->user;
	oup = up = itop(s);

 	if(new == NR4STDISC && up != NULL){
		/* Clean up. If the user has already closed the socket,
		 * then up will be null (s was set to -1 by the close routine).
		 * If not, then this is an abnormal close (e.g., a reset)
		 * and clearing out the pointer in the socket structure will
		 * prevent any further operations on what will be a freed
		 * control block. Also wake up anybody waiting on events
		 * related to this cb so they will notice it disappearing.
		 */
 		up->cb.nr4 = NULL;
 		up->errcodes[0] = cb->dreason;
 	}
 	if(new == NR4STCON && old == NR4STDISC){
		/* Handle an incoming connection. If this is a server cb,
		 * then we're being handed a "clone" cb and we need to
		 * create a new socket structure for it. In either case,
		 * find out who we're talking to and wake up the guy waiting
		 * for the connection.
		 */
		if(cb->clone){
			/* Clone the socket */
			ns = socket(AF_NETROM,SOCK_SEQPACKET,0);
			nup = itop(ns);
			ASSIGN(*nup,*up);
			cb->user = ns;
			nup->cb.nr4 = cb;
			cb->clone = 0; /* to avoid getting here again */
			/* Allocate new memory for the name areas */
			nup->name = mallocw(sizeof(struct sockaddr_nr));
			nup->peername = mallocw(sizeof(struct sockaddr_nr));
			/* Store the new socket # in the old one */
			up->rdysock = ns;
			up = nup;
			s = ns;
		} else {
			/* Allocate space for the peer's name */
			up->peername = mallocw(sizeof(struct sockaddr_nr));
			/* Store the old socket # in the old socket */
			up->rdysock = s;
		}
		/* Load the addresses. Memory for the name has already
		 * been allocated, either above or in the original bind.
		 */
		sp.sa = up->name;
		sp.nr->nr_family = AF_NETROM;
		ASSIGN(sp.nr->nr_addr,up->cb.nr4->local);
		up->namelen = sizeof(struct sockaddr_nr);

		sp.sa = up->peername;
		sp.nr->nr_family = AF_NETROM;
		ASSIGN(sp.nr->nr_addr,up->cb.nr4->remote);
		up->peernamelen = sizeof(struct sockaddr_nr);

		/* Wake up the guy accepting it, and let him run */
		ksignal(oup,1);
		kwait(NULL);
	}
 	/* Ignore all other state transitions */	
	ksignal(up,0);	/* In case anybody's waiting */
}

int
checknraddr(name,namelen)
struct sockaddr *name;
int namelen;
{
	struct sockaddr_nr *sock;

	sock = (struct sockaddr_nr *)name;
	if(sock->nr_family != AF_NETROM || namelen != sizeof(struct sockaddr_nr))
		return -1;
	return 0;
}
char *
nrpsocket(p)
struct sockaddr *p;
{
	struct sockaddr_nr *nrp;
	static char buf[30];
	char tmp[11];

	nrp = (struct sockaddr_nr *)p;
	pax25(tmp,nrp->nr_addr.user);
	sprintf(buf,"%s @ ",tmp);
	pax25(tmp,nrp->nr_addr.node);
	strcat(buf,tmp);

	return buf;
}
char *
nrstate(up)
struct usock *up;
{
	return Nr4states[up->cb.nr4->state];
}
int
so_n4_stat(up)
struct usock *up;
{
	donrdump(up->cb.nr4);
	return 0;
}

