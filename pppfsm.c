/*
 *  PPPFSM.C	-- PPP Finite State Machine
 *
 *	This implementation of PPP is declared to be in the public domain.
 *
 *	Jan 91	Bill_Simpson@um.cc.umich.edu
 *		Computer Systems Consulting Services
 *
 *	Acknowledgements and correction history may be found in PPP.C
 */

#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ppp.h"
#include "pppfsm.h"
#include "ppplcp.h"
#include "trace.h"


char *fsmStates[] = {
	"Closed",
	"Listen",
	"Req Sent",
	"Ack Rcvd",
	"Ack Sent",
	"Opened",
	"TermSent"
};

char *fsmCodes[] = {
	NULL,
	"Config Req",
	"Config Ack",
	"Config Nak",
	"Config Reject",
	"Termin Req",
	"Termin Ack",
	"Code Reject",
	"Protocol Reject",
	"Echo Request",
	"Echo Reply",
	"Discard Request",
};

static int fsm_sendtermreq(struct fsm_s *fsm_p);
static int fsm_sendtermack(struct fsm_s *fsm_p, byte_t id);

static void fsm_timeout(void *vp);

static void fsm_reset(struct fsm_s *fsm_p);
static void fsm_opening(struct fsm_s *fsm_p);


/************************************************************************/
/* Convert header in host form to network form */
void
htoncnf(
struct config_hdr *cnf,
struct mbuf **bpp
){
	register uint8 *cp;

	/* Prepend bytes for LCP/IPCP header */
	pushdown(bpp, NULL,CONFIG_HDR_LEN);

	/* Load header with proper values */
	cp = (*bpp)->data;
	*cp++ = cnf->code;
	*cp++ = cnf->id;
	put16(cp, cnf->len);
}

/* Extract header from incoming packet */
int
ntohcnf(cnf, bpp)
struct config_hdr *cnf;
struct mbuf **bpp;
{
	uint8 cnfb[CONFIG_HDR_LEN];

	if ( cnf == NULL )
		return -1;

	if ( pullup( bpp, cnfb, CONFIG_HDR_LEN ) < CONFIG_HDR_LEN )
		return -1;

        cnf->code = cnfb[0];
        cnf->id = cnfb[1];
	cnf->len = get16(&cnfb[2]);
	return 0;
}

/***************************************/
/* Extract configuration option header */
int
ntohopt(opt,bpp)
struct option_hdr *opt;
struct mbuf **bpp;
{
	uint8 optb[OPTION_HDR_LEN];

	if ( opt == NULL )
		return -1;

	if ( pullup( bpp, optb, OPTION_HDR_LEN ) < OPTION_HDR_LEN )
		return -1;

	opt->type = optb[0];
	opt->len = optb[1];
	return 0;
}


/************************************************************************/
void
fsm_no_action(fsm_p)
struct fsm_s *fsm_p;
{
	PPP_DEBUG_ROUTINES("fsm_no_action()");
}
int
fsm_no_check(
struct fsm_s *fsm_p,
struct config_hdr *hdr,
struct mbuf **bpp
){
	PPP_DEBUG_ROUTINES("fsm_no_check()");
	return 0;
}


/************************************************************************/
/* General log routine */
void
fsm_log(fsm_p, comment)
struct fsm_s *fsm_p;
char *comment;
{
	if (PPPtrace > 1)
		trace_log(PPPiface,"%s PPP/%s %-8s; %s",
			fsm_p->ppp_p->iface->name,
			fsm_p->pdc->name,
			fsmStates[fsm_p->state],
			comment);
}


/************************************************************************/
/* Set a timer in case an expected event does not occur */
void
fsm_timer(fsm_p)
struct fsm_s *fsm_p;
{
	PPP_DEBUG_ROUTINES("fsm_timer()");

	start_timer( &(fsm_p->timer) );
}


