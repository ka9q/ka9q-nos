/* Miscellaneous Internet servers: discard, echo and remote
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "proc.h"
#include "remote.h"
#include "smtp.h"
#include "tcp.h"
#include "commands.h"
#include "hardware.h"
#include "mailbox.h"
#include "asy.h"
#include "n8250.h"
#include "devparam.h"
#include "telnet.h"

char *Rempass = "";	/* Remote access password */

static int chkrpass(struct mbuf *bp);
static void discserv(int s,void *unused,void *p);
static void echoserv(int s,void *unused,void *p);
static void termserv(int s,void *unused,void *p);
static void termrx(int s,void *p1,void *p2);
static void tunregister(struct iface *,int);
static void tregister(struct iface *);

static int Rem = -1;
static int Bsr = -1;

struct tserv {
	struct tserv *next;
	struct proc *proc;
	struct iface *ifp;
};
struct tserv *Tserv;

/* Start up TCP discard server */
int
dis1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_DISCARD;
	else
		port = atoi(argv[1]);
	return start_tcp(port,"Discard Server",discserv,576);
}
static void
discserv(s,unused,p)
int s;
void *unused;
void *p;
{
	struct mbuf *bp;

	sockowner(s,Curproc);
	logmsg(s,"open discard");
	if(availmem() == 0){
		while(recv_mbuf(s,&bp,0,NULL,NULL) > 0)
			free_p(&bp);
	}
	logmsg(s,"close discard");
	close_s(s);
}
/* Stop discard server */
int
dis0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_DISCARD;
	else
		port = atoi(argv[1]);
	return stop_tcp(port);
}
/* Start up TCP echo server */
int
echo1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_ECHO;
	else
		port = atoi(argv[1]);
	return start_tcp(port,"Echo Server",echoserv,512);
}
static void
echoserv(s,unused,p)
int s;
void *unused;
void *p;
{
	struct mbuf *bp;

	sockowner(s,Curproc);
	logmsg(s,"open echo");
	if(availmem() == 0){
		while(recv_mbuf(s,&bp,0,NULL,NULL) > 0)
			send_mbuf(s,&bp,0,NULL,0);
	}
	logmsg(s,"close echo");
	close_s(s);
}
/* stop echo server */
int
echo0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_ECHO;
	else
		port = atoi(argv[1]);
	return stop_tcp(port);
}
/* Start remote exit/reboot server */
int
rem1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct sockaddr_in lsocket,fsock;
	int i;
	int command;
	struct mbuf *bp;
	int32 addr;
	int (*kp)(int32);

	if(Rem != -1){
		return 0;
	}
	ksignal(Curproc,0);
	chname(Curproc,"Remote listener");
	lsocket.sin_family = AF_INET;
	lsocket.sin_addr.s_addr = INADDR_ANY;
	if(argc < 2)
		lsocket.sin_port = IPPORT_REMOTE;
	else
		lsocket.sin_port = atoi(argv[1]);
	
	Rem = socket(AF_INET,SOCK_DGRAM,0);
	bind(Rem,(struct sockaddr *)&lsocket,sizeof(lsocket));
	for(;;){
		i = sizeof(fsock);
		if(recv_mbuf(Rem,&bp,0,(struct sockaddr *)&fsock,&i) == -1)
			break;
		command = PULLCHAR(&bp);

		switch(command){
#ifdef	MSDOS	/* Only present on PCs running MSDOS */
		case SYS_RESET:
			i = chkrpass(bp);
			logmsg(Rem,"%s - Remote reset %s",
			 psocket((struct sockaddr *)&fsock),
			 i == 0 ? "PASSWORD FAIL" : "" );
			if(i != 0){
				iostop();
				sysreset();	/* No return */
			}
			break;
#endif
		case SYS_EXIT:
			i = chkrpass(bp);
			logmsg(Rem,"%s - Remote exit %s",
			 psocket((struct sockaddr *)&fsock),
			 i == 0 ? "PASSWORD FAIL" : "" );
			if(i != 0){
				iostop();
				exit(0);
			}
			break;
		case KICK_ME:
			if(len_p(bp) >= sizeof(int32))
				addr = pull32(&bp);
			else
				addr = fsock.sin_addr.s_addr;
			for(i=0;(kp = Kicklist[i]) != NULL;i++)
				(*kp)(addr);
			break;
		}
		free_p(&bp);
	}
	close_s(Rem);
	Rem = -1;
	return 0;
}
/* Check remote password */
static int
chkrpass(bp)
struct mbuf *bp;
{
	char *lbuf;
	uint16 len;
	int rval = 0;

