/* SMTP Server state machine - see RFC 821
 *  enhanced 4/88 Dave Trulli nn2z
 */
#include <stdio.h>
#include <time.h>
#ifdef UNIX
#include <sys/types.h>
#endif
#if	defined(__STDC__) || defined(__TURBOC__)
#include <stdarg.h>
#endif
#include <ctype.h>
#include <setjmp.h>
#include "global.h"
#include "mbuf.h"
#include "cmdparse.h"
#include "socket.h"
#include "iface.h"
#include "proc.h"
#include "smtp.h"
#include "commands.h"
#include "dirutil.h"
#include "mailbox.h"
#include "bm.h"
#include "domain.h"

char *Days[7] = {  "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
char *Months[12] = { "Jan","Feb","Mar","Apr","May","Jun",
		"Jul","Aug","Sep","Oct","Nov","Dec" };



static struct list *expandalias(struct list **head,char *user);
static int  getmsgtxt(struct smtpsv *mp);
static struct smtpsv *mail_create(void);
static void mail_clean(struct smtpsv *mp);
static int mailit(FILE *data,char *from,struct list *tolist);
static int router_queue(FILE *data,char *from,struct list *to);
static void smtplog(char *fmt,...);
static void smtpserv(int s,void *unused,void *p);
static int mailuser(FILE *data,char *from,char *to);

/* Command table */
static char *commands[] = {
	"helo",
	"noop",
	"mail from:",
	"quit",
	"rcpt to:",
	"help",
	"data",
	"rset",
	"expn",
	NULL
};
enum smtp_cmd {
	HELO_CMD,
	NOOP_CMD,
	MAIL_CMD,
	QUIT_CMD,
	RCPT_CMD,
	HELP_CMD,
	DATA_CMD,
	RSET_CMD,
	EXPN_CMD
};

/* Reply messages */
static char Help[] = "214-Commands:\n214-HELO NOOP MAIL QUIT RCPT HELP DATA RSET EXPN\n214 End\n";
static char Banner[] = "220 %s SMTP ready\n";
static char Closing[] = "221 Closing\n";
static char Ok[] = "250 Ok\n";
static char Reset[] = "250 Reset state\n";
static char Sent[] = "250 Sent\n";
static char Ourname[] = "250 %s, Share and Enjoy!\n";
static char Enter[] = "354 Enter mail, end with .\n";
static char Ioerr[] = "452 Temp file write error\n";
static char Badcmd[] = "500 Command unrecognized\n";
static char Lowmem[] = "421 System overloaded, try again later\n";
static char Syntax[] = "501 Syntax error\n";
static char Needrcpt[] = "503 Need RCPT (recipient)\n";
static char Unknown[] = "550 <%s> address unknown\n";
static char Noalias[] = "550 No alias for <%s>\n";

static int Ssmtp = -1; /* prototype socket for service */

/* Start up SMTP receiver service */
int
smtp1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct sockaddr_in lsocket;
	int s;
	FILE *network;

	if(Ssmtp != -1){
		return 0;
	}
	ksignal(Curproc,0);	/* Don't keep the parser waiting */
	chname(Curproc,"SMTP listener");

	lsocket.sin_family = AF_INET;
	lsocket.sin_addr.s_addr = INADDR_ANY;
	if(argc < 2)
		lsocket.sin_port = IPPORT_SMTP;
	else
		lsocket.sin_port = atoi(argv[1]);

	Ssmtp = socket(AF_INET,SOCK_STREAM,0);
	bind(Ssmtp,(struct sockaddr *)&lsocket,sizeof(lsocket));
	listen(Ssmtp,1);
	for(;;){
		if((s = accept(Ssmtp,NULL,(int *)NULL)) == -1)
			break;	/* Service is shutting down */

		network = fdopen(s,"r+t");
		if(availmem() != 0){
			fprintf(network,Lowmem);
			fclose(network);
		} else {
			/* Spawn a server */
			newproc("SMTP server",2048,smtpserv,s,(void *)network,NULL,0);
		}
	}
	return 0;
}

