/* Machine or compiler-dependent portions of kernel
 * Turbo-C version for PC
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <dos.h>
#include "global.h"
#include "proc.h"
#include "nospc.h"
#include "commands.h"

static char *Taskers[] = {
	"",
	"DoubleDos",
	"DesqView",
	"Windows",
	"OS/2",
};


static oldNull;

/* Template for contents of jmp_buf in Turbo C */
struct env {
	unsigned	sp;
	unsigned	ss;
	unsigned	flag;
	unsigned	cs;
	unsigned	ip;
	unsigned	bp;
	unsigned	di;
	unsigned	es;
	unsigned	si;
	unsigned	ds;
};

static int chkintstk(void);
static int stkutil(struct proc *pp);
static void pproc(struct proc *pp);

void
kinit()
{
	int i;

	/* Initialize interrupt stack for high-water-mark checking */
	for(i=0;i<Stktop-Intstk;i++)
		Intstk[i] = STACKPAT;

	/* Remember location 0 pattern to detect null pointer derefs */
	oldNull = *(unsigned short *)NULL;

	/* Initialize signal queue */
	Ksig.wp = Ksig.rp = Ksig.entry;
}
/* Print process table info
 * Since things can change while ps is running, the ready proceses are
 * displayed last. This is because an interrupt can make a process ready,
 * but a ready process won't spontaneously become unready. Therefore a
 * process that changes during ps may show up twice, but this is better
 * than not having it showing up at all.
 */
int
ps(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct proc *pp;
	int i;

	printf("Uptime %s Stack %x max intstk %u psp %x",tformat(secclock()),
	 getss(),chkintstk(),_psp);
	if(Mtasker != 0){
		printf(" Running under %s",Taskers[Mtasker]);
	}
	printf("\n");

	printf("ksigs %lu queued %lu hiwat %u woken %lu nops %lu dups %u\n",Ksig.ksigs,
	 Ksig.ksigsqueued,Ksig.maxentries,Ksig.ksigwakes,Ksig.ksignops,Ksig.duksigs);
	Ksig.maxentries = 0;
	printf("kwaits %lu nops %lu from int %lu\n",
	 Ksig.kwaits,Ksig.kwaitnops,Ksig.kwaitints);
	printf("PID       SP        stksize   maxstk    event     fl  in  out  name\n");

	for(pp = Susptab;pp != NULL;pp = pp->next)
		pproc(pp);

	for(i=0;i<PHASH;i++)
		for(pp = Waittab[i];pp != NULL;pp = pp->next)
			pproc(pp);

	for(pp = Rdytab;pp != NULL;pp = pp->next)
		pproc(pp);

	if(Curproc != NULL)
		pproc(Curproc);

	return 0;
}
static void
pproc(pp)
struct proc *pp;
{
	register struct env *ep;
	char insock[5],outsock[5];

	ep = (struct env *)&pp->env;
	if(fileno(pp->input) != -1)
		sprintf(insock,"%3d",fileno(pp->input));
	else
		sprintf(insock,"   ");
	if(fileno(pp->output) != -1)
		sprintf(outsock,"%3d",fileno(pp->output));
	else
		sprintf(outsock,"   ");
	printf("%-10p%-10p%-10u%-10u%-10p%c%c%c %s %s  %s\n",
	 pp,MK_FP(ep->ss,ep->sp),pp->stksize,stkutil(pp),
	 pp->event,
	 pp->flags.istate ? 'I' : ' ',
	 pp->flags.waiting ? 'W' : ' ',
	 pp->flags.suspend ? 'S' : ' ',
	 insock,outsock,pp->name);
}
static int
stkutil(pp)
struct proc *pp;
{
	unsigned i;
	register uint16 *sp;

	i = pp->stksize;
	for(sp = pp->stack;*sp == STACKPAT && sp < pp->stack + pp->stksize;sp++)
		i--;
	return i;
}
/* Return number of used words in interrupt stack */
static int
chkintstk()
{
	register int i;
	register uint16 *cp;

	i = Stktop - Intstk;
	for(cp=Intstk;*cp == STACKPAT && cp < Stktop;cp++)
		i--;
	return i;
}

/* Verify that stack pointer for current process is within legal limits;
 * also check that no one has dereferenced a null pointer
 */
void
chkstk()
{
	uint16 *sbase;
	uint16 *stop;
	uint16 *sp;

	sp = MK_FP(_SS,_SP);
	if(_SS == _DS){
		/* Probably in interrupt context */
		return;
	}
	sbase = Curproc->stack;
	if(sbase == NULL)
		return;	/* Main task -- too hard to check */

	stop = sbase + Curproc->stksize;
	if(sp < sbase || sp >= stop){
		printf("Stack violation, process %s\n",Curproc->name);
		printf("SP = %p, legal stack range [%p,%p)\n",
		sp,sbase,stop);
		fflush(stdout);
		killself();
	}
	if(*(unsigned short *)NULL != oldNull){
		printf("WARNING: Location 0 smashed, process %s\n",Curproc->name);
		*(unsigned short *)NULL = oldNull;
		fflush(stdout);
	}
}
/* Machine-dependent initialization of a task */
void
psetup(pp,iarg,parg1,parg2,pc)
struct proc *pp;	/* Pointer to task structure */
int iarg;		/* Generic integer arg */
void *parg1;		/* Generic pointer arg #1 */
void *parg2;		/* Generic pointer arg #2 */
void (*pc)();		/* Initial execution address */
{
	register int *stktop;
	register struct env *ep;

	/* Set up stack to make it appear as if the user's function was called
	 * by killself() with the specified arguments. When the user returns,
	 * killself() automatically cleans up.
	 *
	 * First, push args on stack in reverse order, simulating what C
	 * does just before it calls a function.
	 */
	stktop = (int *)(pp->stack + pp->stksize);
#ifdef	LARGEDATA
	*--stktop = FP_SEG(parg2);
#endif
	*--stktop = FP_OFF(parg2);
#ifdef	LARGEDATA
	*--stktop = FP_SEG(parg1);
#endif
	*--stktop = FP_OFF(parg1);
	*--stktop = iarg;
		
	/* Now push the entry address of killself(), simulating the call to
	 * the user function.
	 */
#ifdef	LARGECODE
	*--stktop = FP_SEG(killself);
#endif
	*--stktop = FP_OFF(killself);

	/* Set up task environment. Note that for Turbo-C, the setjmp
	 * sets the interrupt enable flag in the environment so that
	 * interrupts will be enabled when the task runs for the first time.
	 * Note that this requires newproc() to be called with interrupts
	 * enabled!
	 */
	setjmp(pp->env);
	ep = (struct env *)&pp->env;
	ep->ss = FP_SEG(stktop);
	ep->sp = FP_OFF(stktop);
	ep->cs = FP_SEG(pc);	/* Doesn't hurt in small model */
	ep->ip = FP_OFF(pc);
	ep->bp = 0;		/* Anchor stack traces */
	/* Task initially runs with interrupts on */
	pp->flags.istate = 1;
}
unsigned
phash(event)
void *event;
{
	register unsigned x;

	/* Fold the two halves of the pointer */
	x = FP_SEG(event) ^ FP_OFF(event);

	/* If PHASH is a power of two, this will simply mask off the
	 * higher order bits
	 */
	return x % PHASH;
}
