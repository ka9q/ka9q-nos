#ifndef	_PROC_H
#define	_PROC_H

#include <setjmp.h>

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef	_TIMER_H
#include "timer.h"
#endif

#define	SIGQSIZE	200	/* Entries in ksignal queue */

/* Kernel process control block */
#define	PHASH	16		/* Number of wait table hash chains */
struct proc {
	struct proc *prev;	/* Process table pointers */
	struct proc *next;	

	struct {
		unsigned int suspend:1;		/* Process is suspended */
		unsigned int waiting:1;		/* Process is waiting */
		unsigned int istate:1;		/* Process has interrupts enabled */
		unsigned int sset:1;		/* Process has set sig */
		unsigned int freeargs:1;	/* Free args on termination */
	} flags;
	jmp_buf env;		/* Process register state */
	jmp_buf sig;		/* State for alert signal */
	int signo;		/* Arg to alert to cause signal */
	void *event;		/* Wait event */
	uint16 *stack;		/* Process stack */
	unsigned stksize;	/* Size of same */
	char *name;		/* Arbitrary user-assigned name */
	int retval;		/* Return value from next kwait() */
	struct timer alarm;	/* Alarm clock timer */
	FILE *input;		/* Process stdin */
	FILE *output;		/* Process stdout */
	int iarg;		/* Copy of iarg */
	void *parg1;		/* Copy of parg1 */
	void *parg2;		/* Copy of parg2 */
};
extern struct proc *Waittab[];	/* Head of wait list */
extern struct proc *Rdytab;	/* Head of ready list */
extern struct proc *Curproc;	/* Currently running process */
extern struct proc *Susptab;	/* Suspended processes */
extern int Stkchk;		/* Stack checking flag */
extern int Kdebug;		/* Control display of current task on screen */

struct sigentry {
	void *event;
	int n;
};
struct ksig {
	struct sigentry entry[SIGQSIZE];
	struct sigentry *wp;
	struct sigentry *rp;
	volatile int nentries;	/* modified both by interrupts and main */
	int maxentries;
	int32 duksigs;
	int lostsigs;
	int32 ksigs;		/* Count of ksignal calls */
	int32 ksigwakes;	/* Processes woken */
	int32 ksignops;		/* ksignal calls that didn't wake anything */
	int32 ksigsqueued;	/* ksignal calls queued with ints off */
	int32 kwaits;		/* Count of kwait calls */
	int32 kwaitnops;	/* kwait calls that didn't block */
	int32 kwaitints;	/* kwait calls from interrupt context (error) */
};
extern struct ksig Ksig;

/* Prepare for an exception signal and return 0. If after this macro
 * is executed any other process executes alert(pp,val), this will
 * invoke the exception and cause this macro to return a second time,
 * but with the return value 1. This cannot be a function since the stack
 * frame current at the time setjmp is called must still be current
 * at the time the signal is taken. Note use of comma operators to return
 * the value of setjmp as the overall macro expression value.
 */
#define	SETSIG(val)	(Curproc->flags.sset=1,\
	Curproc->signo = (val),setjmp(Curproc->sig))

/* In  kernel.c: */
void alert(struct proc *pp,int val);
void chname(struct proc *pp,char *newname);
void killproc(struct proc *pp);
void killself(void);
struct proc *mainproc(char *name);
struct proc *newproc(char *name,unsigned int stksize,
	void (*pc)(int,void *,void *),
	int iarg,void *parg1,void *parg2,int freeargs);
void ksignal(void *event,int n);
int kwait(void *event);
void resume(struct proc *pp);
int setsig(int val);
void suspend(struct proc *pp);

/* In ksubr.c: */
void chkstk(void);
void kinit(void);
unsigned phash(void *event);
void psetup(struct proc *pp,int iarg,void *parg1,void *parg2,
	void ((*pc)(int,void *,void *)) );
#ifdef	AMIGA
void init_psetup(struct proc *pp);
#endif

/* Stack background fill value for high water mark checking */
#define	STACKPAT	0x55aa

/* Value stashed in location 0 to detect null pointer dereferences */
#define	NULLPAT		0xdead

#endif	/* _PROC_H */
