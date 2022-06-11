/* Internet FTP Server
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#ifdef	__TURBOC__
#include <io.h>
#include <dir.h>
#endif
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "socket.h"
#include "dirutil.h"
#include "commands.h"
#include "files.h"
#include "ftp.h"
#include "ftpserv.h"
#include "md5.h"

static void ftpserv(int s,void *unused,void *p);
static int pport(struct sockaddr_in *sock,char *arg);
static void ftplogin(struct ftpserv *ftp,char *pass);
static int sendit(struct ftpserv *ftp,char *command,char *file);
static int recvit(struct ftpserv *ftp,char *command,char *file);

/* Command table */
static char *commands[] = {
	"user",
	"acct",
	"pass",
	"type",
	"list",
	"cwd",
	"dele",
	"name",
	"quit",
	"retr",
	"stor",
	"port",
	"nlst",
	"pwd",
	"xpwd",			/* For compatibility with 4.2BSD */
	"mkd ",
	"xmkd",			/* For compatibility with 4.2BSD */
	"xrmd",			/* For compatibility with 4.2BSD */
	"rmd ",
	"stru",
	"mode",
	"syst",
	"xmd5",
	"xcwd",
	NULL
};

/* Response messages */
static char banner[] = "220 %s FTP version %s ready at %s\n";
static char badcmd[] = "500 Unknown command '%s'\n";
static char binwarn[] = "100 Warning: type is ASCII and %s appears to be binary\n";
static char unsupp[] = "500 Unsupported command or option\n";
static char givepass[] = "331 Enter PASS command\n";
static char logged[] = "230 Logged in\n";
static char typeok[] = "200 Type %s OK\n";
static char only8[] = "501 Only logical bytesize 8 supported\n";
static char deleok[] = "250 File deleted\n";
static char mkdok[] = "200 MKD ok\n";
static char delefail[] = "550 Delete failed: %s\n";
static char pwdmsg[] = "257 \"%s\" is current directory\n";
static char badtype[] = "501 Unknown type \"%s\"\n";
static char badport[] = "501 Bad port syntax\n";
static char unimp[] = "502 Command not yet implemented\n";
static char bye[] = "221 Goodbye!\n";
static char nodir[] = "553 Can't read directory \"%s\": %s\n";
static char cantopen[] = "550 Can't read file \"%s\": %s\n";
static char sending[] = "150 Opening data connection for %s %s\n";
static char cantmake[] = "553 Can't create \"%s\": %s\n";
static char writerr[] = "552 Write error: %s\n";
static char portok[] = "200 Port command okay\n";
static char rxok[] = "226 File received OK\n";
static char txok[] = "226 File sent OK\n";
static char noperm[] = "550 Permission denied\n";
static char noconn[] = "425 Data connection reset\n";
static char lowmem[] = "421 System overloaded, try again later\n";
static char notlog[] = "530 Please log in with USER and PASS\n";
static char userfirst[] = "503 Login with USER first.\n";
static char okay[] = "200 Ok\n";
static char syst[] = "215 %s Type: L%d Version: %s\n";

