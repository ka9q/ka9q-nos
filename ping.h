#ifndef	_PING_H
#define	_PING_H

#ifndef _SESSION_H
#include "session.h"
#endif

struct ping {
	struct session *sp;
	int s;			/* Socket for pinging */
	int32 target;		/* Starting target IP address */
	int incflag;		/* If true, increment target after each ping */
	int32 sent;		/* Total number of pings sent */
	int32 srtt;		/* Smoothed round trip time */
	int32 mdev;		/* Mean deviation */
	int32 maxrtt;		/* Maximum rtt */
	int32 minrtt;		/* Minimum rtt */
	int32 responses;	/* Total number of responses */
	int32 interval;		/* Inter-ping interval, ticks */
	uint16 len;		/* Length of data portion of ping */
};

/* In ping.c: */
void echo_proc(int32 source,int32 dest,struct icmp *icmp,struct mbuf **bpp);
int pingem(int s,int32 target,uint16 seq,uint16 id,uint16 len);

#endif /* _PING_H */

