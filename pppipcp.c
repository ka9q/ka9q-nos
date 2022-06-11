/*
 *  PPPIPCP.C	-- negotiate IP parameters
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
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "slhc.h"
#include "ppp.h"
#include "pppfsm.h"
#include "pppipcp.h"
#include "cmdparse.h"
#include "files.h"
#include "trace.h"


/* These defaults are defined in the PPP RFCs, and must not be changed */
static struct ipcp_value_s ipcp_default = {
	FALSE,			/* no need to negotiate defaults */

	0L,			/* no source address */
	0L,			/* no destination address */

	0,			/* no compression protocol */
	0,			/* no slots */
	0			/* no slot compression */
};

/* for test purposes, accept anything we understand */
static uint16 ipcp_negotiate = IPCP_N_ADDRESS | IPCP_N_COMPRESS;

static byte_t option_length[] = {
	 0,		/* unused */
	10,		/* address */
	 6		/* compression */
};


static int doipcp_local(int argc, char *argv[], void *p);
static int doipcp_open(int argc, char *argv[], void *p);
static int doipcp_pool(int argc, char *argv[], void *p);
static int doipcp_remote(int argc, char *argv[], void *p);

static int doipcp_address(int argc, char *argv[], void *p);
static int doipcp_compress(int argc, char *argv[], void *p);
static int doipcp_default(int argc, char *argv[], void *p);

static void ipcp_option(struct mbuf **bpp,
			struct ipcp_value_s *value_p,
			byte_t o_type,
			byte_t o_length,
			struct mbuf **copy_bpp);
static void ipcp_makeoptions(struct mbuf **bpp,
			struct ipcp_value_s *value_p,
			uint16 negotiating);
static struct mbuf *ipcp_makereq(struct fsm_s *fsm_p);

static int ipcp_check(struct mbuf **bpp,
			struct ipcp_s *ipcp_p,
			struct ipcp_side_s *side_p,
			struct option_hdr *option_p,
			int request);

static int ipcp_request(struct fsm_s *fsm_p,
			struct config_hdr *config,
			struct mbuf **data);
static int ipcp_ack(struct fsm_s *fsm_p,
			struct config_hdr *config,
			struct mbuf **data);
static int ipcp_nak(struct fsm_s *fsm_p,
			struct config_hdr *config,
			struct mbuf **data);
static int ipcp_reject(struct fsm_s *fsm_p,
			struct config_hdr *config,
			struct mbuf **data);

static void ipcp_reset(struct fsm_s *fsm_p);

static int32 ipcp_addr_idle(int32 addr);
static int32 ipcp_lookuppeer(char *peerid);
static int32 ipcp_poolnext(struct ipcp_s *ipcp_p);

static void ipcp_starting(struct fsm_s *fsm_p);
static void ipcp_stopping(struct fsm_s *fsm_p);

static void ipcp_closing(struct fsm_s *fsm_p);
static void ipcp_opening(struct fsm_s *fsm_p);

static void ipcp_free(struct fsm_s *fsm_p);


static struct fsm_constant_s ipcp_constants = {
	"IPcp",
	PPP_IPCP_PROTOCOL,
	0x00FE,				/* codes 1-7 recognized */

	IPcp,
	IPCP_REQ_TRY,
	IPCP_NAK_TRY,
	IPCP_TERM_TRY,
	IPCP_TIMEOUT * 1000L,

	ipcp_free,

	ipcp_reset,
	ipcp_starting,
	ipcp_opening,
	ipcp_closing,
	ipcp_stopping,

	ipcp_makereq,
	ipcp_request,
	ipcp_ack,
	ipcp_nak,
	ipcp_reject,
};


/************************************************************************/

/* "ppp <iface> ipcp" subcommands */
static struct cmds IPcpcmds[] = {
	"close",	doppp_close,	0,	0,	NULL,
	"listen",	doppp_passive,	0,	0,	NULL,
	"local",	doipcp_local,	0,	0,	NULL,
	"open",		doipcp_open,	0,	0,	NULL,
	"pool",		doipcp_pool,	0,	0,	NULL,
	"remote",	doipcp_remote,	0,	0,	NULL,
	"timeout",	doppp_timeout,	0,	0,	NULL,
	"try",		doppp_try,	0,	0,	NULL,
	NULL,
};