/* Start up FTP service */
int
ftpstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_FTP;
	else
		port = atoi(argv[1]);

	return start_tcp(port,"FTP Server",ftpserv,2048);
}
static void
ftpserv(s,n,p)
int s;	/* Socket with user connection */
void *n;
void *p;
{
	struct ftpserv ftp;
	char **cmdp,buf[512],*arg,*cp,*cp1,*file,*mode;
	long t;
	int i;
	struct sockaddr_in socket;

	memset(&ftp,0,sizeof(ftp));	/* Start with clear slate */
	ftp.control = fdopen(s,"r+t");
	sockowner(fileno(ftp.control),Curproc);		/* We own it now */
	setvbuf(ftp.control,NULL,_IOLBF,BUFSIZ);
	if(availmem() != 0){
		fprintf(ftp.control,lowmem);
		fclose(ftp.control);
		return;
	}				

	fclose(stdin);
	stdin = fdup(ftp.control);
	fclose(stdout);
	stdout = fdup(ftp.control);

	/* Set default data port */
	i = SOCKSIZE;
	getpeername(fileno(ftp.control),(struct sockaddr *)&socket,&i);
	socket.sin_port = IPPORT_FTPD;
	ASSIGN(ftp.port,socket);

	logmsg(fileno(ftp.control),"open FTP");
	time(&t);
	cp = ctime(&t);
	if((cp1 = strchr(cp,'\n')) != NULL)
		*cp1 = '\0';
	fprintf(ftp.control,banner,Hostname,Version,cp);
loop:	fflush(ftp.control);
	if((fgets(buf,sizeof(buf),ftp.control)) == NULL){
		/* He closed on us */
		goto finish;
	}
	if(strlen(buf) == 0){
		/* Can't be a legal FTP command */
		fprintf(ftp.control,badcmd,buf);
		goto loop;
	}	
	rip(buf);
	/* Translate first word to lower case */
	for(cp = buf;*cp != ' ' && *cp != '\0';cp++)
		*cp = tolower(*cp);
	/* Find command in table; if not present, return syntax error */
	for(cmdp = commands;*cmdp != NULL;cmdp++)
		if(strncmp(*cmdp,buf,strlen(*cmdp)) == 0)
			break;
	if(*cmdp == NULL){
		fprintf(ftp.control,badcmd,buf);
		goto loop;
	}
	/* Allow only USER, PASS and QUIT before logging in */
	if(ftp.cd == NULL || ftp.path == NULL){
		switch(cmdp-commands){
		case USER_CMD:
		case PASS_CMD:
		case QUIT_CMD:
			break;
		default:
			fprintf(ftp.control,notlog);
			goto loop;
		}
	}
	arg = &buf[strlen(*cmdp)];
	while(*arg == ' ')
		arg++;

	/* Execute specific command */
	switch(cmdp-commands){
	case USER_CMD:
		free(ftp.username);
		ftp.username = strdup(arg);
		fprintf(ftp.control,givepass);
		break;
	case TYPE_CMD:
		switch(arg[0]){
		case 'A':
		case 'a':	/* Ascii */
			ftp.type = ASCII_TYPE;
			fprintf(ftp.control,typeok,arg);
			break;
		case 'l':
		case 'L':
			while(*arg != ' ' && *arg != '\0')
				arg++;
			if(*arg == '\0' || *++arg != '8'){
				fprintf(ftp.control,only8);
				break;
			}
			ftp.type = LOGICAL_TYPE;
			ftp.logbsize = 8;
			fprintf(ftp.control,typeok,arg);
			break;
		case 'B':
		case 'b':	/* Binary */
		case 'I':
		case 'i':	/* Image */
			ftp.type = IMAGE_TYPE;
			fprintf(ftp.control,typeok,arg);
			break;
		default:	/* Invalid */
			fprintf(ftp.control,badtype,arg);
			break;
		}
		break;
	case QUIT_CMD:
		fprintf(ftp.control,bye);
		goto finish;
	case RETR_CMD:
		file = pathname(ftp.cd,arg);
		switch(ftp.type){
		case IMAGE_TYPE:
		case LOGICAL_TYPE:
			mode = READ_BINARY;
			break;
		case ASCII_TYPE:
			mode = READ_TEXT;
			break;
		}
		if(!permcheck(ftp.path,ftp.perms,RETR_CMD,file)){
		 	fprintf(ftp.control,noperm);
		} else if((ftp.fp = fopen(file,mode)) == NULL){
			fprintf(ftp.control,cantopen,file,sys_errlist[errno]);
		} else {
			logmsg(fileno(ftp.control),"RETR %s",file);
			if(ftp.type == ASCII_TYPE && isbinary(ftp.fp)){
				fprintf(ftp.control,binwarn,file);
			}
			sendit(&ftp,"RETR",file);
		}
		FREE(file);
		break;
	case STOR_CMD:
		file = pathname(ftp.cd,arg);
		switch(ftp.type){
		case IMAGE_TYPE:
		case LOGICAL_TYPE:
			mode = WRITE_BINARY;
			break;
		case ASCII_TYPE:
			mode = WRITE_TEXT;
			break;
		}
		if(!permcheck(ftp.path,ftp.perms,STOR_CMD,file)){
		 	fprintf(ftp.control,noperm);
		} else if((ftp.fp = fopen(file,mode)) == NULL){
			fprintf(ftp.control,cantmake,file,sys_errlist[errno]);
		} else {
			logmsg(fileno(ftp.control),"STOR %s",file);
			recvit(&ftp,"STOR",file);
		}
		FREE(file);
		break;
	case PORT_CMD:
		if(pport(&ftp.port,arg) == -1){
			fprintf(ftp.control,badport);
		} else {
			fprintf(ftp.control,portok);
		}
		break;
#ifndef CPM
	case LIST_CMD:
		file = pathname(ftp.cd,arg);
		if(!permcheck(ftp.path,ftp.perms,RETR_CMD,file)){
		 	fprintf(ftp.control,noperm);
		} else if((ftp.fp = dir(file,1)) == NULL){
			fprintf(ftp.control,nodir,file,sys_errlist[errno]);
		} else {
			sendit(&ftp,"LIST",file);
		}
		FREE(file);
		break;
	case NLST_CMD:
		file = pathname(ftp.cd,arg);
		if(!permcheck(ftp.path,ftp.perms,RETR_CMD,file)){
		 	fprintf(ftp.control,noperm);
		} else if((ftp.fp = dir(file,0)) == NULL){
			fprintf(ftp.control,nodir,file,sys_errlist[errno]);
		} else {
			sendit(&ftp,"NLST",file);
		}
		FREE(file);
		break;
	case XCWD_CMD:
	case CWD_CMD:
		file = pathname(ftp.cd,arg);
		if(!permcheck(ftp.path,ftp.perms,RETR_CMD,file)){
		 	fprintf(ftp.control,noperm);
			FREE(file);
#ifdef	MSDOS
		/* Don'tcha just LOVE %%$#@!! MS-DOS? */
		} else if(strcmp(file,"/") == 0 || access(file,0) == 0){
#else
		} else if(access(file,0) == 0){	/* See if it exists */
#endif
			/* Succeeded, record in control block */
			free(ftp.cd);
			ftp.cd = file;
			fprintf(ftp.control,pwdmsg,file);
		} else {
			/* Failed, don't change anything */
			fprintf(ftp.control,nodir,file,sys_errlist[errno]);
			FREE(file);
		}
		break;
	case XPWD_CMD:
	case PWD_CMD:
		fprintf(ftp.control,pwdmsg,ftp.cd);
		break;
#else
	case LIST_CMD:
	case NLST_CMD:
	case CWD_CMD:
	case XCWD_CMD:
	case XPWD_CMD:
	case PWD_CMD:
#endif
	case ACCT_CMD:		
		fprintf(ftp.control,unimp);
		break;
	case DELE_CMD:
		file = pathname(ftp.cd,arg);
		if(!permcheck(ftp.path,ftp.perms,DELE_CMD,file)){
		 	fprintf(ftp.control,noperm);
		} else if(unlink(file) == 0){
			logmsg(fileno(ftp.control),"DELE %s",file);
			fprintf(ftp.control,deleok);
		} else {
			fprintf(ftp.control,delefail,sys_errlist[errno]);
		}
		FREE(file);
		break;
	case PASS_CMD:
		if(ftp.username == NULL)
			fprintf(ftp.control,userfirst);
		else
			ftplogin(&ftp,arg);			
		break;
#ifndef	CPM
	case XMKD_CMD:
	case MKD_CMD:
		file = pathname(ftp.cd,arg);
		if(!permcheck(ftp.path,ftp.perms,MKD_CMD,file)){
			fprintf(ftp.control,noperm);
#ifdef	__TURBOC__
		} else if(mkdir(file) == 0){
#else
		} else if(mkdir(file,0777) == 0){
#endif
			logmsg(fileno(ftp.control),"MKD %s",file);
			fprintf(ftp.control,mkdok);
		} else {
			fprintf(ftp.control,cantmake,file,sys_errlist[errno]);
		}
		FREE(file);
		break;
	case XRMD_CMD:
	case RMD_CMD:
		file = pathname(ftp.cd,arg);
		if(!permcheck(ftp.path,ftp.perms,RMD_CMD,file)){
		 	fprintf(ftp.control,noperm);
		} else if(rmdir(file) == 0){
			logmsg(fileno(ftp.control),"RMD %s",file);
			fprintf(ftp.control,deleok);
		} else {
			fprintf(ftp.control,delefail,sys_errlist[errno]);
		}
		FREE(file);
		break;
	case STRU_CMD:
		if(tolower(arg[0]) != 'f')
			fprintf(ftp.control,unsupp);
		else
			fprintf(ftp.control,okay);
		break;
	case MODE_CMD:
		if(tolower(arg[0]) != 's')
			fprintf(ftp.control,unsupp);
		else
			fprintf(ftp.control,okay);
		break;
	case SYST_CMD:
		fprintf(ftp.control,syst,System,NBBY,Version);
		break;
	case XMD5_CMD:
		file = pathname(ftp.cd,arg);
		switch(ftp.type){
		case IMAGE_TYPE:
		case LOGICAL_TYPE:
			mode = READ_BINARY;
			break;
		case ASCII_TYPE:
			mode = READ_TEXT;
			break;
		}
		if(!permcheck(ftp.path,ftp.perms,RETR_CMD,file)){
		 	fprintf(ftp.control,noperm);
		} else if((ftp.fp = fopen(file,mode)) == NULL){
			fprintf(ftp.control,cantopen,file,sys_errlist[errno]);
		} else {
			uint8 hash[16];

			logmsg(fileno(ftp.control),"XMD5 %s",file);
			if(ftp.type == ASCII_TYPE && isbinary(ftp.fp))
				fprintf(ftp.control,binwarn,file);

			md5hash(ftp.fp,hash,ftp.type == ASCII_TYPE);
			fclose(ftp.fp);
			ftp.fp = NULL;
			fprintf(ftp.control,"200 ");
			for(i=0;i<16;i++)
				fprintf(ftp.control,"%02x",hash[i]);
			fprintf(ftp.control," %s\n",file);
		}
		FREE(file);
		break;
	}
#endif
	goto loop;
finish:
	logmsg(fileno(ftp.control),"close FTP");
	/* Clean up */
	fclose(ftp.control);
	if(ftp.data != NULL)
		fclose(ftp.data);
	if(ftp.fp != NULL)
		fclose(ftp.fp);
	free(ftp.username);
	free(ftp.path);
	free(ftp.cd);
}

