/* Domain header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "domain.h"

static int dn_expand(uint8 *msg,uint8 *eom,uint8 *compressed,char *full,
	int fullen);
static uint8 *getq(struct rr **rrpp,uint8 *msg,uint8 *cp);
static uint8 *ntohrr(struct rr **rrpp,uint8 *msg,uint8 *cp);

int
ntohdomain(dhdr,bpp)
register struct dhdr *dhdr;
struct mbuf **bpp;
{
	uint16 tmp,len;
	register uint16 i;
	uint8 *msg,*cp;
	struct rr **rrpp;

	len = len_p(*bpp);
	msg = mallocw(len);
	pullup(bpp,msg,len);
	memset(dhdr,0,sizeof(*dhdr));

	dhdr->id = get16(&msg[0]);
	tmp = get16(&msg[2]);
	if(tmp & 0x8000)
		dhdr->qr = 1;
	dhdr->opcode = (tmp >> 11) & 0xf;
	if(tmp & 0x0400)
		dhdr->aa = 1;
	if(tmp & 0x0200)
		dhdr->tc = 1;
	if(tmp & 0x0100)
		dhdr->rd = 1;
	if(tmp & 0x0080)
		dhdr->ra = 1;
	dhdr->rcode = tmp & 0xf;
	dhdr->qdcount = get16(&msg[4]);
	dhdr->ancount = get16(&msg[6]);
	dhdr->nscount = get16(&msg[8]);
	dhdr->arcount = get16(&msg[10]);

	/* Now parse the variable length sections */
	cp = &msg[12];

	/* Question section */
	rrpp = &dhdr->questions;
	for(i=0;i<dhdr->qdcount;i++){
		if((cp = getq(rrpp,msg,cp)) == NULL){
			free(msg);
			return -1;
		}
		(*rrpp)->source = RR_QUESTION;
		rrpp = &(*rrpp)->next;
	}
	*rrpp = NULL;

	/* Answer section */
	rrpp = &dhdr->answers;
	for(i=0;i<dhdr->ancount;i++){
		if((cp = ntohrr(rrpp,msg,cp)) == NULL){
			free(msg);
			return -1;
		}
		(*rrpp)->source = RR_ANSWER;
		rrpp = &(*rrpp)->next;
	}
	*rrpp = NULL;

	/* Name server (authority) section */
	rrpp = &dhdr->authority;
	for(i=0;i<dhdr->nscount;i++){
		if((cp = ntohrr(rrpp,msg,cp)) == NULL){
			free(msg);
			return -1;
		}
		(*rrpp)->source = RR_AUTHORITY;
		rrpp = &(*rrpp)->next;
	}
	*rrpp = NULL;

	/* Additional section */
	rrpp = &dhdr->additional;
	for(i=0;i<dhdr->arcount;i++){
		if((cp = ntohrr(rrpp,msg,cp)) == NULL){
			free(msg);
			return -1;
		}
		(*rrpp)->source = RR_ADDITIONAL;
		rrpp = &(*rrpp)->next;
	}
	*rrpp = NULL;
	free(msg);
	return 0;
}
static uint8 *
getq(rrpp,msg,cp)
struct rr **rrpp;
uint8 *msg;
uint8 *cp;
{
	register struct rr *rrp;
	int len;
	char *name;

	*rrpp = rrp = (struct rr *)callocw(1,sizeof(struct rr));
	name = mallocw(512);
	len = dn_expand(msg,NULL,cp,name,512);
	if(len == -1){
		free(name);
		return NULL;
	}
	cp += len;
	rrp->name = strdup(name);
	rrp->type = get16(cp);
	cp += 2;
	rrp->class = get16(cp);
	cp += 2;
	rrp->ttl = 0;
	rrp->rdlength = 0;
	free(name);
	return cp;
}
/* Read a resource record from a domain message into a host structure */
static uint8 *
ntohrr(rrpp,msg,cp)
struct rr **rrpp; /* Where to allocate resource record structure */
uint8 *msg;	/* Pointer to beginning of domain message */
uint8 *cp;	/* Pointer to start of encoded RR record */
{
	register struct rr *rrp;
	int len;
	char *name;

	*rrpp = rrp = (struct rr *)callocw(1,sizeof(struct rr));
	name = mallocw(512);
	if((len = dn_expand(msg,NULL,cp,name,512)) == -1){
		free(name);
		return NULL;
	}
	cp += len;
	rrp->name = strdup(name);
	rrp->type = get16(cp);
	cp += 2;
	rrp->class = get16(cp);
	cp+= 2;
	rrp->ttl = get32(cp);
	cp += 4;
	rrp->rdlength = get16(cp);
	cp += 2;
	switch(rrp->type){
	case TYPE_A:
		/* Just read the address directly into the structure */
		rrp->rdata.addr = get32(cp);
		cp += 4;
		break;
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
		/* These types all consist of a single domain name;
		 * convert it to ascii format
		 */
		len = dn_expand(msg,NULL,cp,name,512);
		if(len == -1){
			free(name);
			return NULL;
		}
		rrp->rdata.name = strdup(name);
		rrp->rdlength = strlen(name);
		cp += len;
		break;
	case TYPE_HINFO:
		len = *cp++;
		rrp->rdata.hinfo.cpu = mallocw(len+1);
		strncpy(rrp->rdata.hinfo.cpu,(char *)cp,len);
		cp += len;

		len = *cp++;
		rrp->rdata.hinfo.os = mallocw(len+1);
		strncpy(rrp->rdata.hinfo.os,(char *)cp,len);
		cp += len;
		break;
	case TYPE_MX:
		rrp->rdata.mx.pref = get16(cp);
		cp += 2;
		/* Get domain name of exchanger */
		len = dn_expand(msg,NULL,cp,name,512);
		if(len == -1){
			free(name);
			return NULL;
		}
		rrp->rdata.mx.exch = strdup(name);
		cp += len;
		break;
	case TYPE_SOA:
		/* Get domain name of name server */
		len = dn_expand(msg,NULL,cp,name,512);
		if(len == -1){
			free(name);
			return NULL;
		}
		rrp->rdata.soa.mname = strdup(name);
		cp += len;

		/* Get domain name of responsible person */
		len = dn_expand(msg,NULL,cp,name,512);
		if(len == -1){
			free(name);
			return NULL;
		}
		rrp->rdata.soa.rname = strdup(name);
		cp += len;

		rrp->rdata.soa.serial = get32(cp);
		cp += 4;
		rrp->rdata.soa.refresh = get32(cp);
		cp += 4;
		rrp->rdata.soa.retry = get32(cp);
		cp += 4;
		rrp->rdata.soa.expire = get32(cp);
		cp += 4;
		rrp->rdata.soa.minimum = get32(cp);
		cp += 4;
		break;
	case TYPE_TXT:
		/* Just stash */
		rrp->rdata.data = mallocw(rrp->rdlength);
		memcpy(rrp->rdata.data,cp,rrp->rdlength);
		cp += rrp->rdlength;
		break;
	default:
		/* Ignore */
		cp += rrp->rdlength;
		break;
	}
	free(name);
	return cp;
}

