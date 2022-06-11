/*
 *	CLIENT routines for Simple Mail Transfer Protocol ala RFC821
 *	A.D. Barksdale Garbee II, aka Bdale, N3EUA
 *	Copyright 1986 Bdale Garbee, All Rights Reserved.
 *	Permission granted for non-commercial copying and use, provided
 *	this notice is retained.
 * 	Modified 14 June 1987 by P. Karn for symbolic target addresses,
 *	also rebuilt locking mechanism
 *	Copyright 1987 1988 David Trulli, All Rights Reserved.
 *	Permission granted for non-commercial copying and use, provided
 *	this notice is retained.
 */
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <setjmp.h>
#ifdef UNIX
#include <sys/types.h>
#endif
#ifdef	AMIGA
#include <stat.h>
#else
#include <sys/stat.h>
#endif
#ifdef	__TURBOC__
#include <dir.h>
#include <io.h>
#endif
#include "global.h"
#include <stdarg.h>
#include "mbuf.h"
#include "cmdparse.h"
#include "proc.h"
#include "socket.h"
#include "timer.h"
#include "netuser.h"
#include "smtp.h"
#include "dirutil.h"
#include "commands.h"
#include "session.h"

static struct timer Smtpcli_t;
static int32 Gateway;

#ifdef SMTPTRACE
static unsigned short Smtptrace = 0;		/* used for trace level */
static int dosmtptrace(int argc,char *argv[],void *p);
#endif

static unsigned  short Smtpmaxcli  = MAXSESSIONS;	/* the max client connections allowed */
static int Smtpsessions = 0;		/* number of client connections
					* currently open */
static int Smtpbatch;
int	Smtpmode = 0;

static struct smtpcli *cli_session[MAXSESSIONS]; /* queue of client sessions  */

static void del_job(struct smtp_job *jp);
static void del_session(struct smtpcli *cb);
static int dogateway(int argc,char *argv[],void *p);
static int dosmtpmaxcli(int argc,char *argv[],void *p);
static int dotimer(int argc,char *argv[],void *p);
static int dosmtpkill(int argc,char *argv[],void *p);
static int dosmtplist(int argc,char *argv[],void *p);
static int dobatch(int argc,char *argv[],void *p);
static void execjobs(void);
static int getresp(struct smtpcli *ftp,int mincode);
static void logerr(struct smtpcli *cb,char *line);
static struct smtpcli *lookup(int32 destaddr);
static struct smtpcli *newcb(void);
static int next_job(struct smtpcli *cb);
static void retmail(struct smtpcli *cb);
static void sendcmd(struct smtpcli *cb,char *fmt,...);
static int smtpsendfile(struct smtpcli *cb);
static int setsmtpmode(int argc,char *argv[],void *p);
static struct smtp_job *setupjob(struct smtpcli *cb,char *id,char *from);
static void smtp_send(int unused,void *cb1,void *p);
static int smtpkick(int argc,char *argv[],void *p);

static struct cmds Smtpcmds[] = {
	"batch",	dobatch,	0,	0,	NULL,
	"gateway",	dogateway,	0,	0,	NULL,
	"mode",		setsmtpmode,	0,	0,	NULL,
	"kick",		smtpkick,	0,	0,	NULL,
	"kill",		dosmtpkill,	0,	2,	"kill <jobnumber>",
	"list",		dosmtplist,	0,	0,	NULL,
	"maxclients",	dosmtpmaxcli,	0,	0,	NULL,
	"timer",	dotimer,	0,	0,	NULL,
#ifdef SMTPTRACE
	"trace",	dosmtptrace,	0,	0,	NULL,
#endif
	NULL,
};

int
dosmtp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Smtpcmds,argc,argv,p);
}

static int
dobatch(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Smtpbatch,"SMTP batching",argc,argv);
}
static int
dosmtpmaxcli(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Smtpmaxcli,"Max clients",argc,argv);
}

static int
setsmtpmode(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if (argc < 2) {
		printf("smtp mode: %s\n",
			(Smtpmode & QUEUE) ? "queue" : "route");
	} else {
		switch(*argv[1]) {
		case 'q':
			Smtpmode |= QUEUE;
			break;
		case 'r':
			Smtpmode &= ~QUEUE;
			break;
		default:
			printf("Usage: smtp mode [queue | route]\n");
			break;
		}
	}
	return 0;
}
static int
dogateway(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int32 n;

	if(argc < 2){
		printf("%s\n",inet_ntoa(Gateway));
	} else if((n = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	} else
		Gateway = n;
	return 0;
}

#ifdef SMTPTRACE
static int
dosmtptrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Smtptrace,"SMTP tracing",argc,argv);
}
#endif