/* Shut down FTP server */
int
ftp0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	uint16 port;

	if(argc < 2)
		port = IPPORT_FTP;
	else
		port = atoi(argv[1]);

	return stop_tcp(port);
}
static
int
pport(sock,arg)
struct sockaddr_in *sock;
char *arg;
{
	int32 n;
	int i;

	n = 0;
	for(i=0;i<4;i++){
		n = atoi(arg) + (n << 8);
		if((arg = strchr(arg,',')) == NULL)
			return -1;
		arg++;
	}
	sock->sin_addr.s_addr = n;
	n = atoi(arg);
	if((arg = strchr(arg,',')) == NULL)
		return -1;
	arg++;
	n = atoi(arg) + (n << 8);
	sock->sin_port = n;
	return 0;
}

/* Attempt to log in the user whose name is in ftp->username and password
 * in pass
 */
static void
ftplogin(ftp,pass)
struct ftpserv *ftp;
char *pass;
{
	char *path;
	int anony = 0;

	path = mallocw(200);
	if((ftp->perms = userlogin(ftp->username,pass,&path,200,&anony))
	   == -1){
		fprintf(ftp->control,noperm);
		free(path);
		return;
	}
	/* Set up current directory and path prefix */
#if	defined(AMIGAGONE)
	ftp->cd = pathname("", path);
	ftp->path = strdup(ftp->cd);
	free(path);
#else
	ftp->cd = path;
	ftp->path = strdup(path);
#endif

	fprintf(ftp->control,logged);
	if(!anony)
		logmsg(fileno(ftp->control),"%s logged in",ftp->username);
	else
		logmsg(fileno(ftp->control),"%s logged in, ID %s",ftp->username,pass);
}

