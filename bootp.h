/*************************************************/
/* Center for Information Technology Integration */
/*           The University of Michigan          */
/*                    Ann Arbor                  */
/*                                               */
/* Dedicated to the public domain.               */
/* Send questions to info@citi.umich.edu         */
/*                                               */
/* BOOTP is documented in RFC 951 and RFC 1048   */
/*************************************************/

#ifndef BOOTREQUEST

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_SOCKET_H
#include "socket.h"
#endif

#ifndef _IP_H
#include "ip.h"
#endif

struct bootp {
	uint8	op;			/* packet opcode type */
	uint8	htype;			/* hardware addr type */
	uint8	hlen;			/* hardware addr length */
	uint8	hops;			/* gateway hops */
	int32	xid;			/* transaction ID */
	uint16	secs;			/* seconds since boot began */
	uint16	unused;
	struct in_addr	ciaddr;		/* client IP address */
	struct in_addr	yiaddr;		/* 'your' IP address */
	struct in_addr	siaddr;		/* server IP address */
	struct in_addr	giaddr;		/* gateway IP address */
	uint8	chaddr[16];		/* client hardware address */
	char	sname[64];		/* server host name */
	char	file[128];		/* boot file name */
	uint8	vend[64];		/* vendor-specific area */
};

/*
 * UDP port numbers, server and client.
 */
#define	IPPORT_BOOTPS		67
#define	IPPORT_BOOTPC		68

#define BOOTREQUEST		1
#define BOOTREPLY		2

#define BOOTP_PAD		0
#define BOOTP_SUBNET		1
#define BOOTP_GATEWAY		3
#define BOOTP_DNS		6
#define BOOTP_HOSTNAME		12
#define BOOTP_END		0xff

/*
 * "vendor" data permitted for Stanford boot clients.
 */
struct vend {
	uint8  v_magic[4];     /* magic number */
        unsigned long  v_flags;        /* flags/opcodes, etc. */
        uint8  v_unused[56];   /* currently unused */
};

#define VM_STANFORD     "STAN"  /* v_magic for Stanford */

/* v_flags values */
#define VF_PCBOOT       1       /* an IBMPC or Mac wants environment info */
#define VF_HELP         2       /* help me, I'm not registered */

extern int WantBootp;
extern char bp_ascii[];

void bootp_print_packet(struct bootp *bp);
int bootp_validPacket(struct ip *ip,struct mbuf *bp);

#endif

