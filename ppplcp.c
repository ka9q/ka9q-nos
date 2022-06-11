/*
 *  PPPLCP.C	-- negotiate data link options
 *
 *	This implementation of PPP is declared to be in the public domain.
 *
 *	Jan 91	Bill_Simpson@um.cc.umich.edu
 *		Computer Systems Consulting Services
 *
 *	Acknowledgements and correction history may be found in PPP.C
 */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "socket.h"
#include "ppp.h"
#include "pppfsm.h"
#include "ppplcp.h"
#include "ppppap.h"
#include "cmdparse.h"
#include "devparam.h"
#include "trace.h"


/* These defaults are defined in the PPP RFCs, and must not be changed */
static struct lcp_value_s lcp_default = {
	FALSE,		/* no need to negotiate defaults */

	LCP_MRU_DEFAULT,
	LCP_ACCM_DEFAULT,
	0,		/* no authentication */
	0,		/* no encryption */
	0L,		/* no magic number */
	0L,		/* no reporting period */
};

/* for test purposes, accept anything we understand in the NAK */
static uint16 lcp_negotiate = LCP_N_MRU | LCP_N_ACCM | LCP_N_AUTHENT
		| LCP_N_PFC | LCP_N_ACFC | LCP_N_MAGIC;

static byte_t option_length[] = {
	 0,		/* unused */
	 4,		/* MRU */
	 6,		/* ACCM */
	 4,		/* authentication */
	 4,		/* encryption */
	 6,		/* magic number */
	 6,		/* monitor reporting period */
	 2,		/* Protocol compression */
	 2		/* Address/Control compression */
};


static int dolcp_local(int argc, char *argv[], void *p);
static int dolcp_open(int argc, char *argv[], void *p);
static int dolcp_remote(int argc, char *argv[], void *p);

static int dolcp_accm(int argc, char *argv[], void *p);
static int dolcp_acfc(int argc, char *argv[], void *p);
static int dolcp_auth(int argc, char *argv[], void *p);
static int dolcp_magic(int argc, char *argv[], void *p);
static int dolcp_mru(int argc, char *argv[], void *p);
static int dolcp_pfc(int argc, char *argv[], void *p);
static int dolcp_default(int argc, char *argv[], void *p);

static void lcp_option(struct mbuf **bpp,
			struct lcp_value_s *value_p,
			byte_t o_type,
			byte_t o_length,
			struct mbuf **copy_bpp );
static void lcp_makeoptions(struct mbuf **bpp,
			struct lcp_value_s *value_p,
			uint16 negotiating);
static struct mbuf *lcp_makereq(struct fsm_s *fsm_p);

static int lcp_check(struct mbuf **bpp,
			struct lcp_s *lcp_p,
			struct lcp_side_s *side_p,
			struct option_hdr *option_p,
			int request);

static int lcp_request(struct fsm_s *fsm_p,
			struct config_hdr *config,
			struct mbuf **data);
static int lcp_ack(struct fsm_s *fsm_p,
			struct config_hdr *ackcnf,
			struct mbuf **data);
static int lcp_nak(struct fsm_s *fsm_p,
			struct config_hdr *nakcnf,
			struct mbuf **data);
static int lcp_reject(struct fsm_s *fsm_p,
			struct config_hdr *rejcnf,
			struct mbuf **data);

static void lcp_reset(struct fsm_s *fsm_p);
static void lcp_starting(struct fsm_s *fsm_p);
static void lcp_stopping(struct fsm_s *fsm_p);

static void lcp_closing(struct fsm_s *fsm_p);
static void lcp_opening(struct fsm_s *fsm_p);

static void lcp_free(struct fsm_s *fsm_p);


static struct fsm_constant_s lcp_constants = {
	"Lcp",
	PPP_LCP_PROTOCOL,
	0x0FFE,				/* codes 1-11 recognized */

	Lcp,
	LCP_REQ_TRY,
	LCP_NAK_TRY,
	LCP_TERM_TRY,
	LCP_TIMEOUT * 1000L,

	lcp_free,

	lcp_reset,
	lcp_starting,
	lcp_opening,
	lcp_closing,
	lcp_stopping,

	lcp_makereq,
	lcp_request,
	lcp_ack,
	lcp_nak,
	lcp_reject
};


