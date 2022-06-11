/* Automatic line dialer for asynch ports running SLIP, PPP, etc.
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 *	Mar '91	Bill Simpson & Glenn McGregor
 *		completely re-written;
 *		human readable control file;
 *		includes wait for string, and speed sense;
 *		dials immediately when invoked.
 *	May '91 Bill Simpson
 *		re-ordered command line;
 *		allow dial only;
 *		allow inactivity timeout without ping.
 *	Sep '91 Bill Simpson
 *		Check known DTR & RSLD state for redial decision
 *	Mar '92	Phil Karn
 *		autosense modem control stuff removed
 *		Largely rewritten to do demand dialing
 */
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "netuser.h"
#include "n8250.h"
#include "asy.h"
#include "tty.h"
#include "socket.h"
#include "cmdparse.h"
#include "devparam.h"
#include "files.h"
#include "main.h"
#include "trace.h"
#include "commands.h"
#include "dialer.h"

static int redial(struct iface *ifp,char *file);
static void dropline(void *);
static void dropit(int,void *,void *);
int dialer_kick(struct iface *ifp);
void sd_answer(int,void *,void *);

static int dodial_control(int argc,char *argv[],void *p);
static int dodial_send(int argc,char *argv[],void *p);
static int dodial_speed(int argc,char *argv[],void *p);
static int dodial_status(int argc,char *argv[],void *p);
static int dodial_wait(int argc,char *argv[],void *p);

static struct cmds Dial_cmds[] = {
	"",		donothing,	0, 0, "",
	"control",	dodial_control,	0, 2, "control up | down",
	"send",		dodial_send,	0, 2,
	"send \"string\" [<milliseconds>]",
	"speed",	dodial_speed,	0, 2, "speed <bps>",
	"status",	dodial_status, 0, 2, "status up | down",
	"wait",		dodial_wait,	0, 2,
	"wait <milliseconds> [ \"string\" [speed] ]",
	NULL,	NULL,		0, 0, "Unknown command",
};
/* Set up demand dialing on an asy link. Called from dodialer command
 * in iface.c.
 */
int
sd_init(ifp,timeout,argc,argv)
struct iface *ifp;
int32 timeout;
int argc;
char *argv[];
{
	struct asy *ap;
	struct asydialer *dialer;
	char *cp;

	if(ifp->dev >= ASY_MAX || Asy[ifp->dev].iface != ifp){
		/* "Can't happen" */
		printf("Interface %s not asy port\n",argv[1]);
		return 1;
	}
	ap = &Asy[ifp->dev];
	if(timeout != 0 && argc < 3){
		printf("Usage: dial <iface> <timeout> <raisefile> <dropfile> <ringfile>\n");
		printf("       dial <iface> 0\n");
		return 1;
	}
	if(!ap->rlsd){
		printf("Must set 'r' flag at attach time\n");
		return 1;
	}
	if(ifp->dstate != NULL){
		/* Get rid of the old dialer info */
		dialer = (struct asydialer *)ifp->dstate;
		stop_timer(&dialer->idle);
		if(dialer->actfile != NULL){
			free(dialer->actfile);
			dialer->actfile = NULL;
		}
		if(dialer->dropfile != NULL){
			free(dialer->dropfile);
			dialer->dropfile = NULL;
		}
		if(dialer->ansfile != NULL){
			free(dialer->ansfile);
			dialer->ansfile = NULL;
		}
		free(ifp->dstate);
		ifp->dstate = NULL;
	}
	killproc(ifp->supv);
	ifp->supv = NULL;

	dialer = (struct asydialer *)calloc(1,sizeof(struct asydialer));
	ifp->dstate = dialer;

	ifp->dtickle = dialer_kick;
	set_timer(&dialer->idle,timeout);
	dialer->idle.func = dropline;
	dialer->idle.arg = ifp;
	if(timeout != 0){
		dialer->actfile = strdup(argv[0]);
		dialer->dropfile = strdup(argv[1]);
		dialer->ansfile = strdup(argv[2]);
		cp = if_name(ifp," answerer");
		ifp->supv = newproc(cp,768,sd_answer,ifp->dev,ifp,NULL,0);
	}
	return 0;
}
/* Display status of asynch dialer. Called from dodialer command in
 * iface.c when invoked without full args
 */
