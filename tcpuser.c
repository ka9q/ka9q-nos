/* User calls to TCP
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "socket.h"
#include "internet.h"
#include "tcp.h"
#include "ip.h"
#include "icmp.h"
#include "proc.h"

uint16 Tcp_window = DEF_WND;

struct tcb *
open_tcp(lsocket,fsocket,mode,window,r_upcall,t_upcall,s_upcall,tos,user)
struct socket *lsocket;	/* Local socket */
struct socket *fsocket;	/* Remote socket */
int mode;		/* Active/passive/server */
uint16 window;		/* Receive window (and send buffer) sizes */
void (*r_upcall)();	/* Function to call when data arrives */
void (*t_upcall)();	/* Function to call when ok to send more data */
void (*s_upcall)();	/* Function to call when connection state changes */
int tos;
int user;		/* User linkage area */
{
	struct connection conn;
	register struct tcb *tcb;

	if(lsocket == NULL){
		Net_error = INVALID;
		return NULL;
	}
	conn.local.address = lsocket->address;
	conn.local.port = lsocket->port;
	if(fsocket != NULL){
		conn.remote.address = fsocket->address;
		conn.remote.port = fsocket->port;
	} else {
		conn.remote.address = 0;
		conn.remote.port = 0;
	}
	if((tcb = lookup_tcb(&conn)) == NULL){
		if((tcb = create_tcb(&conn)) == NULL){
			Net_error = NO_MEM;
			return NULL;
		}
	} else if(tcb->state != TCP_LISTEN){
		Net_error = CON_EXISTS;
		return NULL;
	}
	tcb->user = user;
	if(window != 0)
		tcb->window = tcb->rcv.wnd = window;
	else
		tcb->window = tcb->rcv.wnd = Tcp_window;
	tcb->snd.wnd = 1;	/* Allow space for sending a SYN */
	tcb->r_upcall = r_upcall;
	tcb->t_upcall = t_upcall;
	tcb->s_upcall = s_upcall;
	tcb->tos = tos;
	switch(mode){
	case TCP_SERVER:
		tcb->flags.clone = 1;
	case TCP_PASSIVE:	/* Note fall-thru */
		settcpstate(tcb,TCP_LISTEN);
		break;
	case TCP_ACTIVE:
		/* Send SYN, go into TCP_SYN_SENT state */
		tcb->flags.active = 1;
		send_syn(tcb);
		settcpstate(tcb,TCP_SYN_SENT);
		tcp_output(tcb);
		break;
	}
	return tcb;
}
/* User send routine */
long
send_tcp(tcb,bpp)
register struct tcb *tcb;
struct mbuf **bpp;
{
	int32 cnt;

	if(tcb == NULL || bpp == NULL || *bpp == NULL){
		free_p(bpp);
		Net_error = INVALID;
		return -1;
	}
	cnt = len_p(*bpp);
	switch(tcb->state){
	case TCP_CLOSED:
		free_p(bpp);
		Net_error = NO_CONN;
		return -1;
	case TCP_LISTEN:
		if(tcb->conn.remote.address == 0 && tcb->conn.remote.port == 0){
			/* Save data for later */
			append(&tcb->sndq,bpp);
			tcb->sndcnt += cnt;
			break;
		}		
		/* Change state from passive to active */
		tcb->flags.active = 1;
		send_syn(tcb);
		settcpstate(tcb,TCP_SYN_SENT);	/* Note fall-thru */
	case TCP_SYN_SENT:
	case TCP_SYN_RECEIVED:
	case TCP_ESTABLISHED:
	case TCP_CLOSE_WAIT:
		append(&tcb->sndq,bpp);
		tcb->sndcnt += cnt;
		tcp_output(tcb);
		break;
	case TCP_FINWAIT1:
	case TCP_FINWAIT2:
	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		free_p(bpp);
		Net_error = CON_CLOS;
		return -1;
	}
	return (long)cnt;
}
/* User receive routine */
int32
recv_tcp(tcb,bpp,cnt)
register struct tcb *tcb;
struct mbuf **bpp;
int32 cnt;
{
	if(tcb == NULL || bpp == (struct mbuf **)NULL){
		Net_error = INVALID;
		return -1;
	}
	if(tcb->rcvcnt == 0){
		/* If there's nothing on the queue, our action depends on what state
		 * we're in (i.e., whether or not we're expecting any more data).
		 * If no more data is expected, then simply return 0; this is
		 * interpreted as "end of file". Otherwise return -1.
		 */
		switch(tcb->state){
		case TCP_LISTEN:
		case TCP_SYN_SENT:
		case TCP_SYN_RECEIVED:
		case TCP_ESTABLISHED:
		case TCP_FINWAIT1:
		case TCP_FINWAIT2:
			Net_error = WOULDBLK;
			return -1;
		case TCP_CLOSED:
		case TCP_CLOSE_WAIT:
		case TCP_CLOSING:
		case TCP_LAST_ACK:
		case TCP_TIME_WAIT:
			*bpp = NULL;
			return 0;
		}
	}
	/* cnt == 0 means "I want it all" */
	if(cnt == 0)
		cnt = tcb->rcvcnt;
	/* See if the user can take all of it */
	if(tcb->rcvcnt <= cnt){
		cnt = tcb->rcvcnt;
		*bpp = tcb->rcvq;
		tcb->rcvq = NULL;
	} else {
		*bpp = ambufw(cnt);
		pullup(&tcb->rcvq,(*bpp)->data,cnt);
		(*bpp)->cnt = cnt;
	}
	tcb->rcvcnt -= cnt;
	tcb->rcv.wnd += cnt;
	/* Do a window update if it was less than one packet and now it's more */
	if(tcb->rcv.wnd > tcb->mss && tcb->rcv.wnd - cnt < tcb->mss){
		tcb->flags.force = 1;
		tcp_output(tcb);
	}
	return (int)cnt;
}
/* This really means "I have no more data to send". It only closes the
 * connection in one direction, and we can continue to receive data
 * indefinitely.
 */
