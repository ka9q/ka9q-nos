/* Process incoming TCP segments. Page number references are to ARPA RFC-793,
 * the TCP specification.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"
#include "icmp.h"
#include "iface.h"
#include "ip.h"

static void update(struct tcb *tcb,struct tcp *seg,uint16 length);
static void proc_syn(struct tcb *tcb,uint8 tos,struct tcp *seg);
static void add_reseq(struct tcb *tcb,uint8 tos,struct tcp *seg,
	struct mbuf **bp,uint16 length);
static void get_reseq(struct tcb *tcb,uint8 *tos,struct tcp *seq,
	struct mbuf **bp,uint16 *length);
static int trim(struct tcb *tcb,struct tcp *seg,struct mbuf **bpp,
	uint16 *length);
static int in_window(struct tcb *tcb,int32 seq);

/* This function is called from IP with the IP header in machine byte order,
 * along with a mbuf chain pointing to the TCP header.
 */
void
tcp_input(
struct iface *iface,	/* Incoming interface (ignored) */
struct ip *ip,		/* IP header */
struct mbuf **bpp,	/* Data field, if any */
int rxbroadcast,	/* Incoming broadcast - discard if true */
int32 said		/* Authenticated packet */
){
	struct tcb *ntcb;
	register struct tcb *tcb;	/* TCP Protocol control block */
	struct tcp seg;			/* Local copy of segment header */
	struct connection conn;		/* Local copy of addresses */
	struct pseudo_header ph;	/* Pseudo-header for checksumming */
	int hdrlen;			/* Length of TCP header */
	uint16 length;
	int32 t;

	if(bpp == NULL || *bpp == NULL)
		return;

	tcpInSegs++;
	if(rxbroadcast){
		/* Any TCP packet arriving as a broadcast is
		 * to be completely IGNORED!!
		 */
		free_p(bpp);
		return;
	}
	length = ip->length - IPLEN - ip->optlen;
	ph.source = ip->source;
	ph.dest = ip->dest;
	ph.protocol = ip->protocol;
	ph.length = length;
	if(cksum(&ph,*bpp,length) != 0){
		/* Checksum failed, ignore segment completely */
		tcpInErrs++;
		free_p(bpp);
		return;
	}
	/* Form local copy of TCP header in host byte order */
	if((hdrlen = ntohtcp(&seg,bpp)) < 0){
		/* TCP header is too small */
		free_p(bpp);
		return;
	}
	length -= hdrlen;

	/* Fill in connection structure and find TCB */
	conn.local.address = ip->dest;
	conn.local.port = seg.dest;
	conn.remote.address = ip->source;
	conn.remote.port = seg.source;
	
	if((tcb = lookup_tcb(&conn)) == NULL){
		/* If this segment doesn't carry a SYN, reject it */
		if(!seg.flags.syn){
			free_p(bpp);
			reset(ip,&seg);
			return;
		}
		/* See if there's a TCP_LISTEN on this socket with
		 * unspecified remote address and port
		 */
		conn.remote.address = 0;
		conn.remote.port = 0;
		if((tcb = lookup_tcb(&conn)) == NULL){
			/* Nope, try unspecified local address too */
			conn.local.address = 0;
			if((tcb = lookup_tcb(&conn)) == NULL){
				/* No LISTENs, so reject */
				free_p(bpp);
				reset(ip,&seg);
				return;
			}
		}
		/* We've found an server listen socket, so clone the TCB */
		if(tcb->flags.clone){
			ntcb = (struct tcb *)mallocw(sizeof (struct tcb));
			ASSIGN(*ntcb,*tcb);
			tcb = ntcb;
			tcb->timer.arg = tcb;
			/* Put on list */
			tcb->next = Tcbs;
			Tcbs = tcb;
		}
		/* Put all the socket info into the TCB */
		tcb->conn.local.address = ip->dest;
		tcb->conn.remote.address = ip->source;
		tcb->conn.remote.port = seg.source;
	}
	tcb->flags.congest = ip->flags.congest;
	/* Do unsynchronized-state processing (p. 65-68) */
	switch(tcb->state){
	case TCP_CLOSED:
		free_p(bpp);
		reset(ip,&seg);
		return;
	case TCP_LISTEN:
		if(seg.flags.rst){
			free_p(bpp);
			return;
		}
		if(seg.flags.ack){
			free_p(bpp);
			reset(ip,&seg);
			return;
		}
		if(seg.flags.syn){
			/* (Security check is bypassed) */
			/* page 66 */
			proc_syn(tcb,ip->tos,&seg);
			send_syn(tcb);
			settcpstate(tcb,TCP_SYN_RECEIVED);		
			if(length != 0 || seg.flags.fin) {
				/* Continue processing if there's more */
				break;
			}
			tcp_output(tcb);
		}
		free_p(bpp);	/* Unlikely to get here directly */
		return;
	case TCP_SYN_SENT:
		if(seg.flags.ack){
			if(!seq_within(seg.ack,tcb->iss+1,tcb->snd.nxt)){
				free_p(bpp);
				reset(ip,&seg);
				return;
			}
		}
		if(seg.flags.rst){	/* p 67 */
			if(seg.flags.ack){
				/* The ack must be acceptable since we just checked it.
				 * This is how the remote side refuses connect requests.
				 */
				close_self(tcb,RESET);
			}
			free_p(bpp);
			return;
		}
		/* (Security check skipped here) */
#ifdef	PREC_CHECK	/* Turned off for compatibility with BSD */
		/* Check incoming precedence; it must match if there's an ACK */
		if(seg.flags.ack && PREC(ip->tos) != PREC(tcb->tos)){
			free_p(bpp);
			reset(ip,&seg);
			return;
		}
#endif
		if(seg.flags.syn){
			proc_syn(tcb,ip->tos,&seg);
			if(seg.flags.ack){
				/* Our SYN has been acked, otherwise the ACK
				 * wouldn't have been valid.
				 */
				update(tcb,&seg,length);
				settcpstate(tcb,TCP_ESTABLISHED);
			} else {
				settcpstate(tcb,TCP_SYN_RECEIVED);
			}
			if(length != 0 || seg.flags.fin) {
				break;		/* Continue processing if there's more */
			}
			tcp_output(tcb);
		} else {
			free_p(bpp);	/* Ignore if neither SYN or RST is set */
		}
		return;
	}
	/* We reach this point directly in any synchronized state. Note that
	 * if we fell through from LISTEN or SYN_SENT processing because of a
	 * data-bearing SYN, window trimming and sequence testing "cannot fail".
	 *
	 * Begin by trimming segment to fit receive window.
	 */
	if(trim(tcb,&seg,bpp,&length) == -1){
		/* Segment is unacceptable */
		if(!seg.flags.rst){	/* NEVER answer RSTs */
			/* In SYN_RECEIVED state, answer a retransmitted SYN 
			 * with a retransmitted SYN/ACK.
			 */
			if(tcb->state == TCP_SYN_RECEIVED)
				tcb->snd.ptr = tcb->snd.una;
			tcb->flags.force = 1;
			tcp_output(tcb);
		}
		return;
	}
	/* If segment isn't the next one expected, and there's data
	 * or flags associated with it, put it on the resequencing
	 * queue and remind ourselves to ACK it. Then strip off
	 * the SYN/data/FIN and continue to process the ACK (or RST)
	 */
	if(seg.seq != tcb->rcv.nxt
	 && (length != 0 || seg.flags.syn || seg.flags.fin)){
		add_reseq(tcb,ip->tos,&seg,bpp,length);
		if(seg.flags.ack && !seg.flags.rst)
			tcb->flags.force = 1;
		seg.flags.syn = seg.flags.fin = 0;
		length = 0;
	}
	/* This loop first processes the current segment, and then
	 * repeats if it can process the resequencing queue.
	 */
	for(;;){
		/* We reach this point with an acceptable segment; data and flags
		 * (if any) are in the window, and if there's data, syn or fin, 
		 * the starting sequence number equals rcv.nxt
		 * (p. 70)
		 */	
		if(seg.flags.rst){
			if(tcb->state == TCP_SYN_RECEIVED
			 && !tcb->flags.clone && !tcb->flags.active){
				/* Go back to listen state only if this was
				 * not a cloned or active server TCB
				 */
				settcpstate(tcb,TCP_LISTEN);
			} else {
				close_self(tcb,RESET);
			}
			free_p(bpp);
			return;
		}
		/* (Security check skipped here) p. 71 */
#ifdef	PREC_CHECK
		/* Check for precedence mismatch */
		if(PREC(ip->tos) != PREC(tcb->tos)){
			free_p(bpp);
			reset(ip,&seg);
			return;
		}
#endif
		/* Check for erroneous extra SYN */
		if(seg.flags.syn){
			free_p(bpp);
			reset(ip,&seg);
			return;
		}
		/* Update timestamp field */
		if(seg.flags.tstamp
		 && seq_within(tcb->last_ack_sent,seg.seq,seg.seq+length))
			tcb->ts_recent = seg.tsval;
		/* Check ack field p. 72 */
		if(!seg.flags.ack){
			free_p(bpp);	/* All segments after synchronization must have ACK */
			return;
		}
		/* Process ACK */
		switch(tcb->state){
		case TCP_SYN_RECEIVED:
			if(seq_within(seg.ack,tcb->snd.una+1,tcb->snd.nxt)){
				update(tcb,&seg,length);
				settcpstate(tcb,TCP_ESTABLISHED);
			} else {
				free_p(bpp);
				reset(ip,&seg);
				return;
			}
			break;
		case TCP_ESTABLISHED:
		case TCP_CLOSE_WAIT:
		case TCP_FINWAIT2:
			update(tcb,&seg,length);
			break;
		case TCP_FINWAIT1:	/* p. 73 */
			update(tcb,&seg,length);
			if(tcb->sndcnt == 0){
				/* Our FIN is acknowledged */
				settcpstate(tcb,TCP_FINWAIT2);
			}
			break;
		case TCP_CLOSING:
			update(tcb,&seg,length);
			if(tcb->sndcnt == 0){
				/* Our FIN is acknowledged */
				settcpstate(tcb,TCP_TIME_WAIT);
				set_timer(&tcb->timer,MSL2*1000L);
				start_timer(&tcb->timer);
			}
			break;
		case TCP_LAST_ACK:
			update(tcb,&seg,length);
			if(tcb->sndcnt == 0){
				/* Our FIN is acknowledged, close connection */
				close_self(tcb,NORMAL);
				return;
			}			
			break;
		case TCP_TIME_WAIT:
			start_timer(&tcb->timer);
			break;
		}

		/* (URGent bit processing skipped here) */

		/* Process the segment text, if any, beginning at rcv.nxt (p. 74) */
		if(length != 0){
			switch(tcb->state){
			case TCP_SYN_RECEIVED:
			case TCP_ESTABLISHED:
			case TCP_FINWAIT1:
			case TCP_FINWAIT2:
				/* Place on receive queue */
				t = msclock();
				if(t > tcb->lastrx){
					tcb->rxbw = 1000L*length / (t - tcb->lastrx);
					tcb->lastrx = t;
				}
				append(&tcb->rcvq,bpp);
				tcb->rcvcnt += length;
				tcb->rcv.nxt += length;
				tcb->rcv.wnd -= length;
				tcb->flags.force = 1;
				/* Notify user */
				if(tcb->r_upcall)
					(*tcb->r_upcall)(tcb,tcb->rcvcnt);
				break;
			default:
				/* Ignore segment text */
				free_p(bpp);
				break;
			}
		}
		/* process FIN bit (p 75) */
		if(seg.flags.fin){
			tcb->flags.force = 1;	/* Always respond with an ACK */

			switch(tcb->state){
			case TCP_SYN_RECEIVED:
			case TCP_ESTABLISHED:
				tcb->rcv.nxt++;
				settcpstate(tcb,TCP_CLOSE_WAIT);
				break;
			case TCP_FINWAIT1:
				tcb->rcv.nxt++;
				if(tcb->sndcnt == 0){
					/* Our FIN has been acked; bypass TCP_CLOSING state */
					settcpstate(tcb,TCP_TIME_WAIT);
					set_timer(&tcb->timer,MSL2*1000L);
					start_timer(&tcb->timer);
				} else {
					settcpstate(tcb,TCP_CLOSING);
				}
				break;
			case TCP_FINWAIT2:
				tcb->rcv.nxt++;
				settcpstate(tcb,TCP_TIME_WAIT);
				set_timer(&tcb->timer,MSL2*1000L);
				start_timer(&tcb->timer);
				break;
			case TCP_CLOSE_WAIT:
			case TCP_CLOSING:
			case TCP_LAST_ACK:
				break;		/* Ignore */
			case TCP_TIME_WAIT:	/* p 76 */
				start_timer(&tcb->timer);
				break;
			}
			/* Call the client again so he can see EOF */
			if(tcb->r_upcall)
				(*tcb->r_upcall)(tcb,tcb->rcvcnt);
		}
		/* Scan the resequencing queue, looking for a segment we can handle,
		 * and freeing all those that are now obsolete.
		 */
		while(tcb->reseq != NULL && seq_ge(tcb->rcv.nxt,tcb->reseq->seg.seq)){
			get_reseq(tcb,&ip->tos,&seg,bpp,&length);
			if(trim(tcb,&seg,bpp,&length) == 0)
				goto gotone;
			/* Segment is an old one; trim has freed it */
		}
		break;
gotone:	;
	}
	tcp_output(tcb);	/* Send any necessary ack */
}

