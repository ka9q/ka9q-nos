/* mbuf (message buffer) primitives
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <dos.h>	/* TEMP */
#include "global.h"
#include "mbuf.h"
#include "proc.h"

static int32 Pushdowns;		/* Total calls to pushdown() */
static int32 Pushalloc;		/* Calls to pushalloc() that call malloc */
static int32 Allocmbufs;	/* Calls to alloc_mbuf() */
static int32 Freembufs;		/* Calls to free_mbuf() that actually free */
static int32 Cachehits;		/* Hits on free mbuf cache */
static unsigned long Msizes[16];

#define	SMALL_MBUF	32
#define	MED_MBUF	128
#define	LARGE_MBUF	2048

static struct mbuf *Mbufcache[3];

/* Allocate mbuf with associated buffer of 'size' bytes */
struct mbuf *
alloc_mbuf(uint16 size)
{
	struct mbuf *bp = NULL;
	int i;
	int i_state;

	Allocmbufs++;
	/* Record the size of this request */
	if((i = ilog2(size)) >= 0)
		Msizes[i]++;

	if(size <= SMALL_MBUF){
		i = 0;
		size = SMALL_MBUF;
	} else if(size <= MED_MBUF){
		i = 1;
		size = MED_MBUF;
	} else if(size <= LARGE_MBUF){
		i = 2;
		size = LARGE_MBUF;
	} else
		i = 3;

	if(i < 3){
		i_state = dirps();
		if(Mbufcache[i] != NULL){
			bp = Mbufcache[i];
			Mbufcache[i] = bp->anext;
			Cachehits++;
		}
		restore(i_state);
	}
	if(bp == NULL)
		bp = (struct mbuf *)malloc(size + sizeof(struct mbuf));
	if(bp == NULL)
		return NULL;
	/* Clear just the header portion */
	memset(bp,0,sizeof(struct mbuf));
	if((bp->size = size) != 0)
		bp->data = (uint8 *)(bp + 1);
	bp->refcnt++;
	return bp;
}
/* Allocate mbuf, waiting if memory is unavailable */
struct mbuf *
ambufw(uint16 size)
{
	struct mbuf *bp = NULL;
	int i,i_state;

	Allocmbufs++;
	if((i = ilog2(size)) >= 0)
		Msizes[i]++;

	if(size <= SMALL_MBUF){
		i = 0;
		size = SMALL_MBUF;
	} else if(size <= MED_MBUF){
		i = 1;
		size = MED_MBUF;
	} else if(size <= LARGE_MBUF){
		i = 2;
		size = LARGE_MBUF;
	} else
		i = 3;

	if(i < 3){
		i_state = dirps();
		if(Mbufcache[i] != NULL){
			bp = Mbufcache[i];
			Mbufcache[i] = bp->anext;
			Cachehits++;
		}
		restore(i_state);
	}
	if(bp == NULL)
		bp = (struct mbuf *)mallocw(size + sizeof(struct mbuf));

	/* Clear just the header portion */
	memset(bp,0,sizeof(struct mbuf));
	if((bp->size = size) != 0)
		bp->data = (uint8 *)(bp + 1);
	bp->refcnt++;
	return bp;
}

/* Decrement the reference pointer in an mbuf. If it goes to zero,
 * free all resources associated with mbuf.
 * Return pointer to next mbuf in packet chain
 */
struct mbuf *
free_mbuf(struct mbuf **bpp)
{
	struct mbuf *bpnext;
	struct mbuf *bptmp;
	struct mbuf *bp;
	int i_state;

	if(bpp == NULL || (bp = *bpp) == NULL)
		return NULL;

	*bpp = NULL;
	bpnext = bp->next;
	if(bp->dup != NULL){
		bptmp = bp->dup;
		bp->dup = NULL;	/* Nail it before we recurse */
		free_mbuf(&bptmp);	/* Follow indirection */
	}
	/* Decrement reference count. If it has gone to zero, free it. */
	if(--bp->refcnt <= 0){
		Freembufs++;

		i_state = dirps();
		switch(bp->size){
		case SMALL_MBUF:
			bp->anext = Mbufcache[0];
			Mbufcache[0] = bp;
			break;
		case MED_MBUF:
			bp->anext = Mbufcache[1];
			Mbufcache[1] = bp;
			break;
		case LARGE_MBUF:
			bp->anext = Mbufcache[2];
			Mbufcache[2] = bp;
			break;
		default:
			free(bp);
			break;
		}
		restore(i_state);
	}
	return bpnext;
}

