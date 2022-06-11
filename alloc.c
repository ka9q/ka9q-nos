/* memory allocation routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Adapted from alloc routine in K&R; memory statistics and interrupt
 * protection added for use with net package. Must be used in place of
 * standard Turbo-C library routines because the latter check for stack/heap
 * collisions. This causes erroneous failures because process stacks are
 * allocated off the heap.
 */

#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "cmdparse.h"

static unsigned long Memfail;	/* Count of allocation failures */
static unsigned long Allocs;	/* Total allocations */
static unsigned long Frees;	/* Total frees */
static unsigned long Invalid;	/* Total calls to free with garbage arg */
static int Memwait;		/* Number of tasks waiting for memory */
static unsigned long Yellows;	/* Yellow alert garbage collections */
static unsigned long Reds;	/* Red alert garbage collections */
unsigned long Availmem;		/* Heap memory, ABLKSIZE units */
static unsigned long Morecores;
static int Memdebug;
static unsigned long Sizes[16];

/* This debugging pattern MUST be exactly equal in size to the "header"
 * union defined later
 */
static char Debugpat[] = { 0xfe,0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef };

static int domdebug(int argc,char *argv[],void *ptr);
static int dostat(int argc,char *argv[],void *p);
static int dofreelist(int argc,char *argv[],void *p);
static int dothresh(int argc,char *argv[],void *p);
static int dosizes(int argc,char *argv[],void *p);

struct cmds Memcmds[] = {
	"debug",	domdebug,	0, 0, NULL,
	"freelist",	dofreelist,	0, 0, NULL,
	"sizes",	dosizes,	0, 0, NULL,
	"status",	dostat,		0, 0, NULL,
	"thresh",	dothresh,	0, 0, NULL,
	NULL,
};

#ifdef	LARGEDATA
#define	HUGE	huge
#else
#define	HUGE
#endif

union header {
	struct {
		union header HUGE *ptr;
		unsigned long size;
	} s;
	char c[8];	/* For debugging; also ensure size is 8 bytes */
};

typedef union header HEADER;

#define	ABLKSIZE	(sizeof (HEADER))
#define	BTOU(nb)	((((nb) + ABLKSIZE - 1) / ABLKSIZE) + 1)

static HEADER HUGE *morecore(unsigned nu);

static HEADER Base;
static HEADER HUGE *Allocp = NULL;
static unsigned long Heapsize;


/* Memory blocks obtained from MS-DOS by allocmem() call */
struct sysblock {
	unsigned seg;
	unsigned npar;
};
#define	NSYSBLOCK	5
struct sysblock Sysblock[NSYSBLOCK];

