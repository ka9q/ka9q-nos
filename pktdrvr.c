/* Driver for FTP Software's packet driver interface. (PC specific code)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "proc.h"
#include "mbuf.h"
#include "netuser.h"
#include "enet.h"
#include "arcnet.h"
#include "ax25.h"
#include "slip.h"
#include "kiss.h"
#include "iface.h"
#include "arp.h"
#include "trace.h"
#include "pktdrvr.h"
#include "config.h"
#include "devparam.h"

static long access_type(int intno,int if_class,int if_type,
	int if_number, uint8 *type,unsigned typelen,
	INTERRUPT (*receiver)(void) );
static int driver_info(int intno,int handle,int *version,
	int *class,int *type,int *number,int *basic);
static int release_type(int intno,int handle);
static int get_address(int intno,int handle,uint8 *buf,int len);
static int set_rcv_mode(int intno,int handle,int mode);
static int pk_raw(struct iface *iface,struct mbuf **bpp);
static int pk_stop(struct iface *iface);
static int send_pkt(int intno,uint8 *buffer,unsigned length);


static INTERRUPT (*Pkvec[])() = { pkvec0,pkvec1,pkvec2 };
static struct pktdrvr Pktdrvr[PK_MAX];
static int Derr;
char Pkt_sig[] = "PKT DRVR";	/* Packet driver signature */

/*
 * Send routine for packet driver
 */

int
pk_send(
struct mbuf **bpp,	/* Buffer to send */
struct iface *iface,	/* Pointer to interface control block */
int32 gateway,		/* Ignored  */
uint8 tos
){
	if(iface == NULL){
		free_p(bpp);
		return -1;
	}
	return (*iface->raw)(iface,bpp);
}

/* Send raw packet (caller provides header) */
static int
pk_raw(
struct iface *iface,	/* Pointer to interface control block */
struct mbuf **bpp	/* Data field */
){
	register struct pktdrvr *pp;
	uint16 size;
	struct mbuf *bp1;

	iface->rawsndcnt++;
	iface->lastsent = secclock();

	dump(iface,IF_TRACE_OUT,*bpp);
	pp = &Pktdrvr[iface->dev];
	size = len_p(*bpp);

	/* Perform class-specific processing, if any */
	switch(pp->class){
	case CL_ETHERNET:
		if(size < RUNT){
			/* Pad the packet out to the minimum */
#ifdef	SECURE
			/* This option copies the packet to a new mbuf,
			 * padded out with zeros. Otherwise we just lie
			 * to the packet driver about the length, and it
			 * will spit out bytes beyond the end of the mbuf
			 * that might be compromising. The cost is another
			 * alloc, free and copy.
			 */
			bp1 = ambufw(RUNT);
			bp1->cnt = RUNT;
			memset(bp1->data+size,0,RUNT-size);
			pullup(bpp,bp1->data,size);
			free_p(bpp);	/* Shouldn't be necessary */
			*bpp = bp1;
#endif
			size = RUNT;
		}
		break;
	case CL_KISS:
		/* This *really* shouldn't be done here, but it was the
		 * easiest way. Put the type field for KISS TNC on front.
		 */
		pushdown(bpp,NULL,1);
		(*bpp)->data[0] = PARAM_DATA;
		size++;
		break;
	}
	if((*bpp)->next != NULL){
		/* Copy to contiguous buffer, since driver can't handle mbufs */
		bp1 = copy_p(*bpp,size);
		free_p(bpp);
		*bpp = bp1;
		if(*bpp == NULL)
			return -1;
	}
	send_pkt(pp->intno,(*bpp)->data,size);
	free_p(bpp);
	return 0;
}

