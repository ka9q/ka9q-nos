/* Internet FTP client (interactive user)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "session.h"
#include "cmdparse.h"
#include "proc.h"
#include "tty.h"
#include "socket.h"
#include "ftp.h"
#include "ftpcli.h"
#include "commands.h"
#include "netuser.h"
#include "dirutil.h"
#include "internet.h"

#define	POLLRATE	500	/* 500ms between more file polls */
#define	DIRBUF	256

static int doascii(int argc,char *argv[],void *p);
static int dobatch(int argc,char *argv[],void *p);
static int dobinary(int argc,char *argv[],void *p);
static int docompare(int argc,char *argv[],void *p);
static int doftpcd(int argc,char *argv[],void *p);
static int doget(int argc,char *argv[],void *p);
static int dohash(int argc,char *argv[],void *p);
static int doverbose(int argc,char *argv[],void *p);
static int dolist(int argc,char *argv[],void *p);
static int dols(int argc,char *argv[],void *p);
static int domd5(int argc,char *argv[],void *p);
static int domkdir(int argc,char *argv[],void *p);
static int domcompare(int argc,char *argv[],void *p);
static int domget(int argc,char *argv[],void *p);
static int domput(int argc,char *argv[],void *p);
static int doput(int argc,char *argv[],void *p);
static int doquit(int argc,char *argv[],void *p);
static int doread(int argc,char *argv[],void *p);
static int dormdir(int argc,char *argv[],void *p);
static int dotype(int argc,char *argv[],void *p);
static int doupdate(int argc,char *argv[],void *p);
static int getline(struct session *sp,char *prompt,char *buf,int n);
static int getresp(struct ftpcli *ftp,int mincode);
static long getsub(struct ftpcli *ftp,char *command,char *remotename,
	FILE *fp);
static long putsub(struct ftpcli *ftp,char *remotename,char *localname);
static int compsub(struct ftpcli *ftp,char *localname,char *remotename);
static void sendport(FILE *fp,struct sockaddr_in *socket);
static int keychar(int c);

static char Notsess[] = "Not an FTP session!\n";

static struct cmds Ftpcmds[] = {
	"",		donothing,	0, 0, NULL,
	"ascii",	doascii,	0, 0, NULL,
	"batch",	dobatch,	0, 0, NULL,
	"binary",	dobinary,	0, 0, NULL,
	"cd",		doftpcd,	0, 2, "cd <directory>",
	"compare",	docompare,	0, 2, "compare <remotefile> [<localfile>]",
	"dir",		dolist,		0, 0, NULL,
	"list",		dolist,		0, 0, NULL,
	"get",		doget,		0, 2, "get <remotefile> <localfile>",
	"hash",		dohash,		0, 0, NULL,
	"ls",		dols,		0, 0, NULL,
	"mcompare",	domcompare,	0, 2, "mcompare <file> [<file> ...]",
	"md5",		domd5,		0, 2, "md5 <file>",
	"mget",		domget,		0, 2, "mget <file> [<file> ...]",
	"mkdir",	domkdir,	0, 2, "mkdir <directory>",
	"mput",		domput,		0, 2, "mput <file> [<file> ...]",
	"nlst",		dols,		0, 0, NULL,
	"quit",		doquit,		0, 0, NULL,
	"read",		doread,		0, 2, "read <remotefile>",
	"rmdir",	dormdir,	0, 2, "rmdir <directory>",
	"put",		doput,		0, 2, "put <localfile> <remotefile>",
	"type",		dotype,		0, 0, NULL,
	"update",	doupdate,	0, 0, NULL,
	"verbose",	doverbose,	0, 0, NULL,
	NULL,	NULL,		0, 0, NULL,
};

