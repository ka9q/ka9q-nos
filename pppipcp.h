#ifndef _PPPIPCP_H
#define _PPPIPCP_H

#ifndef _SLHC_H
#include "slhc.h"
#endif

					/* IPCP option types */
#define IPCP_ADDRESS		0x01
#define IPCP_COMPRESS		0x02
#define IPCP_OPTION_LIMIT	0x02	/* highest # we can handle */

/* Table for IPCP configuration requests */
struct ipcp_value_s {
	uint16 negotiate;		/* negotiation flags */
#define IPCP_N_ADDRESS		(1 << IPCP_ADDRESS)
#define IPCP_N_COMPRESS		(1 << IPCP_COMPRESS)

	int32 address;			/* address for this side */
	int32 other;			/* address for other side */

	uint16 compression;		/* Compression protocol */
	uint16 slots;			/* Slots (0-n)*/
	byte_t slot_compress;		/* Slots may be compressed (flag)*/
};

#define IPCP_SLOT_DEFAULT	16	/* Default # of slots */
#define IPCP_SLOT_HI		64	/* Maximum # of slots */
#define IPCP_SLOT_LO 		 1	/* Minimum # of slots */
#define IPCP_SLOT_COMPRESS	0x01	/* May compress slot id */

struct ipcp_side_s {
	uint16 will_negotiate;
	struct ipcp_value_s want;
	struct ipcp_value_s work;
};

/* Store address pool in otherwise unused address locations */
#define peer_min	local.want.other	/* First IP address in pool */
#define peer_max	remote.want.other	/* Last IP address in pool */

/* IPCP control block */
struct ipcp_s {
	struct ipcp_side_s local;
	struct ipcp_side_s remote;

	struct slcompress *slhcp;	/* pointer to compression block */
};

#define IPCP_REQ_TRY	20		/* REQ attempts */
#define IPCP_NAK_TRY	10		/* NAK attempts */
#define IPCP_TERM_TRY	10		/* tries on TERM REQ */
#define IPCP_TIMEOUT	3		/* Seconds to wait for response */


int doppp_ipcp(int argc, char *argv[], void *p);
void ipcp_init(struct ppp_s *ppp_p);

#endif /* _PPPIPCP_H */