/* "ppp <iface> ipcp {local | remote}" subcommands */
static struct cmds IPcpside_cmds[] = {
	"address",	doipcp_address,	0,	0,	NULL,
	"compress",	doipcp_compress,0,	0,	NULL,
	"default",	doipcp_default,	0,	0,	NULL,
	NULL,
};


int
doppp_ipcp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp = p;
	register struct ppp_s *ppp_p = ifp->edv;

	return subcmd(IPcpcmds, argc, argv, &(ppp_p->fsm[IPcp]));
}


static int
doipcp_local(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct fsm_s *fsm_p = p;
	struct ipcp_s *ipcp_p = fsm_p->pdv;
	return subcmd(IPcpside_cmds, argc, argv, &(ipcp_p->local));
}


static int
doipcp_open(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct fsm_s *fsm_p = p;

	doppp_active( argc, argv, p );

	if ( fsm_p->ppp_p->phase == pppREADY ) {
		fsm_start( fsm_p );
	}
	return 0;
}


/* Set a pool of peer addresses for PPP interface */
static int
doipcp_pool(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct fsm_s *fsm_p = p;
	struct ipcp_s *ipcp_p = fsm_p->pdv;
	int32 pool_addr;
	int pool_cnt;

	if (argc < 2) {
		if ( ipcp_p->peer_min == 0L ) {
			printf("None");
		} else {
			printf("%s thru ", inet_ntoa(ipcp_p->peer_min));
			printf("%s\n", inet_ntoa(ipcp_p->peer_max));
		}
		return 0;
	}

	if ((pool_addr = resolve(argv[1])) == 0L) {
		printf(Badhost,argv[1]);
	}

	/* May specify a consecutive range of addresses; otherwise assume 1 */
	if (argc < 3)
		pool_cnt = 1;
	else
		pool_cnt = (int)strtol( argv[2], NULL, 0 );

	if (pool_cnt <= 0) {
		printf("Pool count %s (%d) must be > 0\n");
		return -1;
	}

	ipcp_p->peer_min = pool_addr;
	ipcp_p->peer_max = pool_addr + pool_cnt - 1;
	return 0;
}


static int
doipcp_remote(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct fsm_s *fsm_p = p;
	struct ipcp_s *ipcp_p = fsm_p->pdv;
	return subcmd(IPcpside_cmds, argc, argv, &(ipcp_p->remote));
}


/************************************************************************/
/* Set addresses for PPP interface */
static int
doipcp_address(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ipcp_side_s *side_p = p;
	int32 x32;

	if (argc < 2) {
		printf("%s\n", inet_ntoa(side_p->want.address));
		return 0;
	} else if ( stricmp(argv[1],"allow") == 0 ) {
		return bit16cmd( &(side_p->will_negotiate), IPCP_N_ADDRESS,
			"Allow Address", --argc, &argv[1] );
	}
	if ((x32 = resolve(argv[1])) == 0L) {
		printf(Badhost,argv[1]);
	}
	side_p->want.address = x32;
	side_p->want.negotiate |= IPCP_N_ADDRESS;
	return 0;
}


/* Set IP compression type for PPP interface */
static int
doipcp_compress(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ipcp_side_s *side_p = p;

	if (argc < 2) {
		if ( side_p->want.negotiate & IPCP_N_COMPRESS ) {
			switch ( side_p->want.compression ) {
			case PPP_COMPR_PROTOCOL:
				printf("TCP header compression enabled; "
					"Slots = %d, slot compress = %x\n",
					side_p->want.slots,
					side_p->want.slot_compress);
				break;
			default:
				printf("0x%04x\n", side_p->want.compression);
				break;
			};
		} else {
			printf("None\n");
		}
	} else if ( stricmp(argv[1],"allow") == 0 ) {
		return bit16cmd( &(side_p->will_negotiate), IPCP_N_COMPRESS,
			"Allow Compression", --argc, &argv[1] );
	} else if ( stricmp(argv[1],"tcp") == 0
		 || stricmp(argv[1],"vj") == 0 ) {
		side_p->want.compression = PPP_COMPR_PROTOCOL;
		if ( argc >= 3 ) {
			side_p->want.slots = strtol(argv[2],NULL,0);
			if ( side_p->want.slots < 1 || side_p->want.slots > 255 ) {
				printf( "slots must be in range 1 to 255" );
				return 1;
			}
		} else {
			side_p->want.slots = IPCP_SLOT_DEFAULT;
		}
		if ( argc >= 4 ) {
			side_p->want.slot_compress = strtol(argv[3],NULL,0);
		} else {
			side_p->want.slot_compress = IPCP_SLOT_COMPRESS;
		}
		side_p->want.negotiate |= IPCP_N_COMPRESS;
	} else if (stricmp(argv[1],"none") == 0) {
		side_p->want.negotiate &= ~IPCP_N_COMPRESS;
	} else {
		printf("allow tcp none\n");
		return 1;
	}
	return 0;
}


