/* User interface subroutines for AX.25
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "lapb.h"
#include "ax25.h"
#include "lapb.h"
#include <ctype.h>

/* Open an AX.25 connection */
struct ax25_cb *
open_ax25(iface,local,remote,mode,window,r_upcall,t_upcall,s_upcall,user)
struct iface *iface;	/* Interface */
uint8 *local;		/* Local address */
uint8 *remote;		/* Remote address */
int mode;		/* active/passive/server */
uint16 window;		/* Window size in bytes */
void (*r_upcall)();	/* Receiver upcall handler */
void (*t_upcall)();	/* Transmitter upcall handler */
void (*s_upcall)();	/* State-change upcall handler */
int user;		/* User linkage area */
{
	struct ax25_cb *axp;
	uint8 remtmp[AXALEN];

	if(remote == NULL){
		remote = remtmp;
		setcall(remote," ");
	}
	if((axp = find_ax25(remote)) != NULL && axp->state != LAPB_DISCONNECTED)
		return NULL;	/* Only one to a customer */
	if(axp == NULL && (axp = cr_ax25(remote)) == NULL)
		return NULL;
	memcpy(axp->remote,remote,AXALEN);
	memcpy(axp->local,local,AXALEN);
	axp->iface = iface;
	axp->window = window;
	axp->r_upcall = r_upcall;
	axp->t_upcall = t_upcall;
	axp->s_upcall = s_upcall;
	axp->user = user;

	switch(mode){
	case AX_SERVER:
		axp->flags.clone = 1;
	case AX_PASSIVE:	/* Note fall-thru */
		axp->state = LAPB_LISTEN;
		return axp;
	case AX_ACTIVE:
		break;
	}    
	switch(axp->state){
	case LAPB_DISCONNECTED:
		est_link(axp);
		lapbstate(axp,LAPB_SETUP);
		break;
	case LAPB_SETUP:
		free_q(&axp->txq);
		break;
	case LAPB_DISCPENDING:	/* Ignore */
		break;
	case LAPB_RECOVERY:
	case LAPB_CONNECTED:
		free_q(&axp->txq);
		est_link(axp);
		lapbstate(axp,LAPB_SETUP);
		break;
	}
	return axp;
}

/* Send data on an AX.25 connection. Caller provides optional PID. If
 * a PID is provided, then operate in stream mode, i.e., a large packet
 * is automatically packetized into a series of paclen-sized data fields.
 *
 * If pid == -1, it is assumed the packet (which may actually be a queue
 * of distinct packets) already has a PID on the front and it is passed
 * through directly even if it is very large.
 */
int
send_ax25(
struct ax25_cb *axp,
struct mbuf **bpp,
int pid
){
	struct mbuf *bp1;
	uint16 offset,len,size;

	if(axp == NULL || bpp == NULL || *bpp == NULL){
		free_p(bpp);
		return -1;
	}
	if(pid != -1){
		offset = 0;
		len = len_p(*bpp);
		/* It is important that all the pushdowns be done before
		 * any part of the original packet is freed.
		 * Otherwise the pushdown might erroneously overwrite
		 * a part of the packet that had been duped and freed.
		 */
		while(len != 0){
			size = min(len,axp->paclen);
			dup_p(&bp1,*bpp,offset,size);
			len -= size;
			offset += size;
			pushdown(&bp1,NULL,1);
			bp1->data[0] = pid;
			enqueue(&axp->txq,&bp1);
		}
		free_p(bpp);
	} else {
		enqueue(&axp->txq,bpp);
	}
	return lapb_output(axp);
}

/* Receive incoming data on an AX.25 connection */
struct mbuf *
recv_ax25(axp,cnt)
struct ax25_cb *axp;
uint16 cnt;
{
	struct mbuf *bp;

	if(axp->rxq == NULL)
		return NULL;

	if(cnt == 0){
		/* This means we want it all */
		bp = axp->rxq;
		axp->rxq = NULL;
	} else {
		bp = ambufw(cnt);
		bp->cnt = pullup(&axp->rxq,bp->data,cnt);
	}
	/* If this has un-busied us, send a RR to reopen the window */
	if(len_p(axp->rxq) < axp->window
	 && (len_p(axp->rxq) + bp->cnt) >= axp->window)
		sendctl(axp,LAPB_RESPONSE,RR);

	return bp;
}

/* Close an AX.25 connection */
int
disc_ax25(axp)
struct ax25_cb *axp;
{
	if(axp == NULL)
		return -1;
	switch(axp->state){
	case LAPB_DISCONNECTED:
		break;		/* Ignored */
	case LAPB_LISTEN:
		del_ax25(axp);
		break;
	case LAPB_DISCPENDING:
		lapbstate(axp,LAPB_DISCONNECTED);
		break;
	case LAPB_CONNECTED:
	case LAPB_RECOVERY:
		free_q(&axp->txq);
		axp->retries = 0;
		sendctl(axp,LAPB_COMMAND,DISC|PF);
		stop_timer(&axp->t3);
		start_timer(&axp->t1);
		lapbstate(axp,LAPB_DISCPENDING);
		break;
	}
	return 0;
}

/* Verify that axp points to a valid ax25 control block */
int
ax25val(axp)
struct ax25_cb *axp;
{
	register struct ax25_cb *axp1;

	if(axp == NULL)
		return 0;	/* Null pointer can't be valid */
	for(axp1 = Ax25_cb;axp1 != NULL; axp1 = axp1->next)
		if(axp1 == axp)
			return 1;
	return 0;
}

/* Force a retransmission */
int
kick_ax25(axp)
struct ax25_cb *axp;
{
	if(!ax25val(axp))
		return -1;
	recover(axp);
	return 0;
}

/* Abruptly terminate an AX.25 connection */
int
reset_ax25(axp)
struct ax25_cb *axp;
{
	void (*upcall)();

	if(axp == NULL)
		return -1;
	upcall = axp->s_upcall;
	lapbstate(axp,LAPB_DISCONNECTED);
	/* Clean up if the standard upcall isn't in use */
	if(upcall != s_ascall)
		del_ax25(axp);
	return 0;
}
