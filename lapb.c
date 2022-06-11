/* Link Access Procedures Balanced (LAPB), the upper sublayer of
 * AX.25 Level 2.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "ip.h"
#include "netrom.h"

static void handleit(struct ax25_cb *axp,int pid,struct mbuf **bp);
static void procdata(struct ax25_cb *axp,struct mbuf **bp);
static int ackours(struct ax25_cb *axp,uint16 n);
static void clr_ex(struct ax25_cb *axp);
static void enq_resp(struct ax25_cb *axp);
static void inv_rex(struct ax25_cb *axp);

/* Process incoming frames */
int
lapb_input(
struct ax25_cb *axp,		/* Link control structure */
int cmdrsp,			/* Command/response flag */
struct mbuf **bpp		/* Rest of frame, starting with ctl */
){
	int control;
	int class;		/* General class (I/S/U) of frame */
	uint16 type;		/* Specific type (I/RR/RNR/etc) of frame */
	char pf;		/* extracted poll/final bit */
	char poll = 0;
	char final = 0;
	uint16 nr;		/* ACK number of incoming frame */
	uint16 ns;		/* Seq number of incoming frame */
	uint16 tmp;

	if(bpp == NULL || *bpp == NULL || axp == NULL){
		free_p(bpp);
		return -1;
	}

	/* Extract the various parts of the control field for easy use */
	if((control = PULLCHAR(bpp)) == -1){
		free_p(bpp);	/* Probably not necessary */
		return -1;
	}
	type = ftype(control);
	class = type & 0x3;
	pf = control & PF;
	/* Check for polls and finals */
	if(pf){
		switch(cmdrsp){
		case LAPB_COMMAND:
			poll = YES;
			break;
		case LAPB_RESPONSE:
			final = YES;
			break;
		}
	}
	/* Extract sequence numbers, if present */
	switch(class){
	case I:
	case I+2:
		ns = (control >> 1) & MMASK;
	case S:	/* Note fall-thru */
		nr = (control >> 5) & MMASK;
		break;
	}
	/* This section follows the SDL diagrams by K3NA fairly closely */
	switch(axp->state){
	case LAPB_DISCONNECTED:
		switch(type){
		case SABM:	/* Initialize or reset link */
			sendctl(axp,LAPB_RESPONSE,UA|pf);	/* Always accept */
			clr_ex(axp);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED);/* Resets state counters */
			axp->srt = Axirtt;
			axp->mdev = 0;
			set_timer(&axp->t1,2*axp->srt);
			start_timer(&axp->t3);
			break;
		case DM:	/* Ignore to avoid infinite loops */
			break;
		default:	/* All others get DM */
			if(poll)
				sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		}
		break;
	case LAPB_SETUP:
		switch(type){
		case SABM:	/* Simultaneous open */
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			break;
		case DISC:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case UA:	/* Connection accepted */
			/* Note: xmit queue not cleared */
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED);
			break;			
		case DM:	/* Connection refused */
			free_q(&axp->txq);
			stop_timer(&axp->t1);
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			break;
		default:	/* All other frames ignored */
			break;
		}
		break;
	case LAPB_DISCPENDING:
		switch(type){
		case SABM:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case DISC:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			break;
		case UA:
		case DM:
			stop_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCONNECTED);
			break;
		default:	/* Respond with DM only to command polls */
			if(poll)
				sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		}
		break;
	case LAPB_CONNECTED:
		switch(type){
		case SABM:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			clr_ex(axp);
			free_q(&axp->txq);
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
			break;
		case DISC:
			free_q(&axp->txq);
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			stop_timer(&axp->t1);
			stop_timer(&axp->t3);
			axp->reason = LB_NORMAL;
			lapbstate(axp,LAPB_DISCONNECTED);
			break;
		case DM:
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			break;
		case UA:
			est_link(axp);
			lapbstate(axp,LAPB_SETUP);	/* Re-establish */	
			break;			
		case FRMR:
			est_link(axp);
			lapbstate(axp,LAPB_SETUP);	/* Re-establish link */
			break;
		case RR:
		case RNR:
			axp->flags.remotebusy = (control == RNR) ? YES : NO;
			if(poll)
				enq_resp(axp);
			ackours(axp,nr);
			break;
		case REJ:
			axp->flags.remotebusy = NO;
			if(poll)
				enq_resp(axp);
			ackours(axp,nr);
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			/* This may or may not actually invoke transmission,
			 * depending on whether this REJ was caused by
			 * our losing his prior ACK.
			 */
			inv_rex(axp);
			break;	
		case I:
			ackours(axp,nr); /** == -1) */
			if(len_p(axp->rxq) >= axp->window){
				/* Too bad he didn't listen to us; he'll
				 * have to resend the frame later. This
				 * drastic action is necessary to avoid
				 * deadlock.
				 */
				if(poll)
					sendctl(axp,LAPB_RESPONSE,RNR|pf);
				free_p(bpp);
				break;
			}
			/* Reject or ignore I-frames with receive sequence number errors */
			if(ns != axp->vr){
				if(axp->proto == V1 || !axp->flags.rejsent){
					axp->flags.rejsent = YES;
					sendctl(axp,LAPB_RESPONSE,REJ | pf);
				} else if(poll)
					enq_resp(axp);
				axp->response = 0;
				break;
			}
			axp->flags.rejsent = NO;
			axp->vr = (axp->vr+1) & MMASK;
			tmp = len_p(axp->rxq) >= axp->window ? RNR : RR;
			if(poll){
				sendctl(axp,LAPB_RESPONSE,tmp|PF);
			} else {
				axp->response = tmp;
			}
			procdata(axp,bpp);
			break;
		default:	/* All others ignored */
			break;
		}
		break;
	case LAPB_RECOVERY:
		switch(type){
		case SABM:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			clr_ex(axp);
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
			break;
		case DISC:
			free_q(&axp->txq);
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			stop_timer(&axp->t1);
			stop_timer(&axp->t3);
			axp->response = UA;
			axp->reason = LB_NORMAL;
			lapbstate(axp,LAPB_DISCONNECTED);
			break;
		case DM:
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			break;
		case UA:
			est_link(axp);
			lapbstate(axp,LAPB_SETUP);	/* Re-establish */	
			break;
		case FRMR:
			est_link(axp);
			lapbstate(axp,LAPB_SETUP);	/* Re-establish link */
			break;
		case RR:
		case RNR:
			axp->flags.remotebusy = (control == RNR) ? YES : NO;
			if(axp->proto == V1 || final){
				stop_timer(&axp->t1);
				ackours(axp,nr);
				if(axp->unack != 0){
					inv_rex(axp);
				} else {
					start_timer(&axp->t3);
					lapbstate(axp,LAPB_CONNECTED);
				}
			} else {
				if(poll)
					enq_resp(axp);
				ackours(axp,nr);
				/* Keep timer running even if all frames
				 * were acked, since we must see a Final
				 */
				if(!run_timer(&axp->t1))
					start_timer(&axp->t1);
			}
			break;
		case REJ:
			axp->flags.remotebusy = NO;
			/* Don't insist on a Final response from the old proto */
			if(axp->proto == V1 || final){
				stop_timer(&axp->t1);
				ackours(axp,nr);
				if(axp->unack != 0){
					inv_rex(axp);
				} else {
					start_timer(&axp->t3);
					lapbstate(axp,LAPB_CONNECTED);
				}
			} else {
				if(poll)
					enq_resp(axp);
				ackours(axp,nr);
				if(axp->unack != 0){
					/* This is certain to trigger output */
					inv_rex(axp);
				}
				/* A REJ that acks everything but doesn't
				 * have the F bit set can cause a deadlock.
				 * So make sure the timer is running.
				 */
				if(!run_timer(&axp->t1))
					start_timer(&axp->t1);
			}
			break;
		case I:
			ackours(axp,nr); /** == -1) */
			/* Make sure timer is running, since an I frame
			 * cannot satisfy a poll
			 */
			if(!run_timer(&axp->t1))
				start_timer(&axp->t1);
			if(len_p(axp->rxq) >= axp->window){
				/* Too bad he didn't listen to us; he'll
				 * have to resend the frame later. This
				 * drastic action is necessary to avoid
				 * memory deadlock.
				 */
				sendctl(axp,LAPB_RESPONSE,RNR | pf);
				free_p(bpp);
				break;
			}
			/* Reject or ignore I-frames with receive sequence number errors */
			if(ns != axp->vr){
				if(axp->proto == V1 || !axp->flags.rejsent){
					axp->flags.rejsent = YES;
					sendctl(axp,LAPB_RESPONSE,REJ | pf);
				} else if(poll)
					enq_resp(axp);

				axp->response = 0;
				break;
			}
			axp->flags.rejsent = NO;
			axp->vr = (axp->vr+1) & MMASK;
			tmp = len_p(axp->rxq) >= axp->window ? RNR : RR;
			if(poll){
				sendctl(axp,LAPB_RESPONSE,tmp|PF);
			} else {
				axp->response = tmp;
			}
			procdata(axp,bpp);
			break;
		default:
			break;		/* Ignored */
		}
		break;
	}
	free_p(bpp);	/* In case anything's left */

	/* See if we can send some data, perhaps piggybacking an ack.
	 * If successful, lapb_output will clear axp->response.
	 */
	lapb_output(axp);
	if(axp->response != 0){
		sendctl(axp,LAPB_RESPONSE,axp->response);
		axp->response = 0;
	}
	return 0;
}
/* Handle incoming acknowledgements for frames we've sent.
 * Free frames being acknowledged.
 * Return -1 to cause a frame reject if number is bad, 0 otherwise
 */