/************************************************************************/
/* Send a packet to the remote host */
int
fsm_send(
struct fsm_s *fsm_p,
byte_t code,
byte_t id,
struct mbuf **data
){
	struct ppp_s *ppp_p = fsm_p->ppp_p;
	struct iface *iface = ppp_p->iface;
	struct config_hdr hdr;
	struct mbuf *bp;

	switch( hdr.code = code ) {
	case CONFIG_REQ:
	case TERM_REQ:
	case ECHO_REQ:
		/* Save ID field for match against replies from remote host */
		fsm_p->lastid = ppp_p->id;
		/* fallthru */
	case PROT_REJ:
	case DISCARD_REQ:
		/* Use a unique ID field value */
		hdr.id = ppp_p->id++;
		break;

	case CONFIG_ACK:
	case CONFIG_NAK:
	case CONFIG_REJ:
	case TERM_ACK:
	case CODE_REJ:
	case ECHO_REPLY:
		/* Use ID sent by remote host */
		hdr.id = id;
		break;

	default:
		/* we're in trouble */
		trace_log(PPPiface, "%s PPP/%s %-8s;"
			" Send with bogus code: %d",
			iface->name,
			fsm_p->pdc->name,
			fsmStates[fsm_p->state],
			code);
		return -1;
	};

	switch( code ) {
	case ECHO_REQ:
	case ECHO_REPLY:
	case DISCARD_REQ:
	{
		struct lcp_s *lcp_p = fsm_p->pdv;
		bp = ambufw(4);
		put32(bp->data, lcp_p->local.work.magic_number);
		*data = bp;
	}
	};

	hdr.len = len_p(*data) + CONFIG_HDR_LEN;

	/* Prepend header to packet data */
	htoncnf(&hdr,data);

	if (PPPtrace > 1) {
		trace_log(PPPiface, "%s PPP/%s %-8s;"
			" Sending %s, id: %d, len: %d",
			iface->name,
			fsm_p->pdc->name,
			fsmStates[fsm_p->state],
			fsmCodes[code],
			hdr.id,hdr.len);
	}

	ppp_p->OutNCP[fsm_p->pdc->fsmi]++;

	return( (*iface->output)
		(iface, NULL, NULL, fsm_p->pdc->protocol, data) );
}


/************************************************************************/
/* Send a configuration request */
int
fsm_sendreq(fsm_p)
struct fsm_s *fsm_p;
{
	struct mbuf *bp;

	PPP_DEBUG_ROUTINES("fsm_sendreq()");

	if ( fsm_p->retry <= 0 )
		return -1;

	fsm_p->retry--;
	fsm_timer(fsm_p);

	bp = (*fsm_p->pdc->makereq)(fsm_p);
	return( fsm_send(fsm_p, CONFIG_REQ, 0, &bp) );
}


/************************************************************************/
/* Send a termination request */
static int
fsm_sendtermreq(fsm_p)
struct fsm_s *fsm_p;
{
	PPP_DEBUG_ROUTINES("fsm_sendtermreq()");

	if ( fsm_p->retry <= 0 )
		return -1;

	fsm_p->retry--;
	fsm_timer(fsm_p);
	return( fsm_send(fsm_p, TERM_REQ, 0, NULL) );
}


/************************************************************************/
/* Send Terminate Ack */
static int
fsm_sendtermack(fsm_p,id)
struct fsm_s *fsm_p;
byte_t id;
{
	PPP_DEBUG_ROUTINES("fsm_sendtermack()");

	return( fsm_send(fsm_p, TERM_ACK, id, NULL) );
}


/************************************************************************/
/* Reset state machine */
static void
fsm_reset(fsm_p)
struct fsm_s *fsm_p;
{
	PPP_DEBUG_ROUTINES("fsm_reset()");

	fsm_p->state = (fsm_p->flags & (FSM_ACTIVE | FSM_PASSIVE))
			? fsmLISTEN : fsmCLOSED;
	fsm_p->retry = fsm_p->try_req;
	fsm_p->retry_nak = fsm_p->try_nak;

