/* Internet TTY "link" (keyboard chat) server
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "telnet.h"
#include "session.h"
#include "proc.h"
#include "tty.h"
#include "mailbox.h"
#include "commands.h"

static int Sttylink = -1;	/* Protoype socket for service */

int
ttylstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct sockaddr_in lsocket;
	int s,type;
	FILE *network;

	if(Sttylink != -1){
		return 0;
	}
	ksignal(Curproc,0);	/* Don't keep the parser waiting */
	chname(Curproc,"TTYlink listener");

	lsocket.sin_family = AF_INET;
	lsocket.sin_addr.s_addr = INADDR_ANY;
	if(argc < 2)
		lsocket.sin_port = IPPORT_TTYLINK;
	else
		lsocket.sin_port = atoi(argv[1]);

	Sttylink = socket(AF_INET,SOCK_STREAM,0);
	bind(Sttylink,(struct sockaddr *)&lsocket,sizeof(lsocket));
	listen(Sttylink,1);
	for(;;){
		if((s = accept(Sttylink,NULL,(int *)NULL)) == -1)
			break;	/* Service is shutting down */
		
		network = fdopen(s,"r+t");
		if(availmem() != 0){
			fprintf(network,"System is overloaded; try again later\n");
			fclose(network);
		} else {
			type = TELNET;
			newproc("chat",2048,ttylhandle,s,
			 (void *)&type,(void *)network,0);
		}
	}
	return 0;
}
/* This function handles all incoming "chat" sessions, be they TCP,
 * NET/ROM or AX.25
 */
void
ttylhandle(s,t,p)
int s;
void *t;
void *p;
{
	int type;
	struct session *sp;
	struct sockaddr addr;
	int len = MAXSOCKSIZE;
	struct telnet tn;
	FILE *network;
	char *tmp;

	type = * (int *)t;
	network = (FILE *)p;
	sockowner(fileno(network),Curproc);	/* We own it now */
	getpeername(fileno(network),&addr,&len);
	logmsg(fileno(network),"open %s",Sestypes[type]);
	tmp = malloc(BUFSIZ);
	sprintf(tmp,"ttylink %s",psocket(&addr));

	/* Allocate a session descriptor */
	if((sp = newsession(tmp,type,1)) == NULL){
		fprintf(network,"Too many sessions\n");
		fclose(network);
		free(tmp);
		return;
	}
	free(tmp);
	/* Initialize a Telnet protocol descriptor */
	memset(&tn,0,sizeof(tn));
	tn.session = sp;	/* Upward pointer */
	sp->cb.telnet = &tn;	/* Downward pointer */
	sp->network = network;
	sp->proc = Curproc;
	setvbuf(sp->network,NULL,_IOLBF,BUFSIZ);

	printf("\007Incoming %s session %u from %s\007\n",
	 Sestypes[type],sp->index,psocket(&addr));

	tnrecv(&tn);
}

/* Shut down Ttylink server */
int
ttyl0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	close_s(Sttylink);
	Sttylink = -1;
	return 0;
}
