/* "Dumb terminal" mailbox interface
 * Copyright 1991 Phil Karn, KA9Q
 *
 *	May '91	Bill Simpson
 *		move to separate file for compilation & linking
 *	Sep '91 Bill Simpson
 *		minor changes for DTR & RLSD
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#ifndef	UNIX
#include "n8250.h"
#endif
#include "asy.h"
#include "socket.h"
#include "usock.h"
#include "telnet.h"
#include "mailbox.h"
#include "tipmail.h"
#include "devparam.h"

static struct tipcb {
	struct tipcb *next;
	struct proc *proc;
	struct proc *in;
	struct iface *iface;
	int (*rawsave)(struct iface *,struct mbuf **);
	FILE *network;
	int echo;
	struct timer timer;
} *Tiplist;
#define	NULLTIP	(struct tipcb *)0

static void tip_in(int dev,void *n1,void *n2);
static void tipidle(void *t);

unsigned Tiptimeout = 180;	/* Default tip inactivity timeout (seconds) */

/* Input process */
static void
tip_in(dev,n1,n2)
int dev;
void *n1,*n2;
{
	struct tipcb *tip;
	struct mbuf *bp;
	char *buf[2], line[MBXLINE];
	int c, ret, pos = 0;

	tip = (struct tipcb *) n1;
	while((c = get_asy(dev)) != -1){
		Asy[dev].iface->lastrecv = secclock();
		ret = 0;
		if(tip->echo == WONT){
			switch(c){
			case 18:	/* CTRL-R */
				bp = NULL;
				pushdown(&bp,line,pos);
				pushdown(&bp,"^R\r\n",4);
				ret = 1;
				break;
			case 0x7f:	/* DEL */
			case '\b':
				bp = NULL;
				if(pos){
					--pos;
					bp = qdata("\b \b",3);
				}
				ret = 1;
				break;
			case '\r':
				c = '\n';	/* CR => NL */
			case '\n':
				bp = qdata("\r\n",2);
				break;
			default:
				bp = NULL;
				pushdown(&bp,NULL,1);
				*bp->data = c;
				break;
			}
			asy_send(dev,&bp);
			tip->iface->lastsent = secclock();
			if(ret)
				continue;
		}
		line[pos++] = c;
		if(pos == MBXLINE - 1 || tip->echo == WILL
		  || c == '\n'){
			line[pos] = '\0';
			pos = 0;
			fputs(line,tip->network);
			fflush(tip->network);
		}
	}
	/* get_asy() failed, terminate */
	fclose(tip->network);
	tip->in = tip->proc;
	tip->proc = Curproc;
	buf[1] = Asy[dev].iface->name;
	tip0(2,buf,NULL);
}
/* Start mailbox on serial line */
int
tipstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;
	register struct asy *ap;
	struct tipcb *tip;
	struct mbuf *bp;
	char *buf[2];
	int dev, c, cmd, s[2], type = TIP;

	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	for(dev=0,ap = Asy;dev < ASY_MAX;dev++,ap++)
		if(ap->iface == ifp)
			break;
	if(dev == ASY_MAX){
		printf("Interface %s not asy port\n",argv[1]);
		return 1;
	}
	if(ifp->raw == bitbucket){
		printf("Tip session already active on %s\n",argv[1]);
		return 1;
	}
	ksignal(Curproc,0);	/* Don't keep the parser waiting */
	chname(Curproc,"Mbox tip");
	tip = (struct tipcb *) callocw(1,sizeof(struct tipcb));

	/* Save output handler and temporarily redirect output to null */
	tip->rawsave = ifp->raw;
	ifp->raw = bitbucket;
	tip->iface = ifp;
	tip->proc = Curproc;
	tip->timer.func = tipidle;
	tip->timer.arg = (void *) tip;
	tip->next = Tiplist;
	Tiplist = tip;
	buf[1] = ifp->name;

	/* Suspend packet input drivers */
	suspend(ifp->rxproc);

	for(;;) {
		/* Wait for DCD to be asserted */
		get_rlsd_asy(dev,1);

		if(socketpair(AF_LOCAL,SOCK_STREAM,0,s) == -1){
			printf("Could not create socket pair, errno %d\n",errno);
			tip0(2,buf,p);
			return 1;
		}
		tip->echo = WONT;
		tip->network = fdopen(s[0],"r+t");
		newproc("mbx_incom",2048,mbx_incom,s[1],(void *)type,NULL,0);
		set_timer(&tip->timer,Tiptimeout*1000);
		start_timer(&tip->timer);

		/* Now fork into two paths, one rx, one tx */
		tip->in = newproc("Mbox tip in",
				256,tip_in,dev,(void *)tip,NULL,0);
		while((c = getc(tip->network)) != -1) {
			if(c == IAC){	/* ignore most telnet options */
				if((cmd = getc(tip->network)) == -1)
					break;
				if(cmd > 250 && cmd < 255) {
					if((c = getc(tip->network)) == -1)
						break;
					switch(cmd){
					case WILL:
						if(c == TN_ECHO) {
							tip->echo = cmd;
							cmd = DO;
						}
						else
							cmd = DONT;
						break;
					case WONT:
						if(c == TN_ECHO)
							tip->echo = cmd;
						cmd = DONT;
						break;
					case DO:
					case DONT:
						cmd = WONT;
						break;
					}
					fprintf(tip->network,"%c%c%c",IAC,cmd,c);
					fflush(tip->network);
				}
				continue;
			}
			if(c == '\n')
				bp = qdata("\r\n",2);
			else {
				bp = NULL;
				pushdown(&bp,NULL,1);
				*bp->data = c;
			}
			asy_send(dev,&bp);
			ifp->lastsent = secclock();
		}
		fclose(tip->network);
		killproc(tip->in);
		tip->in = NULL;
		kwait(itop(s[1])); /* let mailbox terminate, if necessary */
		stop_timer(&tip->timer);

		/* Tell line to go down */
		ifp->ioctl(ifp,PARAM_DOWN,TRUE,0L);

		/* Wait for DCD to be dropped */
		get_rlsd_asy(dev,0);
	}
}
int
tip0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;
	struct tipcb *tip, *prev = NULLTIP;
	struct proc *proc;

	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	for(tip = Tiplist; tip != NULLTIP; prev = tip, tip = tip->next)
		if(tip->iface == ifp) {
			if(prev != NULLTIP)
				prev->next = tip->next;
			else
				Tiplist = tip->next;
			proc = tip->proc;
			fclose(tip->network);
			ifp->raw = tip->rawsave;
			resume(ifp->rxproc);
			stop_timer(&tip->timer);
			killproc(tip->in);
			free(tip);
			killproc(proc);
			return 0;
		}
	return 0;
}
static void
tipidle(t)
void *t;
{
	struct tipcb *tip;
	static char *msg = "You have been idle too long. Please hang up.\r\n";
	struct mbuf *bp;
	tip = (struct tipcb *) t;
	if(secclock() - tip->iface->lastrecv < Tiptimeout){
		set_timer(&tip->timer,(Tiptimeout-secclock() *
		 tip->iface->lastrecv)*1000);
		start_timer(&tip->timer);
		return;
	}
	bp = qdata(msg,strlen(msg));
	asy_send(tip->iface->dev,&bp);
	tip->iface->lastsent = secclock();
	fclose(tip->network);
}

static int Stelnet = -1;

/* Start up Telnet server */
int
telnet1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct sockaddr_in lsocket;
	int s;
	int type;

	if(Stelnet != -1){
		return 0;
	}
	ksignal(Curproc,0); 	/* Don't keep the parser waiting */
	chname(Curproc,"Telnet listener");

	lsocket.sin_family = AF_INET;
	lsocket.sin_addr.s_addr = INADDR_ANY;
	if(argc < 2)
		lsocket.sin_port = IPPORT_TELNET;
	else
		lsocket.sin_port = atoi(argv[1]);
	Stelnet = socket(AF_INET,SOCK_STREAM,0);
	bind(Stelnet,(struct sockaddr *)&lsocket,sizeof(lsocket));
	listen(Stelnet,1);
	for(;;){
		if((s = accept(Stelnet,NULL,(int *)NULL)) == -1)
			break;	/* Service is shutting down */

		if(availmem() != 0){
			shutdown(s,1);
		} else {
			/* Spawn a server */
			type = TELNET;
			newproc("mbox",2048,mbx_incom,s,(void *)type,NULL,0);
		}
	}
	return 0;
}
/* Stop telnet server */
int
telnet0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	close_s(Stelnet);
	Stelnet = -1;
	return 0;
}