/* Free packet (a chain of mbufs). Return pointer to next packet on queue,
 * if any
 */
struct mbuf *
free_p(struct mbuf **bpp)
{
	struct mbuf *bp;   
	register struct mbuf *abp;

	if(bpp == NULL || (bp = *bpp) == NULL)
		return NULL;
	abp = bp->anext;
	while(bp != NULL)
		bp = free_mbuf(&bp);
	*bpp = NULL;
	return abp;
}		
/* Free entire queue of packets (of mbufs) */
void
free_q(struct mbuf **q)
{
	struct mbuf *bp;

	while((bp = dequeue(q)) != NULL)
		free_p(&bp);
}

/* Count up the total number of bytes in a packet */
uint16
len_p(struct mbuf *bp)
{
	register uint16 cnt = 0;

	while(bp != NULL){
		cnt += bp->cnt;
		bp = bp->next;
	}
	return cnt;
}
/* Count up the number of packets in a queue */
uint16
len_q(struct mbuf *bp)
{
	register uint16 cnt;

	for(cnt=0;bp != NULL;cnt++,bp = bp->anext)
		;
	return cnt;
}
/* Trim mbuf to specified length by lopping off end */
void
trim_mbuf(struct mbuf **bpp,uint16 length)
{
	register uint16 tot = 0;
	register struct mbuf *bp;

	if(bpp == NULL || *bpp == NULL)
		return;	/* Nothing to trim */

	if(length == 0){
		/* Toss the whole thing */
		free_p(bpp);
		return;
	}
	/* Find the point at which to trim. If length is greater than
	 * the packet, we'll just fall through without doing anything
	 */
	for( bp = *bpp; bp != NULL; bp = bp->next){
		if(tot + bp->cnt < length){
			tot += bp->cnt;
		} else {
			/* Cut here */
			bp->cnt = length - tot;
			free_p(&bp->next);
			bp->next = NULL;
			break;
		}
	}
}
/* Duplicate/enqueue/dequeue operations based on mbufs */

/* Duplicate first 'cnt' bytes of packet starting at 'offset'.
 * This is done without copying data; only the headers are duplicated,
 * but without data segments of their own. The pointers are set up to
 * share the data segments of the original copy. The return pointer is
 * passed back through the first argument, and the return value is the
 * number of bytes actually duplicated.
 */
uint16
dup_p(
struct mbuf **hp,
register struct mbuf *bp,
register uint16 offset,
register uint16 cnt
){
	struct mbuf *cp;
	uint16 tot;

	if(cnt == 0 || bp == NULL || hp == NULL){
		if(hp != NULL)
			*hp = NULL;
		return 0;
	}
	if((*hp = cp = alloc_mbuf(0)) == NULL){
		return 0;
	}
	/* Skip over leading mbufs that are smaller than the offset */
	while(bp != NULL && bp->cnt <= offset){
		offset -= bp->cnt;
		bp = bp->next;
	}
	if(bp == NULL){
		free_mbuf(&cp);
		*hp = NULL;
		return 0;	/* Offset was too big */
	}
	tot = 0;
	for(;;){
		/* Make sure we get the original, "real" buffer (i.e. handle the
		 * case of duping a dupe)
		 */
		if(bp->dup != NULL)
			cp->dup = bp->dup;
		else
			cp->dup = bp;

		/* Increment the duplicated buffer's reference count */
		cp->dup->refcnt++;

		cp->data = bp->data + offset;
		cp->cnt = min(cnt,bp->cnt - offset);
		offset = 0;
		cnt -= cp->cnt;
		tot += cp->cnt;
		bp = bp->next;
		if(cnt == 0 || bp == NULL || (cp->next = alloc_mbuf(0)) == NULL)
			break;
		cp = cp->next;
	}
	return tot;
}
/* Copy first 'cnt' bytes of packet into a new, single mbuf */
struct mbuf *
copy_p(
register struct mbuf *bp,
register uint16 cnt
){
	register struct mbuf *cp;
	register uint8 *wp;
	register uint16 n;

	if(bp == NULL || cnt == 0 || (cp = alloc_mbuf(cnt)) == NULL)
		return NULL;
	wp = cp->data;
	while(cnt != 0 && bp != NULL){
		n = min(cnt,bp->cnt);
		memcpy(wp,bp->data,n);
		wp += n;
		cp->cnt += n;
		cnt -= n;
		bp = bp->next;
	}
	return cp;
}
/* Copy and delete "cnt" bytes from beginning of packet. Return number of
 * bytes actually pulled off
 */
