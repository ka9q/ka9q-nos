/* Application programming interface routines - based loosely on the
 * "socket" model in Berkeley UNIX.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#ifdef	__STDC__
#include <stdarg.h>
#endif
#include <errno.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "proc.h"
#include "lzw.h"
#include "usock.h"
#include "socket.h"

char *Socktypes[] = {
	"Not Used",
	"TCP",
	"UDP",
	"AX25 I",
	"AX25 UI",
	"Raw IP",
	"NETROM3",
	"NETROM",
	"Loc St",
	"Loc Dg"
};
char *Sock_errlist[] = {
	"operation would block",
	"not connected",
	"socket type not supported",
	"address family not supported",
	"is connected",
	"operation not supported",
	"alarm",
	"abort",
	"interrupt",
	"connection refused",
	"message size",
	"address in use"
};

char Badsocket[] = "Bad socket";
struct usock **Usock;		/* Socket entry array */

/* Initialize user socket array */
void
sockinit(void)
{
	if(Usock != (struct usock **)NULL)
		return;	/* Already initialized */
	Usock = (struct usock **)callocw(Nsock,sizeof(struct usock *));
}

/* Create a user socket, return socket index
 * The mapping to actual protocols is as follows:
 *		
 *		
 * ADDRESS FAMILY	Stream		Datagram	Raw	    Seq. Packet
 *
 * AF_INET		TCP		UDP		IP
 * AF_AX25		I-frames	UI-frames
 * AF_NETROM						NET/ROM L3  NET/ROM L4
 * AF_LOCAL		stream loopback	packet loopback
 */
int
socket(
int af,		/* Address family */
int type,	/* Stream or datagram */
int protocol	/* Used for raw IP sockets */
){
	register struct usock *up;
	struct socklink *sp;
	int s;

	for(s=0;s<Nsock;s++)
		if(Usock[s] == NULL)
			break;
	if(s == Nsock){
		errno = EMFILE;
		return -1;
	}
	Usock[s] = up = (struct usock *)calloc(1,sizeof(struct usock));

	s =_mk_fd(s,_FL_SOCK);
	up->index = s;
	up->refcnt = 1;
	errno = 0;
	up->rdysock = -1;
	up->owner = Curproc;
	switch(af){
	case AF_LOCAL:
		switch(type){
		case SOCK_STREAM:
			up->type = TYPE_LOCAL_STREAM;
			break;
		case SOCK_DGRAM:
			up->type = TYPE_LOCAL_DGRAM;
			break;
		default:
			errno = ESOCKTNOSUPPORT;
			break;
		}
		break;
	case AF_AX25:
		switch(type){
		case SOCK_STREAM:
			up->type = TYPE_AX25I;
			break;
		case SOCK_DGRAM:
			up->type = TYPE_AX25UI;
			break;
		default:
			errno = ESOCKTNOSUPPORT;
			break;
		}
		break;
	case AF_NETROM:
		switch(type){
		case SOCK_RAW:
			up->type = TYPE_NETROML3;
			break;
		case SOCK_SEQPACKET:
			up->type = TYPE_NETROML4;
			break;
		default:
			errno = ESOCKTNOSUPPORT;
			break;
		}
		break;
	case AF_INET:
		switch(type){
		case SOCK_STREAM:
			up->type = TYPE_TCP;
			break;
		case SOCK_DGRAM:
			up->type = TYPE_UDP;
			break;
		case SOCK_RAW:
			up->type = TYPE_RAW;
			break;
		default:
			errno = ESOCKTNOSUPPORT;
			break;
		}
		break;
	default:
		errno = EAFNOSUPPORT;
		break;
	}
	/* Look for entry in protocol table */
	for(sp = Socklink;sp->type != -1;sp++){
		if(up->type == sp->type)
			break;
	}
	up->sp = sp;
	if(sp->type == -1 || sp->socket == NULL
	  ||(*sp->socket)(up,protocol) == -1){
		errno = ESOCKTNOSUPPORT;
		return -1;
	}
	return s;
}

/* Attach a local address/port to a socket. If not issued before a connect
 * or listen, will be issued automatically
 */