	(*fsm_p->pdc->reset)(fsm_p);
}


/************************************************************************/
/* Configuration negotiation complete */
static void
fsm_opening(fsm_p)
struct fsm_s *fsm_p;
{
	fsm_log(fsm_p, "Opened");

	stop_timer(&(fsm_p->timer));

	(*fsm_p->pdc->opening)(fsm_p);
	fsm_p->state = fsmOPENED;
}


/************************************************************************/
/*			E V E N T   P R O C E S S I N G			*/
/************************************************************************/

/* Process incoming packet */
void
fsm_proc(
struct fsm_s *fsm_p,
struct mbuf **bpp
){
	struct config_hdr hdr;

	PPPtrace = fsm_p->ppp_p->trace;
	PPPiface = fsm_p->ppp_p->iface;

	if ( ntohcnf(&hdr, bpp) == -1 )
		fsm_log( fsm_p, "short configuration packet" );

	if (PPPtrace > 1)
		trace_log(PPPiface, "%s PPP/%s %-8s;"
			" Processing %s, id: %d, len: %d",
			fsm_p->ppp_p->iface->name,
			fsm_p->pdc->name,
			fsmStates[fsm_p->state],
			fsmCodes[hdr.code],
			hdr.id,	hdr.len);

	hdr.len -= CONFIG_HDR_LEN;		/* Length includes envelope */
	trim_mbuf(bpp, hdr.len);		/* Trim off padding */

	switch(hdr.code) {
	case CONFIG_REQ:
		switch(fsm_p->state) {
		case fsmOPENED:		/* Unexpected event? */
			(*fsm_p->pdc->closing)(fsm_p);
			fsm_reset(fsm_p);
			/* fallthru */
		case fsmLISTEN:
			(*fsm_p->pdc->starting)(fsm_p);
			fsm_sendreq(fsm_p);
			/* fallthru */
		case fsmREQ_Sent:
		case fsmACK_Sent:	/* Unexpected event? */
			fsm_p->state =
			((*fsm_p->pdc->request)(fsm_p, &hdr, bpp) == 0)
				? fsmACK_Sent : fsmREQ_Sent;
			break;

		case fsmACK_Rcvd:
			if ((*fsm_p->pdc->request)(fsm_p, &hdr, bpp) == 0) {
				fsm_opening(fsm_p);
			} else {
				/* give peer time to respond */
				fsm_timer(fsm_p);
			}
			break;

		case fsmCLOSED:
			/* Don't accept any connections */
			fsm_sendtermack(fsm_p, hdr.id);
			/* fallthru */
		case fsmTERM_Sent:
			/* We are attempting to close connection; */
			/* wait for timeout to resend a Terminate Request */
			free_p(bpp);
			break;
		};
		break;

	case CONFIG_ACK:
		switch(fsm_p->state) {
		case fsmREQ_Sent:
			if ((*fsm_p->pdc->ack)(fsm_p, &hdr, bpp) == 0) {
				fsm_p->state = fsmACK_Rcvd;
			}
			break;

		case fsmACK_Sent:
			if ((*fsm_p->pdc->ack)(fsm_p, &hdr, bpp) == 0) {
				fsm_opening(fsm_p);
			}
			break;

		case fsmOPENED:		/* Unexpected event? */
			(*fsm_p->pdc->closing)(fsm_p);
			(*fsm_p->pdc->starting)(fsm_p);
			fsm_reset(fsm_p);
			/* fallthru */
		case fsmACK_Rcvd:	/* Unexpected event? */
			free_p(bpp);
			fsm_sendreq(fsm_p);
			fsm_p->state = fsmREQ_Sent;
			break;

		case fsmCLOSED:
		case fsmLISTEN:
			/* Out of Sync; kill the remote */
			fsm_sendtermack(fsm_p, hdr.id);
			/* fallthru */
		case fsmTERM_Sent:
			/* We are attempting to close connection; */
			/* wait for timeout to resend a Terminate Request */
			free_p(bpp);
			break;
		};
		break;

	case CONFIG_NAK:
		switch(fsm_p->state) {
		case fsmREQ_Sent:
		case fsmACK_Sent:
			/* Update our config request to reflect NAKed options */
			if ((*fsm_p->pdc->nak)(fsm_p, &hdr, bpp) == 0) {
				/* Send updated config request */
				fsm_sendreq(fsm_p);
			}
			break;

		case fsmOPENED:		/* Unexpected event? */
			(*fsm_p->pdc->closing)(fsm_p);
			(*fsm_p->pdc->starting)(fsm_p);
			fsm_reset(fsm_p);
			/* fallthru */
		case fsmACK_Rcvd:	/* Unexpected event? */
			free_p(bpp);
			fsm_sendreq(fsm_p);
			fsm_p->state = fsmREQ_Sent;
			break;

		case fsmCLOSED:
		case fsmLISTEN:
			/* Out of Sync; kill the remote */
			fsm_sendtermack(fsm_p, hdr.id);
			/* fallthru */
		case fsmTERM_Sent:
			/* We are attempting to close connection; */
			/* wait for timeout to resend a Terminate Request */
			free_p(bpp);
			break;
		};
		break;

	case CONFIG_REJ:
		switch(fsm_p->state) {
		case fsmREQ_Sent:
		case fsmACK_Sent:
			if((*fsm_p->pdc->reject)(fsm_p, &hdr, bpp) == 0) {
				fsm_sendreq(fsm_p);
			}
			break;

		case fsmOPENED:		/* Unexpected event? */
			(*fsm_p->pdc->closing)(fsm_p);
			(*fsm_p->pdc->starting)(fsm_p);
			fsm_reset(fsm_p);
			/* fallthru */
		case fsmACK_Rcvd:	/* Unexpected event? */
			free_p(bpp);
			fsm_sendreq(fsm_p);
			fsm_p->state = fsmREQ_Sent;
			break;

		case fsmCLOSED:
		case fsmLISTEN:
			/* Out of Sync; kill the remote */
			fsm_sendtermack(fsm_p, hdr.id);
			/* fallthru */
		case fsmTERM_Sent:
			/* We are attempting to close connection; */
			/* wait for timeout to resend a Terminate Request */
			free_p(bpp);
			break;
		};
		break;

	case TERM_REQ:
		fsm_log(fsm_p, "Peer requested Termination");

		switch(fsm_p->state) {
		case fsmOPENED:
			fsm_sendtermack(fsm_p, hdr.id);
			(*fsm_p->pdc->closing)(fsm_p);
			(*fsm_p->pdc->stopping)(fsm_p);
			fsm_reset(fsm_p);
			break;

		case fsmACK_Rcvd:
		case fsmACK_Sent:
			fsm_p->state = fsmREQ_Sent;
			/* fallthru */
		case fsmREQ_Sent:
		case fsmTERM_Sent:
			/* waiting for timeout */
			/* fallthru */
		case fsmCLOSED:
		case fsmLISTEN:
			/* Unexpected, but make them happy */
			fsm_sendtermack(fsm_p, hdr.id);
			break;
		};
		break;

	case TERM_ACK:
		switch(fsm_p->state) {
		case fsmTERM_Sent:
			stop_timer(&(fsm_p->timer));

			fsm_log(fsm_p, "Terminated");
			(*fsm_p->pdc->stopping)(fsm_p);
			fsm_reset(fsm_p);
			break;

		case fsmOPENED:
			/* Remote host has abruptly closed connection */
			fsm_log(fsm_p, "Terminated unexpectly");
			(*fsm_p->pdc->closing)(fsm_p);
			fsm_reset(fsm_p);
			if ( fsm_sendreq(fsm_p) == 0 ) {
				fsm_p->state = fsmREQ_Sent;
			}
			break;

		case fsmACK_Sent:
		case fsmACK_Rcvd:
			fsm_p->state = fsmREQ_Sent;
			/* fallthru */
		case fsmREQ_Sent:
			/* waiting for timeout */
			/* fallthru */
		case fsmCLOSED:
		case fsmLISTEN:
			/* Unexpected, but no action needed */
			break;
		};
		break;

	case CODE_REJ:
		trace_log(PPPiface,"%s PPP/%s Code Reject;"
			" indicates faulty implementation",
			fsm_p->ppp_p->iface->name,
			fsm_p->pdc->name);
		(*fsm_p->pdc->stopping)(fsm_p);
		fsm_reset(fsm_p);
		free_p(bpp);
		break;

	case PROT_REJ:
		trace_log(PPPiface,"%s PPP/%s Protocol Reject;"
			" please do not use this protocol",
			fsm_p->ppp_p->iface->name,
			fsm_p->pdc->name);
		free_p(bpp);
		break;

	case ECHO_REQ:
		switch(fsm_p->state) {
		case fsmOPENED:
			fsm_send( fsm_p, ECHO_REPLY, hdr.id, bpp );
			break;

		case fsmCLOSED:
		case fsmLISTEN:
			/* Out of Sync; kill the remote */
			fsm_sendtermack(fsm_p, hdr.id);
			/* fallthru */
		case fsmREQ_Sent:
		case fsmACK_Rcvd:
		case fsmACK_Sent:
		case fsmTERM_Sent:
			/* ignore */
			free_p(bpp);
			break;
		};
		break;

	case ECHO_REPLY:
	case DISCARD_REQ:
	case QUALITY_REPORT:
		free_p(bpp);
		break;

	default:
		trace_log(PPPiface,"%s PPP/%s Unknown packet type: %d;"
			" Sending Code Reject",
			fsm_p->ppp_p->iface->name,
			fsm_p->pdc->name,
			hdr.code);

		hdr.len += CONFIG_HDR_LEN;	/* restore length */
		htoncnf( &hdr, bpp );	/* put header back on */
		fsm_send( fsm_p, CODE_REJ, hdr.id, bpp );

		switch(fsm_p->state) {
		case fsmREQ_Sent:
		case fsmACK_Rcvd:
		case fsmACK_Sent:
		case fsmOPENED:
			fsm_p->state = fsmLISTEN;
			break;

		case fsmCLOSED:
		case fsmLISTEN:
		case fsmTERM_Sent:
			/* no change */
			break;
		};
		break;
	}
}


