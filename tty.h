#ifndef	_TTY_H
#define	_TTY_H

#ifndef	_MBUF_H
#include "mbuf.h"
#endif

#ifndef _SESSION_H
#include "session.h"
#endif

/* In ttydriv.c: */
int ttydriv(struct session *sp,uint8 c);

#endif /* _TTY_H */