uint16
pullup(
struct mbuf **bph,
void *buf,
uint16 cnt
){
	struct mbuf *bp;
	uint16 n,tot;
	uint8 *obp = buf;

	tot = 0;
	if(bph == NULL)
		return 0;
	while(cnt != 0 && (bp = *bph) != NULL){
		n = min(cnt,bp->cnt);
		if(obp != NULL){
			if(n == 1){	/* Common case optimization */
				*obp++ = *bp->data;
			} else if(n > 1){
				memcpy(obp,bp->data,n);
				obp += n;
			}
		}
		tot += n;
		cnt -= n;
		bp->data += n;
		bp->cnt -= n;		
		if(bp->cnt == 0){
			/* If this is the last mbuf of a packet but there
			 * are others on the queue, return a pointer to
			 * the next on the queue. This allows pullups to
			 * to work on a packet queue
			 */
			if(bp->next == NULL && bp->anext != NULL){
				*bph = bp->anext;
				free_mbuf(&bp);
			} else
				*bph = free_mbuf(&bp);
		}
	}
	return tot;
}
/* Copy data from within mbuf to user-provided buffer, starting at
 * 'offset' bytes from start of mbuf and copying no more than 'len'
 * bytes. Return actual number of bytes copied
 */
uint16
extract(
struct mbuf *bp,
uint16 offset,
void *buf,
uint16 len
){
	uint8 *obp = buf;
	uint16 copied = 0;
	uint16 n;

	/* Skip over offset if greater than first mbuf(s) */
	while(bp != NULL && offset >= bp->cnt){
		offset -= bp->cnt;
		bp = bp->next;
	}
	while(bp != NULL && len != 0){
		n = min(len,bp->cnt - offset);	/* offset must be < bp->cnt */
		memcpy(obp,bp->data+offset,n);
		copied += n;
		obp += n;
		len -= n;
		if(n + offset == bp->cnt)
			bp = bp->next;	/* Buffer exhausted, get next */
		offset = 0;		/* No more offset after first */
	}
	return copied;
}
/* Append mbuf to end of mbuf chain */
void
append(
struct mbuf **bph,
struct mbuf **bpp
){
	register struct mbuf *p;

	if(bph == NULL || bpp == NULL || *bpp == NULL)
		return;

	if(*bph == NULL){
		/* First one on chain */
		*bph = *bpp;
	} else {
		for(p = *bph ; p->next != NULL ; p = p->next)
			;
		p->next = *bpp;
	}
	*bpp = NULL;	/* We've consumed it */
}
/* Insert specified amount of contiguous new space at the beginning of an
 * mbuf chain. If enough space is available in the first mbuf, no new space
 * is allocated. Otherwise a mbuf of the appropriate size is allocated and
 * tacked on the front of the chain.
 *
 * This operation is the logical inverse of pullup(), hence the name.
 */
void
pushdown(struct mbuf **bpp,void *buf,uint16 size)
{
	struct mbuf *bp;

	Pushdowns++;
	if(bpp == NULL)
		return;
	/* Check that bp is real, that it hasn't been duplicated, and
	 * that it itself isn't a duplicate before checking to see if
	 * there's enough space at its front.
	 */
	if((bp = *bpp) != NULL && bp->refcnt == 1 && bp->dup == NULL
	 && bp->data - (uint8 *)(bp+1) >= size){
		/* No need to alloc new mbuf, just adjust this one */
		bp->data -= size;
		bp->cnt += size;
	} else {
		(*bpp) = ambufw(size);
		(*bpp)->next = bp;
		bp = *bpp;
		bp->cnt = size;
		Pushalloc++;
	}
	if(buf != NULL)
		memcpy(bp->data,buf,size);
}
/* Append packet to end of packet queue */
void
enqueue(
struct mbuf **q,
struct mbuf **bpp
){
	register struct mbuf *p;
	uint8 i_state;

	if(q == NULL || bpp == NULL || *bpp == NULL)
		return;
	i_state = dirps();
	if(*q == NULL){
		/* List is empty, stick at front */
		*q = *bpp;
	} else {
		for(p = *q ; p->anext != NULL ; p = p->anext)
			;
		p->anext = *bpp;
	}
	*bpp = NULL;	/* We've consumed it */
	restore(i_state);
	ksignal(q,1);
}
/* Unlink a packet from the head of the queue */
struct mbuf *
dequeue(struct mbuf **q)
{
	register struct mbuf *bp;
	uint8 i_state;

	if(q == NULL)
		return NULL;
	i_state = dirps();
	if((bp = *q) != NULL){
		*q = bp->anext;
		bp->anext = NULL;
	}
	restore(i_state);
	return bp;
}	