/* list jobs wating to be sent in the mqueue */
static int
dosmtplist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char tstring[80];
	char line[20];
	char host[LINELEN];
	char to[LINELEN];
	char from[LINELEN];
	char *cp;
	char	status;
	struct stat stbuf;
	struct tm *tminfo, *localtime();
	FILE *fp;

	printf("S     Job    Size Date  Time  Host                 From\n");
	filedir(Mailqueue,0,line);
	while(line[0] != '\0') {
		sprintf(tstring,"%s/%s",Mailqdir,line);
		if ((fp = fopen(tstring,READ_TEXT)) == NULL) {
			printf("Can't open %s: %s\n",tstring,sys_errlist[errno]);
			continue;
		}
		if ((cp = strrchr(line,'.')) != NULL)
			*cp = '\0';
		sprintf(tstring,"%s/%s.lck",Mailqdir,line);
		if (access(tstring,0))
			status = ' ';
		else
			status = 'L';
		sprintf(tstring,"%s/%s.txt",Mailqdir,line);
		stat(tstring,&stbuf);
		tminfo = localtime(&stbuf.st_ctime);
		fgets(host,sizeof(host),fp);
		rip(host);
		fgets(from,sizeof(from),fp);
		rip(from);
		printf("%c %7s %7ld %02d/%02d %02d:%02d %-20s %s\n      ",
			status, line, stbuf.st_size,
			tminfo->tm_mon+1,
			tminfo->tm_mday,
			tminfo->tm_hour,
			tminfo->tm_min,
			host,from);
		while (fgets(to,sizeof(to),fp) != NULL) {
			rip(to);
			printf("%s ",to);
		}
		printf("\n");
		(void) fclose(fp);
		kwait(NULL);
		filedir(Mailqueue,1,line);
	}
	return 0;
}

/* kill a job in the mqueue */
static int
dosmtpkill(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char s[SLINELEN];
	char *cp,c;
	sprintf(s,"%s/%s.lck",Mailqdir,argv[1]);
	cp = strrchr(s,'.');
	if (!access(s,0)) {
		Current->ttystate.echo = Current->ttystate.edit = 0;
		c = keywait("Warning, the job is locked by SMTP. Remove (y/n)? ",0);
		Current->ttystate.echo = Current->ttystate.edit = 1;
		if (c != 'y')
			return 0;
		(void) unlink(s);
	}
	strcpy(cp,".wrk");
	if (unlink(s))
		printf("Job id %s not found\n",argv[1]);
	strcpy(cp,".txt");
	(void) unlink(s);
	return 0;
}

/* Set outbound spool scan interval */
static int
dotimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		printf("%lu/%lu\n",
		read_timer(&Smtpcli_t) /1000L,
		dur_timer(&Smtpcli_t)/ 1000L);
		return 0;
	}
	Smtpcli_t.func = (void (*)())smtptick;/* what to call on timeout */
	Smtpcli_t.arg = NULL;		/* dummy value */
	set_timer(&Smtpcli_t,atol(argv[1])*1000L);	/* set timer duration */
	start_timer(&Smtpcli_t);		/* and fire it up */
	return 0;
}

static int
smtpkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int32 addr = 0;
	if(argc > 1 && (addr = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	}
	smtptick(addr);
	return 0;
}

/* This is the routine that gets called every so often to do outgoing
 * mail processing. When called with a null argument, it runs the entire
 * queue; if called with a specific non-zero IP address from the remote
 * kick server, it only starts up sessions to that address.
 */
