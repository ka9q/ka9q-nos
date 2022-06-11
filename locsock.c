#include <errno.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "usock.h"

int
so_los(struct usock *up,int protocol)
{
	up->cb.local = (struct loc *) callocw(1,sizeof(struct loc));
	up->cb.local->peer = up;	/* connect to self */
	up->type = TYPE_LOCAL_STREAM;
	up->cb.local->hiwat = LOCSFLOW;
	return 0;
}
int
so_lod(struct usock *up,int protocol)
{
	up->cb.local = (struct loc *) callocw(1,sizeof(struct loc));
	up->cb.local->peer = up;	/* connect to self */
	up->type = TYPE_LOCAL_DGRAM;
	up->cb.local->hiwat = LOCDFLOW;
	return 0;
}
int
so_lo_recv(
struct usock *up,
struct mbuf **bpp,
struct sockaddr *from,
int *fromlen
){
	int s;

	while(up->cb.local != NULL && up->cb.local->q == NULL
	  && up->cb.local->peer != NULL){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up)) != 0){
			return -1;
		}
	}
	if(up->cb.local == NULL){
		/* Socket went away */
		errno = EBADF;
		return -1;
	}
	if(up->cb.local->q == NULL &&
	   up->cb.local->peer == NULL){
		errno = ENOTCONN;
		return -1;
	}
	/* For datagram sockets, this will return the
	 * first packet on the queue. For stream sockets,
	 * this will return everything.
	 */
	*bpp = dequeue(&up->cb.local->q);
	if(up->cb.local->q == NULL && (up->cb.local->flags & LOC_SHUTDOWN)){
		s = up->index;
		close_s(s);
	}
	ksignal(up,0);
	return len_p(*bpp);
}
int
so_los_send(
struct usock *up,
struct mbuf **bpp,
struct sockaddr *to
){
	if(up->cb.local->peer == NULL){
		free_p(bpp);
		errno = ENOTCONN;
		return -1;
	}
	append(&up->cb.local->peer->cb.local->q,bpp);
	ksignal(up->cb.local->peer,0);
	/* If high water mark has been reached, block */
	while(up->cb.local->peer != NULL &&
	      len_p(up->cb.local->peer->cb.local->q) >=
	      up->cb.local->peer->cb.local->hiwat){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up->cb.local->peer)) != 0){
			return -1;
		}
	}
	if(up->cb.local->peer == NULL){
		errno = ENOTCONN;
		return -1;
	}
	return 0;
}
int	
so_lod_send(
struct usock *up,
struct mbuf **bpp,
struct sockaddr *to
){
	if(up->cb.local->peer == NULL){
		free_p(bpp);
		errno = ENOTCONN;
		return -1;
	}
	enqueue(&up->cb.local->peer->cb.local->q,bpp);
	ksignal(up->cb.local->peer,0);
	/* If high water mark has been reached, block */
	while(up->cb.local->peer != NULL &&
	      len_q(up->cb.local->peer->cb.local->q) >=
	      up->cb.local->peer->cb.local->hiwat){
		if(up->noblock){
			errno = EWOULDBLOCK;
			return -1;
		} else if((errno = kwait(up->cb.local->peer)) != 0){
			return -1;
		}
	}
	if(up->cb.local->peer == NULL){
		errno = ENOTCONN;
		return -1;
	}
	return 0;
}
int
so_lod_qlen(struct usock *up,int rtx)
{
	int len;

	switch(rtx){
	case 0:
		len = len_q(up->cb.local->q);
		break;
	case 1:
		if(up->cb.local->peer != NULL)
			len = len_q(up->cb.local->peer->cb.local->q);
		break;
	}
	return len;
}
int
so_los_qlen(struct usock *up,int rtx)
{
	int len;

	switch(rtx){
	case 0:
		len = len_p(up->cb.local->q);
		break;
	case 1:
		if(up->cb.local->peer != NULL)
			len = len_p(up->cb.local->peer->cb.local->q);
		break;
	}
	return len;
}
int
so_loc_shut(struct usock *up,int how)
{
	int s;

	s = up->index;

	if(up->cb.local->q == NULL)
		close_s(s);
	else
		up->cb.local->flags = LOC_SHUTDOWN;
	return 0;
}
int
so_loc_close(struct usock *up)
{
	if(up->cb.local->peer != NULL){
		up->cb.local->peer->cb.local->peer = NULL;
		ksignal(up->cb.local->peer,0);
	}
	free_q(&up->cb.local->q);
	free(up->cb.local);
	return 0;
}
char *
lopsocket(struct sockaddr *p)
{
	return "";
}
so_loc_stat(struct usock *up)
{
	int s;

	s = up->index;

	printf("Inqlen: %d packets\n",socklen(s,0));
	printf("Outqlen: %d packets\n",socklen(s,1));
	return 0;
}
