#ifndef _PPPLCP_H
#define _PPPLCP_H

					/* LCP option types */
#define LCP_MRU			0x01
#define LCP_ACCM		0x02
#define LCP_AUTHENT		0x03
#define LCP_ENCRYPT		0x04
#define LCP_MAGIC		0x05
#define LCP_QUALITY		0x06
#define LCP_PFC			0x07
#define LCP_ACFC		0x08
#define LCP_OPTION_LIMIT	0x08	/* highest # we can handle */

/* Table for LCP configuration requests */
struct lcp_value_s {
	uint16 negotiate;		/* negotiation flags */
#define LCP_N_MRU		(1 << LCP_MRU)
#define LCP_N_ACCM		(1 << LCP_ACCM)
#define LCP_N_AUTHENT		(1 << LCP_AUTHENT)
#define LCP_N_ENCRYPT		(1 << LCP_ENCRYPT)
#define LCP_N_MAGIC		(1 << LCP_MAGIC)
#define LCP_N_QUALITY		(1 << LCP_QUALITY)
#define LCP_N_PFC		(1 << LCP_PFC)
#define LCP_N_ACFC		(1 << LCP_ACFC)

	uint16 mru;			/* Maximum Receive Unit */
	int32 accm;			/* Async Control Char Map */
	uint16 authentication;		/* Authentication protocol */
	uint16 encryption;		/* Encryption protocol */
	int32 magic_number;		/* Magic number value */
	int32 reporting_period;		/* Link Quality reporting period */
};

/* Other configuration option values */
#define LCP_ACCM_DEFAULT	0xffffffffL
#define LCP_MRU_DEFAULT	1500
#define LCP_MRU_HI	4096		/* High MRU limit */
#define LCP_MRU_LO	128		/* Lower MRU limit */

/*
 *	local.want:	Options to request.
 *			Contains desired value.
 *			Only non-default options need to be negotiated.
 *			Initially, all are default.
 *	local.will:	Options to accept in a NAK from remote.
 *	local.work:	Options currently being negotiated.
 *			Value is valid only when negotiate bit is set.
 *
 *	remote.want:	Options to suggest by NAK if not present in REQ.
 *			Contains desired value.
 *	remote.will:	Options to accept in a REQ from remote.
 *	remote.work:	Options currently being negotiated.
 *			Value is valid only when negotiate bit is set.
 */

struct lcp_side_s {
	uint16	will_negotiate;
	struct lcp_value_s want;
	struct lcp_value_s work;
};

/* LCP control block */
struct lcp_s {
	struct lcp_side_s local;
	struct lcp_side_s remote;
};

#define LCP_REQ_TRY	20		/* REQ attempts */
#define LCP_NAK_TRY	10		/* NAK attempts */
#define LCP_TERM_TRY	10		/* tries on TERM REQ */
#define LCP_TIMEOUT	3		/* Seconds to wait for response */


int doppp_lcp(int argc, char *argv[], void *p);

void ppp_ready(struct ppp_s *ppp_p);

void lcp_init(struct ppp_s *ppp_p);

#endif /* _PPPLCP_H */