/* Allocate block of 'nb' bytes */
void *
malloc(nb)
size_t nb;
{
	int i;
	int i_state;
	register HEADER HUGE *p, HUGE *q;
	register unsigned nu;

	if(nb == 0)
		return NULL;

	Allocs++;
	/* Record the size of this request */
	if((i = ilog2(nb)) >= 0)
		Sizes[i]++;
	
	/* Round up to full block, then add one for header */
	nu = BTOU(nb);

	i_state = dirps();
	/* Initialize heap pointers if necessary */
	if((q = Allocp) == NULL){
		Base.s.ptr = Allocp = q = &Base;
		Base.s.size = 1;
	}
	/* Search heap list */
	for(p = q->s.ptr; ; q = p, p = p->s.ptr){
		if(p->s.size >= nu){
			/* This chunk is at least as large as we need */
			if(p->s.size <= nu + 1){
				/* This is either a perfect fit (size == nu)
				 * or the free chunk is just one unit larger.
				 * In either case, alloc the whole thing,
				 * because there's no point in keeping a free
				 * block only large enough to hold the header.
				 */
				q->s.ptr = p->s.ptr;
			} else {
				/* Carve out piece from end of entry */
				p->s.size -= nu;
				p += p->s.size;
				p->s.size = nu;
			}
#ifdef	circular
			Allocp = q;
#endif
			p->s.ptr = p;	/* for auditing */
			Availmem -= p->s.size;
			p++;
			break;
		}
		/* We've searched all the way around the list without
		 * finding anything. Try to get more core from the system,
		 * unless we're in an interrupt handler
		 */
		if(p == Allocp && (!i_state || (p = morecore(nu)) == NULL)){
			p = NULL;
			Memfail++;
			break;
		}
	}
	restore(i_state);
#ifdef	LARGEDATA
	/* On the brain-damaged Intel CPUs in "large data" model,
	 * make sure the pointer's offset field isn't null
	 * (unless the entire pointer is null).
	 * The Turbo C compiler and certain
	 * library functions like strrchr() assume this.
	 */
	if(FP_OFF(p) == 0 && FP_SEG(p) != 0){
		/* Return denormalized but equivalent pointer */
		return (void *)MK_FP(FP_SEG(p)-1,16);
	}
#endif
	return (void *)p;
}
/* Get more memory from the system and put it on the heap */
static HEADER HUGE *
morecore(nu)
unsigned nu;
{
	char HUGE *cp;
	HEADER HUGE *up;
	unsigned size;
	unsigned segp;
	unsigned npar;
	struct sysblock *sp;
	int i;
	void *sbrk(int);	/***/

	Morecores++;
	size = nu * ABLKSIZE;
	/* First try to expand our main memory block */
	if((int)(cp = (char HUGE *)sbrk(size)) != -1){
		up = (HEADER *)cp;
		up->s.size = nu;
		up->s.ptr = up;	/* satisfy audit */
		free(up + 1);
		Heapsize += size;
		Frees--;	/* Nullify increment inside free() */
		return Allocp;
	}
#ifndef	__GNUC__
	/* That failed; the main memory block must have grown into another
	 * allocated block, or something else (e.g., the increase handles
	 * call in ioinit()) must have allocated memory just beyond it.
	 * Allocate or extend an additional memory block.
	 */
	npar = (size+16)/16;	/* Convert size from bytes to paragraphs */
	cp = NULL;
	for(sp=Sysblock,i=0;i < NSYSBLOCK;i++,sp++){
		if(sp->npar != 0){
			/* Try to expand this block */
			if(setblock(sp->seg,sp->npar + npar) != -1){
				/* Failed (-1 == SUCCESS; strange!) */
				continue;
			}
			/* Block expansion succeeded */
			cp = MK_FP(sp->seg + sp->npar,0);
			sp->npar += npar;
		} else {
			/* Allocate new block */
			if(allocmem(npar,&segp) != -1){
				return NULL;	/* Complete failure */
			}
			/* -1 indicates SUCCESS (strange) */
			sp->seg = segp;
			sp->npar = npar;
			cp = MK_FP(segp,0);
		}
		break;
	}
	if(cp != (char HUGE *)NULL){
		/* Expand or create succeeded, add to heap */
		up = (HEADER *)cp;
		up->s.size = (npar*16)/ABLKSIZE;
		up->s.ptr = up;	/* satisfy audit */
		free(up + 1);
		Heapsize += npar*16;
		Frees--;	/* Nullify increment inside free() */
		return Allocp;
	}
#endif	/* __GNUC__ */
	return NULL;
}