/* Handle top-level FTP command */
int
doftp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;
	struct ftpcli ftp;
	struct sockaddr_in fsocket;
	int resp,vsave;
	char *bufsav,*cp;
	FILE *control;
	int s;

	/* Allocate a session control block */
	if((sp = newsession(Cmdline,FTP,1)) == NULL){
		printf("Too many sessions\n");
		return 1;
	}
	sp->inproc = keychar;
	memset(&ftp,0,sizeof(ftp));
	ftp.control = ftp.data = NULL;
	ftp.verbose = V_NORMAL;

	sp->cb.ftp = &ftp;	/* Downward link */
	ftp.session = sp;	/* Upward link */

	fsocket.sin_family = AF_INET;
	if(argc < 3)
		fsocket.sin_port = IPPORT_FTP;
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
	/* Open the control connection */
	if((s = socket(AF_INET,SOCK_STREAM,0)) == -1){
		printf("Can't create socket\n");
		keywait(NULL,1);
		freesession(sp);
		return 1;
	}
	if(SETSIG(EABORT)){
		goto quit;
	}
	sp->network = control = ftp.control = fdopen(s,"r+t");
	settos(s,LOW_DELAY);
	printf("Trying %s...\n",psocket(&fsocket));
	if(connect(s,(struct sockaddr *)&fsocket,sizeof(fsocket)) == -1){
		perror("Connect failed");
		goto quit;
	}
	printf("Connected\n");

	/* Wait for greeting from server */
	resp = getresp(&ftp,200);

	if(resp >= 400)
		goto quit;
	/* Now process responses and commands */

	if(SETSIG(EABORT)){
		/* Come back here after a ^C in command state */
		resp = 200;
	}
	while(resp != -1){
		switch(resp){
		case 220:
			/* Sign-on banner; prompt for and send USER command */
			getline(sp,"Enter user name: ",ftp.buf,LINELEN);
			/* Send the command only if the user response
			 * was non-null
			 */
			if(ftp.buf[0] != '\n'){
				fprintf(control,"USER %s",ftp.buf);
				resp = getresp(&ftp,200);
			} else
				resp = 200;	/* dummy */
			break;
		case 331:
			/* turn off echo */
			sp->ttystate.echo = 0;
			getline(sp,"Password: ",ftp.buf,LINELEN);
			printf("\n");
			/* Turn echo back on */
			sp->ttystate.echo = 1;
			/* Send the command only if the user response
			 * was non-null
			 */
			if(ftp.buf[0] != '\n'){
				fprintf(control,"PASS %s",ftp.buf);
				resp = getresp(&ftp,200);
			} else
				resp = 200;	/* dummy */
			break;
		case 230:	/* Successful login */
			/* Find out what type of system we're talking to */
			printf("ftp> syst\n");
			fprintf(control,"SYST\n");
			resp = getresp(&ftp,200);
			break;
		case 215:
			/* Response to SYST command */
			cp = strchr(ftp.line,' ');
			if(cp != NULL && strnicmp(cp+1,System,strlen(System)) == 0){
				ftp.type = IMAGE_TYPE;
				printf("Defaulting to binary mode\n");
			}
			resp = 200;	/* dummy */
			break;
		default:
			/* Test the control channel first */
			if(sockstate(fileno(control)) == NULL){
				resp = -1;
				break;
			}
			getline(sp,"ftp> ",ftp.buf,LINELEN);

			/* Copy because cmdparse modifies the original */
			bufsav = strdup(ftp.buf);
			if((resp = cmdparse(Ftpcmds,ftp.buf,&ftp)) != -1){
				/* Valid command, free buffer and get another */
				FREE(bufsav);
			} else {
				/* Not a local cmd, send to remote server */
				fputs(bufsav,control);
				FREE(bufsav);

				/* Enable display of server response */
				vsave = ftp.verbose;
				ftp.verbose = V_NORMAL;
				resp = getresp(&ftp,200);
				ftp.verbose = vsave;
			}
		}
	}
quit:	cp = sockerr(fileno(control));
	printf("Closed: %s\n",cp != NULL ? cp : "EOF");

	if(ftp.fp != NULL && ftp.fp != stdout)
		fclose(ftp.fp);
	if(ftp.data != NULL)
		fclose(ftp.data);
	if(ftp.control != NULL){
		fclose(ftp.control);
		ftp.control = NULL;
		sp->network = NULL;
	}
	keywait(NULL,1);
	if(ftp.session != NULL)
		freesession(ftp.session);
	return 0;
}

