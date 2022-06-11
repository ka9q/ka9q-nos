#ifndef	_PC_H
#define	_PC_H
#define _HARDWARE_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#define	NSW	10	/* Number of stopwatch "memories" */

#define	KBIRQ	1	/* IRQ for PC keyboard */

/* Extended keyboard codes for function keys */
#define	F1	59	/* Function key 1 */
#define	F2	60
#define	F3	61
#define	F4	62
#define	F5	63
#define	F6	64
#define	F7	65
#define	F8	66
#define	F9	67
#define	F10	68

#define	CURSHOM	71	/* Home key */
#define	CURSUP	72	/* Up arrow key */
#define	PAGEUP	73	/* Page up key */
#define	CURSLEFT 75	/* Cursor left key */
#define CURSRIGHT 77	/* Cursor right key */	
#define	CURSEND	79	/* END key */
#define	CURSDWN	80	/* Down arrow key */
#define	PAGEDWN	81	/* Page down key */

#define	AF1	104	/* ALT-F1 */
#define	AF2	105
#define	AF3	106
#define	AF4	107
#define	AF5	108
#define	AF6	109
#define	AF7	110
#define	AF8	111
#define	AF9	112
#define	AF10	113
#define	AF11	139
#define	AF12	140

struct stopwatch {
	long calls;
	uint16 maxval;
	uint16 minval;
	int32 totval;
};
extern struct stopwatch Sw[];
extern uint16 Intstk[];	/* Interrupt stack defined in pcgen.asm */
extern uint16 Stktop[];	/* Top of interrupt stack */
extern void (*Shutdown[])();	/* List of functions to call at shutdown */
extern int Mtasker;	/* Type of multitasker, if any */

/* In n8250.c: */
void asytimer(void);

/* In dos.c: */
extern unsigned *Refcnt;
int _creat(const char *file,int mode);
int _open(const char *file,int mode);
int dup(int fd);
int _close(int fd);
int _read(int fd,void *buf,unsigned cnt);
int _write(int fd,const void *buf,unsigned cnt);
long _lseek(int fd,long offset,int whence);

/* In dma.c: */
unsigned long dma_map(void *p,unsigned short len,int copy);
void dma_unmap(void *p,int copy);
int dis_dmaxl(int chan);
int ena_dmaxl(int chan);
unsigned long dmalock(void *p,unsigned short len);
unsigned long dmaunlock(unsigned long physaddr,unsigned short len);
void *dma_malloc(int32 *physaddr,unsigned short len);

/* In random.c: */
void rtype(uint16 c);

/* In scc.c: */
void scctimer(void);
void sccstop(void);

/* In pc.c: */
long bioscnt(void);
void clrbit(unsigned port,char bits);
void ctick(void);
int32 divrem(int32 dividend,uint16 divisor);
INTERRUPT  (*getirq(unsigned int))(void);
int getmask(unsigned irq);
int intcontext(void);
void ioinit(void);
void iostop(void);
void kbsave(int c);
int kbread(void);
int maskoff(unsigned irq);
int maskon(unsigned irq);
void pctick(void);
void setbit(unsigned port,char bits);
int setirq(unsigned irq,INTERRUPT (*handler)(void));
void sysreset(void);
void systick(void);
void writebit(unsigned port,char mask,int val);

/* In pcgen.asm: */
INTERRUPT btick(void);
void chktasker(void);
void chtimer(INTERRUPT (*)());
int32 divrem(int32 dividend,uint16 divisor);
uint16 getss(void);
void giveup(void);
uint16 kbraw(void);
uint16 longdiv(uint16 divisor,int n,uint16 *dividend);
uint16 longmul(uint16 multiplier,int n,uint16 *multiplicand);
INTERRUPT nullvec(void);
INTERRUPT kbint(void);
void uchtimer(void);
uint16 clockbits(void);

/* In stopwatch.asm: */
void swstart(void);
uint16 stopval(void);

/* In sw.c: */
void swstop(int n);

#endif	/* _PC_H */