/* Process an incoming ICMP response */
void
tcp_icmp(
int32 icsource,			/* Sender of ICMP message (not used) */
int32 source,			/* Original IP datagram source (i.e. us) */
int32 dest,			/* Original IP datagram dest (i.e., them) */
uint8 type,			/* ICMP error codes */
uint8 code,
struct mbuf **bpp		/* First 8 bytes of TCP header */
){
	struct tcp seg;
	struct connection conn;
	register struct tcb *tcb;

	/* Extract the socket info from the returned TCP header fragment
	 * Note that since this is a datagram we sent, the source fields
	 * refer to the local side.
	 */
	ntohtcp(&seg,bpp);
	conn.local.port = seg.source;
	conn.remote.port = seg.dest;
	conn.local.address = source;
	conn.remote.address = dest;
	if((tcb = lookup_tcb(&conn)) == NULL)
		return;	/* Unknown connection, ignore */

	/* Verify that the sequence number in the returned segment corresponds
	 * to something currently unacknowledged. If not, it can safely
	 * be ignored.
	 */
	if(!seq_within(seg.seq,tcb->snd.una,tcb->snd.nxt))
		return;

	/* Destination Unreachable and Time Exceeded messages never kill a
	 * connection; the info is merely saved for future reference.
	 */
	switch(type){
	case ICMP_DEST_UNREACH:
	case ICMP_TIME_EXCEED:
		tcb->type = type;
		tcb->code = code;
		tcb->unreach++;
		break;
	case ICMP_QUENCH:
		/* Source quench; reduce slowstart threshold to half
		 * current window and restart slowstart
		 */
		tcb->ssthresh = tcb->cwind / 2;
		tcb->ssthresh = max(tcb->ssthresh,tcb->mss);
		/* Shrink congestion window to 1 packet */
		tcb->cwind = tcb->mss;
		tcb->quench++;
		break;
	}
}
/* Send an acceptable reset (RST) response for this segment
 * The RST reply is composed in place on the input segment
 */