/* Control verbosity level */
static int
doverbose(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	if((ftp = (struct ftpcli *)p) == NULL)
		return -1;
	return setshort(&ftp->verbose,"Verbose",argc,argv);
}
/* Enable/disable command batching */
static int
dobatch(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	if((ftp = (struct ftpcli *)p) == NULL)
		return -1;
	return setbool(&ftp->batch,"Command batching",argc,argv);
}
/* Enable/disable update flag */
static int
doupdate(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	if((ftp = (struct ftpcli *)p) == NULL)
		return -1;
	return setbool(&ftp->update,"Update with MD5",argc,argv);
}
/* Set verbosity to high (convenience command) */
static int
dohash(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	if((ftp = (struct ftpcli *)p) == NULL)
		return -1;
	ftp->verbose = V_HASH;
	return 0;
}
	
/* Close session */
static int
doquit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL)
		return -1;
	fprintf(ftp->control,"QUIT\n");
	getresp(ftp,200);	/* Get the closing message */
	getresp(ftp,200);	/* Wait for the server to close */
	return -1;
}

/* Translate 'cd' to 'cwd' for convenience */
static int
doftpcd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL)
		return -1;
	fprintf(ftp->control,"CWD %s\n",argv[1]);
	return getresp(ftp,200);
}
/* Translate 'mkdir' to 'xmkd' for convenience */
static int
domkdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL)
		return -1;
	fprintf(ftp->control,"XMKD %s\n",argv[1]);
	return getresp(ftp,200);
}
/* Translate 'rmdir' to 'xrmd' for convenience */
static int
dormdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL)
		return -1;
	fprintf(ftp->control,"XRMD %s\n",argv[1]);
	return getresp(ftp,200);
}
static int
dobinary(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *args[2];

	args[1] = "I";
	return dotype(2,args,p);
}
static int
doascii(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *args[2];

	args[1] = "A";
	return dotype(2,args,p);
}

