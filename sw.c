/* These routines, plus the assembler hooks in stopwatch.asm, implement a
 * general purpose "stopwatch" facility useful for timing the execution of
 * code segments. The PC's "timer 2" channel (the one ordinarily
 * used to drive the speaker) is used. It is driven with a 838 ns
 * clock. The timer is 16 bits wide, so it "wraps around" in only 55 ms.
 *
 * There is an array of "stopwatch" structures used for recording the number
 * of uses and the min/max times for each. Since only one hardware timer is
 * available, only one stopwatch can actually be running at any one time.
 *
 * This facility is useful mainly for timing routines that must execute with
 * interrupts disabled. An interrupt that occurs while the timer is running
 * will not stop it, so it would show an errneously large value.
 *
 * To start a timer, call swstart(). To stop it and record its value,
 * call swstop(n), where n is the number of the stopwatch structure.
 * The stopwatch structures can be displayed with the "watch" command.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "nospc.h"

struct stopwatch Sw[NSW];

/* Stop a stopwatch and record its value.
 * Uses stopval() routine in stopwatch.asm
 */
void
swstop(n)
int n;
{
	register struct stopwatch *sw;
	int32 ticks;

	ticks = 65536 - stopval();
	sw = &Sw[n];

	if(sw->calls++ == 0){
		sw->maxval = ticks;
		sw->minval = ticks;
	} else if(ticks > sw->maxval){
		sw->maxval = ticks;
	} else if(ticks < sw->minval){
		sw->minval = ticks;
	}
	sw->totval += ticks;
}
int
doswatch(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct stopwatch *sw;
	long maxval,minval,avgval;
	int i;

	if(argc > 1){
		/* Clear timers */
		for(i=0,sw=Sw;i < NSW;i++,sw++){
			sw->calls = 0;
			sw->totval = 0;
		}
	}
	for(i=0,sw=Sw;sw < &Sw[NSW];i++,sw++){
		if(sw->calls == 0)
			continue;
		minval = sw->minval * 838L/1000;
		maxval = sw->maxval * 838L/1000;
		avgval = sw->totval / sw->calls * 838L / 1000L;
		printf("%u: calls %lu min %lu max %lu avg %lu tot %lu\n",
		 i,sw->calls,minval,maxval,avgval,sw->totval);

	}
	return 0;
}
