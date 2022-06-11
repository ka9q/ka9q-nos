/* Internet Telnet client
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#ifdef	__TURBOC__
#include <io.h>
#include <fcntl.h>
#endif
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "telnet.h"
#include "session.h"
#include "proc.h"
#include "tty.h"
#include "commands.h"
#include "internet.h"
#include "netuser.h"
#include "cmdparse.h"

int Refuse_echo = 0;
int Tn_cr_mode = 0;    /* if true turn <cr> to <cr-nul> */
int Topt = 0;

char *T_options[] = {
	"Transmit Binary",
	"Echo",
	"",
	"Suppress Go Ahead",
	"",
	"Status",
	"Timing Mark"
};

static int keychar(int c);

int
dotopt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	setbool(&Topt,"Telnet option tracing",argc,argv);
	return 0;
}
/* Execute user telnet command */
int
dotelnet(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;
	struct sockaddr_in fsocket;
	int s;

	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,TELNET,1)) == NULL){
		printf("Too many sessions\n");
		return 1;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	fsocket.sin_family = AF_INET;
	if(argc < 3)
		fsocket.sin_port = IPPORT_TELNET;
	else
		fsocket.sin_port = atoi(argv[2]);

	if(SETSIG(EABORT)){
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	printf("Resolving %s...\n",argv[1]);
	if((fsocket.sin_addr.s_addr = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	if((s = socket(AF_INET,SOCK_STREAM,0)) == -1){
		printf("Can't create socket\n");
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	settos(s,LOW_DELAY);
	sp->network = fdopen(s,"r+t");
	setvbuf(sp->network,NULL,_IOLBF,BUFSIZ);
	return tel_connect(sp,(struct sockaddr *)&fsocket,SOCKSIZE);
}
/* Generic interactive connect routine, used by Telnet, AX.25, NET/ROM */
int
tel_connect(sp,fsocket,len)
struct session *sp;
struct sockaddr *fsocket;
int len;
{
	struct telnet tn;

	memset(&tn,0,sizeof(tn));
	tn.eolmode = Tn_cr_mode;
	tn.session = sp;	/* Upward pointer */
	sp->cb.telnet = &tn;	/* Downward pointer */

	printf("Trying %s...\n",psocket(fsocket));
	if(connect(fileno(sp->network),fsocket,len) == -1){
		perror("connect failed");
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	printf("Connected\n");
	sp->inproc = NULL;	/* No longer respond to ^C */	
	tnrecv(&tn);
	return 0;
}

/* Telnet input routine, common to both telnet and ttylink */
void
tnrecv(tn)
struct telnet *tn;
{
	int c;
	struct session *sp;
	char *cp;
	FILE *network;

	sp = tn->session;
	network = sp->network;

	/* Fork off the transmit process */
	sp->proc1 = newproc("tel_out",1024,tel_output,0,tn,NULL,0);

	/* Process input on the connection */
	while((c = getc(network)) != EOF){
		if(c != IAC){
			/* Ordinary character */
			putchar((char)c);
			if(sp->record != NULL)
				putc(c,sp->record);
			continue;
		}
		/* IAC received, get command sequence */
		c = getc(network);
		switch(c){
		case WILL:
			c = getc(network);
			willopt(tn,c);
			break;
		case WONT:
			c = getc(network);
			wontopt(tn,c);
			break;
		case DO:
			c = getc(network);
			doopt(tn,c);
			break;
		case DONT:
			c = getc(network);
			dontopt(tn,c);
			break;
		case IAC:	/* Escaped IAC */
			putchar(IAC);
			if(sp->record != NULL)
				putc(IAC,sp->record);
			break;
		}
	}
quit:	/* A close was received from the remote host.
	 * Notify the user, kill the output task and wait for a response
	 * from the user before freeing the session.
	 */
	fmode(sp->output,STREAM_ASCII); /* Restore newline translation */
	setvbuf(sp->output,NULL,_IOLBF,BUFSIZ);
	cp = sockerr(fileno(network));
	printf("Closed: %s\n", cp != NULL ? cp : "EOF");
	killproc(sp->proc1);
	sp->proc1 = NULL;
	fclose(sp->network);
	sp->network = NULL;
	keywait(NULL,1);
	freesession(sp);
}

/* User telnet output task, started by user telnet command */
void
tel_output(unused,tn1,p)
int unused;
void *tn1;
void *p;
{
	struct session *sp;
	int c;
	struct telnet *tn;

	tn = (struct telnet *)tn1;
	sp = tn->session;

	/* Send whatever's typed on the terminal */
	while((c = getc(sp->input)) != EOF){
		putc(c,sp->network);
		if(!tn->remote[TN_ECHO] && sp->record != NULL)
			putc(c,sp->record);

		/* By default, output is transparent in remote echo mode.
		 * If eolmode is set, turn a cr into cr-null.
		 * This can only happen when in remote echo (raw) mode, since
		 * the tty driver normally maps \r to \n in cooked mode.
		 */
		if(c == '\r' && tn->eolmode)
			putc('\0',sp->network);

		if(tn->remote[TN_ECHO])
			fflush(sp->network);
	}
	/* Make sure our parent doesn't try to kill us after we exit */
	sp->proc1 = NULL;
}
int
doecho(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		if(Refuse_echo)
			printf("Refuse\n");
		else
			printf("Accept\n");
	} else {
		if(argv[1][0] == 'r')
			Refuse_echo = 1;
		else if(argv[1][0] == 'a')
			Refuse_echo = 0;
		else
			return -1;
	}
	return 0;
}
/* set for unix end of line for remote echo mode telnet */
int
doeol(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		if(Tn_cr_mode)
			printf("null\n");
		else
			printf("standard\n");
	} else {
		if(argv[1][0] == 'n')
			Tn_cr_mode = 1;
		else if(argv[1][0] == 's')
			Tn_cr_mode = 0;
		else {
			printf("Usage: %s [standard|null]\n",argv[0]);
			return -1;
		}
	}
	return 0;
}

/* The guts of the actual Telnet protocol: negotiating options */
void
willopt(tn,opt)
struct telnet *tn;
int opt;
{
	int ack;

	if(Topt){
		printf("recv: will ");
		if(opt <= NOPTIONS)
			printf("%s\n",T_options[opt]);
		else
			printf("%u\n",opt);
	}
	switch(opt){
	case TN_TRANSMIT_BINARY:
	case TN_ECHO:
	case TN_SUPPRESS_GA:
		if(tn->remote[opt] == 1)
			return;		/* Already set, ignore to prevent loop */
		if(opt == TN_ECHO){
			if(Refuse_echo){
				/* User doesn't want to accept */
				ack = DONT;
				break;
			} else {
				/* Put tty into raw mode */
				tn->session->ttystate.edit = 0;
				tn->session->ttystate.echo = 0;
				fmode(tn->session->network,STREAM_BINARY);
				setvbuf(tn->session->network,NULL,_IONBF,0);
				fmode(stdout,STREAM_BINARY);
				setvbuf(stdout,NULL,_IONBF,0);
			}
		}
		tn->remote[opt] = 1;
		ack = DO;			
		break;
	default:
		ack = DONT;	/* We don't know what he's offering; refuse */
	}
	answer(tn,ack,opt);
}
void
wontopt(tn,opt)
struct telnet *tn;
int opt;
{
	if(Topt){
		printf("recv: wont ");
		if(opt <= NOPTIONS)
			printf("%s\n",T_options[opt]);
		else
			printf("%u\n",opt);
	}
	if(opt <= NOPTIONS){
		if(tn->remote[opt] == 0)
			return;		/* Already clear, ignore to prevent loop */
		tn->remote[opt] = 0;
		if(opt == TN_ECHO){
			/* Put tty into cooked mode */
			tn->session->ttystate.edit = 1;
			tn->session->ttystate.echo = 1;
			fmode(tn->session->network,STREAM_ASCII);
			setvbuf(tn->session->network,NULL,_IOLBF,BUFSIZ);
			fmode(stdout,STREAM_ASCII);
			setvbuf(stdout,NULL,_IOLBF,BUFSIZ);
		}
	}
	answer(tn,DONT,opt);	/* Must always accept */
}
void
doopt(tn,opt)
struct telnet *tn;
int opt;
{
	int ack;

	if(Topt){
		printf("recv: do ");
		if(opt <= NOPTIONS)
			printf("%s\n",T_options[opt]);
		else
			printf("%u\n",opt);
	}
	switch(opt){
	case TN_SUPPRESS_GA:
		if(tn->local[opt] == 1)
			return;		/* Already set, ignore to prevent loop */
		tn->local[opt] = 1;
		ack = WILL;
		break;
	default:
		ack = WONT;	/* Don't know what it is */
	}
	answer(tn,ack,opt);
}
void
dontopt(tn,opt)
struct telnet *tn;
int opt;
{
	if(Topt){
		printf("recv: dont ");
		if(opt <= NOPTIONS)
			printf("%s\n",T_options[opt]);
		else
			printf("%u\n",opt);
	}
	if(opt <= NOPTIONS){
		if(tn->local[opt] == 0){
			/* Already clear, ignore to prevent loop */
			return;
		}
		tn->local[opt] = 0;
	}
	answer(tn,WONT,opt);
}
void
answer(tn,r1,r2)
struct telnet *tn;
int r1,r2;
{
	if(Topt){
		switch(r1){
		case WILL:
			printf("sent: will ");
			break;
		case WONT:
			printf("sent: wont ");
			break;
		case DO:
			printf("sent: do ");
			break;
		case DONT:
			printf("sent: dont ");
			break;
		}
		if(r2 <= NOPTIONS)
			printf("%s\n",T_options[r2]);
		else
			printf("%u\n",r2);
	}
	fprintf(tn->session->network,"%c%c%c",IAC,r1,r2);
	fflush(tn->session->network);
}
static int
keychar(c)
int c;
{
	if(c != CTLC)
		return 1;	/* Ignore all but ^C */

	fprintf(Current->output,"^C\n");
	alert(Current->proc,EABORT);
	return 0;
}