/* Handle "type" command from user */
static int
dotype(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL)
		return -1;
	if(argc < 2){
		switch(ftp->type){
		case IMAGE_TYPE:
			printf("Image\n");
			break;
		case ASCII_TYPE:
			printf("Ascii\n");
			break;
		case LOGICAL_TYPE:
			printf("Logical bytesize %u\n",ftp->logbsize);
			break;
		}
		return 0;
	}
	switch(*argv[1]){
	case 'i':
	case 'I':
	case 'b':
	case 'B':
		ftp->type = IMAGE_TYPE;
		break;
	case 'a':
	case 'A':
		ftp->type = ASCII_TYPE;
		break;
	case 'L':
	case 'l':
		ftp->type = LOGICAL_TYPE;
		ftp->logbsize = atoi(argv[2]);
		break;
	default:
		printf("Invalid type %s\n",argv[1]);
		return 1;
	}
	return 0;
}
/* Start receive transfer. Syntax: get <remote name> [<local name>] */
static int
doget(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename,*localname;
	register struct ftpcli *ftp;
	FILE *fp;
	char *mode;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL){
		printf(Notsess);
		return 1;
	}
	remotename = argv[1];
	if(argc < 3)
		localname = remotename;
	else
		localname = argv[2];

	switch(ftp->type){
	case IMAGE_TYPE:
	case LOGICAL_TYPE:
		mode = WRITE_BINARY;
		break;
	case ASCII_TYPE:
		mode = WRITE_TEXT;
		break;
	}
	if((fp = fopen(localname,mode)) == NULL){
		printf("Can't write %s",localname);
		perror("");
		return 1;
	}
	getsub(ftp,"RETR",remotename,fp);
	fclose(fp);
	return 0;
}
/* Read file direct to screen. Syntax: read <remote name> */
static int
doread(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;

	if((ftp = (struct ftpcli *)p) == NULL){
		printf(Notsess);
		return 1;
	}
	getsub(ftp,"RETR",argv[1],stdout);
	return 0;
}
/* Get a collection of files */
static int
domget(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;
	FILE *files,*fp;
	char *buf,*mode;
	int i;
	long r;

	if((ftp = (struct ftpcli *)p) == NULL){
		printf(Notsess);
		return 1;
	}
	switch(ftp->type){
	case IMAGE_TYPE:
	case LOGICAL_TYPE:
		mode = WRITE_BINARY;
		break;
	case ASCII_TYPE:
		mode = WRITE_TEXT;
		break;
	}
	buf = mallocw(DIRBUF);
	ftp->state = RECEIVING_STATE;
	for(i=1;i<argc;i++){
		files = tmpfile();
		r = getsub(ftp,"NLST",argv[i],files);
		if(ftp->abort)
			break;	/* Aborted */
		if(r == -1){
			printf("Can't NLST %s\n",argv[i]);
			continue;
		}
		/* The tmp file now contains a list of the remote files, so
		 * go get 'em. Break out if the user signals an abort.
		 */
		rewind(files);
		while(fgets(buf,DIRBUF,files) != NULL){
			rip(buf);
			if(!ftp->update || compsub(ftp,buf,buf) != 0){
				if((fp = fopen(buf,mode)) == NULL){
					printf("Can't write %s",buf);
					perror("");
					continue;
				}
				getsub(ftp,"RETR",buf,fp);
				fclose(fp);
			}
			if(ftp->abort){
				/* User abort */
				ftp->abort = 0;
				fclose(files);
				free(buf);
				ftp->state = COMMAND_STATE;
				return 1;
			}
		}
		fclose(files);
	}
	free(buf);
	ftp->state = COMMAND_STATE;
	ftp->abort = 0;
	return 0;
}
/* List remote directory. Syntax: dir <remote files> [<local name>] */
static int
dolist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;
	FILE *fp;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL){
		printf(Notsess);
		return 1;
	}

	if(argc > 2)
		fp = fopen(argv[2],WRITE_TEXT);

	else
		fp = stdout;

	if(fp == NULL){
		printf("Can't write local file");
		perror("");
		return 1;
	}

	getsub(ftp,"LIST",argv[1],fp);
	return 0;
}
/* Remote directory list, short form. Syntax: ls <remote files> [<local name>] */
static int
dols(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;
	FILE *fp;

	if((ftp = (struct ftpcli *)p) == NULL){
		printf(Notsess);
		return 1;
	}
	if(argc > 2)
		fp = fopen(argv[2],WRITE_TEXT);

	else
		fp = stdout;

	if(fp == NULL){
		printf("Can't write local file");
		perror("");
		return 1;
	}
	getsub(ftp,"NLST",argv[1],fp);
	return 0;
}
static int
domd5(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename;
	register struct ftpcli *ftp;
	FILE *control;
	int resp;
	int typewait = 0;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL){
		printf(Notsess);
		return 1;
	}
	control = ftp->control;
	remotename = argv[1];
	if(ftp->typesent != ftp->type){
		switch(ftp->type){
		case ASCII_TYPE:
			fprintf(control,"TYPE A\n");
			break;
		case IMAGE_TYPE:
			fprintf(control,"TYPE I\n");
			break;
		case LOGICAL_TYPE:
			fprintf(control,"TYPE L %d\n",ftp->logbsize);
			break;
		}
		ftp->typesent = ftp->type;
		if(!ftp->batch){
			resp = getresp(ftp,200);
			if(resp == -1 || resp > 299)
				goto failure;
		} else
			typewait = 1;

	}
	fprintf(control,"XMD5 %s\n",remotename);
	if(typewait)
		(void)getresp(ftp,200);
	(void)getresp(ftp,200);