int
sd_stat(ifp)
struct iface *ifp;
{
	struct asydialer *dialer = (struct asydialer *)ifp->dstate;
	struct asy *ap;

	if(dialer == NULL){
		printf("No dialer active on %s\n",ifp->name);
		return 1;
	}
	ap = &Asy[ifp->dev];
	printf("%s: %s,",ifp->name,(ap->msr & MSR_RLSD) ? "UP":"DOWN");
	printf(" idle timer %lu/%lu sec\n",read_timer(&dialer->idle)/1000L,
	  dur_timer(&dialer->idle)/1000L);
	if(dialer->actfile != NULL)
		printf("up script: %s\n",dialer->actfile);
	if(dialer->dropfile != NULL)
		printf("down script: %s\n",dialer->dropfile);
	if(dialer->ansfile != NULL)
		printf("answer script: %s\n",dialer->ansfile);
	printf("Calls originated %lu, Calls answered %lu\n",
		dialer->originates,dialer->answers);
	printf("Calls timed out %lu, carrier transitions %lu\n",
		dialer->localdrops,ap->cdchanges);
	return 0;
}
/* Called by interface output routine just before sending each packet */
int
dialer_kick(ifp)
struct iface *ifp;
{
	struct asy *asyp;
	struct asydialer *dialer;

	dialer = (struct asydialer *)ifp->dstate;
	asyp = &Asy[ifp->dev];
	stop_timer(&dialer->idle);
	if(asyp->rlsd && !(asyp->msr & MSR_RLSD)
	 && dialer->actfile != NULL){
		/* Line down, try a redial */
		dialer->originates++;
		if(redial(ifp,dialer->actfile) != 0 
		 || !(asyp->msr & MSR_RLSD)){
			/* Redial failed, drop line and return failure */
			dialer->localdrops++;
			redial(ifp,dialer->dropfile);
			return -1;
		}
	}
	start_timer(&dialer->idle);
	return 0;
}

/* Called when idle line timer expires -- executes script to drop line */
static void
dropline(p)
void *p;
{
	/* Fork this off to prevent wedging the timer task */
	newproc("dropit",1024,dropit,0,p,NULL,0);
}

static void
dropit(i,p,u)
int i;
void *p;
void *u;
{
	struct iface *ifp = p;
	struct asy *ap;
	struct asydialer *dialer;

	dialer = (struct asydialer *)ifp->dstate;
	ap = &Asy[ifp->dev];
	if(ap->msr & MSR_RLSD){
		dialer->localdrops++;
		redial(ifp,dialer->dropfile);	/* Drop only if still up */
	}
}

void
sd_answer(dev,p1,p2)
int dev;
void *p1,*p2;
{
	struct iface *ifp;
	struct asydialer *dialer;
	struct asy *asyp;

	ifp = (struct iface *)p1;
	asyp = &Asy[dev];
	dialer = (struct asydialer *)ifp->dstate;
	if(dialer == NULL)
		return;	/* Can't happen */
	for(;;){
		while((asyp->msr & MSR_TERI) == 0)
			kwait(&asyp->msr);
		asyp->msr &= ~MSR_TERI;
		dialer->answers++;
		redial(ifp,dialer->ansfile);
	}
}

/* execute dialer commands
 * returns: -1 fatal error, 0 OK, 1 try again
 */
