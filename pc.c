/* OS- and machine-dependent stuff for IBM-PC running MS-DOS and Turbo-C
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <conio.h>
#include <dir.h>
#include <dos.h>
#include <io.h>
#include <sys/stat.h>
#include <string.h>
#include <process.h>
#include <fcntl.h>
/*#include <alloc.h> */
#include <stdarg.h>
#include <bios.h>
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "internet.h"
#include "session.h"
#include "socket.h"
#include "usock.h"
#include "cmdparse.h"
#include "nospc.h"
#include "display.h"

static void statline(struct display *dp,struct session *sp);

extern int Curdisp;
extern struct proc *Display;
unsigned _stklen = 8192;
int Tick;
static int32 Clock;
extern INTERRUPT (*Kbvec)();

/* This flag is set by setirq() if IRQ 8-15 is used, indicating
 * that the machine is a PC/AT with a second 8259 interrupt controller.
 * If this flag is set, the interrupt return code in pcgen.asm will
 * send an End of Interrupt command to the second 8259 as well as the
 * first.
 */
#ifdef	CPU386
int Isat = 1;
#else
int Isat;
#endif

static int saved_break;

int
errhandler(errval,ax,bp,si)
int errval,ax,bp,si;
{
	return 3;	/* Fail the system call */
}

/* Called at startup time to set up console I/O, memory heap */
void
ioinit()
{
	union REGS inregs;

	/* Increase the size of the file table.
	 * Note: this causes MS-DOS
	 * to allocate a block of memory to hold the larger file table.
	 * By default, this happens right after our program, which means
	 * any further sbrk() calls from morecore (called from malloc)
	 * will fail. Hence there is now code in alloc.c that can call
	 * the MS-DOS allocmem() function to grab additional MS-DOS
	 * memory blocks that are not contiguous with the program and
	 * put them on the heap.
	 */
	inregs.h.ah = 0x67;
	inregs.x.bx = Nfiles;	/* Up to the base of the socket numbers */
	intdos(&inregs,&inregs);	

	/* Allocate space for the fd reference count table */
	Refcnt = (unsigned *)calloc(sizeof(unsigned),Nfiles);

	Refcnt[3] = 1;
	Refcnt[4] = 1;
	_close(3);
	_close(4);

	/* Fail all I/O errors */
	harderr(errhandler);

	saved_break = getcbrk();
	setcbrk(0);

	/* Link timer handler into timer interrupt chain */
	chtimer(btick);

	/* Find out what multitasker we're running under, if any */
	chktasker();

	/* Hook keyboard interrupt */
	Kbvec = getirq(KBIRQ);
	setirq(KBIRQ,kbint);
}
/* Called just before exiting to restore console state */
void
iostop()
{
	struct iface *ifp,*iftmp;
	void (**fp)(void);

	setcbrk(saved_break);

	for(ifp = Ifaces;ifp != NULL;ifp = iftmp){
		iftmp = ifp->next;
		if_detach(ifp);
	}
	/* Call list of shutdown functions */
	for(fp = Shutdown;*fp != NULL;fp++){
		(**fp)();
	}
	fcloseall();
	setirq(KBIRQ,Kbvec);	/* Restore original keyboard vec */
}
/* Spawn subshell */
int
doshell(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *command;
	int ret;

	if((command = getenv("COMSPEC")) == NULL)
		command = "/COMMAND.COM";
	ret = spawnv(P_WAIT,command,argv);

	return ret;
}

/* Read characters from the keyboard, translating them to "real" ASCII.
 * If none are ready, block. The special keys are translated to values
 * above 256, e.g., F-10 is 256 + 68 = 324.
 */