failure:;
	return 0;
}
static int
docompare(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename,*localname;
	register struct ftpcli *ftp;

	ftp = (struct ftpcli *)p;
	if(ftp == NULL){
		printf(Notsess);
		return 1;
	}
	remotename = argv[1];
	if(argc > 2)
		localname = argv[2];
	else
		localname = remotename;

	if(compsub(ftp,localname,remotename) == 0)
		printf("Same\n");
	else
		printf("Different\n");
	return 0;
}
/* Compare a collection of files */
static int
domcompare(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;
	FILE *files;
	char *buf;
	int i;
	long r;

	if((ftp = (struct ftpcli *)p) == NULL){
		printf(Notsess);
		return 1;
	}
	buf = mallocw(DIRBUF);
	ftp->state = RECEIVING_STATE;
	for(i=1;i<argc;i++){
		files = tmpfile();
		r = getsub(ftp,"NLST",argv[i],files);
		if(ftp->abort)
			break;	/* Aborted */
		if(r == -1){
			printf("Can't NLST %s\n",argv[i]);
			continue;
		}
		/* The tmp file now contains a list of the remote files, so
		 * go get 'em. Break out if the user signals an abort.
		 */
		rewind(files);
		while(fgets(buf,DIRBUF,files) != NULL){
			rip(buf);
			if(compsub(ftp,buf,buf) == 0)
				printf("%s - Same\n",buf);
			else
				printf("%s - Different\n",buf);

			if(ftp->abort){
				/* User abort */
				ftp->abort = 0;
				fclose(files);
				free(buf);
				ftp->state = COMMAND_STATE;
				return 1;
			}
		}
		fclose(files);
	}
	free(buf);
	ftp->state = COMMAND_STATE;
	ftp->abort = 0;
	return 0;
}
/* Common subroutine to compare a local with a remote file
 * Return 1 if files are different, 0 if they are the same
 */
static int
compsub(ftp,localname,remotename)
struct ftpcli *ftp;
char *localname;
char *remotename;
{
	char *mode,*cp;
	FILE *control,*fp;
	int resp,i;
	int typewait = 0;
	uint8 remhash[16];
	uint8 lochash[16];

	control = ftp->control;

	switch(ftp->type){
	case IMAGE_TYPE:
	case LOGICAL_TYPE:
		mode = READ_BINARY;
		break;
	case ASCII_TYPE:
		mode = READ_TEXT;
		break;
	}
	if((fp = fopen(localname,mode)) == NULL){
		printf("Can't read local file %s\n",localname);
		return 1;
	}
	if(ftp->typesent != ftp->type){
		switch(ftp->type){
		case ASCII_TYPE:
			fprintf(control,"TYPE A\n");
			break;
		case IMAGE_TYPE:
			fprintf(control,"TYPE I\n");
			break;
		case LOGICAL_TYPE:
			fprintf(control,"TYPE L %d\n",ftp->logbsize);
			break;
		}
		ftp->typesent = ftp->type;
		if(!ftp->batch){
			resp = getresp(ftp,200);
			if(resp == -1 || resp > 299)
				goto failure;
		} else
			typewait = 1;
	}
	fprintf(control,"XMD5 %s\n",remotename);
	/* Try to overlap the two MD5 operations */
	md5hash(fp,lochash,ftp->type == ASCII_TYPE);
	fclose(fp);
	if(typewait && (resp = getresp(ftp,200)) > 299)
		goto failure;
	if((resp = getresp(ftp,200)) > 299){
		if(resp == 500)
			ftp->update = 0;	/* XMD5 not supported */
		goto failure;
	}	
	if((cp = strchr(ftp->line,' ')) == NULL){
		printf("Error in response\n");
		goto failure;
	}
	/* Convert ascii/hex back to binary */
	readhex(remhash,cp,sizeof(remhash));
	if(ftp->verbose > 1){
		printf("Loc ");
		for(i=0;i<sizeof(lochash);i++)
			printf("%02x",lochash[i]);
		printf(" %s\n",localname);
	}
	if(memcmp(lochash,remhash,sizeof(remhash)) == 0)
		return 0;
	else
		return 1;
failure:;
	return 1;
}


/* Common code to LIST/NLST/RETR and mget
 * Returns number of bytes received if successful
 * Returns -1 on error
 */