static int
ackours(
struct ax25_cb *axp,
uint16 n
){	
	struct mbuf *bp;
	int acked = 0;	/* Count of frames acked by this ACK */
	uint16 oldest;	/* Seq number of oldest unacked I-frame */
	int32 rtt,abserr;

	/* Free up acknowledged frames by purging frames from the I-frame
	 * transmit queue. Start at the remote end's last reported V(r)
	 * and keep going until we reach the new sequence number.
	 * If we try to free a null pointer,
	 * then we have a frame reject condition.
	 */
	oldest = (axp->vs - axp->unack) & MMASK;
	while(axp->unack != 0 && oldest != n){
		if((bp = dequeue(&axp->txq)) == NULL){
			/* Acking unsent frame */
			return -1;
		}
		free_p(&bp);
		axp->unack--;
		acked++;
		if(axp->flags.rtt_run && axp->rtt_seq == oldest){
			/* A frame being timed has been acked */
			axp->flags.rtt_run = 0;
			/* Update only if frame wasn't retransmitted */
			if(!axp->flags.retrans){
				rtt = msclock() - axp->rtt_time;
				abserr = (rtt > axp->srt) ? rtt - axp->srt :
				 axp->srt - rtt;

				/* Run SRT and mdev integrators */
				axp->srt = ((axp->srt * 7) + rtt + 4) >> 3;
				axp->mdev = ((axp->mdev*3) + abserr + 2) >> 2;
				/* Update timeout */
				set_timer(&axp->t1,4*axp->mdev+axp->srt);
			}
		}
		axp->flags.retrans = 0;
		axp->retries = 0;
		oldest = (oldest + 1) & MMASK;
	}
	if(axp->unack == 0){
		/* All frames acked, stop timeout */
		stop_timer(&axp->t1);
		start_timer(&axp->t3);
	} else if(acked != 0) { 
		/* Partial ACK; restart timer */
		start_timer(&axp->t1);
	}
	if(acked != 0){
		/* If user has set a transmit upcall, indicate how many frames
		 * may be queued
		 */
		if(axp->t_upcall != NULL)
			(*axp->t_upcall)(axp,axp->paclen * (axp->maxframe - axp->unack));
	}
	return 0;
}