int
smtptick(target)
int32 target;
{
	register struct smtpcli *cb;
	struct smtp_job *jp;
	struct list *ap;
	char	tmpstring[LINELEN], wfilename[13], prefix[9];
	char	from[LINELEN], to[LINELEN];
	char *cp, *cp1;
	int32 destaddr;
	FILE *wfile;

#ifdef SMTPTRACE
	if (Smtptrace > 5)
		printf("smtp daemon entered, target = %s\n",inet_ntoa(target));
#endif
	if(availmem() != 0){
		/* Memory is tight, don't do anything */
		/* Restart timer */
		start_timer(&Smtpcli_t);
		return 0;
	}
	for(filedir(Mailqueue,0,wfilename);wfilename[0] != '\0';
		filedir(Mailqueue,1,wfilename)){

		/* save the prefix of the file name which it job id */
		cp = wfilename;
		cp1 = prefix;
		while (*cp && *cp != '.')
			*cp1++ = *cp++;
		*cp1 = '\0';

		/* lock this file from the smtp daemon */
		if (mlock(Mailqdir,prefix))
			continue;

		sprintf(tmpstring,"%s/%s",Mailqdir,wfilename);
		if ((wfile = fopen(tmpstring,READ_TEXT)) == NULL) {
			/* probably too many open files */
			(void) rmlock(Mailqdir,prefix);
			/* continue to next message. The failure
			* may be temporary */
			continue;
		}

		(void) fgets(tmpstring,LINELEN,wfile);	/* read target host */
		rip(tmpstring);

		if ((destaddr = mailroute(tmpstring)) == 0) {
			fclose(wfile);
			printf("** smtp: Unknown address %s\n",tmpstring);
			(void) rmlock(Mailqdir,prefix);
			continue;
		}
		if(target != 0 && destaddr != target){
			fclose(wfile);
			(void) rmlock(Mailqdir,prefix);
			continue;	/* Not the proper target of a kick */
		}
		if ((cb = lookup(destaddr)) == NULL) {
			/* there are enough processes running already */
			if (Smtpsessions >= Smtpmaxcli) {
#ifdef SMTPTRACE
				if (Smtptrace) {
					printf("smtp daemon: too many processes\n");
				}
#endif
				fclose(wfile);
				(void) rmlock(Mailqdir,prefix);
				break;
			}
			if ((cb = newcb()) == NULL) {
				fclose(wfile);
				(void) rmlock(Mailqdir,prefix);
				break;
			} 
			cb->ipdest = destaddr;
			cb->destname = strdup(tmpstring);
		} else {
			if(cb->lock){
				/* This system is already is sending mail lets not
				* interfere with its send queue.
				*/
				fclose(wfile);
				(void) rmlock(Mailqdir,prefix);
				continue;
			}
		}

		(void) fgets(from,LINELEN,wfile);	/* read from */
		rip(from);
		if ((jp = setupjob(cb,prefix,from)) == NULL) {
			fclose(wfile);
			(void) rmlock(Mailqdir,prefix);
			del_session(cb);
			break;
		}
		while (fgets(to,LINELEN,wfile) != NULL) {
			rip(to);
			if (addlist(&jp->to,to,DOMAIN) == NULL) {
				fclose(wfile);
				del_session(cb);
			}
		}
		fclose(wfile);
#ifdef SMTPTRACE
		if (Smtptrace > 1) {
			printf("queue job %s From: %s To:",prefix,from);
			for (ap = jp->to; ap != NULL; ap = ap->next)
				printf(" %s",ap->val);
			printf("\n");
		}
#endif
	}

	/* start sending that mail */
	execjobs();

	/* Restart timer */
	start_timer(&Smtpcli_t);
	return 0;
}

/* This is the master state machine that handles a single SMTP transaction.
 * It is called with a queue of jobs for a particular host.
 * The logic is complicated by the "Smtpbatch" variable, which controls
 * the batching of SMTP commands. If Smtpbatch is true, then many of the
 * SMTP commands are sent in one swell foop before waiting for any of
 * the responses. Unfortunately, this breaks many brain-damaged SMTP servers
 * out there, so provisions have to be made to operate SMTP in lock-step mode.
 */