/************************************************************************/

/* "ppp <iface> lcp" subcommands */
static struct cmds Lcpcmds[] = {
	"close",	doppp_close,	0,	0,	NULL,
	"listen",	doppp_passive,	0,	0,	NULL,
	"local",	dolcp_local,	0,	0,	NULL,
	"open",		dolcp_open,	0,	0,	NULL,
	"remote",	dolcp_remote,	0,	0,	NULL,
	"timeout",	doppp_timeout,	0,	0,	NULL,
	"try",		doppp_try,	0,	0,	NULL,
	NULL,
};

/* "ppp <iface> lcp [local | remote]" subcommands */
static struct cmds Lcpside_cmds[] = {
	"accm",		dolcp_accm,	0,	0,	NULL,
	"acfc",		dolcp_acfc,	0,	0,	NULL,
	"authenticate",	dolcp_auth,	0,	0,	NULL,
	"magic",	dolcp_magic,	0,	0,	NULL,
	"mru",		dolcp_mru,	0,	0,	NULL,
	"pfc",		dolcp_pfc,	0,	0,	NULL,
	"default",	dolcp_default,	0,	0,	NULL,
	NULL,
};


int
doppp_lcp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp = p;
	register struct ppp_s *ppp_p = ifp->edv;

	return subcmd(Lcpcmds, argc, argv, &(ppp_p->fsm[Lcp]));
}


static int
dolcp_local(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct fsm_s *fsm_p = p;
	struct lcp_s *lcp_p = fsm_p->pdv;
	return subcmd(Lcpside_cmds, argc, argv, &(lcp_p->local));
}


static int
dolcp_open(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct fsm_s *fsm_p = p;

	doppp_active( argc, argv, p );

	if ( fsm_p->ppp_p->phase >= pppLCP ) {
		fsm_start( fsm_p );
	}
	return 0;
}


static int
dolcp_remote(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct fsm_s *fsm_p = p;
	struct lcp_s *lcp_p = fsm_p->pdv;
	return subcmd(Lcpside_cmds, argc, argv, &(lcp_p->remote));
}

/************************************************************************/

static int
dolcp_accm(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct lcp_side_s *side_p = p;

	if (argc < 2) {
		printf("0x%08lx\n",side_p->want.accm);
	} else if (stricmp(argv[1],"allow") == 0) {
		return bit16cmd(&(side_p->will_negotiate),LCP_N_ACCM,
			"Allow ACCM", --argc, &argv[1] );
	} else {
		side_p->want.accm = strtoul(argv[1], NULL, 0);
		if ( side_p->want.accm != LCP_ACCM_DEFAULT )
			side_p->want.negotiate |= LCP_N_ACCM;
		else
			side_p->want.negotiate &= ~LCP_N_ACCM;
	}
	return 0;
}


static int
dolcp_acfc(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct lcp_side_s *side_p = p;

	if (stricmp(argv[1],"allow") == 0) {
		return bit16cmd(&(side_p->will_negotiate),LCP_N_ACFC,
			"Allow Address/Control Field Compression", --argc, &argv[1] );
	}
	return bit16cmd( &(side_p->want.negotiate), LCP_N_ACFC,
		"Address/Control Field Compression", argc, argv );
}


static int
dolcp_auth(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct lcp_side_s *side_p = p;

	if (argc < 2) {
		if ( side_p->want.negotiate & LCP_N_AUTHENT ) {
			switch ( side_p->want.authentication ) {
			case PPP_PAP_PROTOCOL:
				printf("Pap\n");
				break;
			default:
				printf("0x%04x\n", side_p->want.authentication);
				break;
			};
		} else {
			printf("None\n");
		}
	} else if (stricmp(argv[1],"allow") == 0) {
		return bit16cmd(&(side_p->will_negotiate),LCP_N_AUTHENT,
			"Allow Authentication", --argc, &argv[1] );
	} else if (stricmp(argv[1],"pap") == 0) {
		side_p->want.negotiate |= LCP_N_AUTHENT;
		side_p->want.authentication = PPP_PAP_PROTOCOL;
	} else if (stricmp(argv[1],"none") == 0) {
		side_p->want.negotiate &= ~LCP_N_AUTHENT;
	} else {
		printf("allow pap none\n");
		return 1;
	}
	return 0;
}