void
reset(ip,seg)
struct ip *ip;			/* Offending IP header */
register struct tcp *seg;	/* Offending TCP header */
{
	struct mbuf *hbp;
	uint16 tmp;

	if(seg->flags.rst)
		return;	/* Never send an RST in response to an RST */

	/* Swap port numbers */
	tmp = seg->source;
	seg->source = seg->dest;
	seg->dest = tmp;

	if(seg->flags.ack){
		/* This reset is being sent to clear a half-open connection.
		 * Set the sequence number of the RST to the incoming ACK
		 * so it will be acceptable.
		 */
		seg->flags.ack = 0;
		seg->seq = seg->ack;
		seg->ack = 0;
	} else {
		/* We're rejecting a connect request (SYN) from TCP_LISTEN state
		 * so we have to "acknowledge" their SYN.
		 */
		seg->flags.ack = 1;
		seg->ack = seg->seq;
		seg->seq = 0;
		if(seg->flags.syn)
			seg->ack++;
	}
	/* Set remaining parts of packet */
	seg->flags.urg = 0;
	seg->flags.psh = 0;
	seg->flags.rst = 1;
	seg->flags.syn = 0;
	seg->flags.fin = 0;
	seg->flags.mss = 0;
	seg->flags.wscale = 0;
	seg->flags.tstamp = 0;
	seg->wnd = 0;
	seg->up = 0;
	seg->checksum = 0;	/* force recomputation */

	hbp = ambufw(TCP_HDR_PAD);	/* Prealloc room for headers */
	hbp->data += TCP_HDR_PAD;
	htontcp(seg,&hbp,ip->dest,ip->source);
	/* Ship it out (note swap of addresses) */
	ip_send(ip->dest,ip->source,TCP_PTCL,ip->tos,0,&hbp,len_p(hbp),0,0);
	tcpOutRsts++;
}