static int
doipcp_default(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct ipcp_side_s *side_p = p;

	ASSIGN( side_p->want, ipcp_default );
	return 0;
}


/************************************************************************/
/*			E V E N T   P R O C E S S I N G			*/
/************************************************************************/

static void
ipcp_option( bpp, value_p, o_type, o_length, copy_bpp )
struct mbuf **bpp;
struct ipcp_value_s *value_p;
byte_t o_type;
byte_t o_length;
struct mbuf **copy_bpp;
{
	struct mbuf *bp;
	register uint8 *cp;
	register int toss = o_length - OPTION_HDR_LEN;

	if ((bp = alloc_mbuf(o_length)) == NULL) {
		return;
	}
	cp = bp->data;
	*cp++ = o_type;
	*cp++ = o_length;

	switch ( o_type ) {
	case IPCP_ADDRESS:
		cp = put32(cp, value_p->address);
		cp = put32(cp, value_p->other);
		toss -= 8;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS) {
	trace_log(PPPiface, "    making IP source address: %s",
		inet_ntoa(value_p->address));
	trace_log(PPPiface, "    making IP destination address %s",
		inet_ntoa(value_p->other));
}
#endif
		break;

	case IPCP_COMPRESS:
		cp = put16(cp, value_p->compression);
		toss -= 2;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    making IP compression 0x%04x",
		value_p->compression);
#endif
		if ( value_p->compression == PPP_COMPR_PROTOCOL ) {
			*cp++ = value_p->slots - 1;
			*cp++ = value_p->slot_compress;
			toss -= 2;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    with IP compression slots %d, flag %x",
		value_p->slots,
		value_p->slot_compress);
#endif
		}
		break;

	default:
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    making unimplemented type %d", o_type);
#endif
		break;
	};

	while ( toss-- > 0 ) {
		*cp++ = pullchar(copy_bpp);
	}
	bp->cnt += o_length;
	append(bpp, &bp);
}


/************************************************************************/
/* Build a list of options */
static void
ipcp_makeoptions(bpp, value_p, negotiating)
struct mbuf **bpp;
struct ipcp_value_s *value_p;
uint16 negotiating;
{
	register int o_type;

	PPP_DEBUG_ROUTINES("ipcp_makeoptions()");

	for ( o_type = 1; o_type <= IPCP_OPTION_LIMIT; o_type++ ) {
		if (negotiating & (1 << o_type)) {
			ipcp_option( bpp, value_p,
				o_type, option_length[ o_type ], NULL);
		}
	}
}


/************************************************************************/
/* Build a request to send to remote host */
static struct mbuf *
ipcp_makereq(fsm_p)
struct fsm_s *fsm_p;
{
	struct ipcp_s *ipcp_p = fsm_p->pdv;
	struct mbuf *req_bp = NULL;

	PPP_DEBUG_ROUTINES("ipcp_makereq()");

	ipcp_makeoptions( &req_bp, &(ipcp_p->local.work),
				ipcp_p->local.work.negotiate );
	return(req_bp);
}


/************************************************************************/
/* Check the options, updating the working values.
 * Returns -1 if ran out of data, ACK/NAK/REJ as appropriate.
 */