static int
dolcp_magic(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct lcp_side_s *side_p = p;
	int result = 0;

	if (argc < 2) {
		printf("%d\n",side_p->want.magic_number);
	} else if (stricmp(argv[1],"allow") == 0) {
		return bit16cmd(&(side_p->will_negotiate),LCP_N_MAGIC,
			"Allow Magic Number", --argc, &argv[1] );
	} else {
		register int32 x = strtoul(argv[1], NULL, 0);

		if ( !x ) {
			int test;

			/* Check for keyword */
			result = setbool( &test, "Magic Number", argc, argv );

			if ( test ) {
				/* Make a non-zero random number */
				x = rdclock() << ((rdclock() & 0xf)+8);
			}
		}
		if ( x ) {
			side_p->want.negotiate |= LCP_N_MAGIC;
		} else {
			side_p->want.negotiate &= ~LCP_N_MAGIC;
		}
		side_p->want.magic_number = x;
	}
	return result;
}


static int
dolcp_mru(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct lcp_side_s *side_p = p;

	if (argc < 2) {
		printf("%d\n",side_p->want.mru);
	} else if (stricmp(argv[1],"allow") == 0) {
		return bit16cmd(&(side_p->will_negotiate),LCP_N_MRU,
			"Allow MRU", --argc, &argv[1] );
	} else {
		register int x = (int)strtol( argv[1], NULL, 0 );

		if (x < LCP_MRU_LO || x > LCP_MRU_HI) {
			printf("MRU %s (%d) out of range %d thru %d\n",
				argv[1], x, LCP_MRU_LO, LCP_MRU_HI);
			return -1;
		} else if ( x != LCP_MRU_DEFAULT ) {
			side_p->want.negotiate |= LCP_N_MRU;
		} else {
			side_p->want.negotiate &= ~LCP_N_MRU;
		}
		side_p->want.mru = x;
	}
	return 0;
}


static int
dolcp_pfc(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct lcp_side_s *side_p = p;

	if (stricmp(argv[1],"allow") == 0) {
		return bit16cmd(&(side_p->will_negotiate),LCP_N_PFC,
			"Allow Protocol Field Compression", --argc, &argv[1] );
	}
	return bit16cmd( &(side_p->want.negotiate), LCP_N_PFC,
		"Protocol Field Compression", argc, argv );
}


static int
dolcp_default(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct lcp_side_s *side_p = p;

	ASSIGN( side_p->want, lcp_default );
	return 0;
}


/************************************************************************/
/*			E V E N T   P R O C E S S I N G			*/
/************************************************************************/

static void
lcp_option( bpp, value_p, o_type, o_length, copy_bpp )
struct mbuf **bpp;
struct lcp_value_s *value_p;
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
	case LCP_MRU:
		put16(cp, value_p->mru);
		toss -= 2;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    making MRU: %d", value_p->mru);
#endif
		break;

	case LCP_ACCM:
		put32(cp, value_p->accm);
		toss -= 4;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    making ACCM: 0x%08lx", value_p->accm);
#endif
		break;

	case LCP_AUTHENT:
		put16(cp, value_p->authentication);
		toss -= 2;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    making Auth Protocol: 0x%04x",
		value_p->authentication);
#endif
		break;

	case LCP_MAGIC:
		put32(cp, value_p->magic_number);
		toss -= 4;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    making Magic Number: 0x%08lx",
		value_p->magic_number);
#endif
		break;

	case LCP_PFC:
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    making Protocol compression");
#endif
		break;

	case LCP_ACFC:
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    making Addr/Ctl compression");
#endif
		break;

	case LCP_ENCRYPT:		/* not implemented */
	case LCP_QUALITY:		/* not implemented */
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
lcp_makeoptions(bpp, value_p, negotiating)
struct mbuf **bpp;
struct lcp_value_s *value_p;
uint16 negotiating;
{
	register int o_type;

	PPP_DEBUG_ROUTINES("lcp_makeoptions()");

	for ( o_type = 1; o_type <= LCP_OPTION_LIMIT; o_type++ ) {
		if (negotiating & (1 << o_type)) {
			lcp_option( bpp, value_p,
				o_type, option_length[ o_type ], NULL);
		}
	}
}


