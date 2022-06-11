/* Internet Control Message Protocol (ICMP)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ip.h"
#include "icmp.h"
#include "ping.h"
#include "netuser.h"

struct mib_entry Icmp_mib[] = {
	"",			0,
	"icmpInMsgs",		0,
	"icmpInErrors",		0,
	"icmpInDestUnreachs",	0,
	"icmpInTimeExcds",	0,
	"icmpInParmProbs",	0,
	"icmpInSrcQuenchs",	0,
	"icmpInRedirects",	0,
	"icmpInEchos",		0,
	"icmpInEchoReps",	0,
	"icmpInTimestamps",	0,
	"icmpInTimestampReps",	0,
	"icmpInAddrMasks",	0,
	"icmpInAddrMaskReps",	0,
	"icmpOutMsgs",		0,
	"icmpOutErrors",	0,
	"icmpOutDestUnreachs",	0,
	"icmpOutTimeExcds",	0,
	"icmpOutParmProbs",	0,
	"icmpOutSrcQuenchs",	0,
	"icmpOutRedirects",	0,
	"icmpOutEchos",		0,
	"icmpOutEchoReps",	0,
	"icmpOutTimestamps",	0,
	"icmpOutTimestampReps",	0,
	"icmpOutAddrMasks",	0,
	"icmpOutAddrMaskReps",	0,
};

/* Process an incoming ICMP packet */
void
icmp_input(
struct iface *iface,	/* Incoming interface (ignored) */
struct ip *ip,		/* Pointer to decoded IP header structure */
struct mbuf **bpp,	/* Pointer to ICMP message */
int rxbroadcast,
int32 said
){
	struct icmplink *ipp;
	struct icmp icmp;	/* ICMP header */
	struct ip oip;		/* Offending datagram header */
	uint16 type;		/* Type of ICMP message */
	uint16 length;

	icmpInMsgs++;
	if(rxbroadcast){
		/* Broadcast ICMP packets are to be IGNORED !! */
		icmpInErrors++;
		free_p(bpp);
		return;
	}
	length = ip->length - IPLEN - ip->optlen;
	if(cksum(NULL,*bpp,length) != 0){
		/* Bad ICMP checksum; discard */
		icmpInErrors++;
		free_p(bpp);
		return;
	}
	ntohicmp(&icmp,bpp);

	/* Process the message. Some messages are passed up to the protocol
	 * module for handling, others are handled here.
	 */
	type = icmp.type;
	
	switch(type){
	case ICMP_TIME_EXCEED:	/* Time-to-live Exceeded */
	case ICMP_DEST_UNREACH:	/* Destination Unreachable */
	case ICMP_QUENCH:	/* Source Quench */
	case ICMP_IPSP:		/* Bad security packet */
		switch(type){
		case ICMP_TIME_EXCEED:	/* Time-to-live Exceeded */
			icmpInTimeExcds++;
			break;
		case ICMP_DEST_UNREACH:	/* Destination Unreachable */
			icmpInDestUnreachs++;
			break;
		case ICMP_QUENCH:	/* Source Quench */
			icmpInSrcQuenchs++;
			break;
		}
		ntohip(&oip,bpp);	/* Extract offending IP header */
		if(Icmp_trace){
			printf("ICMP from %s:",inet_ntoa(ip->source));
			printf(" dest %s %s",inet_ntoa(oip.dest),
			 smsg(Icmptypes,ICMP_TYPES,type));
			switch(type){
			case ICMP_TIME_EXCEED:
				printf(" %s\n",
				 smsg(Exceed,NEXCEED,icmp.code));
				break;
			case ICMP_DEST_UNREACH:
				printf(" %s\n",
				 smsg(Unreach,NUNREACH,icmp.code));
				break;
			case ICMP_IPSP:
				printf(" %s\n",smsg(Said_icmp,NIPSP,icmp.code));
				break;
			default:
				printf(" %u\n",icmp.code);
				break;
			}
		}
		for(ipp = Icmplink;ipp->funct != NULL;ipp++)
			if(ipp->proto == oip.protocol)
				break;
		if(ipp->funct != NULL){
			(*ipp->funct)(ip->source,oip.source,oip.dest,icmp.type,
			 icmp.code,bpp);
		}
		break;
	case ICMP_ECHO:		/* Echo Request */
		/* Change type to ECHO_REPLY, recompute checksum,
		 * and return datagram.
		 */
		icmpInEchos++;
		icmp.type = ICMP_ECHO_REPLY;
		htonicmp(&icmp,bpp);
		icmpOutEchoReps++;
		ip_send(ip->dest,ip->source,ICMP_PTCL,ip->tos,0,bpp,length,0,0);
		return;
	case ICMP_REDIRECT:	/* Redirect */
		icmpInRedirects++;
		ntohip(&oip,bpp);	/* Extract offending IP header */
		if(Icmp_trace){
			printf("ICMP from %s:",inet_ntoa(ip->source));
			printf(" dest %s %s",inet_ntoa(oip.dest),
			 smsg(Icmptypes,ICMP_TYPES,type));
			printf(" new gateway %s\n",inet_ntoa(icmp.args.address));
		}
		break;
	case ICMP_PARAM_PROB:	/* Parameter Problem */
		icmpInParmProbs++;
		break;
	case ICMP_ECHO_REPLY:	/* Echo Reply */
		icmpInEchoReps++;
		echo_proc(ip->source,ip->dest,&icmp,bpp);
		break;
	case ICMP_TIMESTAMP:	/* Timestamp */
		icmpInTimestamps++;
		break;
	case ICMP_TIME_REPLY:	/* Timestamp Reply */
		icmpInTimestampReps++;
		break;
	case ICMP_INFO_RQST:	/* Information Request */
		break;
	case ICMP_INFO_REPLY:	/* Information Reply */
		break;
	}
	free_p(bpp);
}
/* Return an ICMP response to the sender of a datagram.
 * Unlike most routines, the callER frees the mbuf.
 */