static int
ipcp_check( bpp, ipcp_p, side_p, option_p, request )
struct mbuf **bpp;
struct ipcp_s *ipcp_p;
struct ipcp_side_s *side_p;
struct option_hdr *option_p;
int request;
{
	int toss = option_p->len - OPTION_HDR_LEN;
	int option_result = CONFIG_ACK;		/* Assume good values */
	int test;

	switch(option_p->type) {
	case IPCP_ADDRESS:
		side_p->work.address = pull32(bpp);
		side_p->work.other = pull32(bpp);
		toss -= 8;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS) {
	trace_log(PPPiface, "    checking IP source address: %s",
		inet_ntoa(side_p->work.address));
	trace_log(PPPiface, "    checking IP destination address %s",
		inet_ntoa(side_p->work.other));
}
#endif
		if ( !request ) {
			/* override any undesirable changes */
			if (ipcp_p->remote.want.address != 0L) {
				ipcp_p->local.work.other
					= ipcp_p->remote.want.address;
			}
			if (ipcp_p->local.want.address != 0L) {
				ipcp_p->local.work.address
					= ipcp_p->local.want.address;
			}
			break;
		}

		/* Ensure that addresses match */
		if (ipcp_p->remote.work.address == ipcp_p->remote.want.address) {
			if (ipcp_p->remote.want.address == 0L) {
				/* don't know address either */
				option_result = CONFIG_REJ;
			}
		} else if (ipcp_p->remote.want.address == 0L) {
			ipcp_p->local.work.other = ipcp_p->remote.work.address;
		} else {
			ipcp_p->remote.work.address = ipcp_p->remote.want.address;
			option_result = CONFIG_NAK;
		}

		if (ipcp_p->remote.work.other == ipcp_p->local.want.address) {
			if (ipcp_p->local.want.address == 0L) {
				/* don't know address either */
				option_result = CONFIG_REJ;
			}
		} else if (ipcp_p->local.want.address == 0L) {
			ipcp_p->local.work.address = ipcp_p->remote.work.other;
		} else {
			option_result = CONFIG_NAK;
			ipcp_p->remote.work.other = ipcp_p->local.want.address;
		}
		break;

	case IPCP_COMPRESS:
		side_p->work.compression = pull16(bpp);
		toss -= 2;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    checking IP compression 0x%04x",
		side_p->work.compression);
#endif
		/* Check if requested type is acceptable */
		switch ( side_p->work.compression ) {
		case PPP_COMPR_PROTOCOL:
			if ( (test = pullchar(bpp)) == -1 ) {
				return -1;
			}
			if ( (side_p->work.slots = test + 1) < IPCP_SLOT_LO) {
				side_p->work.slots = IPCP_SLOT_LO;
				option_result = CONFIG_NAK;
			} else if (side_p->work.slots > IPCP_SLOT_HI) {
				side_p->work.slots = IPCP_SLOT_HI;
				option_result = CONFIG_NAK;
			}

			if ( (test = pullchar(bpp)) == -1 ) {
				return -1;
			}
			if ( (side_p->work.slot_compress = test) > 1 ) {
				side_p->work.slot_compress = 1;
				option_result = CONFIG_NAK;
			}
			toss -= 2;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    with IP compression slots %d, flag %x",
		side_p->work.slots,
		side_p->work.slot_compress);
#endif
			break;

		default:
			if ( side_p->want.negotiate & IPCP_N_COMPRESS ) {
				side_p->work.compression = side_p->want.compression;
				side_p->work.slots = side_p->want.slots;
				side_p->work.slot_compress = side_p->want.slot_compress;
			} else {
				side_p->work.compression = PPP_COMPR_PROTOCOL;
				side_p->work.slots = IPCP_SLOT_DEFAULT;
				side_p->work.slot_compress = IPCP_SLOT_COMPRESS;
			}
			option_result = CONFIG_NAK;
			break;
		};
		break;

	default:
		option_result = CONFIG_REJ;
		break;
	};

	if (option_p->type > IPCP_OPTION_LIMIT
	 || !(side_p->will_negotiate & (1 << option_p->type))) {
		option_result = CONFIG_REJ;
	}

	if ( toss < 0 )
		return -1;

	if ( !request  &&  toss > 0 ) {
		/* toss extra bytes in option */
		while( toss-- > 0 ) {
			if ( pullchar(bpp) == -1 )
				return -1;
		}
	}

	return (option_result);
}