/* Shutdown SMTP service (existing connections are allowed to finish) */
int
smtp0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	close_s(Ssmtp);
	Ssmtp = -1;
	return 0;
}

static void
smtpserv(s,n,p)
int s;
void *n;
void *p;
{
	struct smtpsv *mp;
	char **cmdp,buf[LINELEN],*arg,*cp,*cmd,*newaddr;
	struct list *ap,*list;
	int cnt;
	char address_type;
	FILE *network;

	network = (FILE *)n;
	sockowner(fileno(network),Curproc);		/* We own it now */
	logmsg(fileno(network),"open SMTP");

	if((mp = mail_create()) == NULL){
		printf(Nospace);
		logmsg(fileno(network),"close SMTP - no space");
		fclose(network);
		return;
	}
	mp->network = network;

	(void) fprintf(network,Banner,Hostname);

loop:	if (fgets(buf,sizeof(buf),network) == NULL) {
		/* He closed on us */
		goto quit;
	}
	cnt = strlen(buf);
	if(cnt < 4){
		/* Can't be a legal command */
		fprintf(network,Badcmd);
		goto loop;
	}	
	rip(buf);
	cmd = buf;

	/* Translate entire buffer to lower case */
	for(cp = cmd;*cp != '\0';cp++)
		*cp = tolower(*cp);

	/* Find command in table; if not present, return syntax error */
	for(cmdp = commands;*cmdp != NULL;cmdp++)
		if(strncmp(*cmdp,cmd,strlen(*cmdp)) == 0)
			break;
	if(*cmdp == NULL){
		(void) fprintf(network,Badcmd);
		goto loop;
	}
	arg = &cmd[strlen(*cmdp)];
	/* Skip spaces after command */
	while(*arg == ' ')
		arg++;
	/* Execute specific command */
	switch(cmdp-commands) {
	case HELO_CMD:
		free(mp->system);
		mp->system = strdup(arg);
		(void) fprintf(network,Ourname,Hostname);
		break;
	case NOOP_CMD:
		(void) fprintf(network,Ok);
		break;
	case MAIL_CMD:
		if((cp = getname(arg)) == NULL){
			(void) fprintf(network,Syntax);
			break;
		}
		free(mp->from);
		mp->from = strdup(cp);
		(void) fprintf(network,Ok);
		break;
	case QUIT_CMD:
		(void) fprintf(network,Closing);
		goto quit;
	case RCPT_CMD:	/* Specify recipient */
		if((cp = getname(arg)) == NULL){
			(void) fprintf(network,Syntax);
			break;
		}

		/* rewrite address if possible */
		if((newaddr = rewrite_address(cp)) != NULL) {
			strcpy(buf,newaddr);
			cp = buf;
			free(newaddr);
		}

		/* check if address is ok */
		if ((address_type = validate_address(cp)) == BADADDR) {
			(void) fprintf(network,Unknown,cp);
			break;
		}
		/* if a local address check for an alias */
		if (address_type == LOCAL)
			expandalias(&mp->to, cp);
		else
			/* a remote address is added to the list */
			addlist(&mp->to, cp, address_type);

		(void) fprintf(network,Ok);
		break;
	case HELP_CMD:
		(void) fprintf(network,Help);
		break;
	case DATA_CMD:
		if(mp->to == NULL)
			(void) fprintf(network,Needrcpt);
		else if ((mp->data = tmpfile()) == NULL)
			(void) fprintf(network,Ioerr);
		 else
			getmsgtxt(mp);
		break;
	case RSET_CMD:
		del_list(mp->to);
		mp->to = NULL;
		(void) fprintf(network,Reset);
		break;
	case EXPN_CMD:
		if (*arg == '\0') {
			(void) fprintf(network,Syntax);
			break;
		}

		list = NULL;
		/* rewrite address if possible */
		if((newaddr = rewrite_address(arg)) != NULL)
			if(strcmp(newaddr,arg) == 0) {
				free(newaddr);
				newaddr = NULL;
			}
			else {
				strcpy(buf,newaddr);
				arg = buf;
			}
		list = NULL;
		expandalias(&list,arg);
		if (strcmp(list->val,arg) == 0 && list->next == NULL)
			if(newaddr == NULL) {
				(void) fprintf(network,Noalias,arg);
				del_list(list);
				break;
			}
		ap = list;
		while (ap->next != NULL) {
			(void) fprintf(network,"250-%s\n",ap->val);
			ap = ap->next;
		}
		fprintf(network,"250 %s\n",ap->val);
		del_list(list);
		free(newaddr);
		break;
	}
	goto loop;

quit:
	logmsg(fileno(network),"close SMTP");
	fclose(network);
	mail_clean(mp);
	smtptick(0L);			/* start SMTP daemon immediately */
}