/* Establish data link */
void
est_link(axp)
struct ax25_cb *axp;
{
	clr_ex(axp);
	axp->retries = 0;
	sendctl(axp,LAPB_COMMAND,SABM|PF);
	stop_timer(&axp->t3);
	start_timer(&axp->t1);
}
/* Clear exception conditions */
static void
clr_ex(axp)
struct ax25_cb *axp;
{
	axp->flags.remotebusy = NO;
	axp->flags.rejsent = NO;
	axp->response = 0;
	stop_timer(&axp->t3);
}
/* Enquiry response */
static void
enq_resp(axp)
struct ax25_cb *axp;
{
	char ctl;

	ctl = len_p(axp->rxq) >= axp->window ? RNR|PF : RR|PF;	
	sendctl(axp,LAPB_RESPONSE,ctl);
	axp->response = 0;
	stop_timer(&axp->t3);
}
/* Invoke retransmission */
static void
inv_rex(axp)
struct ax25_cb *axp;
{
	axp->vs -= axp->unack;
	axp->vs &= MMASK;
	axp->unack = 0;
}
/* Send S or U frame to currently connected station */
int
sendctl(axp,cmdrsp,cmd)
struct ax25_cb *axp;
int cmdrsp;
int cmd;
{
	if((ftype((char)cmd) & 0x3) == S)	/* Insert V(R) if S frame */
		cmd |= (axp->vr << 5);
	return sendframe(axp,cmdrsp,cmd,NULL);
}
/* Start data transmission on link, if possible
 * Return number of frames sent
 */