/************************************************************************/
/* Check options requested by the remote host */
static int
ipcp_request(
struct fsm_s *fsm_p,
struct config_hdr *config,
struct mbuf **data
){
	struct ipcp_s *ipcp_p = fsm_p->pdv;
	int32 signed_length = config->len;
	struct mbuf *reply_bp = NULL;	/* reply packet */
	int reply_result = CONFIG_ACK;		/* reply to request */
	uint16 desired;				/* desired to negotiate */
	struct option_hdr option;		/* option header storage */
	int option_result;			/* option reply */

	PPP_DEBUG_ROUTINES("ipcp_request()");
	ipcp_p->remote.work.negotiate = FALSE;	/* clear flags */

	/* Process options requested by remote host */
	while (signed_length > 0  &&  ntohopt(&option, data) != -1) {
		if ((signed_length -= option.len) < 0) {
			PPP_DEBUG_CHECKS("IPCP REQ: bad header length");
			free_p(data);
			free_p(&reply_bp);
			return -1;
		}

		if ( ( option_result = ipcp_check( data, ipcp_p,
				&(ipcp_p->remote), &option, TRUE ) ) == -1 ) {
			PPP_DEBUG_CHECKS("IPCP REQ: ran out of data");
			free_p(data);
			free_p(&reply_bp);
			return -1;
		}

#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS) {
	trace_log(PPPiface, "IPCP REQ: result %s, option %d, length %d",
		fsmCodes[option_result],
		option.type,
		option.len);
}
#endif
		if ( option_result < reply_result ) {
			continue;
		} else if ( option_result > reply_result ) {
			/* Discard current list of replies */
			free_p(&reply_bp);
			reply_bp = NULL;
			reply_result = option_result;
		}

		/* remember that we processed option */
		if ( option_result != CONFIG_REJ
		 && option.type <= IPCP_OPTION_LIMIT ) {
			ipcp_p->remote.work.negotiate |= (1 << option.type);
		}

		/* Add option response to the return list */
		ipcp_option( &reply_bp, &(ipcp_p->remote.work),
			option.type, option.len, data );
	}

	/* Now check for any missing options which are desired */
	if ( fsm_p->retry_nak > 0
	 &&  (desired = ipcp_p->remote.want.negotiate
		       & ~ipcp_p->remote.work.negotiate) != 0 ) {
		switch ( reply_result ) {
		case CONFIG_ACK:
			free_p(&reply_bp);
			reply_bp = NULL;
			reply_result = CONFIG_NAK;
			/* fallthru */
		case CONFIG_NAK:
			ipcp_makeoptions( &reply_bp, &(ipcp_p->remote.want),
				desired );
			fsm_p->retry_nak--;
			break;
		case CONFIG_REJ:
			/* do nothing */
			break;
		};
	} else if ( reply_result == CONFIG_NAK ) {
		/* if too many NAKs, reject instead */
		if ( fsm_p->retry_nak > 0 )
			fsm_p->retry_nak--;
		else
			reply_result = CONFIG_REJ;
	}

	/* Send ACK/NAK/REJ to remote host */
	fsm_send(fsm_p, reply_result, config->id, &reply_bp);
	free_p(data);
	return (reply_result != CONFIG_ACK);
}


/************************************************************************/
/* Process configuration ACK sent by remote host */
static int
ipcp_ack(
struct fsm_s *fsm_p,
struct config_hdr *config,
struct mbuf **data
){
	struct mbuf *req_bp;
	int error = FALSE;

	PPP_DEBUG_ROUTINES("ipcp_ack()");

	/* ID field must match last request we sent */
	if (config->id != fsm_p->lastid) {
		PPP_DEBUG_CHECKS("IPCP ACK: wrong ID");
		free_p(data);
		return -1;
	}

	/* Get a copy of last request we sent */
	req_bp = ipcp_makereq(fsm_p);

	/* Overall buffer length should match */
	if (config->len != len_p(req_bp)) {
		PPP_DEBUG_CHECKS("IPCP ACK: buffer length mismatch");
		error = TRUE;
	} else {
		register int req_char;
		register int ack_char;

		/* Each byte should match */
		while ((req_char = pullchar(&req_bp)) != -1) {
			if ((ack_char = pullchar(data)) == -1
			 || ack_char != req_char ) {
				PPP_DEBUG_CHECKS("IPCP ACK: data mismatch");
				error = TRUE;
				break;
			}
		}
	}
	free_p(&req_bp);
	free_p(data);

	if (error) {
		return -1;
	}

	PPP_DEBUG_CHECKS("IPCP ACK: valid");
	return 0;
}