/************************************************************************/
/* Timeout while waiting for reply from remote host */
static void
fsm_timeout(vp)
void *vp;
{
	struct fsm_s *fsm_p = (struct fsm_s *)vp;

	PPPtrace = fsm_p->ppp_p->trace;
	PPPiface = fsm_p->ppp_p->iface;

	fsm_log( fsm_p, "Timeout" );

	switch(fsm_p->state) {
	case fsmREQ_Sent:
	case fsmACK_Rcvd:
	case fsmACK_Sent:
		if (fsm_p->retry > 0) {
			fsm_sendreq(fsm_p);
			fsm_p->state = fsmREQ_Sent;
		} else {
			fsm_log(fsm_p, "Request retry exceeded");
			fsm_reset(fsm_p);
		}
		break;

	case fsmTERM_Sent:
		if (fsm_p->retry > 0) {
			fsm_sendtermreq(fsm_p);
		} else {
			fsm_log(fsm_p, "Terminate retry exceeded");
			(*fsm_p->pdc->stopping)(fsm_p);
			fsm_reset(fsm_p);
		}
		break;

	case fsmCLOSED:
	case fsmLISTEN:
	case fsmOPENED:
		/* nothing to do */
		break;
	}
}


/************************************************************************/
/*			I N I T I A L I Z A T I O N			*/
/************************************************************************/