int
icmp_output(
struct ip *ip,		/* Header of offending datagram */
struct mbuf *data,	/* Data portion of datagram - FREED BY CALLER */
uint8 type,		/* Codes to send */
uint8 code,
union icmp_args *args
){
	struct mbuf *bp;
	struct icmp icmp;	/* ICMP protocol header */
	uint16 dlen;		/* Length of data portion of offending pkt */
	uint16 length;		/* Total length of reply */

	if(ip == NULL)
		return -1;
	if(ip->protocol == ICMP_PTCL){
		/* Peek at type field of ICMP header to see if it's safe to
		 * return an ICMP message
		 */
		switch(data->data[0]){
		case ICMP_ECHO_REPLY:
		case ICMP_ECHO:
		case ICMP_TIMESTAMP:
		case ICMP_TIME_REPLY:
		case ICMP_INFO_RQST:
		case ICMP_INFO_REPLY:
			break;	/* These are all safe */
		default:
			/* Never send an ICMP error message about another
			 * ICMP error message!
			 */
			return -1;
		}
	}
	/* Compute amount of original datagram to return.
	 * We return the original IP header, and up to 8 bytes past that.
	 */
	dlen = min(8,len_p(data));
	length = dlen + ICMPLEN + IPLEN + ip->optlen;
	/* Take excerpt from data portion */
	if(data != NULL && dup_p(&bp,data,0,dlen) == 0)
		return -1;	/* The caller will free data */

	/* Recreate and tack on offending IP header */
	htonip(ip,&bp,IP_CS_NEW);
	icmp.type = type;
	icmp.code = code;
	icmp.args.unused = 0;
	switch(icmp.type){
	case ICMP_PARAM_PROB:
		icmpOutParmProbs++;
		icmp.args.pointer = args->pointer;
		break;
	case ICMP_REDIRECT:
		icmpOutRedirects++;
		icmp.args.address = args->address;
		break;
	case ICMP_ECHO:
		icmpOutEchos++;
		break;
	case ICMP_ECHO_REPLY:
		icmpOutEchoReps++;
		break;
	case ICMP_INFO_RQST:
		break;
	case ICMP_INFO_REPLY:
		break;
	case ICMP_TIMESTAMP:
		icmpOutTimestamps++;
		break;
	case ICMP_TIME_REPLY:
		icmpOutTimestampReps++;
		icmp.args.echo.id = args->echo.id;
		icmp.args.echo.seq = args->echo.seq;
		break;
	case ICMP_ADDR_MASK:
		icmpOutAddrMasks++;
		break;
	case ICMP_ADDR_MASK_REPLY:
		icmpOutAddrMaskReps++;
		break;
	case ICMP_DEST_UNREACH:
		if(icmp.code == ICMP_FRAG_NEEDED)
			icmp.args.mtu = args->mtu;
		icmpOutDestUnreachs++;
		break;
	case ICMP_TIME_EXCEED:
		icmpOutTimeExcds++;
		break;
	case ICMP_QUENCH:
		icmpOutSrcQuenchs++;
		break;
	}
	icmpOutMsgs++;
	/* Now stick on the ICMP header */
	htonicmp(&icmp,&bp);
	return ip_send(INADDR_ANY,ip->source,ICMP_PTCL,ip->tos,0,&bp,length,0,0);
}
