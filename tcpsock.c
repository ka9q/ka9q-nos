#include "global.h"
#include "tcp.h"
#include "socket.h"
#include "usock.h"

static void s_trcall(struct tcb *tcb,int32 cnt);
static void s_tscall(struct tcb *tcb,int old,int new);
static void s_ttcall(struct tcb *tcb,int32 cnt);
static void trdiscard(struct tcb *tcb,int32 cnt);
static void autobind(struct usock *up);

uint16 Lport = 1024;

int
so_tcp(struct usock *up,int protocol)
{
	up->type = TYPE_TCP;
	return 0;
}
int
so_tcp_listen(struct usock *up,int backlog)
{
	struct sockaddr_in *local;
	struct socket lsock;

	if(up->name == NULL)
		autobind(up);

	local = (struct sockaddr_in *)up->name;
	lsock.address = local->sin_addr.s_addr;
	lsock.port = local->sin_port;
	up->cb.tcb = open_tcp(&lsock,NULL,
	 backlog ? TCP_SERVER:TCP_PASSIVE,0,
	s_trcall,s_ttcall,s_tscall,up->tos,up->index);
	return 0;
}
int
so_tcp_conn(struct usock *up)
{
	int s;
	struct tcb *tcb;
	struct socket lsock,fsock;
	struct sockaddr_in *local,*remote;

	if(up->name == NULL){
		autobind(up);
	}
	
	if(checkipaddr(up->peername,up->peernamelen) == -1){
		errno = EAFNOSUPPORT;
		return -1;
	}
	s = up->index;
	/* Construct the TCP-style ports from the sockaddr structs */
	local = (struct sockaddr_in *)up->name;
	remote = (struct sockaddr_in *)up->peername;

	if(local->sin_addr.s_addr == INADDR_ANY)
		/* Choose a local address */
		local->sin_addr.s_addr = locaddr(remote->sin_addr.s_addr);

	lsock.address = local->sin_addr.s_addr;
	lsock.port = local->sin_port;
	fsock.address = remote->sin_addr.s_addr;
	fsock.port = remote->sin_port;

	/* Open the TCB in active mode */
	up->cb.tcb = open_tcp(&lsock,&fsock,TCP_ACTIVE,0,
	 s_trcall,s_ttcall,s_tscall,up->tos,s);

	/* Wait for the connection to complete */
	while((tcb = up->cb.tcb) != NULL && tcb->state != TCP_ESTABLISHED){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(tcb == NULL){
		/* Probably got refused */
		FREE(up->peername);
		errno = ECONNREFUSED;
		return -1;
	}
	return 0;
}
int
so_tcp_recv(struct usock *up,struct mbuf **bpp,struct sockaddr *from,
 int *fromlen)
{
	long cnt;
	struct tcb *tcb;

	while((tcb = up->cb.tcb) != NULL && tcb->r_upcall != trdiscard
	 && (cnt = recv_tcp(tcb,bpp,0)) == -1){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(tcb == NULL){
		/* Connection went away */
		errno = ENOTCONN;
		return -1;
	} else if(tcb->r_upcall == trdiscard){
		/* Receive shutdown has been done */
		errno = ENOTCONN;	/* CHANGE */
		return -1;
	}
	return cnt;
}
int
so_tcp_send(struct usock *up,struct mbuf **bpp,struct sockaddr *to)
{
	struct tcb *tcb;
	long cnt;

	if((tcb = up->cb.tcb) == NULL){
		free_p(bpp);
		errno = ENOTCONN;
		return -1;
	}		
	cnt = send_tcp(tcb,bpp);

	while((tcb = up->cb.tcb) != NULL &&
	 tcb->sndcnt > tcb->window){
		/* Send queue is full */
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(tcb == NULL){
		errno = ENOTCONN;
		return -1;
	}
	return cnt;
}
int
so_tcp_qlen(struct usock *up,int rtx)
{
	int len;

	switch(rtx){
	case 0:
		len = up->cb.tcb->rcvcnt;
		break;
	case 1:
		len = up->cb.tcb->sndcnt;
		break;
	}
	return len;
}
int
so_tcp_kick(struct usock *up)
{
	kick_tcp(up->cb.tcb);
	return 0;
}
int
so_tcp_shut(struct usock *up,int how)
{
	switch(how){
	case 0:	/* No more receives -- replace upcall */
		up->cb.tcb->r_upcall = trdiscard;
		break;
	case 1:	/* Send EOF */
		close_tcp(up->cb.tcb);
		break;
	case 2:	/* Blow away TCB */
		reset_tcp(up->cb.tcb);
		up->cb.tcb = NULL;
		break;
	}
	return 0;
}
int
so_tcp_close(struct usock *up)
{
	if(up->cb.tcb != NULL){	/* In case it's been reset */
		up->cb.tcb->r_upcall = trdiscard;
		/* Tell the TCP_CLOSED upcall there's no more socket */
		up->cb.tcb->user = -1;
		close_tcp(up->cb.tcb);
	}
	return 0;
}
/* TCP receive upcall routine */
static void
s_trcall(struct tcb *tcb,int32 cnt)
{
	/* Wake up anybody waiting for data, and let them run */
	ksignal(itop(tcb->user),1);
	kwait(NULL);
}
/* TCP transmit upcall routine */
static void
s_ttcall(struct tcb *tcb,int32 cnt)
{
	/* Wake up anybody waiting to send data, and let them run */
	ksignal(itop(tcb->user),1);
	kwait(NULL);
}
/* TCP state change upcall routine */
static void
s_tscall(struct tcb *tcb,int old,int new)
{
	int s,ns;
	struct usock *up,*nup,*oup;
	union sp sp;

	s = tcb->user;
	oup = up = itop(s);

	switch(new){
	case TCP_CLOSED:
		/* Clean up. If the user has already closed the socket,
		 * then up will be null (s was set to -1 by the close routine).
		 * If not, then this is an abnormal close (e.g., a reset)
		 * and clearing out the pointer in the socket structure will
		 * prevent any further operations on what will be a freed
		 * control block. Also wake up anybody waiting on events
		 * related to this tcb so they will notice it disappearing.
		 */
		if(up != NULL){
			up->cb.tcb = NULL;
			up->errcodes[0] = tcb->reason;
			up->errcodes[1] = tcb->type;
			up->errcodes[2] = tcb->code;
			ksignal(up,0); /* Wake up anybody waiting */
		}
		del_tcp(tcb);
		break;
	case TCP_SYN_RECEIVED:
		/* Handle an incoming connection. If this is a server TCB,
		 * then we're being handed a "clone" TCB and we need to
		 * create a new socket structure for it. In either case,
		 * find out who we're talking to and wake up the guy waiting
		 * for the connection.
		 */
		if(tcb->flags.clone){
			/* Clone the socket */
			ns = socket(AF_INET,SOCK_STREAM,0);
			nup = itop(ns);
			ASSIGN(*nup,*up);
			tcb->user = ns;
			nup->cb.tcb = tcb;
			/* Allocate new memory for the name areas */
			nup->name = mallocw(SOCKSIZE);
			nup->peername = mallocw(SOCKSIZE);
			nup->index = ns;
			/* Store the new socket # in the old one */
			up->rdysock = ns;
			up = nup;
			s = ns;
		} else {
			/* Allocate space for the peer's name */
			up->peername = mallocw(SOCKSIZE);
			/* Store the old socket # in the old socket */
			up->rdysock = s;
		}
		/* Load the addresses. Memory for the name has already
		 * been allocated, either above or in the original bind.
		 */
		sp.sa = up->name;
		sp.in->sin_family = AF_INET;
		sp.in->sin_addr.s_addr = up->cb.tcb->conn.local.address;
		sp.in->sin_port = up->cb.tcb->conn.local.port;
		up->namelen = SOCKSIZE;

		sp.sa = up->peername;
		sp.in->sin_family = AF_INET;
		sp.in->sin_addr.s_addr = up->cb.tcb->conn.remote.address;
		sp.in->sin_port = up->cb.tcb->conn.remote.port;
		up->peernamelen = SOCKSIZE;

		/* Wake up the guy accepting it, and let him run */
		ksignal(oup,1);
		kwait(NULL);
		break;
	default:	/* Ignore all other state transitions */
		break;
	}
	ksignal(up,0);	/* In case anybody's waiting */
}
/* Discard data received on a TCP connection. Used after a receive shutdown or
 * close_s until the TCB disappears.
 */
static void
trdiscard(struct tcb *tcb,int32 cnt)
{
	struct mbuf *bp;

	recv_tcp(tcb,&bp,cnt);
	free_p(&bp);
}

/* Issue an automatic bind of a local address */
static void
autobind(struct usock *up)
{
	struct sockaddr_in local;

	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = Lport++;
	bind(up->index,(struct sockaddr *)&local,sizeof(struct sockaddr_in));
}
char *
tcpstate(struct usock *up)
{
	if(up->cb.tcb == NULL)
		return NULL;
	return Tcpstates[up->cb.tcb->state];
}
int
so_tcp_stat(struct usock *up)
{
	st_tcp(up->cb.tcb);
	return 0;
}

struct inet {
	struct inet *next;
	struct tcb *tcb;
	char *name;
	int stack;
	void (*task)(int,void *,void *);
};
#define	NULLINET	(struct inet *)0
struct inet *Inet_list;

static void i_upcall(struct tcb *tcb,int oldstate,int newstate);


/* Start a TCP server. Create TCB in listen state and post upcall for
 * when a connection comes in
 */ 
int
start_tcp(uint16 port,char *name,void (*task)(int,void *,void *),int stack)
{
	struct inet *in;
	struct socket lsocket;

	in = (struct inet *)calloc(1,sizeof(struct inet));
	lsocket.address = INADDR_ANY;
	lsocket.port = port;
	in->tcb = open_tcp(&lsocket,NULL,TCP_SERVER,0,NULL,NULL,i_upcall,0,-1);
	if(in->tcb == NULL){
		free(in);
		return -1;
	}
	in->stack = stack;
	in->task = task;
	in->name = strdup(name);
	in->next = Inet_list;
	Inet_list = in;
	return 0;
}

/* State change upcall that takes incoming TCP connections */
static void
i_upcall(struct tcb *tcb,int oldstate,int newstate)
{
	struct inet *in;
	struct sockaddr_in sock;
	struct usock *up;
	int s;

	if(oldstate != TCP_LISTEN)
		return;	/* "can't happen" */
	if(newstate == TCP_CLOSED){
		/* Called when server is shut down */
		del_tcp(tcb);
		return;
	}
	for(in = Inet_list;in != NULLINET;in = in->next)
		if(in->tcb->conn.local.port == tcb->conn.local.port)
			break;
	if(in == NULLINET)
		return;	/* not in table - "can't happen" */

	/* Create a socket, hook it up with the TCB */
	s = socket(AF_INET,SOCK_STREAM,0);
	up = itop(s);
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = tcb->conn.local.address;
	sock.sin_port = tcb->conn.local.port;
	bind(s,(struct sockaddr *)&sock,SOCKSIZE);

	sock.sin_addr.s_addr = tcb->conn.remote.address;
	sock.sin_port = tcb->conn.remote.port;
	up->peernamelen = SOCKSIZE;
	up->peername = mallocw(up->peernamelen);
	memcpy(up->peername,&sock,SOCKSIZE);
	up->cb.tcb = tcb;
	tcb->user = s;

	/* Set the normal upcalls */
	tcb->r_upcall = s_trcall;
	tcb->t_upcall = s_ttcall;
	tcb->s_upcall = s_tscall;

	/* And spawn the server task */
	newproc(in->name,in->stack,in->task,s,NULL,NULL,0);
}
/* Close down a TCP server created earlier by inet_start */
int
stop_tcp(uint16 port)
{
	struct inet *in,*inprev;

	inprev = NULLINET;
	for(in = Inet_list;in != NULLINET;inprev=in,in = in->next)
		if(in->tcb->conn.local.port == port)
			break;
	if(in == NULLINET)
		return -1;
	close_tcp(in->tcb);
	free(in->name);
	if(inprev != NULLINET)
		inprev->next = in->next;
	else
		Inet_list = in->next;
	free(in);
	return 0;
}