static void
smtp_send(unused,cb1,p)
int unused;
void *cb1;
void *p;
{
	register struct smtpcli *cb;
	register struct list *tp;
	struct sockaddr_in fsocket;
	char *cp;
	int rcode;
	int rcpts;
	int goodrcpt;
	int i,s;
	int init = 1;

	cb = (struct smtpcli *)cb1;
	cb->lock = 1;
	fsocket.sin_family = AF_INET;
	fsocket.sin_addr.s_addr = cb->ipdest;
	fsocket.sin_port = IPPORT_SMTP;

	s = socket(AF_INET,SOCK_STREAM,0);
#ifdef SMTPTRACE
	if (Smtptrace) 
		printf("SMTP client Trying...\n");
#endif
	if(connect(s,(struct sockaddr *)&fsocket,SOCKSIZE) == 0){
		cb->network = fdopen(s,"r+t");
#ifdef SMTPTRACE
		if (Smtptrace) 
			printf("Connected\n");
#endif
	} else {
		cp = sockerr(s);
#ifdef SMTPTRACE
		if (Smtptrace) 
			printf("Connect failed: %s\n",cp != NULL ? cp : "");
#endif
		logmsg(s,"SMTP %s Connect failed: %s",psocket(&fsocket),
		    cp != NULL ? cp : "");
	}
	if(!Smtpbatch){
		rcode = getresp(cb,200);
		if(rcode == -1 || rcode >= 400)
			goto quit;
	}
	/* Say HELO */
	sendcmd(cb,"HELO %s\n",Hostname);
	if(!Smtpbatch){
		rcode = getresp(cb,200);
		if(rcode == -1 || rcode >= 400)
			goto quit;
	}
	do {	/* For each message... */

		/* if this file open fails, skip it */
		if ((cb->tfile = fopen(cb->tname,READ_TEXT)) == NULL)
			continue;

		/* Send MAIL and RCPT commands */
		sendcmd(cb,"MAIL FROM:<%s>\n",cb->jobq->from);
		if(!Smtpbatch){
			rcode = getresp(cb,200);
			if(rcode == -1 || rcode >= 400)
				goto quit;
		}
		rcpts = 0;
		goodrcpt = 0;
		for (tp = cb->jobq->to; tp != NULL; tp = tp->next){
			sendcmd(cb,"RCPT TO:<%s>\n",tp->val);
			if(!Smtpbatch){
				rcode = getresp(cb,200);
				if(rcode == -1)
					goto quit;
				if(rcode < 400)
					goodrcpt = 1; /* At least one good */
			}
			rcpts++;
		}
		/* Send DATA command */
		sendcmd(cb,"DATA\n");
		if(!Smtpbatch){
			rcode = getresp(cb,200);
			if(rcode == -1 || rcode >= 400)
				goto quit;
		}
		if(Smtpbatch){
			/* Now wait for the responses to come back. The first time
			 * we do this, we wait first for the start banner and
			 * HELO response. In any case, we wait for the response to
			 * the MAIL command here.
			 */
			for(i= init ? 3 : 1;i > 0;i--){
				rcode = getresp(cb,200);
				if(rcode == -1 || rcode >= 400)
					goto quit;
			}
			init = 0;

			/* Now process the responses to the RCPT commands */
			for(i=rcpts;i!=0;i--){
				rcode = getresp(cb,200);
				if(rcode == -1)
					goto quit;
				if(rcode < 400)
					goodrcpt = 1; /* At least one good */
			}
			/* And finally get the response to the DATA command.
			 * Some servers will return failure here if no recipients
			 * are valid, some won't.
			 */
			rcode = getresp(cb,200);
			if(rcode == -1 || rcode >= 400)
				goto quit;

			/* check for no good rcpt on the list */
			if (goodrcpt == 0){
				sendcmd(cb,".\n");  /* Get out of data mode */
				goto quit;
			}
		}
		/* Send the file. This also closes it */
		smtpsendfile(cb);

		/* Wait for the OK response */
		rcode = getresp(cb,200);
		if(rcode == -1)
			goto quit;
		if((rcode >= 200 && rcode < 300) || rcode >= 500){
			/* if a good transfer or permanent failure remove job */

			if (cb->errlog != NULL)
				retmail(cb);
			/* Unlink the textfile */
			(void) unlink(cb->tname);
			(void) unlink(cb->wname);	/* unlink workfile */
			logmsg(s,"SMTP sent job %s To: %s From: %s",
			 cb->jobq->jobname,cb->jobq->to->val,cb->jobq->from);
		}
	} while(next_job(cb));
quit:
	sendcmd(cb,"QUIT\n");
	if (cb->errlog != NULL){
		retmail(cb);
		(void) unlink(cb->wname);	/* unlink workfile */
		(void) unlink(cb->tname);	/* unlink text */
	}
	(void) fclose(cb->network);
	if(cb->tfile != NULL)
		fclose(cb->tfile);
	cb->lock = 0;
	del_session(cb);
}


/* create mail lockfile */
int
mlock(dir,id)
char *dir,*id;
{
	char lockname[LINELEN];
	int fd;
	struct stat statbuf;

#ifdef	MSDOS
	if(strlen(id) > 8) {		/* truncate long filenames */
		id[8] = '\0';
		if(id[7] == '/')
			id[7] = '\0';
	}
#endif
	/* Try to create the lock file in an atomic operation */
	sprintf(lockname,"%s/%s.lck",dir,id);
#ifdef        AMIGA
	/* don't ask, really, just don't ask... I'd do file locking on
	 * an Amiga much more differently than this.
	 */
	if(access(lockname, 0) == 0)
		return -1;
#endif
	fd = open(lockname, O_WRONLY|O_EXCL|O_CREAT,0600);
	if(fd != -1){
		/* Lock succeeded */
		close(fd);
		return 0;
	}
	/* See if the dir doesn't exist */
	if(stat(dir,&statbuf) == -1 || (statbuf.st_mode & S_IFMT) != S_IFDIR){
		printf("Lock directory %s missing\n",dir);
		fflush(stdout);
		return 0;
	}
	return -1;	/* lock failed */
}

