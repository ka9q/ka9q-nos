#ifndef	_TIMER_H
#define	_TIMER_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

/* Software timers
 * There is one of these structures for each simulated timer.
 * Whenever the timer is running, it is on a linked list
 * pointed to by "Timers". The list is sorted in ascending order of
 * expiration, with the first timer to expire at the head. This
 * allows the timer process to avoid having to scan the entire list
 * on every clock tick; once it finds an unexpired timer, it can
 * stop searching.
 *
 * Stopping a timer or letting it expire causes it to be removed
 * from the list. Starting a timer puts it on the list at the right
 * place.
 */
struct timer {
	struct timer *next;	/* Linked-list pointer */
	int32 duration;		/* Duration of timer, in ticks */
	int32 expiration;	/* Clock time at expiration */
	void (*func)(void *);	/* Function to call at expiration */
	void *arg;		/* Arg to pass function */
	char state;		/* Timer state */
#define	TIMER_STOP	0
#define	TIMER_RUN	1
#define	TIMER_EXPIRE	2
};
#define	MAX_TIME	(int32)4294967295	/* Max long integer */
#ifndef	MSPTICK
#define	MSPTICK		55		/* Milliseconds per tick */
#endif
#ifndef	EALARM
#define	EALARM		106
#endif
/* Useful user macros that hide the timer structure internals */
#define	dur_timer(t)	((t)->duration*MSPTICK)
#define	run_timer(t)	((t)->state == TIMER_RUN)

extern int Tick;
extern void (*Cfunc[])();	/* List of clock tick functions */

/* In timer.c: */
void kalarm(int32 ms);
int ppause(int32 ms);
int32 read_timer(struct timer *t);
void set_timer(struct timer *t,int32 x);
void start_timer(struct timer *t);
void stop_timer(struct timer *timer);
char *tformat(int32 t);

/* In hardware.c: */
int32 msclock(void);
int32 secclock(void);
int32 usclock(void);

#endif	/* _TIMER_H */