/* Start FSM (after open event, and physical line up) */
void
fsm_start(fsm_p)
struct fsm_s *fsm_p;
{
	if ( fsm_p->pdv == NULL )
		return;

	PPPtrace = fsm_p->ppp_p->trace;
	PPPiface = fsm_p->ppp_p->iface;

	fsm_log(fsm_p, "Start");

	if ( !(fsm_p->flags & (FSM_ACTIVE | FSM_PASSIVE)) )
		return;

	switch ( fsm_p->state ) {
	case fsmCLOSED:
	case fsmLISTEN:
	case fsmTERM_Sent:
		(*fsm_p->pdc->starting)(fsm_p);
		fsm_reset(fsm_p);

		if ( fsm_p->flags & FSM_ACTIVE ){
			fsm_sendreq(fsm_p);
			fsm_p->state = fsmREQ_Sent;
		}
		break;
	default:
		/* already started */
		break;
	};
}


/************************************************************************/
/* Physical Line Down Event */
void
fsm_down(fsm_p)
struct fsm_s *fsm_p;
{
	if ( fsm_p->pdv == NULL )
		return;

	PPPtrace = fsm_p->ppp_p->trace;
	PPPiface = fsm_p->ppp_p->iface;

	fsm_log(fsm_p, "Down");

	switch ( fsm_p->state ) {
	case fsmREQ_Sent:
	case fsmACK_Rcvd:
	case fsmACK_Sent:
		stop_timer(&(fsm_p->timer));
		fsm_reset(fsm_p);
		break;

	case fsmOPENED:
		(*fsm_p->pdc->closing)(fsm_p);
		/* fallthru */
	case fsmTERM_Sent:
		fsm_reset(fsm_p);
		break;

	case fsmCLOSED:
	case fsmLISTEN:
		/* nothing to do */
		break;
	};
}