	len = len_p(bp);
	if(strlen(Rempass) != len)
		return rval;
	lbuf = mallocw(len);
	pullup(&bp,lbuf,len);
	if(strncmp(Rempass,lbuf,len) == 0)
		rval = 1;
	free(lbuf);
	return rval;
}
int
rem0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	close_s(Rem);
	return 0;
}

/* Start up TCP term server */
int
term1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_TERM;
	else
		port = atoi(argv[1]);
	return start_tcp(port,"Term Server",termserv,576);
}
static void
termserv(s,unused,p)
int s;
void *unused;
void *p;
{
	FILE *network = NULL;
	FILE *asy;
	char *buf = NULL;
	struct iface *ifp;
	struct route *rp;
	struct sockaddr_in fsocket;
	struct proc *rxproc = NULL;
	int i;
	
	sockowner(s,Curproc);
	logmsg(s,"open term");
	network = fdopen(s,"r+");

	if(network == NULL || (buf = malloc(BUFSIZ)) == NULL)
		goto quit;

	if(SETSIG(EABORT)){
		fprintf(network,"Abort\r\n");
		goto quit;
	}
	/* Prompt for and check remote password */
	fprintf(network,"Password: ");
	fgets(buf,BUFSIZ,network);
	rip(buf);
	if(strcmp(buf,Rempass) != 0){
		fprintf(network,"Login incorrect\n");
		goto quit;
	}
	/* Prompt for desired interface. Verify that it exists, that
	 * we're not using it for our TCP connection, that it's an
	 * asynch port, and that there isn't already another tip, term
	 * or dialer session active on it.
	 */
	for(;;){
		fprintf(network,"Interface: ");
		fgets(buf,BUFSIZ,network);
		rip(buf);
		if((ifp = if_lookup(buf)) == NULL){
			fprintf(network,"Interface %s does not exist\n",buf);
			continue;
		}
		if(getpeername(s,(struct sockaddr *)&fsocket,&i) != -1
		 && !ismyaddr(fsocket.sin_addr.s_addr)
		 && (rp = rt_lookup(fsocket.sin_addr.s_addr)) != NULL
		 && rp->iface == ifp){
			fprintf(network,"You're using interface %s!\n",ifp->name);
			continue;
		}
		if((asy = asyopen(buf,"r+b")) != NULL)
			break;
		fprintf(network,"Can't open interface %s\n",buf);
		fprintf(network,"Try to bounce current user? ");
		fgets(buf,BUFSIZ,network);
		if(buf[0] == 'y' || buf[0] == 'Y'){
			tunregister(ifp,1);
			kwait(NULL);
		}
	}
	setvbuf(asy,NULL,_IONBF,0);
	tregister(ifp);
	fprintf(network,"Wink DTR? ");
	fgets(buf,BUFSIZ,network);
	if(buf[0] == 'y' || buf[0] == 'Y'){
		asy_ioctl(ifp,PARAM_DTR,1,0);	/* drop DTR */
		ppause(1000L);
		asy_ioctl(ifp,PARAM_DTR,1,1);	/* raise DTR */
	}
	fmode(network,STREAM_BINARY);	/* Switch to raw mode */
	setvbuf(network,NULL,_IONBF,0);
	fprintf(network,"Turn off local echo? ");
	fgets(buf,BUFSIZ,network);
	if(buf[0] == 'y' || buf[0] == 'Y'){
		fprintf(network,"%c%c%c",IAC,WILL,TN_ECHO);
		/* Eat the response */
		for(i=0;i<3;i++)
			(void)fgetc(network);
	}
#ifdef	notdef
	FREE(buf);
#endif
	/* Now fork into receive and transmit processes */
	rxproc = newproc("term rx",1500,termrx,s,network,asy,0);

	/* We continue to handle the TCP->asy direction */
	fblock(network,PART_READ);
	while((i = fread(buf,1,BUFSIZ,network)) > 0)
		fwrite(buf,1,i,asy);
quit:	fclose(network);
	fclose(asy);
	killproc(rxproc);
	logmsg(s,"close term");
	free(buf);
	close_s(s);
	tunregister(ifp,0);
}
void
termrx(s,p1,p2)
int s;
void *p1,*p2;
{
	int i;
	FILE *network = (FILE *)p1;
	FILE *asy = (FILE *)p2;
	char buf[BUFSIZ];
	
	fblock(asy,PART_READ);
	while((i = fread(buf,1,BUFSIZ,asy)) > 0){
		fwrite(buf,1,i,network);
		kwait(NULL);
	}
}
void
tregister(ifp)
struct iface *ifp;
{
	struct tserv *tserv;