/************************************************************************/
/* Process configuration NAK sent by remote host */
static int
ipcp_nak(
struct fsm_s *fsm_p,
struct config_hdr *config,
struct mbuf **data
){
	struct ipcp_s *ipcp_p = fsm_p->pdv;
	struct ipcp_side_s *local_p = &(ipcp_p->local);
	int32 signed_length = config->len;
	struct option_hdr option;
	int last_option = 0;
	int result;

	PPP_DEBUG_ROUTINES("ipcp_nak()");

	/* ID field must match last request we sent */
	if (config->id != fsm_p->lastid) {
		PPP_DEBUG_CHECKS("IPCP NAK: wrong ID");
		free_p(data);
		return -1;
	}

	/* First, process in order.  Then, process extra "important" options */
	while (signed_length > 0  &&  ntohopt(&option, data) != -1) {
		if ((signed_length -= option.len) < 0) {
			PPP_DEBUG_CHECKS("IPCP NAK: bad header length");
			free_p(data);
			return -1;
		}
		if ( option.type > IPCP_OPTION_LIMIT ) {
			PPP_DEBUG_CHECKS("IPCP NAK: option out of range");
		} else if ( option.type < last_option
		 || !(local_p->work.negotiate & (1 << option.type)) ) {
			if (local_p->work.negotiate & (1 << option.type)) {
				PPP_DEBUG_CHECKS("IPCP NAK: option out of order");
				free_p(data);
				return -1;		/* was requested */
			}
			local_p->work.negotiate |= (1 << option.type);
			last_option = IPCP_OPTION_LIMIT + 1;
		} else {
			last_option = option.type;
		}
		if ( ( result = ipcp_check( data, ipcp_p,
				local_p, &option, FALSE ) ) == -1 ) {
			PPP_DEBUG_CHECKS("IPCP NAK: ran out of data");
			free_p(data);
			return -1;
		}
		/* update the negotiation status */
		if ( result == CONFIG_REJ
		  && option.type <= IPCP_OPTION_LIMIT ) {
			local_p->work.negotiate &= ~(1 << option.type);
		}
	}
	PPP_DEBUG_CHECKS("IPCP NAK: valid");
	free_p(data);
	return 0;
}


/************************************************************************/
/* Process configuration reject sent by remote host */
static int
ipcp_reject(
struct fsm_s *fsm_p,
struct config_hdr *config,
struct mbuf **data
){
	struct ipcp_s *ipcp_p = fsm_p->pdv;
	struct ipcp_side_s *local_p = &(ipcp_p->local);
	int32 signed_length = config->len;
	struct option_hdr option;
	int last_option = 0;

	PPP_DEBUG_ROUTINES("ipcp_reject()");

	/* ID field must match last request we sent */
	if (config->id != fsm_p->lastid) {
		PPP_DEBUG_CHECKS("IPCP REJ: wrong ID");
		free_p(data);
		return -1;
	}

	/* Process in order, checking for errors */
	while (signed_length > 0  &&  ntohopt(&option, data) != -1) {
		register int k;

		if ((signed_length -= option.len) < 0) {
			PPP_DEBUG_CHECKS("IPCP REJ: bad header length");
			free_p(data);
			return -1;
		}
		if ( option.type > IPCP_OPTION_LIMIT ) {
			PPP_DEBUG_CHECKS("IPCP REJ: option out of range");
		} else if (option.type < last_option
		 || !(local_p->work.negotiate & (1 << option.type))) {
			PPP_DEBUG_CHECKS("IPCP REJ: option out of order");
			free_p(data);
			return -1;
		}
		for ( k = option.len - OPTION_HDR_LEN; k-- > 0; ) {
			if ( pullchar(data) == -1 ) {
				PPP_DEBUG_CHECKS("IPCP REJ: ran out of data");
				free_p(data);
				return -1;
			}
		}
		last_option = option.type;

		if ( option.type <= IPCP_OPTION_LIMIT ) {
			local_p->work.negotiate &= ~(1 << option.type);
		}
	}
	PPP_DEBUG_CHECKS("IPCP REJ: valid");
	free_p(data);
	return 0;
}


