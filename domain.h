#ifndef	_DOMAIN_H
#define	_DOMAIN_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

#define	INITRTT		2000L	/* Initial smoothed response time */
#define	MAXCNAME	10	/* Maximum amount of cname recursion */

#define	TYPE_A		1	/* Host address */
#define	TYPE_NS		2	/* Name server */
#define	TYPE_MD		3	/* Mail destination (obsolete) */
#define	TYPE_MF		4	/* Mail forwarder (obsolete) */
#define	TYPE_CNAME	5	/* Canonical name */
#define	TYPE_SOA	6	/* Start of Authority */
#define	TYPE_MB		7	/* Mailbox name (experimental) */
#define	TYPE_MG		8	/* Mail group member (experimental) */
#define	TYPE_MR		9	/* Mail rename name (experimental) */
#define	TYPE_NULL	10	/* Null (experimental) */
#define	TYPE_WKS	11	/* Well-known sockets */
#define	TYPE_PTR	12	/* Pointer record */
#define	TYPE_HINFO	13	/* Host information */
#define	TYPE_MINFO	14	/* Mailbox information (experimental)*/
#define	TYPE_MX		15	/* Mail exchanger */
#define	TYPE_TXT	16	/* Text strings */
#define	TYPE_ANY	255	/* Matches any type */

#define	CLASS_IN	1	/* The ARPA Internet */

struct dserver {
	struct dserver *prev;	/* Linked list pointers */
	struct dserver *next;

	int32 address;		/* IP address of server */
	int32 timeout;		/* Current timeout, ticks */
	int32 srtt;		/* Smoothed round trip time, ticks */
	int32 mdev;		/* Mean deviation, ticks */
	int32 queries;		/* Query packets sent to this server */
	int32 responses;	/* Response packets received from this server */
};
extern struct dserver *Dlist;
extern int Dsocket;		/* Socket to use for domain queries */

/* Round trip timing parameters */
#define	AGAIN	8	/* Average RTT gain = 1/8 */
#define	LAGAIN	3	/* Log2(AGAIN) */
#define	DGAIN	4	/* Mean deviation gain = 1/4 */
#define	LDGAIN	2	/* log2(DGAIN) */

/* Header for all domain messages */
struct dhdr {
	uint16 id;		/* Identification */
	uint8 qr;		/* Query/Response */
#define	QUERY		0
#define	RESPONSE	1
	uint8 opcode;
#define	IQUERY		1
	uint8 aa;		/* Authoratative answer */
	uint8 tc;		/* Truncation */
	uint8 rd;		/* Recursion desired */
	uint8 ra;		/* Recursion available */
	uint8 rcode;		/* Response code */
#define	NO_ERROR	0
#define	FORMAT_ERROR	1
#define	SERVER_FAIL	2
#define	NAME_ERROR	3
#define	NOT_IMPL	4
#define	REFUSED		5
	uint16 qdcount;		/* Question count */
	uint16 ancount;		/* Answer count */
	uint16 nscount;		/* Authority (name server) count */
	uint16 arcount;		/* Additional record count */
	struct rr *questions;	/* List of questions */
	struct rr *answers;	/* List of answers */
	struct rr *authority;	/* List of name servers */
	struct rr *additional;	/* List of additional records */
};

struct mx {
	uint16 pref;
	char *exch;
};

struct hinfo {
	char *cpu;
	char *os;
};

struct soa {
	char *mname;
	char *rname;
	int32 serial;
	int32 refresh;
	int32 retry;
	int32 expire;
	int32 minimum;
};

struct rr {
	struct rr *last;
	struct rr *next;
	uint8 source;
#define RR_NONE		0
#define RR_FILE		1	/* from file */
#define RR_QUESTION	4	/* from server reply */
#define RR_ANSWER	5	/* from server reply */
#define RR_AUTHORITY	6	/* from server reply */
#define RR_ADDITIONAL	7	/* from server reply */
#define RR_QUERY	8	/* test name (see QUERY)*/
#define RR_INQUERY	9	/* test resource (see IQUERY)*/

	char *comment;		/* optional comment */
	char *name;		/* Domain name, ascii form */
	int32 ttl;		/* Time-to-live */
#define TTL_MISSING	0x80000000L
	uint16 class;		/* IN, etc */
#define CLASS_MISSING	0
	uint16 type;		/* A, MX, etc */
#define TYPE_MISSING	0
	uint16 rdlength;		/* Length of data field */
	union {
		int32 addr;		/* Used for type == A */
		struct soa soa;		/* Used for type == SOA */
		struct mx mx;		/* Used for type == MX */
		struct hinfo hinfo;	/* Used for type == HINFO */
		char *name;		/* for domain names */
		char *data;		/* for anything else */
	} rdata;
};
extern struct proc *Dfile_updater;

/* In domain.c */
int add_nameserver(int32 address);
void free_rr(struct rr *rrlp);
struct rr *inverse_a(int32 ip_address);
struct rr *resolve_rr(char *dname,uint16 dtype);
char *resolve_a(int32 ip_address, int shorten);
struct rr *resolve_mailb(char *name);

/* In domhdr.c: */
int ntohdomain(struct dhdr *dhdr,struct mbuf **bpp);

#endif	/* _DOMAIN_H */