int
bind(
int s,			/* Socket index */
struct sockaddr *name,	/* Local name */
int namelen		/* Length of name */
){
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(name == NULL){
		errno = EFAULT;
		return -1;
	}
	if(up->name != NULL){
		/* Bind has already been issued */
		errno = EINVAL;
		return -1;
	}
	sp = up->sp;
	if(sp->check != NULL && (*sp->check)(name,namelen) == -1){
		/* Incorrect length or family for chosen protocol */
		errno = EAFNOSUPPORT;
		return -1;	
	}
	/* Stash name in an allocated block */
	up->namelen = namelen;
	up->name = mallocw(namelen);
	memcpy(up->name,name,namelen);

	/* a bind routine is optional - don't fail if it isn't present */
	if(sp->bind != NULL && (*sp->bind)(up) == -1){
		errno = EOPNOTSUPP;
		return -1;
	}
	return 0;
}
/* Post a listen on a socket */
int
listen(
int s,		/* Socket index */
int backlog	/* 0 for a single connection, !=0 for multiple connections */
){
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(up->cb.p != NULL){
		errno = EISCONN;
		return -1;
	}
	sp = up->sp;
	/* Fail if listen routine isn't present */
	if(sp->listen == NULL || (*sp->listen)(up,backlog) == -1){
		errno = EOPNOTSUPP;
		return -1;
	}
	return 0;
}
/* Initiate active open. For datagram sockets, merely bind the remote address. */
int
connect(
int s,			/* Socket index */
struct sockaddr *peername,		/* Peer name */
int peernamelen		/* Length of peer name */
){
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(peername == NULL){
		/* Connect must specify a remote address */
		errno = EFAULT;
		return -1;
	}
	sp = up->sp;
	/* Check name format, if checking routine is available */
	if(sp->check != NULL && (*sp->check)(peername,peernamelen) == -1){
		errno = EAFNOSUPPORT;
		return -1;
	}
	if(up->peername != NULL)
		free(up->peername);
	up->peername = mallocw(peernamelen);
	memcpy(up->peername,peername,peernamelen);
	up->peernamelen = peernamelen;

	/* a connect routine is optional - don't fail if it isn't present */
	if(sp->connect != NULL && (*sp->connect)(up) == -1){
		return -1;
	}
	return 0;
}
/* Wait for a connection. Valid only for connection-oriented sockets. */
int
accept(
int s,			/* Socket index */
struct sockaddr *peername,		/* Peer name */
int *peernamelen	/* Length of peer name */
){
	int i;
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(up->cb.p == NULL){
		errno = EOPNOTSUPP;
		return -1;
	}
	sp = up->sp;
	/* Fail if accept flag isn't set */
	if(sp->accept == FALSE){
		errno = EOPNOTSUPP;
		return -1;
	}
	/* Wait for the state-change upcall routine to signal us */
	while(up->cb.p != NULL && up->rdysock == -1){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(up->cb.p == NULL){
		/* Blown away */
		errno = EBADF;
		return -1;
	}
	i = up->rdysock;
	up->rdysock = -1;

	up = itop(i);
	if(peername != NULL && peernamelen != NULL){
		*peernamelen = min(up->peernamelen,*peernamelen);
		memcpy(peername,up->peername,*peernamelen);
	}
	return i;
}
/* Low-level receive routine. Passes mbuf back to user; more efficient than
 * higher-level functions recv() and recvfrom(). Datagram sockets ignore
 * the len parameter.
 */
int
recv_mbuf(
int s,			/* Socket index */
struct mbuf **bpp,	/* Place to stash receive buffer */
int flags,		/* Unused; will control out-of-band data, etc */
struct sockaddr *from,		/* Peer address (only for datagrams) */
int *fromlen		/* Length of peer address */
){
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	sp = up->sp;
	/* Fail if recv routine isn't present */
	if(sp->recv == NULL){
		errno = EOPNOTSUPP;
		return -1;
	}
	return (*sp->recv)(up,bpp,from,fromlen);
}
/* Low level send routine; user supplies mbuf for transmission. More
 * efficient than send() or sendto(), the higher level interfaces.
 * The "to" and "tolen" parameters are ignored on connection-oriented
 * sockets.
 *
 * In case of error, bp is freed so the caller doesn't have to worry about it.
 */
int
send_mbuf(
int s,			/* Socket index */
struct mbuf **bpp,	/* Buffer to send */
int flags,		/* not currently used */
struct sockaddr *to,		/* Destination, only for datagrams */
int tolen		/* Length of destination */
){
	register struct usock *up;
	int cnt;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		free_p(bpp);
		errno = EBADF;
		return -1;
	}
	sp = up->sp;
	/* Fail if send routine isn't present (shouldn't happen) */
	if(sp->send == NULL){
		free_p(bpp);
		return -1;
	}
	/* If remote address is supplied, check it */
	if(to != NULL && (sp->check != NULL)
	 && (*sp->check)(to,tolen) == -1){
		free_p(bpp);
		errno = EAFNOSUPPORT;
		return -1;
	}
	/* The proto send routine is expected to free the buffer
	 * we pass it even if the send fails
	 */
	if((cnt = (*sp->send)(up,bpp,to)) == -1){
		errno = EOPNOTSUPP;
		return -1;
	}
	return cnt;
}
/* Return local name passed in an earlier bind() call */
int
getsockname(
int s,		/* Socket index */
struct sockaddr *name,	/* Place to stash name */
int *namelen	/* Length of same */
){
	register struct usock *up;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(name == NULL || namelen == (int *)NULL){
		errno = EFAULT;
		return -1;
	}
	if(up->name == NULL){
		/* Not bound yet */
		*namelen = 0;
		return 0;
	}
	if(up->name != NULL){
		*namelen = min(*namelen,up->namelen);
		memcpy(name,up->name,*namelen);
	}
	return 0;
}
/* Get remote name, returning result of earlier connect() call. */
int
getpeername(
int s,			/* Socket index */
struct sockaddr *peername,		/* Place to stash name */
int *peernamelen	/* Length of same */
){
	register struct usock *up;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(up->peername == NULL){
		errno = ENOTCONN;
		return -1;
	}
	if(peername == NULL || peernamelen == (int *)NULL){
		errno = EFAULT;
		return -1;
	}
	*peernamelen = min(*peernamelen,up->peernamelen);
	memcpy(peername,up->peername,*peernamelen);
	return 0;
}
/* Return length of protocol queue, either send or receive. */
int
socklen(
int s,		/* Socket index */
int rtx		/* 0 = receive queue, 1 = transmit queue */
){
	register struct usock *up;
	struct socklink *sp;
	int len = -1;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(up->cb.p == NULL){
		errno = ENOTCONN;
		return -1;
	}
	if(rtx < 0 || rtx > 1){
		errno = EINVAL;
		return -1;
	}
	sp = up->sp;
	/* Fail if qlen routine isn't present */
	if(sp->qlen == NULL || (len = (*sp->qlen)(up,rtx)) == -1){
		errno = EOPNOTSUPP;
		return -1;
	}
	return len;
}
/* Force retransmission. Valid only for connection-oriented sockets. */
int
sockkick(
int s	/* Socket index */
){
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	sp = up->sp;
	/* Fail if kick routine isn't present */
	if(sp->kick == NULL){
		errno = EOPNOTSUPP;
		return -1;
	}
 	if((*sp->kick)(up) == -1)
		return -1;
	return 0;
}