/* remove mail lockfile */
int
rmlock(dir,id)
char *dir,*id;
{
	char lockname[LINELEN];
#ifdef	MSDOS
	if(strlen(id) > 8) {		/* truncate long filenames */
		id[8] = '\0';
		if(id[7] == '/')
			id[7] = '\0';
	}
#endif
	sprintf(lockname,"%s/%s.lck",dir,id);
	return(unlink(lockname));
}

/* free the message struct and data */
static void
del_session(cb)
register struct smtpcli *cb;
{
	register struct smtp_job *jp,*tp;
	register int i;

	if (cb == NULL)
		return;
	for(i=0; i<MAXSESSIONS; i++) 
		if(cli_session[i] == cb) {
			cli_session[i] = NULL;
			break;
		}

	free(cb->wname);
	free(cb->tname);
	free(cb->destname);
	for (jp = cb->jobq; jp != NULL;jp = tp) {
			tp = jp->next;
			del_job(jp);
	}
	del_list(cb->errlog);
	free(cb);
	Smtpsessions--;	/* number of connections active */
}

static void
del_job(jp)
register struct smtp_job *jp;
{
	if ( *jp->jobname != '\0')
		(void) rmlock(Mailqdir,jp->jobname);
	free(jp->from);
	del_list(jp->to);
	free(jp);
}

/* delete a list of list structs */
void
del_list(lp)
struct list *lp;
{
	register struct list *tp, *tp1;
	for (tp = lp; tp != NULL; tp = tp1) {
		tp1 = tp->next;
		free(tp->val);
		free(tp);
	}
}

/* stub for calling mdaemon to return message to sender */
static void
retmail(cb)
struct smtpcli *cb;
{
	FILE *infile;
#ifdef SMTPTRACE
	if (Smtptrace > 5) {
		printf("smtp job %s returned to sender\n",cb->wname);
	}
#endif
	if ((infile = fopen(cb->tname,READ_TEXT)) == NULL)
		return;
	mdaemon(infile,cb->jobq->from,cb->errlog,1);
	fclose(infile);
}

/* look to see if a smtp control block exists for this ipdest */
static struct smtpcli *
lookup(destaddr)
int32 destaddr;
{
	register int i;

	for(i=0; i<MAXSESSIONS; i++) {
		if (cli_session[i] == NULL)
			continue;
		if(cli_session[i]->ipdest == destaddr)
			return cli_session[i];
	}
	return NULL;
}

/* create a new  smtp control block */
static struct smtpcli *
newcb()
{
	register int i;
	register struct smtpcli *cb;

	for(i=0; i<MAXSESSIONS; i++) {
		if(cli_session[i] == NULL) {
			cb = (struct smtpcli *)callocw(1,sizeof(struct smtpcli));
			cb->wname = mallocw((unsigned)strlen(Mailqdir)+JOBNAME);
			cb->tname = mallocw((unsigned)strlen(Mailqdir)+JOBNAME);
			cli_session[i] = cb;
			Smtpsessions++;	/* number of connections active */
			return(cb);
		}
	}
	return NULL;
}

static void
execjobs()
{
	register struct smtpcli *cb;
	register int i;

	for(i=0; i<MAXSESSIONS; i++) {
		cb = cli_session[i];
		if (cb == NULL) 
			continue;
		if(cb->lock)
			continue;

		sprintf(cb->tname,"%s/%s.txt",Mailqdir,cb->jobq->jobname);
		sprintf(cb->wname,"%s/%s.wrk",Mailqdir,cb->jobq->jobname);

		newproc("smtp_send", 1024, smtp_send, 0, cb,NULL,0);

#ifdef SMTPTRACE
		if (Smtptrace) 
			printf("Trying Connection to %s\n",inet_ntoa(cb->ipdest));
#endif


	}
}
	
