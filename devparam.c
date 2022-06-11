#include <string.h>
#include <ctype.h>
#include "global.h"
#include "devparam.h"

struct param {
	int number;
	char *name;
};
static struct param Parms[] = {
	PARAM_DATA,	"Data",
	PARAM_TXDELAY,	"TxDelay",
	PARAM_PERSIST,	"Persist",
	PARAM_SLOTTIME,	"SlotTime",
	PARAM_TXTAIL,	"TxTail",
	PARAM_FULLDUP,	"FullDup",
	PARAM_HW,	"Hardware",
	PARAM_MUTE,	"TxMute",
	PARAM_DTR,	"DTR",
	PARAM_RTS,	"RTS",
	PARAM_SPEED,	"Speed",
	PARAM_ENDDELAY,	"EndDelay",
	PARAM_GROUP,	"Group",
	PARAM_IDLE,	"Idle",
	PARAM_MIN,	"Min",
	PARAM_MAXKEY,	"MaxKey",
	PARAM_WAIT,	"Wait",
	PARAM_DOWN,	"Down",
	PARAM_UP,	"Up",
	PARAM_BLIND,	"Blind",
	PARAM_RETURN,	"Return",
	-1,		NULL,
};
	
/* Convert a packet radio interface control token into a number
 * Used by the various ioctl routines and by KISS TNC commands
 */
int
devparam(s)
char *s;
{
	int len;
	struct param *sp;

	len = strlen(s);
	if(isdigit(s[0]))
		return atoi(s);

	sp = &Parms[0];
	while(sp->number != -1){
		if(strnicmp(s,sp->name,len) == 0)
			return sp->number;
		sp++;
	}		
	return -1;
}

char *
parmname(n)
int n;
{
	struct param *sp;

	sp = &Parms[0];
	while(sp->number != -1){
		if(sp->number == n)
			return sp->name;
		sp++;
	}		
	return NULL;
}