int
kbread()
{
	uint16 c;

	while((c = kbraw()) == 0)
		kwait(&Kbvec);

	rtype(c);	/* Randomize random number state */

	/* Convert "extended ascii" to something more standard */
	if((c & 0xff) != 0)
		return c & 0xff;
	c >>= 8;
	switch(c){
	case 3:		/* NULL (bizzare!) */
		c = 0;
		break;
	case 83:	/* DEL key */
		c = DEL;
		break;
	default:	/* Special key */
		c += 256;
	}
	return c;
}
/* Install hardware interrupt handler.
 * Takes IRQ numbers from 0-7 (0-15 on AT) and maps to actual 8086/286 vectors
 * Note that bus line IRQ2 maps to IRQ9 on the AT
 */
int
setirq(irq,handler)
unsigned irq;
INTERRUPT (*handler)();
{
	/* Set interrupt vector */
	if(irq < 8){
		setvect(8+irq,handler);
	} else if(irq < 16){
		Isat = 1;
		setvect(0x70 + irq - 8,handler);
	} else {
		return -1;
	}
	return 0;
}
/* Return pointer to hardware interrupt handler.
 * Takes IRQ numbers from 0-7 (0-15 on AT) and maps to actual 8086/286 vectors
 */
INTERRUPT
(*getirq(irq))()
unsigned int irq;
{
	/* Set interrupt vector */
	if(irq < 8){
		return getvect(8+irq);
	} else if(irq < 16){
		return getvect(0x70 + irq - 8);
	} else {
		return NULL;
	}
}
/* Disable hardware interrupt */
int
maskoff(irq)
unsigned irq;
{
	if(irq < 8){
		setbit(0x21,(char)(1<<irq));
	} else if(irq < 16){
		irq -= 8;
		setbit(0xa1,(char)(1<<irq));
	} else {
		return -1;
	}
	return 0;
}
/* Enable hardware interrupt */
int
maskon(irq)
unsigned irq;
 {
	if(irq < 8){
		clrbit(0x21,(char)(1<<irq));
	} else if(irq < 16){
		irq -= 8;
		clrbit(0xa1,(char)(1<<irq));
	} else {
		return -1;
	}
	return 0;
}
/* Return 1 if specified interrupt is enabled, 0 if not, -1 if invalid */
int
getmask(irq)
unsigned irq;
{
	if(irq < 8)
		return (inportb(0x21) & (1 << irq)) ? 0 : 1;
	else if(irq < 16){
		irq -= 8;
		return (inportb(0xa1) & (1 << irq)) ? 0 : 1;
	} else
		return -1;
}
/* Called from assembler stub linked to BIOS interrupt 1C, called on each
 * hardware clock tick. Signal a clock tick to the timer process.
 */
void
ctick()
{
	Tick++;
	Clock++;
	ksignal(&Tick,1);
}
/* Read the Clock global variable, with interrupts off to avoid possible
 * inconsistency on 16-bit machines
 */
int32
rdclock()
{
	int i_state;
	int32 rval;

	i_state = dirps();
	rval = Clock;
	restore(i_state);
	return rval;
}

/* Called from the timer process on every tick. NOTE! This function
 * can NOT be called at interrupt time because it calls the BIOS
 */
void
pctick()
{
	long t;
	static long oldt;	/* Value of bioscnt() on last call */

	/* Check for day change */
	t = bioscnt();
	if(t < oldt){
		/* Call the regular DOS time func to handle the midnight flag */
		(void)time(NULL);
	}
}

/* Set bit(s) in I/O port */
void
setbit(port,bits)
unsigned port;
char bits;
{
	outportb(port,inportb(port)|bits);
}
/* Clear bit(s) in I/O port */
void
clrbit(port,bits)
unsigned port;
char bits;
{
	outportb(port,inportb(port) & ~bits);
}
/* Set or clear selected bits(s) in I/O port */
void
writebit(port,mask,val)
unsigned port;
char mask;
int val;
{
	register char x;

	x = inportb(port);
	if(val)
		x |= mask;
	else
		x &= ~mask;
	outportb(port,x);
}
void *
ltop(l)
long l;
{
	register unsigned seg,offset;

	seg = l >> 16;
	offset = l;
	return MK_FP(seg,offset);
}
#ifdef	notdef	/* Assembler versions in pcgen.asm */
/* Multiply a 16-bit multiplier by an arbitrary length multiplicand.
 * Product is left in place of the multiplicand, and the carry is
 * returned
 */