/* Put memory block back on heap */
void
free(blk)
void *blk;
{
	register HEADER HUGE *p, HUGE *q;
	unsigned short HUGE *ptr;
	int i_state;
	int i;

	if(blk == NULL)
		return;		/* Required by ANSI */
	Frees++;
	p = ((HEADER HUGE *)blk) - 1;
	/* Audit check */
	if(p->s.ptr != p){
		Invalid++;
		if(istate()){
			ptr = (unsigned short *)&blk;
			printf("free: WARNING! invalid pointer (%p) proc %s\n",
			 blk,Curproc->name);
			stktrace();

			logmsg(-1,"free: WARNING! invalid pointer (%p) pc = 0x%x %x proc %s\n",
			 blk,ptr[-1],ptr[-2],Curproc->name);
		}
		return;
	}
	Availmem += p->s.size;
	if(Memdebug){
		/* Fill data area with pattern to detect later overwrites */
		for(i=1;i<p->s.size;i++){
			memcpy(p[i].c,Debugpat,sizeof(Debugpat));
		}
	}
	i_state = dirps();
 	/* Search the free list looking for the right place to insert */
	for(q = Allocp; !(p > q && p < q->s.ptr); q = q->s.ptr){
		/* Highest address on circular list? */
		if(q >= q->s.ptr && (p > q || p < q->s.ptr))
			break;
	}
	if(p + p->s.size == q->s.ptr){
		/* Combine with front of this entry */
		p->s.size += q->s.ptr->s.size;
		p->s.ptr = q->s.ptr->s.ptr;
		if(Memdebug){
			memcpy(q->s.ptr->c,Debugpat,sizeof(Debugpat));
		}
	} else {
		/* Link to front of this entry */
		p->s.ptr = q->s.ptr;
	}
	if(q + q->s.size == p){
		/* Combine with end of this entry */
		q->s.size += p->s.size;
		q->s.ptr = p->s.ptr;
		if(Memdebug){
			memcpy(p->c,Debugpat,sizeof(Debugpat));
		}
	} else {
		/* Link to end of this entry */
		q->s.ptr = p;
	}
#ifdef	circular
	Allocp = q;
#endif
	restore(i_state);
	if(Memwait != 0)
		ksignal(&Memwait,0);
}

/* Move existing block to new area */
void *
realloc(area,size)
void *area;
size_t size;
{
	unsigned osize;
	HEADER HUGE *hp;
	void *new;

	hp = ((HEADER *)area) - 1;
	osize = (hp->s.size -1) * ABLKSIZE;

	/* We must copy the block since freeing it may cause the heap
	 * debugging code to scribble over it.
	 */
	if((new = malloc(size)) != NULL)
		memcpy(new,area,size>osize? osize : size);
	free(area);
	return new;
}

/* Allocate block of cleared memory */
void *
calloc(nelem,size)
size_t nelem;	/* Number of elements */
size_t size;	/* Size of each element */
{
	register unsigned i;
	register char *cp;

	i = nelem * size;
	if((cp = malloc(i)) != NULL)
		memset(cp,0,i);
	return cp;
}
/* Version of malloc() that waits if necessary for memory to become available */
void *
mallocw(nb)
size_t nb;
{
	register void *p;

	while((p = malloc(nb)) == NULL){
		Memwait++;
		kwait(&Memwait);
		Memwait--;
	}
	return p;
}
/* Version of calloc that waits if necessary for memory to become available */
void *
callocw(nelem,size)
unsigned nelem;	/* Number of elements */
unsigned size;	/* Size of each element */
{
	register unsigned i;
	register char *cp;

	i = nelem * size;
	cp = mallocw(i);
	memset(cp,0,i);
	return cp;
}
/* Return 0 if at least Memthresh memory is available. Return 1 if
 * less than Memthresh but more than Memthresh/2 is available; i.e.,
 * if a yellow garbage collection should be performed. Return 2 if
 * less than Memthresh/2 is available, i.e., a red collection should
 * be performed.
 */
int
availmem()
{
	void *p;

	if(Availmem*ABLKSIZE >= Memthresh)
		return 0;	/* We're clearly OK */

	/* There's not enough on the heap; try calling malloc to see if
	 * it can get more from the system
	 */
	if((p = malloc(Memthresh)) != NULL){
		free(p);
		return 0;	/* Okay */
	}
	if((p = malloc(Memthresh/2)) != NULL){
		free(p);
		return 1;	/* Yellow alert */
	}
	return 2;		/* Red alert */
}

