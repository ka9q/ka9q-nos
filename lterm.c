/* Support local term on com port */
#include <stdio.h>
#include "global.h"
#include "internet.h"
#include "netuser.h"
#include "socket.h"
#include "session.h"
#include "n8250.h"
#include "asy.h"

static void lterm_rx(int,void *,void *);

int
dolterm(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	FILE *network = NULL;
	struct iface *ifp;
	int (*rawsave)(struct iface *,struct mbuf **);
	int s;	/* Network socket */
	struct sockaddr_in fsocket;
	struct session *sp;
	int c;
	int otrigchar;

	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(ifp->dev >= ASY_MAX || Asy[ifp->dev].iface != ifp ){
		printf("Interface %s not asy port\n",argv[1]);
		return 1;
	}
	if(ifp->raw == bitbucket){
		printf("tip or dialer session already active on %s\n",argv[1]);
		return 1;
	}
	fsocket.sin_family = AF_INET;
	if((fsocket.sin_addr.s_addr = resolve(argv[2])) == 0){
		printf(Badhost,argv[2]);
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	if(argc > 3)
		fsocket.sin_port = atoi(argv[3]);
	else
		fsocket.sin_port = IPPORT_TELNET;

	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,TIP,1)) == NULL){
		printf("Too many sessions\n");
		return 1;
	}
	/* Save output handler and temporarily redirect output to null */
	rawsave = ifp->raw;
	ifp->raw = bitbucket;

	/* Suspend the packet input driver. Note that the transmit driver
	 * is left running since we use it to send buffers to the line.
	 */
	suspend(ifp->rxproc);

	/* Temporarily change the trigger character */
	otrigchar = Asy[ifp->dev].trigchar;
	Asy[ifp->dev].trigchar = -1;

#ifdef	notdef
	/* Wait for CD (wired to DTR from local terminal) to go high */
	get_rlsd_asy(ifp->dev,1);
#endif
	if((s = socket(AF_INET,SOCK_STREAM,0)) == -1){
		printf("Can't create socket\n");
		keywait(NULL,1);
		freesession(sp);
		goto cleanup;
	}
	settos(s,LOW_DELAY);
	network = fdopen(s,"r+b");
	setvbuf(network,NULL,_IONBF,0);
	if(connect(s,(struct sockaddr *)&fsocket,SOCKSIZE) == -1){
		perror("connect failed");
		keywait(NULL,1);
		freesession(sp);
		goto cleanup;
	}
	/* Spawn task to handle network -> serial port traffic */
	sp->proc1 = newproc("lterm",512,lterm_rx,ifp->dev,(void *)network,NULL,0);

	/* Loop sending from the serial port to the network */
	while((c = get_asy(ifp->dev)) != -1){
		putchar(c);
		putc(c,network);
		fflush(network);
	}			
cleanup:
	killproc(sp->proc1);
	sp->proc1 = NULL;
	ifp->raw = rawsave;
	resume(ifp->rxproc);
	keywait(NULL,1);
	freesession(sp);
	return 0;
}
/* Task to handle network -> serial port traffic */
static void
lterm_rx(dev,n1,n2)
int dev;
void *n1,*n2;
{
	int c;
	char c1;
	FILE *network = (FILE *)n1;

	while((c = fgetc(network)) != EOF){
		c1 = c;
		putchar(c1);
		asy_write(dev,(uint8 *)&c1,1);
		Asy[dev].iface->lastsent = secclock();
	}
}