uint16
longmul(multiplier,n,multiplicand)
uint16 multiplier;
int n;				/* Number of words in multiplicand[] */
register uint16 *multiplicand;	/* High word is in multiplicand[0] */
{
	register int i;
	unsigned long pc;
	uint16 carry;

	carry = 0;
	multiplicand += n;
	for(i=n;i != 0;i--){
		multiplicand--;
		pc = carry + (unsigned long)multiplier * *multiplicand;
		*multiplicand = pc;
		carry = pc >> 16;
	}
	return carry;
}
/* Divide a 16-bit divisor into an arbitrary length dividend using
 * long division. The quotient is returned in place of the dividend,
 * and the function returns the remainder.
 */
uint16
longdiv(divisor,n,dividend)
uint16 divisor;
int n;				/* Number of words in dividend[] */
register uint16 *dividend;	/* High word is in dividend[0] */
{
	/* Before each division, remquot contains the 32-bit dividend for this
	 * step, consisting of the 16-bit remainder from the previous division
	 * in the high word plus the current 16-bit dividend word in the low
	 * word.
	 *
	 * Immediately after the division, remquot contains the quotient
	 * in the low word and the remainder in the high word (which is
	 * exactly where we need it for the next division).
	 */
	unsigned long remquot;
	register int i;

	if(divisor == 0)
		return 0;	/* Avoid divide-by-zero crash */
	remquot = 0;
	for(i=0;i<n;i++,dividend++){
		remquot |= *dividend;
		if(remquot == 0)
			continue;	/* Avoid unnecessary division */
#ifdef	__TURBOC__
		/* Use assembly lang routine that returns both quotient
		 * and remainder, avoiding a second costly division
		 */
		remquot = divrem(remquot,divisor);
		*dividend = remquot;	/* Extract quotient in low word */
		remquot &= ~0xffffL;	/* ... and mask it off */
#else
		*dividend = remquot / divisor;
		remquot = (remquot % divisor) << 16;
#endif
	}
	return remquot >> 16;
}
#endif
void
sysreset()
{
	void (*foo)(void);

	foo = MK_FP(0xffff,0);	/* FFFF:0000 is hardware reset vector */
	(*foo)();
}

void
display(i,v1,v2)
int i;
void *v1;
void *v2;
{
	struct session *sp;
	struct display *dp;
	static struct display *lastdp;
	static long lastkdebug;

	for(;;){
		sp = Current;
		if(sp == NULL || sp->output == NULL
		 || (dp = (struct display *)sp->output->ptr) == NULL){
			/* Something weird happened */
			ppause(500L);
			continue;
		}
		if(dp != lastdp || Kdebug != lastkdebug)
			dp->flags.dirty_screen = 1;
		statline(dp,sp);
		dupdate(dp);
		lastdp = dp;
		lastkdebug = Kdebug;
		kalarm(100L);	/* Poll status every 100 ms */
		kwait(dp);
		kalarm(0L);
	}
}

/* Compose status line for bottom of screen
 * Return 1 if session has unacked data and status should be polled,
 * 0 otherwise.
 *
 */
static void
statline(dp,sp)
struct display *dp;
struct session *sp;
{
	int attr;
	char buf[81];
	struct text_info text_info;
	int unack;
	int s1;
	int s = -1;

	/* Determine attribute to use */
	gettextinfo(&text_info);
	if(text_info.currmode == MONO)
		attr = 0x07;	/* Regular white on black */
	else
		attr = 0x02;	/* Green on black */

	if(sp->network != NULL && (s = fileno(sp->network)) != -1){
		unack = socklen(s,1);
		if(sp->type == FTP && (s1 = fileno(sp->cb.ftp->data)) != -1)
			unack += socklen(s1,1);
	}
	sprintf(buf,"%2d: %s",sp->index,sp->name);
	statwrite(dp,0,buf,strlen(buf),attr);