#ifdef	MSDOS
/* Illegal characters in a DOS filename */
static char badchars[] = "\"[]:|<>+=;,";
#endif

/* Return 1 if the file operation is allowed, 0 otherwise */
int
permcheck(path,perms,op,file)
char *path;
int perms;
int op;
char *file;
{
#ifdef	MSDOS
	char *cp;
#endif

	if(file == NULL || path == NULL)
		return 0;	/* Probably hasn't logged in yet */
#ifdef	MSDOS
	/* Check for characters illegal in MS-DOS file names */
	for(cp = badchars;*cp != '\0';cp++){
		if(strchr(file,*cp) != NULL)
			return 0;	
	}
#endif
#ifndef MAC
	/* The target file must be under the user's allowed search path */
	if(strncmp(file,path,strlen(path)) != 0)
		return 0;
#endif

	switch(op){
	case RETR_CMD:
		/* User must have permission to read files */
		if(perms & FTP_READ)
			return 1;
		return 0;
	case DELE_CMD:
	case RMD_CMD:
		/* User must have permission to (over)write files */
		if(perms & FTP_WRITE)
			return 1;
		return 0;
	case STOR_CMD:
	case MKD_CMD:
		/* User must have permission to (over)write files, or permission
		 * to create them if the file doesn't already exist
		 */
		if(perms & FTP_WRITE)
			return 1;
		if(access(file,2) == -1 && (perms & FTP_CREATE))
			return 1;
		return 0;
	}
	return 0;	/* "can't happen" -- keep lint happy */
}
static int
sendit(ftp,command,file)
struct ftpserv *ftp;
char *command;
char *file;
{
	long total;
	struct sockaddr_in dport;
	int s;

	s = socket(AF_INET,SOCK_STREAM,0);
	dport.sin_family = AF_INET;
	dport.sin_addr.s_addr = INADDR_ANY;
	dport.sin_port = IPPORT_FTPD;
	bind(s,(struct sockaddr *)&dport,SOCKSIZE);
	fprintf(ftp->control,sending,command,file);
	fflush(ftp->control);
	if(connect(s,(struct sockaddr *)&ftp->port,SOCKSIZE) == -1){
		fclose(ftp->fp);
		ftp->fp = NULL;
		close_s(s);
		ftp->data = NULL;
		fprintf(ftp->control,noconn);
		return -1;
	}
	ftp->data = fdopen(s,"r+");
	/* Do the actual transfer */
	total = sendfile(ftp->fp,ftp->data,ftp->type,0);

	if(total == -1){
		/* An error occurred on the data connection */
		fprintf(ftp->control,noconn);
		shutdown(fileno(ftp->data),2);	/* Blow away data connection */
		fclose(ftp->data);
	} else {
		fprintf(ftp->control,txok);
	}
	fclose(ftp->fp);
	ftp->fp = NULL;
	fclose(ftp->data);
	ftp->data = NULL;
	if(total == -1)
		return -1;
	else
		return 0;
}
static int
recvit(ftp,command,file)
struct ftpserv *ftp;
char *command;
char *file;
{
	struct sockaddr_in dport;
	long total;
	int s;

	s = socket(AF_INET,SOCK_STREAM,0);
	dport.sin_family = AF_INET;
	dport.sin_addr.s_addr = INADDR_ANY;
	dport.sin_port = IPPORT_FTPD;
	bind(s,(struct sockaddr *)&dport,SOCKSIZE);
	fprintf(ftp->control,sending,command,file);
	fflush(ftp->control);
	if(connect(s,(struct sockaddr *)&ftp->port,SOCKSIZE) == -1){
		fclose(ftp->fp);
		ftp->fp = NULL;
		close_s(s);
		ftp->data = NULL;
		fprintf(ftp->control,noconn);
		return -1;
	}
	ftp->data = fdopen(s,"r+");
	/* Do the actual transfer */
	total = recvfile(ftp->fp,ftp->data,ftp->type,0);

#ifdef	CPM
	if(ftp->type == ASCII_TYPE)
		putc(CTLZ,ftp->fp);
#endif
	if(total == -1) {
		/* An error occurred while writing the file */
		fprintf(ftp->control,writerr,sys_errlist[errno]);
		shutdown(fileno(ftp->data),2);	/* Blow it away */
		fclose(ftp->data);
	} else {
		fprintf(ftp->control,rxok);
		fclose(ftp->data);
	}
	ftp->data = NULL;
	fclose(ftp->fp);
	ftp->fp = NULL;
	if(total == -1)
		return -1;
	else
		return 0;
}