static long
getsub(ftp,command,remotename,fp)
register struct ftpcli *ftp;
char *command,*remotename;
FILE *fp;
{
	unsigned long total;
	FILE *control;
	int cnt,resp,i,savmode;
	struct sockaddr_in lsocket;
	struct sockaddr_in lcsocket;
	int32 startclk,rate;
	int vsave;
	int typewait = 0;
	int prevstate;
	int d;

	if(ftp == NULL)
		return -1;
	savmode = ftp->type;
	control = ftp->control;

	/* Open the data connection */
	d = socket(AF_INET,SOCK_STREAM,0);
	listen(d,0);	/* Accept only one connection */

	switch(ftp->type){
	case IMAGE_TYPE:
	case LOGICAL_TYPE:
		ftp->data = fdopen(d,"r+b");
		break;
	case ASCII_TYPE:
		ftp->data = fdopen(d,"r+t");
		break;
	}
	prevstate = ftp->state;
	ftp->state = RECEIVING_STATE;

	/* Send TYPE message, if necessary */
	if(strcmp(command,"LIST") == 0 || strcmp(command,"NLST") == 0){
		/* Directory listings are always in ASCII */
		ftp->type = ASCII_TYPE;
	}
	if(ftp->typesent != ftp->type){
		switch(ftp->type){
		case ASCII_TYPE:
			fprintf(control,"TYPE A\n");
			break;
		case IMAGE_TYPE:
			fprintf(control,"TYPE I\n");
			break;
		case LOGICAL_TYPE:
			fprintf(control,"TYPE L %d\n",ftp->logbsize);
			break;
		}
		ftp->typesent = ftp->type;
		if(!ftp->batch){
			resp = getresp(ftp,200);
			if(resp == -1 || resp > 299)
				goto failure;
		} else
			typewait = 1;
	}
	/* Send the PORT message. Use the IP address
	 * on the local end of our control connection.
	 */
	i = SOCKSIZE;
	getsockname(d,(struct sockaddr *)&lsocket,&i); /* Get port number */
	i = SOCKSIZE;
	getsockname(fileno(ftp->control),(struct sockaddr *)&lcsocket,&i);
	lsocket.sin_addr.s_addr = lcsocket.sin_addr.s_addr;
	sendport(control,&lsocket);
	if(!ftp->batch){
		/* Get response to PORT command */
		resp = getresp(ftp,200);
		if(resp == -1 || resp > 299)
			goto failure;
	}

	/* Generate the command to start the transfer */
	if(remotename != NULL)
		fprintf(control,"%s %s\n",command,remotename);
	else
		fprintf(control,"%s\n",command);

	if(ftp->batch){
		/* Get response to TYPE command, if sent */
		if(typewait){
			resp = getresp(ftp,200);
			if(resp == -1 || resp > 299)
				goto failure;
		}
		/* Get response to PORT command */
		resp = getresp(ftp,200);
		if(resp == -1 || resp > 299)
			goto failure;
	}
	/* Get the intermediate "150" response */
	resp = getresp(ftp,100);
	if(resp == -1 || resp >= 400)
		goto failure;

	/* Wait for the server to open the data connection */
	cnt = 0;
	d = accept(d,NULL,&cnt);
	startclk = msclock();

	/* If output is to the screen, temporarily disable hash marking */
	vsave = ftp->verbose;
	if(vsave >= V_HASH && fp == NULL)
		ftp->verbose = V_NORMAL;
	total = recvfile(fp,ftp->data,ftp->type,ftp->verbose);
	/* Immediately close the data connection; some servers (e.g., TOPS-10)
	 * wait for the data connection to close completely before returning
	 * the completion message on the control channel
	 */
	fclose(ftp->data);
	ftp->data = NULL;

#ifdef	CPM
	if(fp != NULL && ftp->type == ASCII_TYPE)
		putc(CTLZ,fp);
#endif
	if(remotename == NULL)
		remotename = "";
	if(total == -1){
		printf("%s %s: Error/abort during data transfer\n",command,remotename);
	} else if(ftp->verbose >= V_SHORT){
		startclk = msclock() - startclk;
		rate = 0;
		if(startclk != 0){	/* Avoid divide-by-zero */
			if(total < 4294967L) {
				rate = (total*1000)/startclk;
			} else {	/* Avoid overflow */
				rate = total/(startclk/1000);
			}
		}
		printf("%s %s: %lu bytes in %lu sec (%lu/sec)\n",
		 command,remotename, total,startclk/1000,rate);
	}
	/* Get the "Sent" message */
	getresp(ftp,200);

	ftp->state = prevstate;
	ftp->verbose = vsave;
	ftp->type = savmode;
	return total;

failure:
	/* Error, quit */
	if(fp != NULL && fp != stdout)
		fclose(fp);
	fclose(ftp->data);
	ftp->data = NULL;
	ftp->state = prevstate;
	ftp->type = savmode;
	return -1;
}
/* Send a file. Syntax: put <local name> [<remote name>] */
static int
doput(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;
	char *remotename,*localname;

	if((ftp = (struct ftpcli *)p) == NULL){
		printf(Notsess);
		return 1;
	}
	localname = argv[1];
	if(argc < 3)
		remotename = localname;
	else
		remotename = argv[2];

	putsub(ftp,remotename,localname);
	return 0;
}
/* Put a collection of files */
static int
domput(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftpcli *ftp;
	FILE *files;
	int i;
	char *buf;

	if((ftp = (struct ftpcli *)p) == NULL){
		printf(Notsess);
		return 1;
	}
	if((files = tmpfile()) == NULL){
		printf("Can't list local files\n");
		return 1;
	}
	for(i=1;i<argc;i++)
		getdir(argv[i],0,files);

	rewind(files);
	buf = mallocw(DIRBUF);
	ftp->state = SENDING_STATE;
	while(fgets(buf,DIRBUF,files) != NULL){
		rip(buf);
		if(!ftp->update || compsub(ftp,buf,buf) != 0)
			putsub(ftp,buf,buf);
		if(ftp->abort)
			break;		/* User abort */
	}
	fclose(files);
	free(buf);
	ftp->state = COMMAND_STATE;
	ftp->abort = 0;
	return 0;
}
/* Common code to put, mput.
 * Returns number of bytes sent if successful
 * Returns -1 on error
 */
