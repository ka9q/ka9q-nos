#ifndef	_DAEMON_H
#define	_DAEMON_H

struct daemon {
	char *name;
	unsigned stksize;
	void (*fp)(int,void *,void *);
};
extern struct daemon Daemons[];

/* In alloc.c: */
void gcollect(int,void*,void*);

/* In main.c: */
void keyboard(int,void*,void*);
void network(int,void *,void *);
void display(int,void *,void *);

/* In kernel.c: */
void killer(int,void*,void*);

/* In random.c: */
void rand_init(int,void*,void*);

/* In timer.c: */
void timerproc(int,void*,void*);

#endif	/* _DAEMON_H */