/************************************************************************/
/*			I N I T I A L I Z A T I O N			*/
/************************************************************************/

/* Reset configuration options before request */
static void
ipcp_reset(fsm_p)
struct fsm_s *fsm_p;
{
	struct ipcp_s *ipcp_p =	fsm_p->pdv;

	PPP_DEBUG_ROUTINES("ipcp_reset()");

	ASSIGN( ipcp_p->local.work, ipcp_p->local.want );
	ipcp_p->local.work.other = ipcp_p->remote.want.address;
	ipcp_p->local.will_negotiate |= ipcp_p->local.want.negotiate;

	ipcp_p->remote.work.negotiate = FALSE;
	ipcp_p->remote.will_negotiate |= ipcp_p->remote.want.negotiate;
}


/************************************************************************/
/* After termination */
static void
ipcp_stopping(fsm_p)
struct fsm_s *fsm_p;
{
	PPP_DEBUG_ROUTINES("ipcp_stopping()");
}


/************************************************************************/
/* Close IPCP */
static void
ipcp_closing(fsm_p)
struct fsm_s *fsm_p;
{
	struct ipcp_s *ipcp_p = 	fsm_p->pdv;

	/* free old slhc configuration, if any */
	slhc_free( ipcp_p->slhcp );
	ipcp_p->slhcp = NULL;

#ifdef notdef
	if (PPPtrace > 1)
		trace_log(PPPiface,"%s PPP/IPCP Drop route to peer (%s)",
			ifp->name,
			inet_ntoa(ipcp_p->local.work.other);

	rt_drop(ipcp_p->local.work.other, (unsigned int)32);
#endif
}


/************************************************************************/
/* configuration negotiation complete */
static void
ipcp_opening(fsm_p)
struct fsm_s *fsm_p;
{
	struct ipcp_s *ipcp_p = 	fsm_p->pdv;
	struct iface *ifp = 		fsm_p->ppp_p->iface;
	int32 address = ipcp_p->local.work.address;
	int rslots = 0;
	int tslots = 0;

	/* Set our IP address to reflect negotiated option */
	if (address != ifp->addr) {
		/* address not the same as last time */
		if (Ip_addr == 0L) {
			/* no global address */
			Ip_addr = address;
		} else if ( Ip_addr == ifp->addr ) {
			/* global was same as local; must be replaced */
			/* !!! TO DO: reset tcp connections */
			Ip_addr = address;
		}
		ifp->addr = address;

		if (PPPtrace > 1)
			trace_log(PPPiface,"%s PPP/IPCP Setting new IP address: %s",
				ifp->name,
				inet_ntoa(address));
	}

#ifdef notdef
	rt_add(ipcp_p->local.work.other, (unsigned int)32, (int32)0,
		ifp, (int32)1, (int32)0, (char)1);