	if(dp->flags.scrollbk){
		sprintf(buf,"Scroll:%-5lu",dp->sfoffs);
	} else if(s != -1 && unack != 0){
		sprintf(buf,"Unack: %-5u",unack);
	} else
		sprintf(buf,"            ");
	statwrite(dp,54,buf,strlen(buf),attr);
	sprintf(buf,"F8:nxt F10:cmd");
	statwrite(dp,66,buf,strlen(buf),attr);
}

/* Return time since startup in milliseconds. If the system has an
 * 8254 clock chip (standard on ATs and up) then resolution is improved
 * below 55 ms (the clock tick interval) by reading back the instantaneous
 * value of the counter and combining it with the global clock tick counter.
 * Otherwise 55 ms resolution is provided.
 *
 * Reading the 8254 is a bit tricky since a tick could occur asynchronously
 * between the two reads. The tick counter is examined before and after the
 * hardware counter is read. If the tick counter changes, try again.
 * Note: the hardware counter counts down from 65536.
 */
int32
msclock()
{
	int32 hi;
	uint16 lo;
	uint16 count[4];	/* extended (48-bit) counter of timer clocks */

	if(!Isat)
		return rdclock() * MSPTICK;

	do {
		hi = rdclock();
		lo = clockbits();
	} while(hi != rdclock());

	count[0] = 0;
	count[1] = hi >> 16;
	count[2] = hi;
	count[3] = -lo;
	longmul(11,4,count);	/* The ratio 11/13125 is exact */
	longdiv(13125,4,count);
	return ((long)count[2] << 16) + count[3];
}
/* Return clock in seconds */
int32
secclock()
{
	int32 hi;
	uint16 lo;
	uint16 count[4];	/* extended (48-bit) counter of timer clocks */

	if(!Isat)
		return (rdclock() * MSPTICK) / 1000L;

	do {
		hi = rdclock();
		lo = clockbits();
	} while(hi != rdclock());

	count[0] = 0;
	count[1] = hi >> 16;
	count[2] = hi;
	count[3] = -lo;
	longmul(11,4,count);	/* The ratio 11/13125 is exact */
	longdiv(13125,4,count);
	longdiv(1000,4,count);	/* Convert to seconds */
	return ((long)count[2] << 16) + count[3];
}
/* Return time in raw clock counts, approx 838 ns */
int32
usclock()
{
	int32 hi;
	uint16 lo;

	do {
		hi = rdclock();
		lo = clockbits();
	} while(hi != rdclock());

	return (hi << 16) - (int32)lo;
}


#if !defined(CPU386)	/* 386s and above always have an AT bus */
int
doisat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Isat,"AT/386 mode",argc,argv);
}
#endif
/* Directly read BIOS count of time ticks. This is used instead of
 * calling biostime(0,0L). The latter calls BIOS INT 1A, AH=0,
 * which resets the midnight overflow flag, losing days on the clock.
 */
long
bioscnt()
{
	long rval;
	int i_state;

	i_state = dirps();
	rval = * (long far *)MK_FP(0x40,0x6c);
	restore(i_state);
	return rval;
}
/* Null stub to replace Borland C++ library function called at startup time
 * to setup the stdin/stdout/stderr streams
 */
void
_setupio()
{
}

/* Return 1 if running in interrupt context, 0 otherwise. Works by seeing if
 * the stack pointer is inside the interrupt stack
 */
int
intcontext()
{
	if(_SS == FP_SEG(Intstk)
	 && _SP >= FP_OFF(Intstk) && _SP <= FP_OFF(Stktop))
		return 1;
	return 0;
}
/* Atomic read-and-decrement operation.
 * Read the variable pointed to by p. If it is
 * non-zero, decrement it. Return the original value.
 */
int
arddec(p)
volatile int *p;
{
	int tmp;
	int i_state;

	i_state = dirps();
	tmp = *p;
	if(tmp != 0)
		(*p)--;
	restore(i_state);
	return tmp;
}