	tserv = (struct tserv *)calloc(1,sizeof(struct tserv));
	tserv->ifp = ifp;
	tserv->proc = Curproc;
	tserv->next = Tserv;
	Tserv = tserv;
}
void
tunregister(ifp,kill)
struct iface *ifp;
int kill;
{
	struct tserv *tserv;
	struct tserv *prev = NULL;

	for(tserv = Tserv;tserv != NULL;prev = tserv,tserv = tserv->next){
		if(tserv->ifp == ifp)
			break;
	}
	if(tserv == NULL)
		return;
	if(kill)
		alert(tserv->proc,EABORT);

	if(prev == NULL)
		Tserv = tserv->next;
	else
		prev->next = tserv->next;
	free(tserv);
}


/* Stop term server */
int
term0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_TERM;
	else
		port = atoi(argv[1]);
	return stop_tcp(port);
}
/* Start BSR server */
int
bsr1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct sockaddr_in lsocket,fsock;
	int i,c;
	struct mbuf *bp;
	char *cp;
	uint8 *dp;
	FILE *asy;

	if(Bsr != -1)
		return 1;

	ksignal(Curproc,0);
	chname(Curproc,"BSR listener");

	if((asy = asyopen(argv[1],"r+b")) == NULL){
		printf("Can't open interface %s\n",argv[1]);
		return 1;
	}
	/* Set up the UDP socket where we'll take commands */
	lsocket.sin_family = AF_INET;
	lsocket.sin_addr.s_addr = INADDR_ANY;
	if(argc < 3)
		lsocket.sin_port = IPPORT_BSR;
	else
		lsocket.sin_port = atoi(argv[2]);
	
	Bsr = socket(AF_INET,SOCK_DGRAM,0);
	bind(Bsr,(struct sockaddr *)&lsocket,sizeof(lsocket));

	/* Process commands */
	for(;;){
		i = sizeof(fsock);
		if(recv_mbuf(Bsr,&bp,0,(struct sockaddr *)&fsock,&i) == -1)
			break;
		/* Check password */
		for(cp = Rempass;;cp++){
			c = PULLCHAR(&bp);
			if(c == -1 || *cp != c)
				goto endcmd;
			if(*cp == '\0')
				break;
		}
		/* Send remainder of packet to BSR */
		while((c = PULLCHAR(&bp)) != -1){
			fputc(c,asy);
		}
		free_p(&bp);	/* Shouldn't be necessary */

		/* Now generate response */
		bp = ambufw(512);	/* Larger than max response */
		dp = bp->data;
		kalarm(500L);	/* Allow BSR time to respond */
		while((c = fgetc(asy)) != -1){
			kalarm(100L);	/* Reset timer */
			*dp++ = c;
			bp->cnt++;
		}
		kalarm(0L);
		/* Send response */
		send_mbuf(Bsr,&bp,0,(struct sockaddr *)&fsock,sizeof(fsock));
endcmd:
		free_p(&bp);
	}
	fclose(asy);
	close_s(Bsr);
	Bsr = -1;
	return 0;
}

int
bsr0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	close_s(Bsr);
	return 0;
}