/* add this job to control block queue */
static struct smtp_job *
setupjob(cb,id,from)
struct smtpcli *cb;
char *id,*from;
{
	register struct smtp_job *p1,*p2;

	p1 = (struct smtp_job *)callocw(1,sizeof(struct smtp_job));
	p1->from = strdup(from);
	strcpy(p1->jobname,id);
	/* now add to end of jobq */
	if ((p2 = cb->jobq) == NULL)
		cb->jobq = p1;
	else {
		while(p2->next != NULL)
			p2 = p2->next;
		p2->next = p1;
	}
	return p1;
}

/* called to advance to the next job */
static int
next_job(cb)
register struct smtpcli *cb;
{
	register struct smtp_job *jp;

	jp = cb->jobq->next;
	del_job(cb->jobq);
	/* remove the error log of previous message */
	del_list(cb->errlog);
	cb->errlog = NULL;
	cb->jobq = jp;
	if (jp == NULL)
		return 0;
	sprintf(cb->tname,"%s/%s.txt",Mailqdir,jp->jobname);
	sprintf(cb->wname,"%s/%s.wrk",Mailqdir,jp->jobname);
#ifdef SMTPTRACE
	if (Smtptrace > 5) {
		printf("sending job %s\n",jp->jobname);
	}
#endif
		return 1;

}


/* Mail routing function. For now just use the hosts file */
int32
mailroute(dest)
char *dest;
{
	int32 destaddr;

	/* look up address or use the gateway */
	destaddr = resolve_mx(dest);
	if (destaddr == 0 && (destaddr = resolve(dest)) == 0)
		if (Gateway != 0) 
			destaddr = Gateway; /* Use the gateway  */
	return destaddr;
	
}

/* save line in error list */
static void
logerr(cb,line)
struct smtpcli *cb;
char *line;
{
	register struct list *lp,*tp;
	tp = (struct list *)callocw(1,sizeof(struct list));
	tp->val = strdup(line);
	/* find end of list */
	if ((lp = cb->errlog) == NULL)
		cb->errlog = tp;
	else {
		while(lp->next != NULL)
			lp = lp->next;
		lp->next = tp;
	}
}

static int
smtpsendfile(cb)
register struct smtpcli *cb;
{
	int error = 0;

	strcpy(cb->buf,"\n");
	while(fgets(cb->buf,sizeof(cb->buf),cb->tfile) != NULL) {
		/* Escape a single '.' character at the beginning of a line */
		if(strcmp(cb->buf,".\n") == 0)
			putc('.',cb->network);
		fputs(cb->buf,cb->network);
	}
	fclose(cb->tfile);
	cb->tfile = NULL;
	/* Send the end-of-message command */
	if(cb->buf[strlen(cb->buf)-1] == '\n')
		sendcmd(cb,".\n");
	else
		sendcmd(cb,"\n.\n");
	return error;
}
/* do a printf() on the network stream with optional local tracing */
static void
sendcmd(struct smtpcli *cb,char *fmt, ...)
{
	va_list args;

	va_start(args,fmt);
#ifdef	SMTPTRACE
	if(Smtptrace){
		printf("smtp sent: ");
		vprintf(fmt,args);
	}
#endif
	vsprintf(cb->buf,fmt,args);
	fputs(cb->buf,cb->network);
	va_end(args);
}

/* Wait for, read and display response from server. Return the result code. */
static int
getresp(cb,mincode)
struct smtpcli *cb;
int mincode;	/* Keep reading until at least this code comes back */
{
	int rval;
	char line[LINELEN];

	fflush(cb->network);
	for(;;){
		/* Get line */
		if(fgets(line,LINELEN,cb->network) == NULL){
			rval = -1;
			break;
		}
		rip(line);		/* Remove cr/lf */
		rval = atoi(line);
#ifdef	SMTPTRACE
		if(Smtptrace)
			printf("smtp recv: %s\n",line);/* Display to user */
#endif
		if(rval >= 500) {	/* Save permanent error replies */
			char tmp[LINELEN];
			if(cb->errlog == NULL) {
				sprintf(tmp,"While talking to %s:",
					cb->destname);
				logerr(cb,tmp);
			}
			if(cb->buf[0] != '\0') { /* Save offending command */
				rip(cb->buf);
				sprintf(tmp,">>> %s",cb->buf);
				logerr(cb,tmp);
				cb->buf[0] = '\0';
			}
			sprintf(tmp,"<<< %s",line);
			logerr(cb,tmp);		/* save the error reply */
		}
		/* Messages with dashes are continued */
		if(line[3] != '-' && rval >= mincode)
			break;
	}
	return rval;
}