static long
putsub(ftp,remotename,localname)
register struct ftpcli *ftp;
char *remotename,*localname;
{
	char *mode;
	int i,resp,d;
	unsigned long total;
	FILE *fp,*control;
	struct sockaddr_in lsocket,lcsocket;
	int32 startclk,rate;
	int typewait = 0;
	int prevstate;

	control = ftp->control;
	if(ftp->type == IMAGE_TYPE)
		mode = READ_BINARY;
	else
		mode = READ_TEXT;

	/* Open the file */
	if((fp = fopen(localname,mode)) == NULL){
		printf("Can't read %s: %s\n",localname,sys_errlist[errno]);
		return -1;
	}
	if(ftp->type == ASCII_TYPE && isbinary(fp)){
		printf("Warning: type is ASCII and %s appears to be binary\n",localname);
	}
	/* Open the data connection */
	d = socket(AF_INET,SOCK_STREAM,0);
	ftp->data = fdopen(d,"w+");
	listen(d,0);
	prevstate = ftp->state;
	ftp->state = SENDING_STATE;

	/* Send TYPE message, if necessary */
	if(ftp->typesent != ftp->type){
		switch(ftp->type){
		case ASCII_TYPE:
			fprintf(control,"TYPE A\n");
			break;
		case IMAGE_TYPE:
			fprintf(control,"TYPE I\n");
			break;
		case LOGICAL_TYPE:
			fprintf(control,"TYPE L %d\n",ftp->logbsize);
			break;
		}
		ftp->typesent = ftp->type;

		/* Get response to TYPE command */
		if(!ftp->batch){
			resp = getresp(ftp,200);
			if(resp == -1 || resp > 299){
				goto failure;
			}
		} else
			typewait = 1;
	}
	/* Send the PORT message. Use the IP address
	 * on the local end of our control connection.
	 */
	i = SOCKSIZE;
	getsockname(d,(struct sockaddr *)&lsocket,&i);
	i = SOCKSIZE;
	getsockname(fileno(ftp->control),(struct sockaddr *)&lcsocket,&i);
	lsocket.sin_addr.s_addr = lcsocket.sin_addr.s_addr;
	sendport(control,&lsocket);
	if(!ftp->batch){
		/* Get response to PORT command */
		resp = getresp(ftp,200);
		if(resp == -1 || resp > 299){
			goto failure;
		}
	}
	/* Generate the command to start the transfer */
	fprintf(control,"STOR %s\n",remotename);

	if(ftp->batch){
		/* Get response to TYPE command, if sent */
		if(typewait){
			resp = getresp(ftp,200);
			if(resp == -1 || resp > 299){
				goto failure;
			}
		}
		/* Get response to PORT command */
		resp = getresp(ftp,200);
		if(resp == -1 || resp > 299){
			goto failure;
		}
	}
	/* Get the intermediate "150" response */
	resp = getresp(ftp,100);
	if(resp == -1 || resp >= 400){
		goto failure;
	}

	/* Wait for the data connection to open. Otherwise the first
	 * block of data would go out with the SYN, and this may confuse
	 * some other TCPs
	 */
	accept(d,NULL,(int *)NULL);

	startclk = msclock();

	total = sendfile(fp,ftp->data,ftp->type,ftp->verbose);
	fflush(ftp->data);
	shutdown(fileno(ftp->data),1);	/* Send EOF (FIN) */
	fclose(fp);

	/* Wait for control channel ack before calculating transfer time;
	 * this accounts for transmitted data in the pipe
	 */
	getresp(ftp,200);
	fclose(ftp->data);
	ftp->data = NULL;

	if(total == -1){
		printf("STOR %s: Error/abort during data transfer\n",remotename);
	} else if(ftp->verbose >= V_SHORT){
		startclk = msclock() - startclk;
		rate = 0;
		if(startclk != 0){	/* Avoid divide-by-zero */
			if(total < 4294967L) {
				rate = (total*1000)/startclk;
			} else {	/* Avoid overflow */
				rate = total/(startclk/1000);
			}
		}
		printf("STOR %s: %lu bytes in %lu sec (%lu/sec)\n",
		 remotename,total,startclk/1000,rate);
	}
	ftp->state = prevstate;
	return total;

failure:
	/* Error, quit */
	fclose(fp);
	fclose(ftp->data);
	ftp->data = NULL;
	ftp->state = prevstate;
	return -1;
}
/* send PORT message */
static void
sendport(fp,socket)
FILE *fp;
struct sockaddr_in *socket;
{
	/* Send PORT a,a,a,a,p,p message */
	fprintf(fp,"PORT %u,%u,%u,%u,%u,%u\n",
		hibyte(hiword(socket->sin_addr.s_addr)),
		lobyte(hiword(socket->sin_addr.s_addr)),
		hibyte(loword(socket->sin_addr.s_addr)),
		lobyte(loword(socket->sin_addr.s_addr)),
		hibyte(socket->sin_port),
		lobyte(socket->sin_port));
}

