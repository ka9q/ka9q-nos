/* "Dumb terminal" session command for serial lines
 * Copyright 1991 Phil Karn, KA9Q
 *
 *	Feb '91	Bill Simpson
 *		rlsd control and improved dialer
 */
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#ifndef	UNIX
#include "n8250.h"
#endif
#include "asy.h"
#include "tty.h"
#include "session.h"
#include "socket.h"
#include "commands.h"
#include "devparam.h"


static void tip_out(int i,void *n1,void *n2);


/* Execute user telnet command */
int
dotip(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;
	char *ifn;
	int c;
	FILE *asy;

	if((asy = asyopen(argv[1],"r+b")) == NULL){
		printf("Can't open %s\n",argv[1]);
		return 1;
	}
	setvbuf(asy,NULL,_IONBF,0);
	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,TIP,1)) == NULL){
		printf("Too many sessions\n");
		return 1;
	}
	/* Put tty into raw mode */
	sp->ttystate.echo = 0;
	sp->ttystate.edit = 0;
	fmode(stdin,STREAM_BINARY);
	fmode(stdout,STREAM_BINARY);

	/* Now fork into two paths, one rx, one tx */
	ifn = malloc(strlen(argv[1]) + 10);
	sprintf(ifn,"%s tip out",argv[1]);
	sp->proc1 = newproc(ifn,256,tip_out,0,asy,NULL,0);
	free( ifn );

	ifn = malloc(strlen(argv[1]) + 10);
	sprintf(ifn,"%s tip in",argv[1]);
	chname( Curproc, ifn );
	free( ifn );

	while((c = fgetc(asy)) != EOF){
		putchar(c);
		if(sp->record != NULL)
			putc(c,sp->record);
	}
	fflush(stdout);

	killproc(sp->proc1);
	sp->proc1 = NULL;
	fclose(asy);
	keywait(NULL,1);
	freesession(sp);
	return 0;
}


/* Output process, DTE version */
static void
tip_out(i,n1,n2)
int i;
void *n1,*n2;
{
	int c;
	FILE *asy = (FILE *)n1;

	while((c = getchar()) != EOF){
		fputc(c,asy);
	}
}