	if (PPPtrace > 1)
		trace_log(PPPiface,"%s PPP/IPCP Add route to peer (%s)",
			ifp->name,
			inet_ntoa(ipcp_p->local.work.other);
#endif

	/* free old slhc configuration, if any */
	slhc_free( ipcp_p->slhcp );
	ipcp_p->slhcp = NULL;

	if (ipcp_p->local.work.negotiate & IPCP_N_COMPRESS) {
		rslots = ipcp_p->local.work.slots;
	}
	if (ipcp_p->remote.work.negotiate & IPCP_N_COMPRESS) {
		tslots = ipcp_p->remote.work.slots;
	}

	if ( rslots != 0 || tslots != 0 ) {
		ipcp_p->slhcp = slhc_init( rslots, tslots );

		if (PPPtrace > 1)
			trace_log(PPPiface,"%s PPP/IPCP Compression enabled;"
				" Recv slots = %d, flag = %x;"
				" Xmit slots = %d, flag = %x",
				ifp->name,
				rslots,
				ipcp_p->local.work.slot_compress,
				tslots,
				ipcp_p->remote.work.slot_compress);
	}
}


/************************************************************************/
/* Check the address against all other assigned addresses */
static int32
ipcp_addr_idle(addr)
int32 addr;
{
	struct iface *ifp;

	/* Check if peer IP address is already in use on another interface */
	/* !!! need to look at *remote* address, not local! */
	for (ifp=Ifaces; ifp != NULL; ifp = ifp->next) {
		if (ifp->addr == addr)
			return 0L;
	}
	return addr;
}


/************************************************************************/
/* Assign the next unused address from a pool */
static int32
ipcp_poolnext(ipcp_p)
struct ipcp_s *ipcp_p;
{
	int32 i = 1L + ipcp_p->peer_max - ipcp_p->peer_min;
	int32 nextaddr = 0L;

	while ( i-- > 0  &&  nextaddr == 0L ) {
		if (++ipcp_p->local.want.other < ipcp_p->peer_min
		 || ipcp_p->local.want.other > ipcp_p->peer_max)
			ipcp_p->local.want.other = ipcp_p->peer_min;

		nextaddr = ipcp_addr_idle(ipcp_p->local.want.other);
	}
	return(nextaddr);
}


/************************************************************************/
/* Check if we have a specific IP address to assign to remote peer host */
/* !!! TO DO: subnet mask, and routing */
static int32
ipcp_lookuppeer(peerid)
char *peerid;
{
	char *buf;
	int32 peer_addr = 0L;

	if (peerid == NULL)
		return 0L;

	if ( (buf = userlookup( peerid, NULL, NULL,
			NULL, &peer_addr )) != NULL ) {
		free(buf);
	}
	return(peer_addr);
}


/************************************************************************/
/* Prepare to begin configuration exchange */
static void
ipcp_starting(fsm_p)
struct fsm_s *fsm_p;
{
	struct ipcp_s *ipcp_p =		fsm_p->pdv;

	PPP_DEBUG_ROUTINES("ipcp_starting()");

	/* If not already set, and we know the name of the peer,
	 * look in login file for an address
	 */
	if ( ipcp_p->remote.want.address == 0L ){
		ipcp_p->remote.want.address
		= ipcp_lookuppeer(fsm_p->ppp_p->peername);
	}

	/* If available, get next address from PPP pool */
	if ((ipcp_p->remote.want.address == 0L)
	 && (ipcp_p->peer_min != 0L)) {
		ipcp_p->remote.want.address = ipcp_poolnext(ipcp_p);
	}

	ipcp_p->local.want.address = fsm_p->ppp_p->iface->addr;
}


/************************************************************************/
static void
ipcp_free(fsm_p)
struct fsm_s *fsm_p;
{
	struct ipcp_s *ipcp_p = fsm_p->pdv;

	slhc_free( ipcp_p->slhcp );
}


/* Initialize configuration structure */
void
ipcp_init(ppp_p)
struct ppp_s *ppp_p;
{
	struct fsm_s *fsm_p = &(ppp_p->fsm[IPcp]);
	struct ipcp_s *ipcp_p;

	PPPtrace = ppp_p->trace;
	PPPiface = ppp_p->iface;

	PPP_DEBUG_ROUTINES("ipcp_init()");

	fsm_p->ppp_p = ppp_p;
	fsm_p->pdc = &ipcp_constants;
	fsm_p->pdv =
	ipcp_p = callocw(1,sizeof(struct ipcp_s));

	/* Set option parameters to first request defaults */
	ASSIGN( ipcp_p->local.want, ipcp_default );
	ipcp_p->local.will_negotiate = ipcp_negotiate;

	ASSIGN( ipcp_p->remote.want, ipcp_default );
	ASSIGN( ipcp_p->remote.work, ipcp_default);
	ipcp_p->remote.will_negotiate = ipcp_negotiate;

	fsm_init(fsm_p);
}


