#ifndef	_DEVPARAM_H
#define	_DEVPARAM_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

/* device parameter control */
enum devparam {
	PARAM_DATA,
	PARAM_TXDELAY,
	PARAM_PERSIST,
	PARAM_SLOTTIME,
	PARAM_TXTAIL,
	PARAM_FULLDUP,
	PARAM_HW,
	PARAM_MUTE,
	PARAM_DTR,
	PARAM_RTS,
	PARAM_SPEED,
	PARAM_ENDDELAY,
	PARAM_GROUP,
	PARAM_IDLE,
	PARAM_MIN,
	PARAM_MAXKEY,
	PARAM_WAIT,
	PARAM_DOWN=0x81,
	PARAM_UP=0x82,
	PARAM_BLIND=0x83,	/* should be vertigo, can't tell down/up? */
	PARAM_RETURN=0xff
};

/* In devparam.c: */
int devparam(char *s);
char *parmname(int n);

#endif	/* _DEVPARAM_H */