/* Convert a compressed domain name to the human-readable form */
static int
dn_expand(msg,eom,compressed,full,fullen)
uint8 *msg;		/* Complete domain message */
uint8 *eom;
uint8 *compressed;	/* Pointer to compressed name */
char *full;		/* Pointer to result buffer */
int fullen;		/* Length of same */
{
	unsigned int slen;	/* Length of current segment */
	register uint8 *cp;
	int clen = 0;	/* Total length of compressed name */
	int indirect = 0;	/* Set if indirection encountered */
	int nseg = 0;		/* Total number of segments in name */

	cp = compressed;
	for(;;){
		slen = *cp++;	/* Length of this segment */
		if(!indirect)
			clen++;
		if((slen & 0xc0) == 0xc0){
			if(!indirect)
				clen++;
			indirect = 1;
			/* Follow indirection */
			cp = &msg[((slen & 0x3f)<<8) + *cp];
			slen = *cp++;
		}
		if(slen == 0)	/* zero length == all done */
			break;
		fullen -= slen + 1;
		if(fullen < 0)
			return -1;
		if(!indirect)
			clen += slen;
		while(slen-- != 0)
			*full++ = (char)*cp++;
		*full++ = '.';
		nseg++;
	}
	if(nseg == 0){
		/* Root name; represent as single dot */
		*full++ = '.';
		fullen--;
	}
	*full++ = '\0';
	fullen--;
	return clen;	/* Length of compressed message */
}