static int
redial(ifp,file)
struct iface *ifp;
char *file;
{
	char *inbuf;
	FILE *fp;
	int (*rawsave)(struct iface *,struct mbuf **);
	int result = 0;

	if((fp = fopen(file,READ_TEXT)) == NULL){
		if(ifp->trace & (IF_TRACE_IN|IF_TRACE_OUT))
			tprintf(ifp,"redial: can't read %s\n",file);
		return -1;
	}
	/* Save output handler and temporarily redirect output to null */
	if(ifp->raw == bitbucket){
		if(ifp->trace & (IF_TRACE_IN|IF_TRACE_OUT))
			tprintf(ifp,"redial: tip or dialer already active on %s\n",ifp->name);

		return -1;
	}

	if(ifp->trace & (IF_TRACE_IN|IF_TRACE_OUT))
		tprintf(ifp,"Commands to %s:\n",ifp->name);

	/* Save output handler and temporarily redirect output to null */
	rawsave = ifp->raw;
	ifp->raw = bitbucket;

	/* Suspend the packet input driver. Note that the transmit driver
	 * is left running since we use it to send buffers to the line.
	 */
	suspend(ifp->rxproc);

	inbuf = mallocw(BUFSIZ);
	while(fgets(inbuf,BUFSIZ,fp) != NULL){
		rip(inbuf);
		logmsg(-1,"%s dialer: %s",ifp->name,inbuf);
		if(ifp->trace & (IF_TRACE_IN|IF_TRACE_OUT))
			tprintf(ifp,"%s\n",inbuf);
		if((result = cmdparse(Dial_cmds,inbuf,ifp)) != 0){
			break;
		}
	}
	free(inbuf);
	fclose(fp);

	if(result == 0){
		ifp->lastsent = ifp->lastrecv = secclock();
	}
	ifp->raw = rawsave;
	resume(ifp->rxproc);
	if(ifp->trace & (IF_TRACE_IN|IF_TRACE_OUT))
		tprintf(ifp,"\nDial %s complete\n",ifp->name);

	return result;
}


static int
dodial_control(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;
	int param;
	int32 arg = 0;

	if(ifp->ioctl == NULL)
		return -1;

	if((param = devparam(argv[1])) == -1) 
		return -1;

	if(argc > 2)
		arg = atol(argv[2]);
	(*ifp->ioctl)(ifp,param,TRUE,arg);
	return 0;
}


static int
dodial_send(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;
	struct mbuf *bp;

	if(argc > 2){
		/* Send characters with inter-character delay
		 * (for dealing with prehistoric Micom switches that
		 * can't take back-to-back characters...yes, they
		 * still exist.)
		 */
		char *cp;
		int32 cdelay = atol(argv[2]);

		for(cp = argv[1];*cp != '\0';cp++){
			asy_write(ifp->dev,(uint8 *)cp,1);
			ppause(cdelay);
		}
	} else {
		if (ifp->trace & IF_TRACE_RAW)
			raw_dump( ifp, IF_TRACE_OUT, bp );
		asy_write(ifp->dev,(uint8 *)argv[1],strlen(argv[1]));
	}
	return 0;
}


static int
dodial_speed(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	if ( argc < 2 ) {
		if(ifp->trace & (IF_TRACE_IN|IF_TRACE_OUT))
			tprintf(ifp,"current speed = %u bps\n", Asy[ifp->dev].speed);
		return 0;
	}
	return asy_speed(ifp->dev,(uint16)atol(argv[1]));
}


static int
dodial_status(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;
	int param;

	if(ifp->iostatus == NULL)
		return -1;

	if((param = devparam(argv[1])) == -1)
		return -1;

	(*ifp->iostatus)(ifp,param,atol(argv[2]));
	return 0;
}


static int
dodial_wait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;
	register int c = -1;
	char *cp;

	kalarm(atol(argv[1]));

	if(argc == 2){
		while((c = get_asy(ifp->dev)) != -1 && errno != EALARM){
			if(ifp->trace & IF_TRACE_IN){
				fputc(c,ifp->trfp);
				fflush(ifp->trfp);
			}
		}
		kalarm(0L);
		return 0;
	}
	/* argc > 2 */
	cp = argv[2];

	while(*cp != '\0' && (c = get_asy(ifp->dev)) != -1){
		if(ifp->trace & IF_TRACE_IN){
			fputc(c,ifp->trfp);
			fflush(ifp->trfp);
		}
		if(*cp++ != c){
			cp = argv[2];
		}
	}
	if(argc > 3){
		uint16 speed = 0;

		if(stricmp(argv[3], "speed") != 0)
			return -1;

		while((c = get_asy(ifp->dev)) != -1){
			if(ifp->trace & IF_TRACE_IN){
				fputc(c,ifp->trfp);
				fflush(ifp->trfp);
			}
			if(isdigit(c)){
				speed *= 10;
				speed += c - '0';
			} else {
				kalarm(0L);
				return asy_speed(ifp->dev,speed);
			}
		}
	}
	kalarm(0L);
	return (c == -1);
}