/* Process an incoming acknowledgement and window indication.
 * From page 72.
 */
static void
update(tcb,seg,length)
register struct tcb *tcb;
register struct tcp *seg;
uint16 length;
{
	int32 acked;
	int winupd = 0;
	int32 swind;	/* Incoming window, scaled (non-SYN only) */
	long rtt;	/* measured round trip time */
	int32 abserr;	/* abs(rtt - srtt) */

	acked = 0;
	if(seq_gt(seg->ack,tcb->snd.nxt)){
		tcb->flags.force = 1;	/* Acks something not yet sent */
		return;
	}
	/* Decide if we need to do a window update.
	 * This is always checked whenever a legal ACK is received,
	 * even if it doesn't actually acknowledge anything,
	 * because it might be a spontaneous window reopening.
	 */
	if(seq_gt(seg->seq,tcb->snd.wl1) || ((seg->seq == tcb->snd.wl1) 
	 && seq_ge(seg->ack,tcb->snd.wl2))){
		if(seg->flags.syn || !tcb->flags.ws_ok)
			swind = seg->wnd;
		else
			swind = seg->wnd << tcb->snd.wind_scale;
		if(swind > tcb->snd.wnd){
			winupd = 1;	/* Don't count as duplicate ack */

			/* If the window had been closed, crank back the send
			 * pointer so we'll immediately resume transmission.
			 * Otherwise we'd have to wait until the next probe.
			 */
			if(tcb->snd.wnd == 0)
				tcb->snd.ptr = tcb->snd.una;
		}
		/* Remember for next time */
		tcb->snd.wnd = swind;
		tcb->snd.wl1 = seg->seq;
		tcb->snd.wl2 = seg->ack;
	}
	/* See if anything new is being acknowledged */
	if(seq_lt(seg->ack,tcb->snd.una))
		return;	/* Old ack, ignore */

	if(seg->ack == tcb->snd.una){
		/* Ack current, but doesn't ack anything */
		if(tcb->sndcnt == 0 || winupd || length != 0 || seg->flags.syn || seg->flags.fin){
			/* Either we have nothing in the pipe, this segment
			 * was sent to update the window, or it carries
			 * data/syn/fin. In any of these cases we
			 * wouldn't necessarily expect an ACK.
			 */
			return;
		}
		/* Van Jacobson "fast recovery" code */
		if(++tcb->dupacks == TCPDUPACKS){
			/* We've had a burst of do-nothing acks, so
			 * we almost certainly lost a packet.
			 * Resend it now to avoid a timeout. (This is
			 * Van Jacobson's 'quick recovery' algorithm.)
			 */
			int32 ptrsave;

			/* Knock the threshold down just as though
			 * this were a timeout, since we've had
			 * network congestion.
			 */
			tcb->ssthresh = tcb->cwind/2;
			tcb->ssthresh = max(tcb->ssthresh,tcb->mss);

			/* Manipulate the machinery in tcp_output() to
			 * retransmit just the missing packet
			 */
			ptrsave = tcb->snd.ptr;
			tcb->snd.ptr = tcb->snd.una;
			tcb->cwind = tcb->mss;
			tcp_output(tcb);
			tcb->snd.ptr = ptrsave;

			/* "Inflate" the congestion window, pretending as
			 * though the duplicate acks were normally acking
			 * the packets beyond the one that was lost.
			 */
			tcb->cwind = tcb->ssthresh + TCPDUPACKS*tcb->mss;
		} else if(tcb->dupacks > TCPDUPACKS){
			/* Continue to inflate the congestion window
			 * until the acks finally get "unstuck".
			 */
			tcb->cwind += tcb->mss;
		}
		/* Clamp the congestion window at the amount currently
		 * on the send queue, with a minimum of one packet.
		 * This keeps us from increasing the cwind beyond what
		 * we're actually putting in the pipe; otherwise a big
		 * burst of data could overwhelm the net.
		 */
		tcb->cwind = min(tcb->cwind,tcb->sndcnt);
		tcb->cwind = max(tcb->cwind,tcb->mss);
		return;
	}
	/* We're here, so the ACK must have actually acked something */
	if(tcb->dupacks >= TCPDUPACKS && tcb->cwind > tcb->ssthresh){
		/* The acks have finally gotten "unstuck". So now we
		 * can "deflate" the congestion window, i.e. take it
		 * back down to where it would be after slow start
		 * finishes.
		 */
		tcb->cwind = tcb->ssthresh;
	}
	tcb->dupacks = 0;
	acked = seg->ack - tcb->snd.una;

	/* Expand congestion window if not already at limit and if
	 * this packet wasn't retransmitted
	 */
	if(tcb->cwind < tcb->snd.wnd && !tcb->flags.retran){
		if(tcb->cwind < tcb->ssthresh){
			/* Still doing slow start/CUTE, expand by amount acked */
			tcb->cwind += min(acked,tcb->mss);
		} else {
			/* Steady-state test of extra path capacity */
			tcb->cwind += ((long)tcb->mss * tcb->mss) / tcb->cwind;
		}
		/* Don't expand beyond the offered window */
		if(tcb->cwind > tcb->snd.wnd)
			tcb->cwind = tcb->snd.wnd;
	}
	tcb->cwind = min(tcb->cwind,tcb->sndcnt);	/* Clamp */
	tcb->cwind = max(tcb->cwind,tcb->mss);

	/* Round trip time estimation */
	rtt = -1;	/* Init to invalid value */
	if(tcb->flags.ts_ok && seg->flags.tstamp){
		/* Determine RTT from timestamp echo */
		rtt = msclock() - seg->tsecr;
	} else if(tcb->flags.rtt_run && seq_ge(seg->ack,tcb->rttseq)){
		/* use standard round trip timing */
		/* A timed sequence number has been acked */
		tcb->flags.rtt_run = 0;
		if(!(tcb->flags.retran)){
			/* This packet was sent only once and now
			 * it's been acked, so process the round trip time
			 */
			rtt = msclock() - tcb->rtt_time;
		}
	}
	if(rtt >= 0){
		tcb->rtt = rtt;	/* Save for display */

		abserr = (rtt > tcb->srtt) ? rtt - tcb->srtt : tcb->srtt - rtt;
		/* Run SRTT and MDEV integrators, with rounding */
		tcb->srtt = ((AGAIN-1)*tcb->srtt + rtt + (AGAIN/2)) >> LAGAIN;
		tcb->mdev = ((DGAIN-1)*tcb->mdev + abserr + (DGAIN/2)) >> LDGAIN;

		rtt_add(tcb->conn.remote.address,rtt);
		/* Reset the backoff level */
		tcb->backoff = 0;
			
		/* Update our tx throughput estimate */
		if(rtt != 0)	/* Avoid division by zero */
			tcb->txbw = 1000*(seg->ack - tcb->rttack)/rtt;
	}
	tcb->sndcnt -= acked;	/* Update virtual byte count on snd queue */
	tcb->snd.una = seg->ack;

	/* If we're waiting for an ack of our SYN, note it and adjust count */
	if(!(tcb->flags.synack)){
		tcb->flags.synack = 1;
		acked--;	/* One less byte to pull from real snd queue */
	}
	/* Remove acknowledged bytes from the send queue and update the
	 * unacknowledged pointer. If a FIN is being acked,
	 * pullup won't be able to remove it from the queue, but that
	 * causes no harm.
	 */
	pullup(&tcb->sndq,NULL,(uint16)acked);

	/* Stop retransmission timer, but restart it if there is still
	 * unacknowledged data.
	 */	
	stop_timer(&tcb->timer);
	if(tcb->snd.una != tcb->snd.nxt)
		start_timer(&tcb->timer);

	/* If retransmissions have been occurring, make sure the
	 * send pointer doesn't repeat ancient history
	 */
	if(seq_lt(tcb->snd.ptr,tcb->snd.una))
		tcb->snd.ptr = tcb->snd.una;

	/* Clear the retransmission flag since the oldest
	 * unacknowledged segment (the only one that is ever retransmitted)
	 * has now been acked.
	 */
	tcb->flags.retran = 0;

	/* If outgoing data was acked, notify the user so he can send more
	 * unless we've already sent a FIN.
	 */
	if(acked != 0 && tcb->t_upcall
	 && (tcb->state == TCP_ESTABLISHED || tcb->state == TCP_CLOSE_WAIT)){
		(*tcb->t_upcall)(tcb,tcb->window - tcb->sndcnt);
	}
}