/* Print heap stats */
static int
dostat(argc,argv,envp)
int argc;
char *argv[];
void *envp;
{
	struct sysblock *sp;
	int i;

	printf("heap size %lu avail %lu (%lu%%) morecores %lu\n",
	 Heapsize,Availmem * ABLKSIZE,100L*Availmem*ABLKSIZE/Heapsize,
	 Morecores);
	if(Sysblock[0].npar != 0){
		printf("Extra blocks:");
		for(i=0,sp=Sysblock;i< NSYSBLOCK;i++,sp++){
			if(sp->npar == 0)
				break;
			printf(" (%x0-%x0)",sp->seg,sp->seg+sp->npar);
		}
		printf("\n");
	}
	printf("allocs %lu frees %lu (diff %lu) alloc fails %lu invalid frees %lu\n",
		Allocs,Frees,Allocs-Frees,Memfail,Invalid);
	printf("garbage collections yellow %lu red %lu\n",Yellows,Reds);
	printf("\n");
	mbufstat();
	return 0;
}

/* Print heap free list */
static int
dofreelist(argc,argv,envp)
int argc;
char *argv[];
void *envp;
{
	HEADER HUGE *p;
	int i = 0;
	int j;
	unsigned corrupt;
	int i_state;

	for(p = Base.s.ptr;p != (HEADER HUGE *)&Base;p = p->s.ptr){
		corrupt = 0;
		if(Memdebug){
			i_state = dirps();
			for(j=1;j<p->s.size;j++){
				if(memcmp(p[j].c,Debugpat,sizeof(Debugpat)) != 0){
					corrupt = j;
					break;
				}
			}
			restore(i_state);
		}
		if(corrupt)
			printf("%p %6lu C: %u",p,p->s.size * ABLKSIZE,corrupt);
		else
			printf("%p %6lu",p,p->s.size * ABLKSIZE);

		if(++i == 4){
			i = 0;
			if(printf("\n") == EOF)
				return 0;
		} else
			printf(" | ");
	}
	if(i != 0)
		printf("\n");
	return 0;
}
static int
dosizes(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int i;

	for(i=0;i<16;i += 4){
		printf("N>=%5u:%7ld| N>=%5u:%7ld| N>=%5u:%7ld| N>=%5u:%7ld\n",
		 1<<i,Sizes[i],	2<<i,Sizes[i+1],
		 4<<i,Sizes[i+2],8<<i,Sizes[i+3]);
	}
	mbufsizes();
	return 0;
}
int
domem(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Memcmds,argc,argv,p);
}

static int
dothresh(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setlong(&Memthresh,"Free memory threshold (bytes)",argc,argv);
}
static int
domdebug(argc,argv,ptr)
int argc;
char *argv[];
void *ptr;
{
	int prev,j,i_state;
	HEADER HUGE *p;

	prev = Memdebug;
	setbool(&Memdebug,"Heap debugging",argc,argv);
	if(prev == 1 || Memdebug == 0)
		return 0;

	/* Turning debugging on; reinitialize free areas to debug pattern */
	i_state = dirps();
	for(p = Base.s.ptr;p != (HEADER HUGE *)&Base;p = p->s.ptr){
		for(j=1;j<p->s.size;j++){
			memcpy(p[j].c,Debugpat,sizeof(Debugpat));
		}
	}
	restore(i_state);
	return 0;
}

/* Background memory compactor, used when memory runs low */
void
gcollect(i,v1,v2)
int i;	/* Args not used */
void *v1;
void *v2;
{
	void (**fp)(int);
	int red;

	for(;;){
		ppause(1000L);	/* Run every second */
		/* If memory is low, collect some garbage. If memory is VERY
		 * low, invoke the garbage collection routines in "red" mode.
		 */
		switch(availmem()){
		case 0:
			continue;	/* All is well */
		case 1:
			red = 0;
			Yellows++;
			break;
		case 2:
			red = 1;
			Reds++;
			break;
		}
		for(fp = Gcollect;*fp != NULL;fp++)
			(**fp)(red);
	}
}
