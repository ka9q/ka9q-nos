/* Internet finger client
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "netuser.h"
#include "commands.h"
#include "tty.h"

static int keychar(int c);

int
dofinger(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct sockaddr_in sock;
	char *cp;
	int s,i;
	int c;
	struct session *sp;
	FILE *network;

	/* Allocate a session descriptor */
	if((sp = newsession(Cmdline,FINGER,1)) == NULL){
		printf("Too many sessions\n");
		keywait(NULL,1);
		return 1;
	}
	sp->inproc = keychar;	/* Intercept ^C */
	sp->ttystate.echo = sp->ttystate.edit = 0;
	sock.sin_family = AF_INET;
	sock.sin_port = IPPORT_FINGER;
	for(i=1;i<argc;i++){
		cp = strchr(argv[i],'@');
		if(cp == NULL){
			printf("%s: local names not supported\n",argv[i]);
			continue;
		}
		*cp++ = '\0';
		printf("%s@%s:\n",argv[i],cp);
		printf("Resolving %s...\n",cp);
		if((sock.sin_addr.s_addr = resolve(cp)) == 0){
			printf("unknown\n");
			continue;
		}
		printf("Trying %s...\n",psocket((struct sockaddr *)&sock));
		if((s = socket(AF_INET,SOCK_STREAM,0)) == -1){
			printf("Can't create socket\n");
			break;
		}
		if(connect(s,(struct sockaddr *)&sock,sizeof(sock)) == -1){
			cp = sockerr(s);
			printf("Connect failed: %s\n",cp != NULL ? cp : "");
			close_s(s);
			continue;
		}
		printf("Connected\n");
		
		sp->network = network = fdopen(s,"r+t");
		fprintf(network,"%s\n",argv[i]);
		fflush(stdout);
		while((c = getc(network)) != EOF)
			putchar(c);

		fclose(network);
		sp->network = NULL;
	}
	keywait(NULL,1);
	freesession(sp);
	return 0;
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