int
close_tcp(tcb)
register struct tcb *tcb;
{
	if(tcb == NULL){
		Net_error = INVALID;
		return -1;
	}
	switch(tcb->state){
	case TCP_CLOSED:
		return 0;	/* Unlikely */
	case TCP_LISTEN:
	case TCP_SYN_SENT:
		close_self(tcb,NORMAL);
		return 0;
	case TCP_SYN_RECEIVED:
	case TCP_ESTABLISHED:
		tcb->sndcnt++;
		tcb->snd.nxt++;
		settcpstate(tcb,TCP_FINWAIT1);
		tcp_output(tcb);
		return 0;
	case TCP_CLOSE_WAIT:
		tcb->sndcnt++;
		tcb->snd.nxt++;
		settcpstate(tcb,TCP_LAST_ACK);
		tcp_output(tcb);
		return 0;
	case TCP_FINWAIT1:
	case TCP_FINWAIT2:
	case TCP_CLOSING:
	case TCP_LAST_ACK:
	case TCP_TIME_WAIT:
		Net_error = CON_CLOS;
		return -1;
	}
	return -1;	/* "Can't happen" */
}
/* Delete TCB, free resources. The user is not notified, even if the TCB is
 * not in the TCP_CLOSED state. This function should normally be called by the
 * user only in response to a state change upcall to TCP_CLOSED state.
 */
int
del_tcp(conn)
struct tcb *conn;
{
	register struct tcb *tcb;
	struct tcb *tcblast = NULL;
	struct reseq *rp,*rp1;

	/* Remove from list */
	for(tcb=Tcbs;tcb != NULL;tcblast = tcb,tcb = tcb->next)
		if(tcb == conn)
			break;
	if(tcb == NULL){
		Net_error = INVALID;
		return -1;	/* conn was NULL, or not on list */ 
	}
	if(tcblast != NULL)
		tcblast->next = tcb->next;
	else
		Tcbs = tcb->next;	/* was first on list */

	stop_timer(&tcb->timer);
	for(rp = tcb->reseq;rp != NULL;rp = rp1){
		rp1 = rp->next;
		free_p(&rp->bp);
		free(rp);
	}
	tcb->reseq = NULL;
	free_p(&tcb->rcvq);
	free_p(&tcb->sndq);
	free(tcb);
	return 0;
}
/* Return 1 if arg is a valid TCB, 0 otherwise */
int
tcpval(tcb)
struct tcb *tcb;
{
	register struct tcb *tcb1;

	if(tcb == NULL)
		return 0;	/* Null pointer can't be valid */
	for(tcb1=Tcbs;tcb1 != NULL;tcb1 = tcb1->next){
		if(tcb1 == tcb)
			return 1;
	}
	return 0;
}
/* Kick a particular TCP connection */
int
kick_tcp(tcb)
register struct tcb *tcb;
{
	if(!tcpval(tcb))
		return -1;
	tcb->flags.force = 1;	/* Send ACK even if no data */
	tcb->backoff = 0;
	tcp_timeout(tcb);
	return 0;
}
/* Kick all TCP connections to specified address; return number kicked */
int
kick(addr)
int32 addr;
{
	register struct tcb *tcb;
	int cnt = 0;

	for(tcb=Tcbs;tcb != NULL;tcb = tcb->next){
		if(tcb->conn.remote.address == addr){
			kick_tcp(tcb);
			cnt++;
		}
	}
	return cnt;
}
/* Clear all TCP connections */
void
reset_all()
{
	register struct tcb *tcb,*tcbnext;

	for(tcb=Tcbs;tcb != NULL;tcb = tcbnext){
		tcbnext = tcb->next;
		reset_tcp(tcb);
	}
	kwait(NULL);	/* Let the RSTs go forth */
}
void
reset_tcp(tcb)
register struct tcb *tcb;
{
	struct tcp fakeseg;
	struct ip fakeip;

	if(tcb == NULL)
		return;
	if(tcb->state != TCP_LISTEN){
		/* Compose a fake segment with just enough info to generate the
		 * correct RST reply
		 */
		memset(&fakeseg,0,sizeof(fakeseg));
		memset(&fakeip,0,sizeof(fakeip));
		fakeseg.dest = tcb->conn.local.port;
		fakeseg.source = tcb->conn.remote.port;
		fakeseg.flags.ack = 1;
		/* Here we try to pick a sequence number with the greatest likelihood
		 * of being in his receive window.
		 */
		fakeseg.ack = tcb->snd.nxt + tcb->snd.wnd - 1;
		fakeip.dest = tcb->conn.local.address;
		fakeip.source = tcb->conn.remote.address;
		fakeip.tos = tcb->tos;
		reset(&fakeip,&fakeseg);
	}
	close_self(tcb,RESET);
}
#ifdef	notused
/* Return character string corresponding to a TCP well-known port, or
 * the decimal number if unknown.
 */
char *
tcp_port(n)
uint16 n;
{
	static char buf[32];

	switch(n){
	case IPPORT_ECHO:
		return "echo";
	case IPPORT_DISCARD:
		return "discard";
	case IPPORT_FTPD:
		return "ftp_data";
	case IPPORT_FTP:
		return "ftp";	
	case IPPORT_TELNET:
		return "telnet";
	case IPPORT_SMTP:
		return "smtp";
	case IPPORT_POP:
		return "pop";
	default:
		sprintf(buf,"%u",n);
		return buf;
	}
}
#endif