/************************************************************************/
/* Close the connection */
void
fsm_close(fsm_p)
struct fsm_s *fsm_p;
{
	if ( fsm_p->pdv == NULL )
		return;

	PPPtrace = fsm_p->ppp_p->trace;
	PPPiface = fsm_p->ppp_p->iface;

	fsm_log(fsm_p, "Close");

	switch ( fsm_p->state ) {
	case fsmOPENED:
		(*fsm_p->pdc->closing)(fsm_p);
		/* fallthru */
	case fsmACK_Sent:
		fsm_p->retry = fsm_p->try_terminate;
		fsm_sendtermreq(fsm_p);
		fsm_p->state = fsmTERM_Sent;
		break;

	case fsmREQ_Sent:
	case fsmACK_Rcvd:
		/* simply wait for REQ timeout to expire */
		fsm_p->retry = 0;
		fsm_p->state = fsmTERM_Sent;
		break;

	case fsmLISTEN:
		fsm_p->state = fsmCLOSED;
		break;

	case fsmTERM_Sent:
	case fsmCLOSED:
		/* nothing to do */
		break;
	};
}


/************************************************************************/
/* Initialize the fsm for this protocol
 * Called from protocol _init
 */
void
fsm_init(fsm_p)
struct fsm_s *fsm_p;
{
	struct timer *t = &(fsm_p->timer);

	PPP_DEBUG_ROUTINES("fsm_init()");

	fsm_p->try_req = fsm_p->pdc->try_req;
	fsm_p->try_nak = fsm_p->pdc->try_nak;
	fsm_p->try_terminate = fsm_p->pdc->try_terminate;
	fsm_reset(fsm_p);

	/* Initialize timer */
	t->func = (void (*)())fsm_timeout;
	t->arg = (void *)fsm_p;
	set_timer(t, fsm_p->pdc->timeout);
	fsm_timer(fsm_p);
	stop_timer(t);
}


void
fsm_free(fsm_p)
struct fsm_s *fsm_p;
{
	if ( fsm_p->pdv != NULL ) {
		(*fsm_p->pdc->free)(fsm_p);

		free( fsm_p->pdv );
		fsm_p->pdv = NULL;
	}
}