/* read the message text */
static int
getmsgtxt(mp)
struct smtpsv *mp;
{
	char buf[LINELEN];
	register char *p = buf;
	long t;
	FILE *network;
	FILE *data;
	char *cp;

	network = mp->network;
	data = mp->data;
	/* Add timestamp; ptime adds newline */
	time(&t);
	fprintf(data,"Received: ");
	if(mp->system != NULL)
		fprintf(data,"from %s ",mp->system);
	fprintf(data,"by %s with SMTP\n\tid AA%ld ; %s",
			Hostname, get_msgid(), ptime(&t));
	if(ferror(data)){
		(void) fprintf(network,Ioerr);
		return 1;
	} else {
		(void) fprintf(network,Enter);
	}
	while(1) {
		if(fgets(p,sizeof(buf),network) == NULL){
			return 1;
		}
		rip(p);
		/* check for end of message ie a . or escaped .. */
		if (*p == '.') {
			if (*++p == '\0') {
				/* Also sends appropriate response */
				if (mailit(data,mp->from,mp->to) != 0)
					(void) fprintf(network,Ioerr);
				else
					(void) fprintf(network,Sent);
				fclose(data);
				data = NULL;
				del_list(mp->to);
				mp->to = NULL;
				return 0;
			} else if (!(*p == '.' && *(p+1) == '\0'))
				p--;
		}
#ifdef	MSDOS
		while((cp = strchr(p,CTLZ)) != NULL)
			*cp = '\n';
#endif
		/* for UNIX mail compatiblity */
		if (strncmp(p,"From ",5) == 0)
			(void) putc('>',data);
		/* Append to data file */
		if(fprintf(data,"%s\n",p) < 0) {
			(void) fprintf(network,Ioerr);
			return 1;
		}
	}
	return 0;
}

/* Create control block, initialize */
static struct smtpsv *
mail_create()
{
	register struct smtpsv *mp;

	mp = (struct smtpsv *)callocw(1,sizeof(struct smtpsv));
	mp->from = strdup("");	/* Default to null From address */
	return mp;
}

/* Free resources, delete control block */
static void
mail_clean(mp)
register struct smtpsv *mp;
{
	if (mp == NULL)
		return;
	free(mp->system);
	free(mp->from);
	if(mp->data != NULL)
		fclose(mp->data);
	del_list(mp->to);
	free(mp);
}


/* Given a string of the form <user@host>, extract the part inside the
 * brackets and return a pointer to it.
 */
char *
getname(cp)
register char *cp;
{
	register char *cp1;

	if ((cp = strchr(cp,'<')) == NULL)
		return NULL;
	cp++;	/* cp -> first char of name */
	if ((cp1 = strchr(cp,'>')) == NULL)
		return NULL;
	*cp1 = '\0';
	return cp;
}

		
/* General mailit function. It takes a list of addresses which have already
** been verified and expanded for aliases. Base on the current mode the message
** is place in an mbox, the outbound smtp queue or the rqueue interface
*/
static int
mailit(data,from,tolist)
FILE *data;
char *from;
struct list *tolist;
{
	struct list *ap, *dlist = NULL;
	register FILE *fp;
	char	mailbox[50], *cp, *host, *qhost;
	int	c, fail = 0;
	time_t	t;

	if ((Smtpmode & QUEUE) != 0)
		return(router_queue(data,from,tolist));

