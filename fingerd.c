/* Internet Finger server
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "files.h"
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "dirutil.h"
#include "commands.h"
#include "mailbox.h"

static void fingerd(int s,void *unused,void *p);

/* Start up finger service */
int
finstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_FINGER;
	else
		port = atoi(argv[1]);

	return start_tcp(port,"Finger Server",fingerd,512);
}
static void
fingerd(s,n,p)
int s;
void *n;
void *p;
{
	char user[80];
	FILE *fp;
	char *file,*cp;
	FILE *network;

	network = fdopen(s,"r+t");

	sockowner(s,Curproc);
	logmsg(s,"open Finger");
	fgets(user,sizeof(user),network);
	rip(user);
	if(strlen(user) == 0){
		fp = dir(Fdir,0);
		if(fp == NULL)
			fprintf(network,"No finger information available\n");
		else
			fprintf(network,"Known users on this system:\n");
	} else {
		file = pathname(Fdir,user);
		cp = pathname(Fdir,"");
		/* Check for attempted security violation (e.g., somebody
		 * might be trying to finger "../ftpusers"!)
		 */
		if(strncmp(file,cp,strlen(cp)) != 0){
			fp = NULL;
			fprintf(network,"Invalid user name %s\n",user);
		} else if((fp = fopen(file,READ_TEXT)) == NULL)
			fprintf(network,"User %s not known\n",user);
		free(cp);
		free(file);
	}
	if(fp != NULL){
		sendfile(fp,network,ASCII_TYPE,0);
		fclose(fp);
	}
	if(strlen(user) == 0 && Listusers != NULL)
		(*Listusers)(network);
	fclose(network);
	logmsg(s,"close Finger");
}
int
fin0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_FINGER;
	else
		port = atoi(argv[1]);

	return stop_tcp(port);
}