/* Copy user data into an mbuf */
struct mbuf *
qdata(void *data,uint16 cnt)
{
	register struct mbuf *bp;

	bp = ambufw(cnt);
	memcpy(bp->data,data,cnt);
	bp->cnt = cnt;
	return bp;
}
/* Pull a 32-bit integer in host order from buffer in network byte order.
 * On error, return 0. Note that this is indistinguishable from a normal
 * return.
 */
int32
pull32(struct mbuf **bpp)
{
	uint8 buf[4];

	if(pullup(bpp,buf,4) != 4){
		/* Return zero if insufficient buffer */
		return 0;
	}
	return get32(buf);
}
/* Pull a 16-bit integer in host order from buffer in network byte order.
 * Return -1 on error
 */
long
pull16(struct mbuf **bpp)
{
	uint8 buf[2];

	if(pullup(bpp,buf,2) != 2){
		return -1;		/* Nothing left */
	}
	return get16(buf);
}
/* Pull single byte from mbuf */
int
pull8(struct mbuf **bpp)
{
	uint8 c;

	if(pullup(bpp,&c,1) != 1)
		return -1;		/* Nothing left */
	return c;
}
int
write_p(FILE *fp,struct mbuf *bp)
{
	while(bp != NULL){
		if(fwrite(bp->data,1,bp->cnt,fp) != bp->cnt)
			return -1;
		bp = bp->next;
	}
	return 0;
}
/* Reclaim unused space in a mbuf chain. If the argument is a chain of mbufs
 * and/or it appears to have wasted space, copy it to a single new mbuf and
 * free the old mbuf(s). But refuse to move mbufs that merely
 * reference other mbufs, or that have other headers referencing them.
 *
 * Be extremely careful that there aren't any other pointers to
 * (or into) this mbuf, since we have no way of detecting them here.
 * This function is meant to be called only when free memory is in
 * short supply.
 */
void
mbuf_crunch(struct mbuf **bpp)
{
	struct mbuf *bp = *bpp;
	struct mbuf *nbp;

	if(bp->refcnt > 1 || bp->dup != NULL){
		/* Can't crunch, there are other refs */
		return;
	}
	if(bp->next == NULL && bp->cnt == bp->size){
		/* Nothing to be gained by crunching */
		return;
	}
	if((nbp = copy_p(bp,len_p(bp))) == NULL){
		/* Copy failed due to lack of (contiguous) space */
		return;
	}
	nbp->anext = bp->anext;
	free_p(&bp);
	*bpp = nbp;
}
void
mbufstat(void)
{
	printf("mbuf allocs %lu free cache hits %lu (%lu%%) mbuf frees %lu\n",
	 Allocmbufs,Cachehits,100*Cachehits/Allocmbufs,Freembufs);
	printf("pushdown calls %lu pushdown calls to alloc_mbuf %lu\n",
	 Pushdowns,Pushalloc);
	printf("Free cache: small %u medium %u large %u\n",
	 len_q(Mbufcache[0]),len_q(Mbufcache[1]),len_q(Mbufcache[2]));
}
void
mbufsizes(void)
{
	int i;

	printf("Mbuf sizes:\n");
	for(i=0;i<16;i += 4){
		printf("N>=%5u:%7ld| N>=%5u:%7ld| N>=%5u:%7ld| N>=%5u:%7ld\n",
		 1<<i,Msizes[i],2<<i,Msizes[i+1],
		 4<<i,Msizes[i+2],8<<i,Msizes[i+3]);
	}
}
/* Mbuf garbage collection - return all mbufs on free cache to heap */
void
mbuf_garbage(int red)
{
	int i_state;
	int i;
	struct mbuf *bp;

	/* Blow entire cache */
	for(i=0;i<3;i++){
		i_state = dirps();		
		while((bp = Mbufcache[i]) != NULL){
			Mbufcache[i] = bp->anext;
			free(bp);
		}
		restore(i_state);
	}
}