/* Determine if the given sequence number is in our receiver window.
 * NB: must not be used when window is closed!
 */
static
int
in_window(tcb,seq)
struct tcb *tcb;
int32 seq;
{
	return seq_within(seq,tcb->rcv.nxt,(int32)(tcb->rcv.nxt+tcb->rcv.wnd-1));
}

/* Process an incoming SYN */
static void
proc_syn(tcb,tos,seg)
register struct tcb *tcb;
uint8 tos;
struct tcp *seg;
{
	uint16 mtu;
	struct tcp_rtt *tp;

	tcb->flags.force = 1;	/* Always send a response */

	/* Note: It's not specified in RFC 793, but SND.WL1 and
	 * SND.WND are initialized here since it's possible for the
	 * window update routine in update() to fail depending on the
	 * IRS if they are left unitialized.
	 */
	/* Check incoming precedence and increase if higher */
	if(PREC(tos) > PREC(tcb->tos))
		tcb->tos = tos;
	tcb->rcv.nxt = seg->seq + 1;	/* p 68 */
	tcb->snd.wl1 = tcb->irs = seg->seq;
	tcb->snd.wnd = seg->wnd;	/* Never scaled in a SYN */
	if(seg->flags.mss)
		tcb->mss = seg->mss;
	if(seg->flags.wscale){
		tcb->snd.wind_scale = seg->wsopt;
		tcb->rcv.wind_scale = DEF_WSCALE;
		tcb->flags.ws_ok = 1;
	}
	if(seg->flags.tstamp && Tcp_tstamps){
		tcb->flags.ts_ok = 1;
		tcb->ts_recent = seg->tsval;
	}
	/* Check the MTU of the interface we'll use to reach this guy
	 * and lower the MSS so that unnecessary fragmentation won't occur
	 */
	if((mtu = ip_mtu(tcb->conn.remote.address)) != 0){
		/* Allow space for the TCP and IP headers */
		if(tcb->flags.ts_ok)
			mtu -= (TSTAMP_LENGTH + TCPLEN + IPLEN + 3) & ~3;
		else
			mtu -= TCPLEN + IPLEN;
		tcb->cwind = tcb->mss = min(mtu,tcb->mss);
	}
	/* See if there's round-trip time experience */
	if((tp = rtt_get(tcb->conn.remote.address)) != NULL){
		tcb->srtt = tp->srtt;
		tcb->mdev = tp->mdev;
	}
}