/* Packet driver receive upcall routine. Called by the packet driver TSR
 * twice for each incoming packet: first with ax == 0 to allocate a buffer,
 * and then with ax == 1 to signal completion of the copy.
 *
 * The packet driver actually calls an assembler hook (pkvec* in pkvec.s)
 * that passes the driver's ax and cx registers to us as args.
 * It then passes our return value back to the packet driver in es:di.
 *
 * Note that pushing es and di to us as args that we can modify only
 * works reliably when the function is of type "interrupt". Otherwise the
 * compiler can cache the args in registers and optimize out the stores back
 * into the stack since C args are normally call-by-value.
 */
uint8 *
pkint(
int dev,
unsigned short cx,
unsigned short ax
){
	register struct pktdrvr *pp;
	uint8 *retval = NULL;	

	if(dev < 0 || dev >= PK_MAX)
		return NULL;	/* Unknown device */
	pp = &Pktdrvr[dev];
	if(pp->iface == NULL)
		return NULL;	/* Unknown device */
	switch(ax){
	case 0:	/* Space allocate call */
		if((pp->buffer = alloc_mbuf(cx+sizeof(struct iface *))) != NULL){
			pp->buffer->data += sizeof(struct iface *);
			pp->buffer->cnt = cx;
			retval = pp->buffer->data;
		}
		break;
	case 1:	/* Packet complete call */
		net_route(pp->iface,&pp->buffer);
		break;
	default:
		break;
	}
	return retval;
}

/* Shut down the packet interface */
static int
pk_stop(
struct iface *iface
){
	struct pktdrvr *pp;

	pp = &Pktdrvr[iface->dev];
	/* Call driver's release_type() entry */
	if(release_type(pp->intno,pp->handle1) == -1)
		printf("%s: release_type error code %u\n",iface->name,Derr);

	if(pp->class == CL_ETHERNET || pp->class == CL_ARCNET){
		release_type(pp->intno,pp->handle2);
		release_type(pp->intno,pp->handle3);
	}
	pp->iface = NULL;
	return 0;
}
/* Attach a packet driver to the system
 * argv[0]: hardware type, must be "packet"
 * argv[1]: software interrupt vector, e.g., x7e
 * argv[2]: interface label, e.g., "trw0"
 * argv[3]: maximum number of packets allowed on transmit queue, e.g., "5"
 * argv[4]: maximum transmission unit, bytes, e.g., "1500"
 * argv[5]: IP address (optional)
 */