int
lapb_output(axp)
register struct ax25_cb *axp;
{
	register struct mbuf *bp;
	struct mbuf *tbp;
	char control;
	int sent = 0;
	int i;

	if(axp == NULL
	 || (axp->state != LAPB_RECOVERY && axp->state != LAPB_CONNECTED)
	 || axp->flags.remotebusy)
		return 0;

	/* Dig into the send queue for the first unsent frame */
	bp = axp->txq;
	for(i = 0; i < axp->unack; i++){
		if(bp == NULL)
			break;	/* Nothing to do */
		bp = bp->anext;
	}
	/* Start at first unsent I-frame, stop when either the
	 * number of unacknowledged frames reaches the maxframe limit,
	 * or when there are no more frames to send
	 */
	while(bp != NULL && axp->unack < axp->maxframe){
		control = I | (axp->vs++ << 1) | (axp->vr << 5);
		axp->vs &= MMASK;
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp == NULL)
			return sent;	/* Probably out of memory */
		sendframe(axp,LAPB_COMMAND,control,&tbp);
		axp->unack++;
		/* We're implicitly acking any data he's sent, so stop any
		 * delayed ack
		 */
		axp->response = 0;
		if(!run_timer(&axp->t1)){
			stop_timer(&axp->t3);
			start_timer(&axp->t1);
		}
		sent++;
		bp = bp->anext;
		if(!axp->flags.rtt_run){
			/* Start round trip timer */
			axp->rtt_seq = (control >> 1) & MMASK;
			axp->rtt_time = msclock();
			axp->flags.rtt_run = 1;
		}
	}
	return sent;
}
/* General purpose AX.25 frame output */
int
sendframe(
struct ax25_cb *axp,
int cmdrsp,
int ctl,
struct mbuf **data
){
	return axsend(axp->iface,axp->remote,axp->local,cmdrsp,ctl,data);
}
/* Set new link state */
void
lapbstate(
struct ax25_cb *axp,
int s
){
	int oldstate;

	oldstate = axp->state;
	axp->state = s;
	if(s == LAPB_DISCONNECTED){
		stop_timer(&axp->t1);
		stop_timer(&axp->t3);
		free_q(&axp->txq);
	}
	/* Don't bother the client unless the state is really changing */
	if(oldstate != s && axp->s_upcall != NULL)
		(*axp->s_upcall)(axp,oldstate,s);
}
/* Process a valid incoming I frame */
static void
procdata(
struct ax25_cb *axp,
struct mbuf **bpp
){
	int pid;
	int seq;

	/* Extract level 3 PID */
	if((pid = PULLCHAR(bpp)) == -1)
		return;	/* No PID */

	if(axp->segremain != 0){
		/* Reassembly in progress; continue */
		seq = PULLCHAR(bpp);
		if(pid == PID_SEGMENT
		 && (seq & SEG_REM) == axp->segremain - 1){
			/* Correct, in-order segment */
			append(&axp->rxasm,bpp);
			if((axp->segremain = (seq & SEG_REM)) == 0){
				/* Done; kick it upstairs */
				*bpp = axp->rxasm;
				axp->rxasm = NULL;
				pid = PULLCHAR(bpp);
				handleit(axp,pid,bpp);
			}
		} else {
			/* Error! */
			free_p(&axp->rxasm);
			axp->rxasm = NULL;
			axp->segremain = 0;
			free_p(bpp);
		}
	} else {
		/* No reassembly in progress */
		if(pid == PID_SEGMENT){
			/* Start reassembly */
			seq = PULLCHAR(bpp);
			if(!(seq & SEG_FIRST)){
				free_p(bpp);	/* not first seg - error! */
			} else {
				/* Put first segment on list */
				axp->segremain = seq & SEG_REM;
				axp->rxasm = (*bpp);
				*bpp = NULL;
			}
		} else {
			/* Normal frame; send upstairs */
			handleit(axp,pid,bpp);
		}
	}
}
/* New-style frame segmenter. Returns queue of segmented fragments, or
 * original packet if small enough
 */