/* Generate an initial sequence number and put a SYN on the send queue */
void
send_syn(tcb)
register struct tcb *tcb;
{
	tcb->iss = geniss();
	tcb->rttseq = tcb->snd.wl2 = tcb->snd.una = tcb->iss;
	tcb->snd.ptr = tcb->snd.nxt = tcb->rttseq;
	tcb->sndcnt++;
	tcb->flags.force = 1;
}

/* Add an entry to the resequencing queue in the proper place */
static void
add_reseq(
struct tcb *tcb,
uint8 tos,
struct tcp *seg,
struct mbuf **bpp,
uint16 length
){
	register struct reseq *rp,*rp1;

	/* Allocate reassembly descriptor */
	if((rp = (struct reseq *)malloc(sizeof (struct reseq))) == NULL){
		/* No space, toss on floor */
		free_p(bpp);
		return;
	}
	ASSIGN(rp->seg,*seg);
	rp->tos = tos;
	rp->bp = (*bpp);
	*bpp = NULL;
	rp->length = length;

	/* Place on reassembly list sorting by starting seq number */
	rp1 = tcb->reseq;
	if(rp1 == NULL || seq_lt(seg->seq,rp1->seg.seq)){
		/* Either the list is empty, or we're less than all other
		 * entries; insert at beginning.
		 */
		rp->next = rp1;
		tcb->reseq = rp;
	} else {
		/* Find the last entry less than us */
		for(;;){
			if(rp1->next == NULL || seq_lt(seg->seq,rp1->next->seg.seq)){
				/* We belong just after this one */
				rp->next = rp1->next;
				rp1->next = rp;
				break;
			}
			rp1 = rp1->next;
		}
	}
}

