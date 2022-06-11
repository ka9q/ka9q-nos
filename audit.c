/* Routines for auditing mbuf consistency. Not used for some time, may
 * not be up to date.
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "proc.h"
#include "mbuf.h"

extern uint8 _Uend;
extern int _STKRED;

union header {
	struct {
		union header *ptr;
		unsigned size;
	} s;
	long l;
};

void audit(struct mbuf *bp,char *file,int line);
static void audit_mbuf(struct mbuf *bp,char *file,int line);
static void dumpbuf(struct mbuf *bp);

/* Perform sanity checks on mbuf. Print any errors, return 0 if none,
 * nonzero otherwise
 */
void
audit(
struct mbuf *bp,
char *file,
int line
){
	register struct mbuf *bp1;

	for(bp1 = bp;bp1 != NULL; bp1 = bp1->next)
		audit_mbuf(bp1,file,line);
}

static void
audit_mbuf(
struct mbuf *bp,
char *file,
int line
){
	union header *blk;
	uint8 *bufstart,*bufend;
	uint16 overhead = sizeof(union header) + sizeof(struct mbuf);
	uint16 datasize;
	int errors = 0;
	uint8 *heapbot,*heaptop;

	if(bp == NULL)
		return;

	heapbot = &_Uend;
	heaptop = (uint8 *) -_STKRED;

	/* Does buffer appear to be a valid malloc'ed block? */
	blk = ((union header *)bp) - 1;
	if(blk->s.ptr != blk){
		printf("Garbage bp %lx\n",(long)bp);
		errors++;
	}
	if((datasize = blk->s.size*sizeof(union header) - overhead) != 0){
		/* mbuf has data area associated with it, verify that
		 * pointers are within it
		 */
		bufstart = (uint8 *)(bp + 1);
		bufend = bufstart + datasize;
		if(bp->data < bufstart){
			printf("Data pointer before buffer\n");
			errors++;
		}
		if(bp->data + bp->cnt > bufend){
			printf("Data pointer + count past bounds\n");
			errors++;
		}
	} else {
		/* Dup'ed mbuf, at least check that pointers are within
		 * heap area
		*/

		if(bp->data < heapbot
		 || bp->data + bp->cnt > heaptop){
			printf("Data outside heap\n");
			errors++;
		}
	}
	/* Now check link list pointers */
	if(bp->next != NULL && ((bp->next < (struct mbuf *)heapbot)
		 || bp->next > (struct mbuf *)heaptop)){
			printf("next pointer out of limits\n");
			errors++;
	}
	if(bp->anext != NULL && ((bp->anext < (struct mbuf *)heapbot)
		 || bp->anext > (struct mbuf *)heaptop)){
			printf("anext pointer out of limits\n");
			errors++;
	}
	if(errors != 0){
		dumpbuf(bp);
		printf("PANIC: buffer audit failure in %s line %d\n",file,line);
		fflush(stdout);
		for(;;)
			;
	}
	return;
}

static void
dumpbuf(struct mbuf *bp)
{
	union header *blk;
	if(bp == NULL){
		printf("NULL BUFFER\n");
		return;
	}
	blk = ((union header *)bp) - 1;
	printf("bp %lx tot siz %u data %lx cnt %u next %lx anext %lx\n",
		(long)bp,blk->s.size * sizeof(union header),
		(long)bp->data,bp->cnt,
		(long)bp->next,(long)bp->anext);
}