int
pk_attach(
int argc,
char *argv[],
void *p
){
	register struct iface *if_pk;
	int class,type;
	unsigned int intno;
	static uint8 iptype[] = {IP_TYPE >> 8,IP_TYPE};
	static uint8 arptype[] = {ARP_TYPE >> 8,ARP_TYPE};
	static uint8 revarptype[] = {REVARP_TYPE >> 8, REVARP_TYPE};
	long handle;
	int i;
#ifdef	ARCNET
	static uint8 arcip[] = {ARC_IP};
	static uint8 arcarp[] = {ARC_ARP};
#endif

	long drvvec;
	char sig[8];	/* Copy of driver signature "PKT DRVR" */
	register struct pktdrvr *pp;
	char tmp[25];
	char *cp;

	for(i=0;i<PK_MAX;i++){
		if(Pktdrvr[i].iface == NULL)
			break;
	}
	if(i >= PK_MAX){
		printf("Too many packet drivers\n");
		return -1;
	}
	if(if_lookup(argv[2]) != NULL){
		printf("Interface %s already exists\n",argv[2]);
		return -1;
	}

	intno = htoi(argv[1]);
	/* Verify that there's really a packet driver there, so we don't
	 * go off into the ozone (if there's any left)
	 */
	drvvec = (long)getvect(intno);
	movblock(FP_OFF(drvvec)+3, FP_SEG(drvvec),
		FP_OFF(sig),FP_SEG(sig),strlen(Pkt_sig));
	if(strncmp(sig,Pkt_sig,strlen(Pkt_sig)) != 0){
		printf("No packet driver loaded at int 0x%x\n",intno);
		return -1;
	}
	if_pk = (struct iface *)callocw(1,sizeof(struct iface));
	if_pk->name = strdup(argv[2]);
	if(argc > 5)
		if_pk->addr = resolve(argv[5]);
	else
		if_pk->addr = Ip_addr;
	pp = &Pktdrvr[i];
	if_pk->mtu = atoi(argv[4]);
	if_pk->dev = i;
	if_pk->raw = pk_raw;
	if_pk->stop = pk_stop;
	pp->intno = intno;
	pp->iface = if_pk;

 	/* Version 1.08 of the packet driver spec dropped the handle
 	 * requirement from the driver_info call.  However, if we are using
 	 * a version 1.05 packet driver, the following call will fail.
  	 */
 	if(driver_info(intno,-1,NULL,&class,&type,NULL,NULL) < 0){
		/* Find out by exhaustive search what class this driver is (ugh) */
		for(class=1;class<=NCLASS;class++){
			/* Store handle in temp long so we can tell an
			 * error return (-1) from a handle of 0xffff
			 */
			handle = access_type(intno,class,ANYTYPE,0,iptype,2,
				Pkvec[if_pk->dev]);
			if(handle != -1 || Derr == TYPE_INUSE){
				pp->handle1 = handle;
				break;
			}
		}
		/* Now that we know, release it and do it all over again with the
		 * right type fields
		 */
		release_type(intno,pp->handle1);
	}
	switch(class){
	case CL_ETHERNET:
		pp->handle1 = access_type(intno,class,ANYTYPE,0,iptype,2,
			Pkvec[if_pk->dev]);
		pp->handle2 = access_type(intno,class,ANYTYPE,0,arptype,2,
			Pkvec[if_pk->dev]);
		pp->handle3 = access_type(intno,class,ANYTYPE,0,revarptype,2,
			Pkvec[if_pk->dev]);
		setencap(if_pk,"Ethernet");

		/**** temp set multicast flag ****/
/*		i = set_rcv_mode(intno,pp->handle1,5);
		printf("set_rcv_mode returns %d, Derr = %d\n",i,Derr); */

		/* Get hardware Ethernet address from driver */
		if_pk->hwaddr = mallocw(EADDR_LEN);
		get_address(intno,pp->handle1,if_pk->hwaddr,EADDR_LEN);
		if(if_pk->hwaddr[0] & 1){
			printf("Warning! Interface '%s' has a multicast address:",
			 if_pk->name);
			printf(" (%s)\n",
			 (*if_pk->iftype->format)(tmp,if_pk->hwaddr));
		}
		break;
#ifdef	ARCNET
	case CL_ARCNET:
		pp->handle1 = access_type(intno,class,ANYTYPE,0,arcip,1,
			Pkvec[if_pk->dev]);
		pp->handle2 = access_type(intno,class,ANYTYPE,0,arcarp,1,
			Pkvec[if_pk->dev]);
		if_pk->output = anet_output;
		/* Get hardware ARCnet address from driver */
		if_pk->hwaddr = mallocw(AADDR_LEN);
		get_address(intno,pp->handle1,if_pk->hwaddr,AADDR_LEN);
		break;
#endif
	case CL_SERIAL_LINE:
		pp->handle1 = access_type(intno,class,ANYTYPE,0,NULL,0,
		 Pkvec[if_pk->dev]);
		setencap(if_pk,"SLIP");
		break;
#ifdef	AX25
	case CL_KISS:	/* Note that the raw routine puts on the command */
	case CL_AX25:
		pp->handle1 = access_type(intno,class,ANYTYPE,0,NULL,0,
		 Pkvec[if_pk->dev]);
		setencap(if_pk,"AX25");
		if_pk->hwaddr = mallocw(AXALEN);
		memcpy(if_pk->hwaddr,Mycall,AXALEN);
		break;
#endif
	case CL_SLFP:
		pp->handle1 = access_type(intno,class,ANYTYPE,0,NULL,0,
		 Pkvec[if_pk->dev]);
		setencap(if_pk,"SLFP");
		get_address(intno,pp->handle1,(uint8 *)&if_pk->addr,4);
		break;
	default:
		printf("Packet driver has unsupported class %u\n",class);
		free(if_pk->name);
		free(if_pk);
		return -1;
	}
	pp->class = class;
	if_pk->next = Ifaces;
	Ifaces = if_pk;
	cp = if_name(if_pk," tx");
	if_pk->txproc = newproc(cp,768,if_tx,if_pk->dev,if_pk,NULL,0);
	free(cp);

	return 0;
}
static long
access_type(
int intno,
int if_class,
int if_type,
int if_number,
uint8 *type,
unsigned typelen,
INTERRUPT (*receiver)()
){
	union REGS regs;
	struct SREGS sregs;

	segread(&sregs);
	regs.h.dl = if_number;		/* Number */
	sregs.ds = FP_SEG(type);	/* Packet type template */
	regs.x.si = FP_OFF(type);
	regs.x.cx = typelen;		/* Length of type */
	sregs.es = FP_SEG(receiver);	/* Address of receive handler */
	regs.x.di = FP_OFF(receiver);
	regs.x.bx = if_type;		/* Type */
	regs.h.ah = ACCESS_TYPE;	/* Access_type() function */
	regs.h.al = if_class;		/* Class */
	int86x(intno,&regs,&regs,&sregs);
	if(regs.x.cflag){
		Derr = regs.h.dh;
		return -1;
	} else
		return regs.x.ax;
}
static int
release_type(
int intno,
int handle
){
	union REGS regs;

	regs.x.bx = handle;
	regs.h.ah = RELEASE_TYPE;
	int86(intno,&regs,&regs);
	if(regs.x.cflag){
		Derr = regs.h.dh;
		return -1;
	} else
		return 0;
}
static int
send_pkt(
int intno,
uint8 *buffer,
unsigned length
){
	union REGS regs;
	struct SREGS sregs;

	segread(&sregs);
	sregs.ds = FP_SEG(buffer);
	sregs.es = FP_SEG(buffer); /* for buggy univation pkt driver - CDY */
	regs.x.si = FP_OFF(buffer);
	regs.x.cx = length;
	regs.h.ah = SEND_PKT;
	int86x(intno,&regs,&regs,&sregs);
	if(regs.x.cflag){
		Derr = regs.h.dh;
		return -1;
	} else
		return 0;
}
static int
driver_info(
int intno,
int handle,
int *version,
int *class,
int *type,
int *number,
int *basic
){
	union REGS regs;

	regs.x.bx = handle;
	regs.h.ah = DRIVER_INFO;
	regs.h.al = 0xff;
	int86(intno,&regs,&regs);
	if(regs.x.cflag){
		Derr = regs.h.dh;
		return -1;
	}
	if(version != NULL)
		*version = regs.x.bx;
	if(class != NULL)
		*class = regs.h.ch;
	if(type != NULL)
		*type = regs.x.dx;
	if(number != NULL)
		*number = regs.h.cl;
	if(basic != NULL)
		*basic = regs.h.al;
	return 0;
}
static int
get_address(
int intno,
int handle,
uint8 *buf,
int len
){
	union REGS regs;
	struct SREGS sregs;

	segread(&sregs);
	sregs.es = FP_SEG(buf);
	regs.x.di = FP_OFF(buf);
	regs.x.cx = len;
	regs.x.bx = handle;
	regs.h.ah = GET_ADDRESS;
	int86x(intno,&regs,&regs,&sregs);
	if(regs.x.cflag){
		Derr = regs.h.dh;
		return -1;
	}
	return 0;
}
static int
set_rcv_mode(
int intno,
int handle,
int mode
){
	union REGS regs;
	struct SREGS sregs;

	segread(&sregs);
	regs.x.cx = mode;
	regs.x.bx = handle;
	regs.h.ah = SET_RCV_MODE;
	int86x(intno,&regs,&regs,&sregs);
	if(regs.x.cflag){
		Derr = regs.h.dh;
		return -1;
	}
	return 0;
}