	do {
		qhost = NULL;
		for(ap = tolist;ap != NULL;ap = ap->next)
			if (ap->type == DOMAIN) {
				if ((host = strrchr(ap->val,'@')) != NULL)
					host++;
				else
					host = Hostname;
				if(qhost == NULL)
			     		qhost = host;
				if(stricmp(qhost,host) == 0) {
					ap->type = BADADDR;
					addlist(&dlist,ap->val,0);
				}
			}
		if(qhost != NULL) {
			rewind(data);
			queuejob(data,qhost,dlist,from);
			del_list(dlist);
			dlist = NULL;
		}
	} while(qhost != NULL);

	for(ap = tolist;ap != NULL;ap = ap->next) {
		if(ap->type != LOCAL) {
			ap->type = DOMAIN;
			continue;
		}
		rewind(data);
		/* strip off host name of LOCAL addresses */
		if ((cp = strchr(ap->val,'@')) != NULL)
			*cp = '\0';

		/* truncate long user names */
		if (strlen(ap->val) > MBOXLEN)
			ap->val[MBOXLEN] = '\0';

		/* if mail file is busy save it in our smtp queue
		 * and let the smtp daemon try later.
		 */
		if (mlock(Mailspool,ap->val)) {
			addlist(&dlist,ap->val,0);
			fail = queuejob(data,Hostname,dlist,from);
			del_list(dlist);
			dlist = NULL;
		}
		else {
			char buf[LINELEN];
			int tocnt = 0;
			sprintf(mailbox,"%s/%s.txt",Mailspool,ap->val);
#ifndef	AMIGA
			if((fp = fopen(mailbox,APPEND_TEXT)) != NULL) {
#else
			if((fp = fopen(mailbox,"r+")) != NULL) {
				(void) fseek(fp, 0L, 2);
#endif
				time(&t);
				fprintf(fp,"From %s %s",from,ctime(&t));
				host = NULL;
				while(fgets(buf,sizeof(buf),data) != NULL){
					if(buf[0] == '\n'){
						if(tocnt == 0)
							fprintf(fp,"%s%s\n",
								Hdrs[APPARTO],
								ap->val);
						fputc('\n',fp);
						break;
					}
					fputs(buf,fp);
					rip(buf);
					switch(htype(buf)){
					case TO:
					case CC:
						++tocnt;
						break;
					case RRECEIPT:
						if((cp = getaddress(buf,0))
						   != NULL){
							free(host);
							host = strdup(cp);
						}
						break;
					}
				}
				while((c = fread(buf,1,sizeof(buf),data)) > 0)
					if(fwrite(buf,1,c,fp) != c)
						break;
				if(ferror(fp))
					fail = 1;
				else
					fprintf(fp,"\n");
				/* Leave a blank line between msgs */
				fclose(fp);
				printf("New mail arrived for %s\n",ap->val);
				if(host != NULL){
					rewind(data); /* Send return receipt */
					mdaemon(data,host,NULL,0);
					free(host);
				}
			} else 
				fail = 1;
			(void) rmlock(Mailspool,ap->val);
			if (fail)
				break;
			smtplog("deliver: To: %s From: %s",ap->val,from);
		}
	}
	return fail;
}

/* Return Date/Time in Arpanet format in passed string */
char *
ptime(t)
long *t;
{
	/* Print out the time and date field as
	 *		"DAY day MONTH year hh:mm:ss ZONE"
	 */
	register struct tm *ltm;
	static char tz[4];
	static char str[40];
	char *p, *getenv();
	/* Read the system time */
	ltm = localtime(t);

	if (*tz == '\0')
		if ((p = getenv("TZ")) == NULL)
			strcpy(tz,"UTC");
		else
			strncpy(tz,p,3);

	/* rfc 822 format */
	sprintf(str,"%s, %.2d %s %02d %02d:%02d:%02d %.3s\n",
		Days[ltm->tm_wday],
		ltm->tm_mday,
		Months[ltm->tm_mon],
		ltm->tm_year,
		ltm->tm_hour,
		ltm->tm_min,
		ltm->tm_sec,
		tz);
	return(str);
}

long 
get_msgid()
{
	char sfilename[LINELEN];
	char s[20];
	register long sequence = 0;
	FILE *sfile;

	sprintf(sfilename,"%s/sequence.seq",Mailqdir);
	sfile = fopen(sfilename,READ_TEXT);

	/* if sequence file exists, get the value, otherwise set it */
	if (sfile != NULL) {
		(void) fgets(s,sizeof(s),sfile);
		sequence = atol(s);
	/* Keep it in range of and 8 digit number to use for dos name prefix. */
		if (sequence < 0L || sequence > 99999999L )
			sequence = 0;
		fclose(sfile);
	}

	/* increment sequence number, and write to sequence file */
	sfile = fopen(sfilename,WRITE_TEXT);
	fprintf(sfile,"%ld",++sequence);
	fclose(sfile);
	return sequence;
}

#ifdef	MSDOS
/* Illegal characters in a DOS filename */
static char baddoschars[] = "\"[]:|<>+=;,";
#endif

/* test if mail address is valid */
int
validate_address(s)
char *s;
{
	char *cp;
	int32 addr;

	/* if address has @ in it the check dest address */
	if ((cp = strrchr(s,'@')) != NULL) {
		cp++;
		/* 1st check if its our hostname
		* if not then check the hosts file and see
		* if we can resolve ther address to a know site
		* or one of our aliases
		*/
		if (strcmp(cp,Hostname) != 0) {
			if ((addr = mailroute(cp)) == 0
				&& (Smtpmode & QUEUE) == 0)
				return BADADDR;
			if (ismyaddr(addr) == NULL)
				return DOMAIN;
		}
		
		/* on a local address remove the host name part */
		*--cp = '\0';
	}

	/* if using an external router leave address alone */
	if ((Smtpmode & QUEUE) != 0)
		return LOCAL;

	/* check for the user%host hack */
	if ((cp = strrchr(s,'%')) != NULL) {
		*cp = '@';
		cp++;
		/* reroute based on host name following the % seperator */
		if (mailroute(cp) == 0)
			return BADADDR;
		else
			return DOMAIN;
	}

#ifdef MSDOS	/* dos file name checks */
	/* Check for characters illegal in MS-DOS file names */
	for(cp = baddoschars;*cp != '\0';cp++){
		if(strchr(s,*cp) != NULL)
			return BADADDR;	
	}
#endif
	return LOCAL;
}

/* place a mail job in the outbound queue */
int
queuejob(dfile,host,to,from)
FILE *dfile;
char *host;
struct list *to;
char *from;
{
	FILE *fp;
	struct list *ap;
	char tmpstring[50], prefix[9], buf[LINELEN];
	register int cnt;

	sprintf(prefix,"%ld",get_msgid());
	mlock(Mailqdir,prefix);
	sprintf(tmpstring,"%s/%s.txt",Mailqdir,prefix);
	if((fp = fopen(tmpstring,WRITE_TEXT)) == NULL) {
		(void) rmlock(Mailqdir,prefix);
		return 1;
	}
	while((cnt = fread(buf, 1, LINELEN, dfile)) > 0)
		if(fwrite(buf, 1, cnt, fp) != cnt)
			break;
	if(ferror(fp)){
		fclose(fp);
		(void) rmlock(Mailqdir,prefix);
		return 1;
	}
	fclose(fp);
	sprintf(tmpstring,"%s/%s.wrk",Mailqdir,prefix);
	if((fp = fopen(tmpstring,WRITE_TEXT)) == NULL) {
		(void) rmlock(Mailqdir,prefix);
		return 1;
	}
	fprintf(fp,"%s\n%s\n",host,from);
	for(ap = to; ap != NULL; ap = ap->next) {
		fprintf(fp,"%s\n",ap->val);
		smtplog("queue job %s To: %s From: %s",prefix,ap->val,from);
	}
	fclose(fp);
	(void) rmlock(Mailqdir,prefix);
	return 0;
}

/* Deliver mail to the appropriate mail boxes */
static int
router_queue(data,from,to)
FILE *data;
char *from;
struct list *to;
{
	int c;
	register struct list *ap;
	FILE *fp;
	char tmpstring[50];
	char prefix[9];

