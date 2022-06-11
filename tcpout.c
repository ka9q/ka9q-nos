/* TCP output segment processing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"
#include "ip.h"

/* Send a segment on the specified connection. One gets sent only
 * if there is data to be sent or if "force" is non zero
 */
void
tcp_output(tcb)
register struct tcb *tcb;
{
	struct mbuf *dbp;	/* Header and data buffer pointers */
	struct tcp seg;		/* Local working copy of header */
	uint16 ssize;		/* Size of current segment being sent,
				 * including SYN and FIN flags */
	uint16 dsize;		/* Size of segment less SYN and FIN */
	int32 usable;		/* Usable window */
	int32 sent;		/* Sequence count (incl SYN/FIN) already
				 * in the pipe but not yet acked */
	int32 rto;		/* Retransmit timeout setting */

	if(tcb == NULL)
		return;

	switch(tcb->state){
	case TCP_LISTEN:
	case TCP_CLOSED:
		return;	/* Don't send anything */
	}
	for(;;){
		memset(&seg,0,sizeof(seg));
		/* Compute data already in flight */
		sent = tcb->snd.ptr - tcb->snd.una;

		/* Compute usable send window as minimum of offered
		 * and congestion windows, minus data already in flight.
		 * Be careful that the window hasn't shrunk --
		 * these are unsigned vars.
		 */
		usable = min(tcb->snd.wnd,tcb->cwind);
		if(usable > sent)
			usable -= sent;	/* Most common case */
		else if(usable == 0 && sent == 0)
			usable = 1;	/* Closed window probe */
		else
			usable = 0;	/* Window closed or shrunken */

		/* Compute size of segment we *could* send. This is the
		 * smallest of the usable window, the mss, or the amount
		 * we have on hand. (I don't like optimistic windows)
		 */
		ssize = min(tcb->sndcnt - sent,usable);
		ssize = min(ssize,tcb->mss);

		/* Now we decide if we actually want to send it.
		 * Apply John Nagle's "single outstanding segment" rule.
		 * If data is already in the pipeline, don't send
		 * more unless it is MSS-sized, the very last packet,
		 * or we're being forced to transmit anyway (e.g., to
		 * ack incoming data).
		 */
		if(!tcb->flags.force && sent != 0 && ssize < tcb->mss
		 && !(tcb->state == TCP_FINWAIT1 && ssize == tcb->sndcnt-sent)){
			ssize = 0;
		}
	 	/* Unless the tcp syndata option is on, inhibit data until
		 * our SYN has been acked. This ought to be OK, but some
		 * old TCPs have problems with data piggybacked on SYNs.
		 */
		if(!tcb->flags.synack && !Tcp_syndata){
			if(tcb->snd.ptr == tcb->iss)
				ssize = min(1,ssize);	/* Send only SYN */
			else
				ssize = 0;	/* Don't send anything */
		}
		/* If we're forced to send an ack while retransmitting,
		 * don't send any data. This will let us use the current
		 * sequence number, which may be necessary for the
		 * ack to be accepted by the receiver
		 */
		if(tcb->flags.force && tcb->snd.ptr != tcb->snd.nxt)
			ssize = 0;
		if(ssize == 0 && !tcb->flags.force)
			break;		/* No need to send anything */

		tcb->flags.force = 0;	/* Only one forced segment! */

		seg.source = tcb->conn.local.port;
		seg.dest = tcb->conn.remote.port;

		/* Set the flags according to the state we're in. It is
		 * assumed that if this segment is associated with a state
		 * transition, then the state change will already have been
		 * made. This allows this routine to be called from a
		 * retransmission timeout with force=1.
		 */
		seg.flags.ack = 1; /* Every state except TCP_SYN_SENT */
		seg.flags.congest = tcb->flags.congest;

		if(tcb->state == TCP_SYN_SENT)
			seg.flags.ack = 0; /* Haven't seen anything yet */

		dsize = ssize;
		if(!tcb->flags.synack && tcb->snd.ptr == tcb->iss){
			/* Send SYN */
			seg.flags.syn = 1;
			dsize--;	/* SYN isn't really in snd queue */
			/* Also send MSS, wscale and tstamp (if OK) */
			seg.mss = Tcp_mss;
			seg.flags.mss = 1;
			seg.wsopt = DEF_WSCALE;
			seg.flags.wscale = 1;
			if(Tcp_tstamps){
				seg.flags.tstamp = 1;
				seg.tsval = msclock();
			}
		}
		/* If there's no data, use snd.nxt rather than snd.ptr to
		 * ensure ack acceptance in case we were retransmitting
		 */
		if(ssize == 0)
			seg.seq = tcb->snd.nxt;
		else
			seg.seq = tcb->snd.ptr;
		tcb->last_ack_sent = seg.ack = tcb->rcv.nxt;
		if(seg.flags.syn || !tcb->flags.ws_ok)
			seg.wnd = tcb->rcv.wnd;
		else
			seg.wnd = tcb->rcv.wnd >> tcb->rcv.wind_scale;

		/* Now try to extract some data from the send queue. Since
		 * SYN and FIN occupy sequence space and are reflected in
		 * sndcnt but don't actually sit in the send queue, extract
		 * will return one less than dsize if a FIN needs to be sent.
		 */
		dbp = ambufw(TCP_HDR_PAD+dsize);
		dbp->data += TCP_HDR_PAD;	/* Allow room for other hdrs */
		if(dsize != 0){
			int32 offset;

			/* SYN doesn't actually take up space on the sndq,
			 * so take it out of the sent count
			 */
			offset = sent;
			if(!tcb->flags.synack && sent != 0)
				offset--;

			dbp->cnt = extract(tcb->sndq,(uint16)offset,dbp->data,dsize);
			if(dbp->cnt != dsize){
				/* We ran past the end of the send queue;
				 * send a FIN
				 */
				seg.flags.fin = 1;
				dsize--;
			}
		}
		/* If the entire send queue will now be in the pipe, set the
		 * push flag
		 */
		if(dsize != 0 && sent + ssize == tcb->sndcnt)
			seg.flags.psh = 1;

		/* If this transmission includes previously transmitted data,
		 * snd.nxt will already be past snd.ptr. In this case,
		 * compute the amount of retransmitted data and keep score
		 */
		if(tcb->snd.ptr < tcb->snd.nxt)
			tcb->resent += min(tcb->snd.nxt - tcb->snd.ptr,ssize);

		tcb->snd.ptr += ssize;
		/* If this is the first transmission of a range of sequence
		 * numbers, record it so we'll accept acknowledgments
		 * for it later
		 */
		if(seq_gt(tcb->snd.ptr,tcb->snd.nxt))
			tcb->snd.nxt = tcb->snd.ptr;

		if(tcb->flags.ts_ok && seg.flags.ack){
			seg.flags.tstamp = 1;
			seg.tsval = msclock();
			seg.tsecr = tcb->ts_recent;
		}
		/* Generate TCP header, compute checksum, and link in data */
		htontcp(&seg,&dbp,tcb->conn.local.address,
		 tcb->conn.remote.address);

		/* If we're sending some data or flags, start retransmission
		 * and round trip timers if they aren't already running.
		 */
		if(ssize != 0){
			/* Set round trip timer. */
			rto = backoff(tcb->backoff) * (4 * tcb->mdev + tcb->srtt);
			set_timer(&tcb->timer,max(MIN_RTO,rto));
			if(!run_timer(&tcb->timer))
				start_timer(&tcb->timer);

			/* If round trip timer isn't running, start it */
			if(tcb->flags.ts_ok || !tcb->flags.rtt_run){
				tcb->flags.rtt_run = 1;
				tcb->rtt_time = msclock();
				tcb->rttseq = tcb->snd.ptr;
				tcb->rttack = tcb->snd.una;
			}
		}
		if(tcb->flags.retran)
			tcpRetransSegs++;
		else
			tcpOutSegs++;

		ip_send(tcb->conn.local.address,tcb->conn.remote.address,
		 TCP_PTCL,tcb->tos,0,&dbp,len_p(dbp),0,0);
	}
}
