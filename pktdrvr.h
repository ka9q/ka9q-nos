#ifndef	_PKTDRVR_H
#define	_PKTDRVR_H

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef	_IFACE_H
#include "iface.h"
#endif

#define	PK_MAX	3	/* Add extra interrupt hooks if you increase this */

/* Packet driver interface classes */
#define	CL_NONE		0
#define	CL_ETHERNET	1
#define	CL_PRONET_10	2
#define	CL_IEEE8025	3
#define	CL_OMNINET	4
#define	CL_APPLETALK	5
#define	CL_SERIAL_LINE	6
#define	CL_STARLAN	7
#define	CL_ARCNET	8
#define	CL_AX25		9
#define	CL_KISS		10
#define CL_IEEE8023	11
#define CL_FDDI 	12
#define CL_INTERNET_X25 13
#define CL_LANSTAR	14
#define CL_SLFP 	15
#define	CL_NETROM	16
#define CL_PPP		17
#define	CL_QTSO		18
#define NCLASS		19

#ifdef	MSDOS

/* Packet driver interface types (not a complete list) */
#define	TC500		1
#define	PC2000		10
#define	WD8003		14
#define	PC8250		15
#define	ANYTYPE		0xffff

/* Packet driver function call numbers. From Appendix B. */
#define	DRIVER_INFO		1
#define	ACCESS_TYPE		2
#define	RELEASE_TYPE		3
#define	SEND_PKT		4
#define	TERMINATE		5
#define	GET_ADDRESS		6
#define	RESET_INTERFACE		7
#define GET_PARAMETERS		10
#define AS_SEND_PKT		11
#define	SET_RCV_MODE		20
#define	GET_RCV_MODE		21
#define	SET_MULTICAST_LIST	22
#define	GET_MULTICAST_LIST	23
#define	GET_STATISTICS		24
#define SET_ADDRESS		25

/* Packet driver error return codes. From Appendix C. */

#define	NO_ERROR	0
#define	BAD_HANDLE	1	/* invalid handle number */
#define	NO_CLASS	2	/* no interfaces of specified class found */
#define	NO_TYPE		3	/* no interfaces of specified type found */
#define	NO_NUMBER	4	/* no interfaces of specified number found */
#define	BAD_TYPE	5	/* bad packet type specified */
#define	NO_MULTICAST	6	/* this interface does not support multicast */
#define	CANT_TERMINATE	7	/* this packet driver cannot terminate */
#define	BAD_MODE	8	/* an invalid receiver mode was specified */
#define	NO_SPACE	9	/* operation failed because of insufficient space */
#define	TYPE_INUSE	10	/* the type had previously been accessed, and not released */
#define	BAD_COMMAND	11	/* the command was out of range, or not	implemented */
#define	CANT_SEND	12	/* the packet couldn't be sent (usually	hardware error) */
#define CANT_SET	13	/* hardware address couldn't be changed (> 1 handle open) */
#define BAD_ADDRESS	14	/* hardware address has bad length or format */
#define CANT_RESET	15	/* couldn't reset interface (> 1 handle open) */

typedef union {
	struct {
		uint8 lo;
		uint8 hi;
	} byte;
	unsigned short word;
} ureg;

#define	CARRY_FLAG	0x1

struct pktdrvr {
	int class;	/* Interface class (ether/slip/etc) */
	int intno;	/* Interrupt vector */
	short handle1;	/* Driver handle(s) */
	short handle2;
	short handle3;
	struct mbuf *buffer;	/* Currently allocated rx buffer */
	struct iface *iface;
};

extern struct pktdrvr Pktdrvr[];

/* In pktdrvr.c: */
uint8 *pkint(int dev,unsigned short cx, unsigned short ax);
void pk_tx(int dev,void *arg1,void *unused);
int pk_send(struct mbuf **bpp,struct iface *iface,int32 gateway,uint8 tos);
extern char Pkt_sig[];

/* In pkvec.asm: */
INTERRUPT pkvec0(void);
INTERRUPT pkvec1(void);
INTERRUPT pkvec2(void);

#endif	/* MSDOS */

#endif	/* _PKTDRVR_H */