	sprintf(prefix,"%ld",get_msgid());
	mlock(Routeqdir,prefix);
	sprintf(tmpstring,"%s/%s.txt",Routeqdir,prefix);
	if((fp = fopen(tmpstring,WRITE_TEXT)) == NULL) {
		(void) rmlock(Routeqdir,prefix);
		return 1;
	}
	rewind(data);
	while((c = getc(data)) != EOF)
		if(putc(c,fp) == EOF)
			break;
	if(ferror(fp)){
		fclose(fp);
		(void) rmlock(Routeqdir,prefix);
		return 1;
	}
	fclose(fp);
	sprintf(tmpstring,"%s/%s.wrk",Routeqdir,prefix);
	if((fp = fopen(tmpstring,WRITE_TEXT)) == NULL) {
		(void) rmlock(Routeqdir,prefix);
		return 1;
	}
	fprintf(fp,"From: %s\n",from);
	for(ap = to;ap != NULL;ap = ap->next) {
		fprintf(fp,"To: %s\n",ap->val);
	}
	fclose(fp);
	(void) rmlock(Routeqdir,prefix);
	smtplog("rqueue job %s From: %s",prefix,from);
	return 0;
}

/* add an element to the front of the list pointed to by head 
** return NULL if out of memory.
*/
struct list *
addlist(head,val,type)
struct list **head;
char *val;
int type;
{
	register struct list *tp;

	tp = (struct list *)callocw(1,sizeof(struct list));

	tp->next = NULL;

	/* allocate storage for the char string */
	tp->val = strdup(val);
	tp->type = type;

	/* add entry to front of existing list */
	if (*head == NULL)
		*head = tp;
	else {
		tp->next = *head;
		*head = tp;
	}
	return tp;

}

#define SKIPWORD(X) while(*X && *X!=' ' && *X!='\t' && *X!='\n' && *X!= ',') X++;
#define SKIPSPACE(X) while(*X ==' ' || *X =='\t' || *X =='\n' || *X == ',') X++;

/* check for and alias and expand alias into a address list */
static struct list *
expandalias(head, user)
struct list **head;
char *user;
{
	FILE *fp;
	register char *s,*p;
	struct rr *rrp, *rrlp;
	int inalias = 0;
	struct list *tp;
	char buf[LINELEN];
	
	/* no alias file found */
	if ((fp = fopen(Alias, READ_TEXT)) == NULL) {
		/* Try MB, MG or MR domain name records */
		rrlp = rrp = resolve_mailb(user);
		while(rrp != NULL){
			if(rrp->rdlength > 0){
				/* remove the trailing dot */
				rrp->rdata.name[rrp->rdlength-1] = '\0';
				/* replace first dot with @ if there is no @ */
				if(strchr(rrp->rdata.name,'@') == NULL
				   && (p = strchr(rrp->rdata.name,'.')) !=
				   NULL)
					*p = '@';
				if(strchr(rrp->rdata.name,'@') != NULL)
					tp = addlist(head,rrp->rdata.name,
						     DOMAIN);
				else
					tp = addlist(head,rrp->rdata.name,
						     LOCAL);
				++inalias;
			}
			rrp = rrp->next;
		}
		free_rr(rrlp);
		if(inalias)
			return tp;
		else
			return addlist(head, user, LOCAL);
	}

	while (fgets(buf,LINELEN,fp) != NULL) {
		p = buf;
		if ( *p == '#' || *p == '\0')
			continue;
		rip(p);

		/* if not in an matching entry skip continuation lines */
		if (!inalias && isspace(*p))
			continue;

		/* when processing an active alias check for a continuation */
		if (inalias) {
			if (!isspace(*p)) 
				break;	/* done */
		} else {
			s = p;
			SKIPWORD(p);
			*p++ = '\0';	/* end the alias name */
			if (strcmp(s,user) != 0)
				continue;	/* no match go on */
			inalias = 1;
		}

		/* process the recipients on the alias line */
		SKIPSPACE(p);
		while(*p != '\0' && *p != '#') {
			s = p;
			SKIPWORD(p);
			if (*p != '\0')
				*p++ = '\0';

			/* find hostname */
			if (strchr(s,'@') != NULL)
				tp = addlist(head,s,DOMAIN);
			else
				tp = addlist(head,s,LOCAL);
			SKIPSPACE(p);
		}
	}
	(void) fclose(fp);

	if (inalias)	/* found and processed and alias. */
		return tp;

	/* no alias found treat as a local address */
	return addlist(head, user, LOCAL);
}

static void
smtplog(char *fmt, ...)
{
	va_list ap;
	char *cp;
	long t;
	FILE *fp;

	if ((fp = fopen(Maillog,APPEND_TEXT)) == NULL)
		return;
	time(&t);
	cp = ctime(&t);
	rip(cp);
	fprintf(fp,"%s ",cp);
	va_start(ap,fmt);
	vfprintf(fp,fmt,ap);
	va_end(ap);
	fprintf(fp,"\n");
	fclose(fp);
}
/* send mail to a single user. Can be called from the ax24 mailbox or
** from the return mail function in the smtp client 
*/
static int
mailuser(data,from,to)
FILE *data;
char *from;
char *to;
{

		int address_type, ret;
		struct list *tolist = NULL;

		/* check if address is ok */
		if ((address_type = validate_address(to)) == BADADDR) {
			return 1;
		}
		/* if a local address check for an alias */
		if (address_type == LOCAL)
			expandalias(&tolist, to);
		else
			/* a remote address is added to the list */
			addlist(&tolist, to, address_type);
		ret = mailit(data,from,tolist);
		del_list(tolist);
		return ret;

}

/* Mailer daemon return mail mechanism */
int
mdaemon(data,to,lp,bounce)
FILE *data;		/* pointer to rewound data file */
char *to;		/* Overridden by Errors-To: line if bounce is true */
struct list *lp;	/* error log for failed mail */
int bounce;		/* True for failed mail, otherwise return receipt */
{
	time_t t;
	FILE *tfile;
	char buf[LINELEN], *cp, *newto = NULL;
	int cnt;
	if(to == NULL || (to != NULL && *to == '\0') || bounce){
		while(fgets(buf,sizeof(buf),data) != NULL) {
			if(buf[0] == '\n')
				break;
			/* Look for Errors-To: */
			if(htype(buf) == ERRORSTO &&
			   (cp = getaddress(buf,0)) != NULL){
				free(newto);
				newto = strdup(cp);
				break;
			}
		}
		if(newto == NULL && ((to != NULL && *to == '\0') ||
		   to == NULL))
			return -1;
		rewind(data);
	}
	if((tfile = tmpfile()) == NULL)
		return -1;
	time(&t);
	fprintf(tfile,"%s%s",Hdrs[DATE],ptime(&t));
	fprintf(tfile,"%s<%ld@%s>\n",Hdrs[MSGID],get_msgid(),Hostname);
	fprintf(tfile,"%sMAILER-DAEMON@%s (Mail Delivery Subsystem)\n",
		Hdrs[FROM],Hostname);
	fprintf(tfile,"%s%s\n",Hdrs[TO],newto != NULL ? newto : to);
	fprintf(tfile,"%s%s\n\n",Hdrs[SUBJECT],
		bounce ? "Failed mail" : "Return receipt");
	if(bounce) {
		fprintf(tfile,"  ===== transcript follows =====\n\n");
		for (; lp != NULL; lp = lp->next)
			fprintf(tfile,"%s\n",lp->val);
		fprintf(tfile,"\n");
	}
	fprintf(tfile,"  ===== %s follows ====\n",
		bounce ? "Unsent message" : "Message header");

	while(fgets(buf,sizeof(buf),data) != NULL){
		if(buf[0] == '\n')
			break;
		fputs(buf,tfile);
	}
	if(bounce){
		fputc('\n',tfile);
		while((cnt = fread(buf,1,sizeof(buf),data)) > 0)
			fwrite(buf,1,cnt,tfile);
	}
	fseek(tfile,0L,0);
	/* A null From<> so no looping replys to MAIL-DAEMONS */
	(void) mailuser(tfile,"",newto != NULL ? newto : to);
	fclose(tfile);
	free(newto);
	return 0;
}