/* Change owner of socket, return previous owner */
struct proc *
sockowner(
int s,			/* Socket index */
struct proc *newowner	/* Process table address of new owner */
){
	register struct usock *up;
	struct proc *pp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return NULL;
	}
	pp = up->owner;
	if(newowner != NULL)
		up->owner = newowner;
	return pp;
}
/* Close down a socket three ways. Type 0 means "no more receives"; this
 * replaces the incoming data upcall with a routine that discards further
 * data. Type 1 means "no more sends", and obviously corresponds to sending
 * a TCP FIN. Type 2 means "no more receives or sends". This I interpret
 * as "abort the connection".
 */
int
shutdown(
int s,		/* Socket index */
int how		/* (see above) */
){
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(up->cb.p == NULL){
		errno = ENOTCONN;
		return -1;
	}
	sp = up->sp;
	/* Just close the socket if special shutdown routine not present */
	if(sp->shut == NULL){
		close_s(s);
	} else if((*sp->shut)(up,how) == -1){
		return -1;
	}
	ksignal(up,0);
	return 0;
}
/* Close a socket, freeing it for reuse. Try to do a graceful close on a
 * TCP socket, if possible
 */
int
close_s(
int s		/* Socket index */
){
	register struct usock *up;
	struct socklink *sp;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	if(--up->refcnt > 0)
		return 0;	/* Others are still using it */
	/* Call proto-specific close routine if there is one */
	if((sp = up->sp) != NULL && sp->close != NULL)
		(*sp->close)(up);

	free(up->name);
	free(up->peername);

	ksignal(up,0);	/* Wake up anybody doing an accept() or recv() */
	Usock[_fd_seq(up->index)] = NULL;
	free(up);
	return 0;
}
/* Increment reference count for specified socket */
int
usesock(int s)
{
	struct usock *up;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	up->refcnt++;
	return 0;
}
/* Blow away all sockets belonging to a certain process. Used by killproc(). */
void
freesock(struct proc *pp)
{
	register struct usock *up;
	register int i;

	for(i=0;i < Nsock;i++){
		up = Usock[i];
		if(up != NULL && up->type != NOTUSED && up->owner == pp)
			shutdown(i,2);
	}
}
/* Set Internet type-of-service to be used */
int
settos(int s, int tos)
{
	struct usock *up;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return -1;
	}
	up->tos = tos;
	return 0;
}

/* Return a pair of mutually connected sockets in sv[0] and sv[1] */
int
socketpair(
int af,
int type,
int protocol,
int sv[]
){
	struct usock *up0, *up1;
	if(sv == NULL){
		errno = EFAULT;
		return -1;
	}
	if(af != AF_LOCAL){
		errno = EAFNOSUPPORT;
		return -1;
	}
	if(type != SOCK_STREAM && type != SOCK_DGRAM){
		errno = ESOCKTNOSUPPORT;
		return -1;
	}
	if((sv[0] = socket(af,type,protocol)) == -1)
		return -1;
	if((sv[1] = socket(af,type,protocol)) == -1){
		close_s(sv[0]);
		return -1;
	}
	up0 = itop(sv[0]);
	up1 = itop(sv[1]);
	up0->cb.local->peer = up1;
	up1->cb.local->peer = up0;
	return sv[1];
}
/* Return end-of-line convention for socket */
char *
eolseq(int s)
{
	struct usock *up;

	if((up = itop(s)) == NULL){
		errno = EBADF;
		return NULL;
	}
	return up->sp->eol;
}
