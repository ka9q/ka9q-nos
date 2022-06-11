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

#include "socket.h"
#include "arp.h"

#define MHOSTS  12     /* max number of 'hosts' structs */
#define BP_MAXDNS 5


struct bootpd_stat {
	int	rcvd;
	int	bad_size;
	int	bad_op;

};

struct host {
        char name[31];		/* host name (and suffix) */
	uint8 htype;		/* hardware type */
	uint8 haddr[MAXHWALEN];	/* hardware address */
        struct in_addr iaddr;	/* internet address */
        char bootfile[32];	/* default boot file name */
};

extern char *ArpNames[];
extern char bp_ascii[];

int readtab(void);
void readtab_shut(void);
void bp_log(char *fmt,...);
void da_status(struct iface *iface);
int da_assign(struct iface *iface,uint8 *hwaddr,int32 *ipaddr);
int da_init(void);
void da_shut(void);
int da_done_net(struct iface *iface);
int da_serve_net(struct iface *iface,int32 rstart,int32 rend);