/* Fetch the first entry off the resequencing queue */
static void
get_reseq(
register struct tcb *tcb,
uint8 *tos,
struct tcp *seg,
struct mbuf **bp,
uint16 *length
){
	register struct reseq *rp;

	if((rp = tcb->reseq) == NULL)
		return;

	tcb->reseq = rp->next;

	*tos = rp->tos;
	ASSIGN(*seg,rp->seg);
	*bp = rp->bp;
	*length = rp->length;
	free(rp);
}

/* Trim segment to fit window. Return 0 if OK, -1 if segment is
 * unacceptable.
 */
static int
trim(
register struct tcb *tcb,
register struct tcp *seg,
struct mbuf **bpp,
uint16 *length
){
	long dupcnt,excess;
	uint16 len;		/* Segment length including flags */
	char accept = 0;

	len = *length;
	if(seg->flags.syn)
		len++;
	if(seg->flags.fin)
		len++;

	/* Segment acceptability tests */
	if(tcb->rcv.wnd == 0){
		/* If our window is closed, then the other end is
		 * probably probing us. If so, they might send us acks
		 * with seg.seq > rcv.nxt. Be sure to accept these
		 */
		if(len == 0 && seq_within(seg->seq,tcb->rcv.nxt,tcb->rcv.nxt+tcb->window))
			return 0;
		return -1;	/* reject all others */
	}
	if(tcb->rcv.wnd > 0){
		/* Some part of the segment must be in the window */
		if(in_window(tcb,seg->seq)){
			accept++;	/* Beginning is */
		} else if(len != 0){
			if(in_window(tcb,(int32)(seg->seq+len-1)) || /* End is */
			 seq_within(tcb->rcv.nxt,seg->seq,(int32)(seg->seq+len-1))){ /* Straddles */
				accept++;
			}
		}
	}
	if(!accept){
		tcb->rerecv += len;
		free_p(bpp);
		return -1;
	}
	if((dupcnt = tcb->rcv.nxt - seg->seq) > 0){
		tcb->rerecv += dupcnt;
		/* Trim off SYN if present */
		if(seg->flags.syn){
			/* SYN is before first data byte */
			seg->flags.syn = 0;
			seg->seq++;
			dupcnt--;
		}
		if(dupcnt > 0){
			pullup(bpp,NULL,(uint16)dupcnt);
			seg->seq += dupcnt;
			*length -= dupcnt;
		}
	}
	if((excess = seg->seq + *length - (tcb->rcv.nxt + tcb->rcv.wnd)) > 0){
		tcb->rerecv += excess;
		/* Trim right edge */
		*length -= excess;
		trim_mbuf(bpp,*length);
		seg->flags.fin = 0;	/* FIN follows last data byte */
	}
	return 0;
}