struct mbuf *
segmenter(
struct mbuf **bpp,	/* Complete packet */
uint16 ssize		/* Max size of frame segments */
){
	struct mbuf *result = NULL;
	struct mbuf *bptmp;
	uint16 len,offset;
	int segments;

	/* See if packet is too small to segment. Note 1-byte grace factor
	 * so the PID will not cause segmentation of a 256-byte IP datagram.
	 */
	len = len_p(*bpp);
	if(len <= ssize+1){
		result = *bpp;
		*bpp = NULL;
		return result;	/* Too small to segment */
	}
	ssize -= 2;		/* ssize now equal to data portion size */
	segments = 1 + (len - 1) / ssize;	/* # segments  */
	offset = 0;

	while(segments != 0){
		offset += dup_p(&bptmp,*bpp,offset,ssize);
		if(bptmp == NULL){
			free_q(&result);
			break;
		}
		/* Make room for segmentation header */
		pushdown(&bptmp,NULL,2);
		bptmp->data[0] = PID_SEGMENT;
		bptmp->data[1] = --segments;
		if(offset == ssize)
			bptmp->data[1] |= SEG_FIRST;
		enqueue(&result,&bptmp);
	}
	free_p(bpp);
	return result;
}

static void
handleit(
struct ax25_cb *axp,
int pid,
struct mbuf **bpp
){
	struct axlink *ipp;

	for(ipp = Axlink;ipp->funct != NULL;ipp++){
		if(ipp->pid == pid)
			break;
	}
	if(ipp->funct != NULL)
		(*ipp->funct)(axp->iface,axp,NULL,NULL,bpp,0);
	else
		free_p(bpp);
}
/* Handle ordinary incoming data (no network protocol) */
void
axnl3(
struct iface *iface,
struct ax25_cb *axp,
uint8 *src,
uint8 *dest,
struct mbuf **bpp,
int mcast
){
	if(axp == NULL){
		beac_input(iface,src,bpp);
	} else {
		append(&axp->rxq,bpp);
		if(axp->r_upcall != NULL)
			(*axp->r_upcall)(axp,len_p(axp->rxq));
	}
}

