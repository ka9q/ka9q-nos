/* General purpose software timer facilities
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "timer.h"
#include "proc.h"
#include "mbuf.h"
#include "commands.h"
#include "daemon.h"
#include "hardware.h"
#include "socket.h"

/* Head of running timer chain.
 * The list of running timers is sorted in increasing order of expiration;
 * i.e., the first timer to expire is always at the head of the list.
 */
static struct timer *Timers;

static void t_alarm(void *x);

/* Process that handles clock ticks */
void
timerproc(i,v1,v2)
int i;
void *v1,*v2;
{
	register struct timer *t;
	register struct timer *expired;
	void (**vf)(void);
	int i_state;
	int tmp;
	int32 clock;

	for(;;){
		/* Atomic read and decrement of Tick */
		for(;;){
			i_state = dirps();
			tmp = Tick;
			if(tmp != 0){
				Tick--;
				restore(i_state);
				break;
			}	
			restore(i_state);
			kwait(&Tick);
		}
		if(!istate()){
			restore(1);
			printf("timer: ints were off!\n");
		}

		/* Call the functions listed in config.c */
		for(vf = Cfunc;*vf != NULL;vf++)
			(*vf)();

		kwait(NULL);	/* Let them all do their writes */

		if(Timers == NULL)
			continue;	/* No active timers, all done */

		/* Initialize null expired timer list */
		expired = NULL;
		clock = rdclock();

		/* Move expired timers to expired list. Note use of
		 * subtraction and comparison to zero rather than the
		 * more obvious simple comparison; this avoids
		 * problems when the clock count wraps around.
		 */
		while(Timers != NULL && (clock - Timers->expiration) >= 0){
			if(Timers->next == Timers){
				printf("PANIC: Timer loop at %lx\n",
				 (long)Timers);
				iostop();
				exit(1);
			}
			/* Save Timers since stop_timer will change it */
			t = Timers;
			stop_timer(t);
			t->state = TIMER_EXPIRE;
			/* Add to expired timer list */
			t->next = expired;
			expired = t;
		}
		/* Now go through the list of expired timers, removing each
		 * one and kicking the notify function, if there is one
		 */
		while((t = expired) != NULL){
			expired = t->next;
			if(t->func){
				(*t->func)(t->arg);
			}
		}
		kwait(NULL);	/* Let them run before handling more ticks */
	}
}
/* Start a timer */
void
start_timer(t)
struct timer *t;
{
	register struct timer *tnext;
	struct timer *tprev = NULL;

	if(t == NULL)
		return;
	if(t->state == TIMER_RUN)
		stop_timer(t);
	if(t->duration == 0)
		return;		/* A duration value of 0 disables the timer */

	t->expiration = rdclock() + t->duration;
	t->state = TIMER_RUN;

	/* Find right place on list for this guy. Once again, note use
	 * of subtraction and comparison with zero rather than direct
	 * comparison of expiration times.
	 */
	for(tnext = Timers;tnext != NULL;tprev=tnext,tnext = tnext->next){
		if((tnext->expiration - t->expiration) >= 0)
			break;
	}
	/* At this point, tprev points to the entry that should go right
	 * before us, and tnext points to the entry just after us. Either or
	 * both may be null.
	 */
	if(tprev == NULL)
		Timers = t;		/* Put at beginning */
	else
		tprev->next = t;

	t->next = tnext;
}
/* Stop a timer */
void
stop_timer(timer)
struct timer *timer;
{
	register struct timer *t;
	struct timer *tlast = NULL;

	if(timer == NULL || timer->state != TIMER_RUN)
		return;

	/* Verify that timer is really on list */
	for(t = Timers;t != NULL;tlast = t,t = t->next)
		if(t == timer)
			break;

	if(t == NULL)
		return;		/* Should probably panic here */

	/* Delete from active timer list */
	if(tlast != NULL)
		tlast->next = t->next;
	else
		Timers = t->next;	/* Was first on list */

	t->state = TIMER_STOP;
}
/* Return milliseconds remaining on this timer */
int32
read_timer(t)
struct timer *t;
{
	int32 remaining;

	if(t == NULL || t->state != TIMER_RUN)
		return 0;
	remaining = t->expiration - rdclock();
	if(remaining <= 0)
		return 0;	/* Already expired */
	else
		return remaining * MSPTICK;
}
void
set_timer(t,interval)
struct timer *t;
int32 interval;
{
	if(t == NULL)
		return;
	/* Round the interval up to the next full tick, and then
	 * add another tick to guarantee that the timeout will not
	 * occur before the interval is up. This is necessary because
	 * we're asynchronous with the system clock.
	 */	
	if(interval != 0)
		t->duration = 1 + (interval + MSPTICK - 1)/MSPTICK;
	else
		t->duration = 0;
}
/* Delay process for specified number of milliseconds.
 * Normally returns 0; returns -1 if aborted by alarm.
 */
int
ppause(ms)
int32 ms;
{
	int val;

	if(Curproc == NULL || ms == 0)
		return 0;
	kalarm(ms);
	/* The actual event doesn't matter, since we'll be alerted */
	while(Curproc->alarm.state == TIMER_RUN){
		if((val = kwait(Curproc)) != 0)
			break;
	}
	kalarm(0L); /* Make sure it's stopped, in case we were killed */	
	return (val == EALARM) ? 0 : -1;
}
static void
t_alarm(x)
void *x;
{
	alert((struct proc *)x,EALARM);
}
/* Send signal to current process after specified number of milliseconds */
void
kalarm(ms)
int32 ms;
{
	if(Curproc != NULL){
		set_timer(&Curproc->alarm,ms);
		Curproc->alarm.func = t_alarm;
		Curproc->alarm.arg = (char *)Curproc;
		start_timer(&Curproc->alarm);
	}
}
/* Convert time count in seconds to printable days:hr:min:sec format */
char *
tformat(t)
int32 t;
{
	static char buf[17],*cp;
	unsigned int days,hrs,mins,secs;
	int minus;

	if(t < 0){
		t = -t;
		minus = 1;
	} else
		minus = 0;

	secs = t % 60;
	t /= 60;
	mins = t % 60;
	t /= 60;
	hrs = t % 24;
	t /= 24;
	days = t;
	if(minus){
		cp = buf+1;
		buf[0] = '-';
	} else
		cp = buf;
	sprintf(cp,"%u:%02u:%02u:%02u",days,hrs,mins,secs);
	
	return buf;
}
	