/* Wait for, read and display response from FTP server. Return the result code.
 */
static int
getresp(ftp,mincode)
struct ftpcli *ftp;
int mincode;	/* Keep reading until at least this code comes back */
{
	int rval;

	fflush(ftp->control);
	for(;;){
		/* Get line */
		if(fgets(ftp->line,LINELEN,ftp->control) == NULL){
			rval = -1;
			break;
		}
		rip(ftp->line);		/* Remove cr/lf */
		rval = atoi(ftp->line);
		if(rval >= 400 || ftp->verbose >= V_NORMAL)
			printf("%s\n",ftp->line);	/* Display to user */

		/* Messages with dashes are continued */
		if(ftp->line[3] != '-' && rval >= mincode)
			break;
	}
	return rval;
}

/* Issue a prompt and read a line from the user */
static int
getline(sp,prompt,buf,n)
struct session *sp;
char *prompt;
char *buf;
int n;
{
	printf(prompt);
	fflush(stdout);
	fgets(buf,n,stdin);
	return strlen(buf);
}
static int
keychar(c)
int c;
{
	struct ftpcli *ftp;

	if(c != CTLC)
		return 1;	/* Ignore all but ^C */

	fprintf(Current->output,"^C\n");
	ftp = Current->cb.ftp;
	switch(ftp->state){
	case COMMAND_STATE:
		alert(Current->proc,EABORT);
		break;
	case SENDING_STATE:
		/* Send a premature EOF.
		 * Unfortunately we can't just reset the connection
		 * since the remote side might end up waiting forever
		 * for us to send something.
		 */
		shutdown(fileno(ftp->data),1);	/* Note fall-thru */
		ftp->abort = 1;
		break;
	case RECEIVING_STATE:
		/* Just blow away the receive socket */
		shutdown(fileno(ftp->data),2);	/* Note fall-thru */
		ftp->abort = 1;
		break;
	}
	return 0;
}