/************************************************************************/
/* Build a request to send to remote host */
static struct mbuf *
lcp_makereq(fsm_p)
struct fsm_s *fsm_p;
{
	struct lcp_s *lcp_p = fsm_p->pdv;
	struct mbuf *req_bp = NULL;

	PPP_DEBUG_ROUTINES("lcp_makereq()");

	lcp_makeoptions( &req_bp, &(lcp_p->local.work),
				lcp_p->local.work.negotiate );
	return(req_bp);
}


/************************************************************************/
/* Check the options, updating the working values.
 * Returns -1 if ran out of data, ACK/NAK/REJ as appropriate.
 */
static int
lcp_check( bpp, lcp_p, side_p, option_p, request )
struct mbuf **bpp;
struct lcp_s *lcp_p;
struct lcp_side_s *side_p;
struct option_hdr *option_p;
int request;
{
	int toss = option_p->len - OPTION_HDR_LEN;
	int option_result = CONFIG_ACK;		/* Assume good values */

	switch(option_p->type) {
	case LCP_MRU:
		side_p->work.mru = pull16(bpp);
		toss -= 2;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    checking MRU: %d", side_p->work.mru);
#endif
		/* Check if new value is appropriate */
		if (side_p->work.mru < LCP_MRU_LO) {
			side_p->work.mru = LCP_MRU_LO;
			option_result = CONFIG_NAK;
		} else if (side_p->work.mru > LCP_MRU_HI) {
			side_p->work.mru = LCP_MRU_HI;
			option_result = CONFIG_NAK;
		}
		if ( request && (side_p->want.negotiate & LCP_N_MRU)
		  && side_p->work.mru > side_p->want.mru ) {
			side_p->work.mru = side_p->want.mru;
			option_result = side_p->want.mru;
		}
		break;

	case LCP_ACCM:
		side_p->work.accm = pull32(bpp);
		toss -= 4;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    checking ACCM: 0x%08lx", side_p->work.accm);
#endif
		/* Remote host may ask to escape more control  */
		/* characters than we require, but must escape */
		/* at least the control chars that we require. */
		if ( (!request || (side_p->want.negotiate & LCP_N_ACCM))
		  && side_p->work.accm !=
		       (side_p->work.accm | side_p->want.accm) ) {
			side_p->work.accm |= side_p->want.accm;
			option_result = CONFIG_NAK;
		}
		break;

	case LCP_AUTHENT:
		side_p->work.authentication = pull16(bpp);
		toss -= 2;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    checking Auth Protocol: 0x%04x",
		side_p->work.authentication);
#endif
		/* Check if new value is appropriate */
		switch ( side_p->work.authentication ) {
		case PPP_PAP_PROTOCOL:
			/* Yes */
			break;
		default:
			side_p->work.authentication = PPP_PAP_PROTOCOL;
			option_result = CONFIG_NAK;
			break;
		};
		break;

	case LCP_MAGIC:
		side_p->work.magic_number = pull32(bpp);
		toss -= 4;
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    checking Magic Number: 0x%08lx",
		side_p->work.magic_number);
#endif

		/* Ensure that magic numbers are different */
		if (side_p->work.magic_number == 0L
		 || lcp_p->remote.work.magic_number == lcp_p->local.work.magic_number) {
			side_p->work.magic_number += rdclock();
			option_result = CONFIG_NAK;
		}
		break;

	case LCP_PFC:
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    checking Protocol compression");
#endif
		break;

	case LCP_ACFC:
#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS)
	trace_log(PPPiface, "    checking Addr/Ctl compression");
