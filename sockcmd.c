/* Socket status display code
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "lzw.h"
#include "usock.h"
#include "socket.h"
#include "commands.h"

/* Socket status display command */
int
dosock(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct usock *up;
	int s,i,n;
	struct sockaddr fsock;
	struct socklink *sp;
	char *cp;

	if(argc < 2){
		printf("S#   Type    PCB       Remote socket         Owner\n");
		for(n=0;n<Nsock;n++){
			s = _mk_fd(n,_FL_SOCK);
			up = itop(s);
			if(up == NULL)
				continue;

			i = sizeof(fsock);
			if(getpeername(s,&fsock,&i) == 0 && i != 0)
				cp = psocket(&fsock);
			else
				cp = "";

			printf("%4d %-8s%-9p %-22s%-9p %-10s\n",
			 s,Socktypes[up->type],up->cb.p,cp,
			 up->owner,up->owner->name);
		}
		return 0;
	}
	s = atoi(argv[1]);
	if(_fd_type(s) != _FL_SOCK){
		printf("Not a valid socket\n");
		return 1;
	}
	up = itop(s);
	if(up == NULL){
		printf("Socket not in use\n");
		return 1;
	}
	sp = up->sp;
	printf("%s %p\n",Socktypes[up->type],up->cb.p);
	if(up->cb.p == NULL)
		return 0;
	if(sp->status != NULL)
		(*sp->status)(up);
	return 0;	
}