#endif
		break;

	case LCP_ENCRYPT:		/* not implemented */
	case LCP_QUALITY:		/* not implemented */
	default:
		option_result = CONFIG_REJ;
		break;
	};

	if (option_p->type > LCP_OPTION_LIMIT
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
/* Check Link Control options requested by the remote host */
static int
lcp_request(fsm_p, config, data)
struct fsm_s *fsm_p;
struct config_hdr *config;
struct mbuf **data;
{
	struct lcp_s *lcp_p = fsm_p->pdv;
	int32 signed_length = config->len;
	struct mbuf *reply_bp = NULL;	/* reply packet */
	int reply_result = CONFIG_ACK;		/* reply to request */
	uint16 desired;				/* desired to negotiate */
	struct option_hdr option;		/* option header storage */
	int option_result;			/* option reply */

	PPP_DEBUG_ROUTINES("lcp_request()");
	lcp_p->remote.work.negotiate = FALSE;	/* clear flags */

	/* Process options requested by remote host */
	while (signed_length > 0  &&  ntohopt(&option, data) != -1) {
		if ((signed_length -= option.len) < 0) {
			PPP_DEBUG_CHECKS("LCP REQ: bad header length");
			free_p(data);
			free_p(&reply_bp);
			return -1;
		}

		if ( ( option_result = lcp_check( data, lcp_p,
				&(lcp_p->remote), &option, TRUE ) ) == -1 ) {
			PPP_DEBUG_CHECKS("LCP REQ: ran out of data");
			free_p(data);
			free_p(&reply_bp);
			return -1;
		}

#ifdef PPP_DEBUG_OPTIONS
if (PPPtrace & PPP_DEBUG_OPTIONS) {
	trace_log(PPPiface, "LCP REQ: result %s, option %d, length %d",
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
		  && option.type <= LCP_OPTION_LIMIT ) {
			lcp_p->remote.work.negotiate |= (1 << option.type);
		}

		/* Add option response to the return list */
		lcp_option( &reply_bp, &(lcp_p->remote.work),
			option.type, option.len, data );
	}

	/* Now check for any missing options which are desired */
	if ( fsm_p->retry_nak > 0
	 &&  (desired = lcp_p->remote.want.negotiate
		       & ~lcp_p->remote.work.negotiate) != 0 ) {
		switch ( reply_result ) {
		case CONFIG_ACK:
			free_p(&reply_bp);
			reply_bp = NULL;
			reply_result = CONFIG_NAK;
			/* fallthru */
		case CONFIG_NAK:
			lcp_makeoptions( &reply_bp, &(lcp_p->remote.want),
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
lcp_ack(
struct fsm_s *fsm_p,
struct config_hdr *config,
struct mbuf **data
){
	struct mbuf *req_bp;
	int error = FALSE;

	PPP_DEBUG_ROUTINES("lcp_ack()");

	/* ID field must match last request we sent */
	if (config->id != fsm_p->lastid) {
		PPP_DEBUG_CHECKS("LCP ACK: wrong ID");
		free_p(data);
		return -1;
	}

	/* Get a copy of last request we sent */
	req_bp = lcp_makereq(fsm_p);

	/* Overall buffer length should match */
	if (config->len != len_p(req_bp)) {
		PPP_DEBUG_CHECKS("LCP ACK: buffer length mismatch");
		error = TRUE;
	} else {
		register int req_char;
		register int ack_char;

		/* Each byte should match */
		while ((req_char = pullchar(&req_bp)) != -1) {
			if ((ack_char = pullchar(data)) == -1
			 || ack_char != req_char ) {
				PPP_DEBUG_CHECKS("LCP ACK: data mismatch");
				/*trace_log(PPPiface, "req=%02X, ack=%02X", req_char, ack_char);*/
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

	PPP_DEBUG_CHECKS("LCP ACK: valid");
	return 0;
}


/************************************************************************/
/* Process configuration NAK sent by remote host */
static int
lcp_nak(
struct fsm_s *fsm_p,
struct config_hdr *config,
struct mbuf **data
){
	struct lcp_s *lcp_p = fsm_p->pdv;
	struct lcp_side_s *local_p = &(lcp_p->local);
	int32 signed_length = config->len;
	struct option_hdr option;
	int last_option = 0;
	int result;

	PPP_DEBUG_ROUTINES("lcp_nak()");

	/* ID field must match last request we sent */
	if (config->id != fsm_p->lastid) {
		PPP_DEBUG_CHECKS("LCP NAK: wrong ID");
		free_p(data);
		return -1;
	}

	/* First, process in order.  Then, process extra "important" options */
	while (signed_length > 0  &&  ntohopt(&option, data) != -1) {
		if ((signed_length -= option.len) < 0) {
			PPP_DEBUG_CHECKS("LCP NAK: bad header length");
			free_p(data);
			return -1;
		}
		if ( option.type > LCP_OPTION_LIMIT ) {
			PPP_DEBUG_CHECKS("LCP NAK: option out of range");
		} else if ( option.type < last_option
		  || !(local_p->work.negotiate & (1 << option.type)) ) {
			if (local_p->work.negotiate & (1 << option.type)) {
				PPP_DEBUG_CHECKS("LCP NAK: option out of order");
				free_p(data);
				return -1;		/* was requested */
			}
			local_p->work.negotiate |= (1 << option.type);
			last_option = LCP_OPTION_LIMIT + 1;
		} else {
			last_option = option.type;
		}
		if ( ( result = lcp_check( data, lcp_p,
				local_p, &option, FALSE ) ) == -1 ) {
			PPP_DEBUG_CHECKS("LCP NAK: ran out of data");
			free_p(data);
			return -1;
		}
		/* update the negotiation status */
		if ( result == CONFIG_REJ
		  && option.type <= LCP_OPTION_LIMIT ) {
			local_p->work.negotiate &= ~(1 << option.type);
		}
	}
	PPP_DEBUG_CHECKS("LCP NAK: valid");
	free_p(data);
	return 0;
}


/************************************************************************/
/* Process configuration reject sent by remote host */
static int
lcp_reject(
struct fsm_s *fsm_p,
struct config_hdr *config,
struct mbuf **data
){
	struct lcp_s *lcp_p = fsm_p->pdv;
	struct lcp_side_s *local_p = &(lcp_p->local);
	int32 signed_length = config->len;
	struct option_hdr option;
	int last_option = 0;

	PPP_DEBUG_ROUTINES("lcp_reject()");

	/* ID field must match last request we sent */
	if (config->id != fsm_p->lastid) {
		PPP_DEBUG_CHECKS("LCP REJ: wrong ID");
		free_p(data);
		return -1;
	}

	/* Process in order, checking for errors */
	while (signed_length > 0  &&  ntohopt(&option, data) != -1) {
		register int k;

		if ((signed_length -= option.len) < 0) {
			PPP_DEBUG_CHECKS("LCP REJ: bad header length");
			free_p(data);
			return -1;
		}
		if ( option.type > LCP_OPTION_LIMIT ) {
			PPP_DEBUG_CHECKS("LCP REJ: option out of range");
		} else if ( option.type < last_option
		 || !(local_p->work.negotiate & (1 << option.type))) {
			PPP_DEBUG_CHECKS("LCP REJ: option out of order");
			free_p(data);
			return -1;
		}
		for ( k = option.len - OPTION_HDR_LEN; k-- > 0; ) {
			if ( pullchar(data) == -1 ) {
				PPP_DEBUG_CHECKS("LCP REJ: ran out of data");
				free_p(data);
				return -1;
			}
		}
		last_option = option.type;

		if ( option.type <= LCP_OPTION_LIMIT ) {
			local_p->work.negotiate &= ~(1 << option.type);
		}
	}
	PPP_DEBUG_CHECKS("LCP REJ: valid");
	free_p(data);
	return 0;
}


/************************************************************************/
/*			I N I T I A L I Z A T I O N			*/
/************************************************************************/

/* Check for PPP Network-Layer Protocol Phase */
void
ppp_ready(ppp_p)
struct ppp_s *ppp_p;
{
	if ( !(ppp_p->flags & (PPP_AP_LOCAL | PPP_AP_REMOTE)) ) {
		/* no pending authentication */
		ppp_p->phase = pppREADY;

		ppp_p->upsince = secclock();
		fsm_start( &(ppp_p->fsm[IPcp]) );
	}
}


/****************************************************************************/
/* Reset configuration options before request */
static void
lcp_reset(fsm_p)
struct fsm_s *fsm_p;
{
	struct lcp_s *lcp_p = 	fsm_p->pdv;

	PPP_DEBUG_ROUTINES("lcp_reset()");

	if ( lcp_p->local.want.negotiate & LCP_N_MAGIC ) {
		lcp_p->local.want.magic_number += rdclock();
	}

	ASSIGN( lcp_p->local.work, lcp_p->local.want );
	lcp_p->local.will_negotiate |= lcp_p->local.want.negotiate;

	lcp_p->remote.work.negotiate = FALSE;
	lcp_p->remote.will_negotiate |= lcp_p->remote.want.negotiate;
}


/************************************************************************/
/* Prepare to begin configuration exchange */
static void
lcp_starting(fsm_p)
struct fsm_s *fsm_p;
{
	PPP_DEBUG_ROUTINES("lcp_starting()");

	fsm_p->ppp_p->phase = pppLCP;
}


/************************************************************************/
/* After termination */
static void
lcp_stopping(fsm_p)
struct fsm_s *fsm_p;
{
	struct iface *ifp = fsm_p->ppp_p->iface;

	PPP_DEBUG_ROUTINES("lcp_stopping()");

	/* Tell the dialer to shut down */
	if ( ifp->supv != NULL )
		alert( ifp->supv, EABORT );

	/* Now, tell the device to go down.
	 * In turn, it should tell our IO status
	 * when it has gone down.
	 */
	ifp->ioctl(ifp,PARAM_DOWN,TRUE,0L);
}


/************************************************************************/
/* Close higher levels in preparation for link shutdown */
static void
lcp_closing(fsm_p)
struct fsm_s *fsm_p;
{
	struct ppp_s *ppp_p = fsm_p->ppp_p;

	ppp_p->phase = pppTERMINATE;

	fsm_down( &(ppp_p->fsm[IPcp]) );
	pap_down( &(ppp_p->fsm[Pap]) );
}


#ifdef TURBOC_SWITCH_BUG
#pragma option -G-
#endif

/************************************************************************/
/* configuration negotiation complete */
static void
lcp_opening(fsm_p)
struct fsm_s *fsm_p;
{
	struct lcp_s *lcp_p = 	fsm_p->pdv;
	struct iface *ifp = 	fsm_p->ppp_p->iface;

	if (ifp->mtu != lcp_p->remote.work.mru) {
		/* Set new Max Transmission Unit for outgoing packets */
		ifp->mtu = lcp_p->remote.work.mru;
		if (PPPtrace > 1)
			trace_log(PPPiface,"    Set new MTU for outgoing packets: %d",
				ifp->mtu);
	}

	/* check for authentication */
	fsm_p->ppp_p->phase = pppAP;
	fsm_p->ppp_p->flags &= ~(PPP_AP_LOCAL | PPP_AP_REMOTE);
	free(fsm_p->ppp_p->peername);
	fsm_p->ppp_p->peername = NULL;

	if (lcp_p->local.work.negotiate & LCP_N_AUTHENT) {
		switch (lcp_p->local.work.authentication) {
		case PPP_PAP_PROTOCOL:
			pap_local(fsm_p->ppp_p);
			break;
		};
	}
	if (lcp_p->remote.work.negotiate & LCP_N_AUTHENT) {
		switch (lcp_p->remote.work.authentication) {
		case PPP_PAP_PROTOCOL:
			pap_remote(fsm_p->ppp_p);
			break;
		};
	}

	/* re-check for authentication */
	ppp_ready(fsm_p->ppp_p);
}

#ifdef TURBOC_SWITCH_BUG
#pragma option -G
#endif


/************************************************************************/
static void
lcp_free(fsm_p)
struct fsm_s *fsm_p;
{
	/* nothing to do */
}


/* Initialize configuration structure */
void
lcp_init(ppp_p)
struct ppp_s *ppp_p;
{
	struct fsm_s *fsm_p = &(ppp_p->fsm[Lcp]);
	struct lcp_s *lcp_p;

	PPPtrace = ppp_p->trace;
	PPPiface = ppp_p->iface;

	PPP_DEBUG_ROUTINES("lcp_init()");

	fsm_p->ppp_p = ppp_p;
	fsm_p->pdc = &lcp_constants;
	fsm_p->pdv =
	lcp_p = callocw(1,sizeof(struct lcp_s));

	/* Set option parameters to first request defaults */
	ASSIGN( lcp_p->local.want, lcp_default );
	lcp_p->local.will_negotiate = lcp_negotiate;

	ASSIGN( lcp_p->remote.want, lcp_default );
	ASSIGN( lcp_p->remote.work, lcp_default );
	lcp_p->remote.will_negotiate = lcp_negotiate;

	fsm_init(fsm_p);
}


