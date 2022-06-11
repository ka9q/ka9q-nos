/*
 *	Client routines for Network News Tranfer Protocol ala RFC977
 *
 *	Copyright 1990 Anders Klemets - SM0RGV, All Rights Reserved.
 *	Permission granted for non-commercial copying and use, provided
 *	this notice is retained.
 *
 *	Changes copyright 1990 Bernie Roehl, All Rights Reserved.
 *	Permission granted for non-commercial copying and use, provided
 *	this notice is retained.
 *
 *  Revision history:
 *
 *     May 11, 1990 - br checked for invalid chars in news filenames
 *
 *     May 10, 1990 - br changed date stamp in 'From ' lines to
 *            seconds since GMT (to make parsing and expiry easier)
 *
 *     May 9, 1990 - br added locking of nntp.dat and history files,
 *            second parameter to NNTP DIR, fixed bug in updating of
 *            nntp.dat
 *
 *     early May, 1990 -- br added NNTP TRACE, NNTP DIR,
 *            server-specific newsgroups and connection windows,
 *            locking of newsgroup files using mlock() and rmlock(),
 *            date stamping of 'From ' lines, increased stack space,
 *            updating of nntp.dat only on successful sessions.
 *
 *     July 19, 1990 pa0gri Delinted and cleaned up. (calls and includes)
 *
 */
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <sys/timeb.h>
#include <ctype.h>
#include <string.h>  /* for strchr() */
#ifdef	__TURBOC__
#include <dir.h>
#endif
#include "global.h"
#include "timer.h"
#include "cmdparse.h"
#include "commands.h"
#include "socket.h"
#include "usock.h"
#include "netuser.h"
#include "proc.h"
#include "smtp.h"
#include "files.h"

#define NNTPMAXLEN	512

struct nntpservers {
	struct timer nntpcli_t;
	char *name;
	char *groups;
	int lowtime, hightime;  /* for connect window */
	struct nntpservers *next;
};

#define MAXGROUPDIRS 10

static struct grouploc {
	char *prefix;        /* e.g. comp, rec, net, talk, alt ... */
	char *directory;     /* directory where these groups should be */
	} groupdirs[MAXGROUPDIRS] = { NULL, NULL };

struct nntpservers *Nntpservers = NULL;
static char *Nntpgroups = NULL;
static unsigned short nntptrace = 1;

static char *validchars = "abcdefghijklmnopqrstuvwxyz0123456789-_";

static void nntptick(void *tp);
static void nntp_job(int i1,void *tp,void *v1);
static int gettxt(FILE *network,FILE *fp);
static int getreply(FILE *network);
static int getarticle(FILE *network,char *msgid);
static int dogroups(int argc,char *argv[],void *p);
static int doadds(int argc,char *argv[],void *p);
static int dodrops(int argc,char *argv[],void *p);
static int dokicks(int argc,char *argv[],void *p);
static int dolists(int argc,char *argv[],void *p);
static int donntrace(int argc,char *argv[],void *p);
static int dondir(int argc,char *argv[],void *p);

/* Tracing levels:
	0 - no tracing
	1 - serious errors reported
	2 - transient errors reported
	3 - session progress reported
	4 - actual received articles displayed
 */

static struct cmds Nntpcmds[] = {
	"addserver",	doadds,	0,	3,
	"nntp addserver <nntpserver> <interval>",
	"directory",	dondir,	0,	0,	NULL,
	"dropserver",	dodrops,	0,	2,
	"nntp dropserver <nntpserver>",
	"groups",	dogroups,	0,	0,	NULL,
	"kick",		dokicks,	0,	2,
	"nntp kick <nntpserver>",
	"listservers",	dolists,	0,	0,	NULL,
	"trace",	donntrace,	0,	0,	NULL,
	NULL,
};

int
donntp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Nntpcmds,argc,argv,p);
}

static int
doadds(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct nntpservers *np;
	for(np = Nntpservers; np != NULL; np = np->next)
		if(stricmp(np->name,argv[1]) == 0)
			break;
	if (np == NULL) {
		np = (struct nntpservers *) callocw(1,sizeof(struct nntpservers));
		np->name = strdup(argv[1]);
		np->next = Nntpservers;
		Nntpservers = np;
		np->groups = NULL;
		np->lowtime = np->hightime = -1;
		np->nntpcli_t.func = nntptick;	/* what to call on timeout */
		np->nntpcli_t.arg = (void *)np;
	}
	if (argc > 3) {
		int i;
		if (np->groups == NULL) {
			np->groups = mallocw(NNTPMAXLEN);
			*np->groups = '\0';
		}
		for (i = 3; i < argc; ++i) {
			if (isdigit(*argv[i])) {
				int lh, ll, hh, hl;
				sscanf(argv[i], "%d:%d-%d:%d", &lh, &ll, &hh, &hl);
				np->lowtime = lh * 100 + ll;
				np->hightime = hh * 100 + hl;
			} else if ((strlen(np->groups)+strlen(argv[i])+2) >= NNTPMAXLEN)
				printf("Group list too long!  Group '%s' ignored!\n", argv[i]);
			else {  /* it's a group, and it fits... add it to list */
				if (*np->groups != '\0')
					strcat(np->groups, ",");
				strcat(np->groups, argv[i]);
			}
		}
		if (*np->groups == '\0') {	/* No groups specified? */
			free(np->groups);
			np->groups = NULL;
		}
	}
	/* set timer duration */
	set_timer(&np->nntpcli_t,atol(argv[2])*1000L);
	start_timer(&np->nntpcli_t);		/* and fire it up */
	return 0;
}

static int
dodrops(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct nntpservers *np, *npprev = NULL;
	for(np = Nntpservers; np != NULL; npprev = np, np = np->next)
		if(stricmp(np->name,argv[1]) == 0) {
			stop_timer(&np->nntpcli_t);
			free(np->name);
			if (np->groups)
				free(np->groups);
			if(npprev != NULL)
				npprev->next = np->next;
			else
				Nntpservers = np->next;
			free(np);
			return 0;
	}
	printf("No such server enabled.\n");
	return 0;
}

static int
dolists(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct nntpservers *np;
	for(np = Nntpservers; np != NULL; np = np->next) {
		char tbuf[80];
		if (np->lowtime != -1 && np->hightime != -1)
			sprintf(tbuf, " -- %02d:%02d-%02d:%02d", np->lowtime/100, np->lowtime%100, np->hightime/100, np->hightime%100);
		else
			tbuf[0] = '\0';
		printf("%-32s (%lu/%lu%s) %s\n", np->name,
			read_timer(&np->nntpcli_t) /1000L,
			dur_timer(&np->nntpcli_t) /1000L,
			tbuf, np->groups ? np->groups : "");
	}
	return 0;
}

static int donntrace(argc, argv, p)
int argc;
char *argv[];
void *p;
{
	return setshort(&nntptrace,"NNTP tracing",argc,argv);
}
	
static char *News_spool = NULL;
static int np_all = 0;  /* non-zero if Newsdir is a malloc'ed space */

static int dondir(argc, argv, p)
int argc;
char *argv[];
void *p;
{
	if (argc < 2) {
		int i;
		printf("spool: %s\n", News_spool ? News_spool : Mailspool);
		printf("control: %s\n", Newsdir);
		for (i = 0; i < MAXGROUPDIRS; ++i)
			if (groupdirs[i].prefix)
				printf("%-10.10s %s\n", groupdirs[i].prefix, groupdirs[i].directory);
	} else {
		char *p;
		if ((p = strchr(argv[1], '=')) != NULL) {  /* set a groupdir */
			int i;
			*p++ = '\0';
			for (i = 0; i < MAXGROUPDIRS; ++i)
				if (groupdirs[i].prefix)
					if (!strnicmp(groupdirs[i].prefix, argv[1], strlen(argv[1]))) {
						if (groupdirs[i].directory) {
							free(groupdirs[i].directory);
							groupdirs[i].directory = NULL;
							}
						if (*p == '\0') {
							free(groupdirs[i].prefix);
							groupdirs[i].prefix = NULL;
						} else
							groupdirs[i].directory = strdup(p);
						return 0;
					}
			if (*p == '\0')  /* trashing a group that's not there */
				return 0;
			for (i = 0; i < MAXGROUPDIRS; ++i){
				if (groupdirs[i].prefix == NULL) {
					groupdirs[i].prefix = strdup(argv[1]);
					if (groupdirs[i].directory) {
						free(groupdirs[i].directory);
						groupdirs[i].directory = NULL;
					}
					groupdirs[i].directory = strdup(p);
					return 0;
				}
			}
			printf("Directory table full\n");
		}
		else {  /* no '=', so just set default */
			if (News_spool)
				free(News_spool);
			News_spool = strdup(argv[1]);
		}
		if (argc > 2) {  /* they specified a newsdir as well */
			if (np_all)
				free(Newsdir);
			Newsdir = strdup(argv[2]);
			np_all = 1;
		}
	}
	return 0;
}
	
static int
dokicks(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct nntpservers *np;
	for(np = Nntpservers; np != NULL; np = np->next)
		if(stricmp(np->name,argv[1]) == 0) {
			/* If the timer is not running, the timeout function has
			* already been called and we don't want to call it again.
			*/
			if(run_timer(&np->nntpcli_t)) {
				stop_timer(&np->nntpcli_t);
				nntptick((void *)np);
			}
			return 0;
	}
	printf("No such server enabled.\n");
	return 0;
}

static int
dogroups(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int i;
	if(argc < 2) {
		if(Nntpgroups == NULL || (Nntpgroups != NULL && strcmp(Nntpgroups,"*") == 0))
			printf("All groups are currently enabled.\n");
		else
			printf("Currently enabled newsgroups:\n%s\n",Nntpgroups);
		return 0;
	}
	if(Nntpgroups == NULL)
		Nntpgroups = mallocw(NNTPMAXLEN);
	*Nntpgroups = '\0';
	for(i=1; i < argc; ++i) {
		if(i > 1)
			strcat(Nntpgroups,",");
		strcat(Nntpgroups,argv[i]);
	}
	return 0;
}

/* This is the routine that gets called every so often to connect to
 * NNTP servers.
 */
static void
nntptick(tp)
void *tp;
{
	newproc("NNTP client", 3072, nntp_job, 0, tp, NULL,0);
}

static void
nntp_job(i1,tp,v1)
int i1;
void *tp, *v1;
{
	FILE *fp, *tmpf;
	int s = -1, i;
	FILE *network;
/*	long pos; */
	struct tm *ltm;
	time_t t;
	int now;
	struct nntpservers *np = (struct nntpservers *) tp;
	struct sockaddr_in fsocket;
	char tbuf[NNTPMAXLEN], buf[NNTPMAXLEN], *cp, *lastdate = NULL;
	if (nntptrace >= 3)
		printf("NNTP daemon entered, target = %s\n",np->name);
	if(availmem() != 0){
		if (nntptrace >= 2)
			printf("NNTP daemon quit -- low memory\n");
		/* Memory is tight, don't do anything */
		start_timer(&np->nntpcli_t);
		return;
	}

	time(&t);	/* more portable than gettime() */
	ltm = localtime(&t);
	now = ltm->tm_hour * 100 + ltm->tm_min;
	if (np->lowtime < np->hightime) {  /* doesn't cross midnight */
		if (now < np->lowtime || now >= np->hightime) {
			if (nntptrace >= 3)
				printf("NNTP window to '%s' not open\n", np->name);
			start_timer(&np->nntpcli_t);
			return;
		}
	} else {
		if (now < np->lowtime && now >= np->hightime) {
			if (nntptrace >= 3)
				printf("NNTP window to '%s' not open\n", np->name);
			start_timer(&np->nntpcli_t);
			return;
		}
	}

	fsocket.sin_addr.s_addr = resolve(np->name);
	if(fsocket.sin_addr.s_addr == 0) {  /* No IP address found */
		if (nntptrace >= 2)
			printf("NNTP can't resolve host '%s'\n", np->name);
		/* Try again later */
		start_timer(&np->nntpcli_t);
		return;
	}
	fsocket.sin_family = AF_INET;
	fsocket.sin_port = IPPORT_NNTP;

	s = socket(AF_INET,SOCK_STREAM,0);
	if(connect(s,(struct sockaddr *)&fsocket,SOCKSIZE) == -1){
		cp = sockerr(s);
		logmsg(s,"NNTP %s Connect failed: %s",psocket(&fsocket),
			cp != NULL ? cp : "");
		if (nntptrace >= 2)
			printf("NNTP %s Connect failed: %s\n",psocket(&fsocket),
		cp != NULL ? cp : "");
		goto quit;
	}
	network = fdopen(s,"r+t");

	/* Eat the banner */
	i = getreply(network);
	if(i == -1 || i >= 400) {
		logmsg(fileno(network),"NNTP %s bad reply on banner (response was %d)",psocket(&fsocket),i);
		if (nntptrace >= 1)
			printf("NNTP %s bad reply on banner (response was %d)\n",psocket(&fsocket),i);
		goto quit;
	}

	if (mlock(Newsdir, "nntp")) {
		if (nntptrace >= 2)
			printf("NNTP %s Connect failed: cannot lock nntp.dat\n", psocket(&fsocket));
		goto quit;
	}
	sprintf(buf,"%s/nntp.dat",Newsdir);
	if((fp = fopen(buf,APPEND_TEXT)) == NULL) {
		logmsg(fileno(network),"NNTP %s Connect failed: Cannot open %s",psocket(&fsocket),
			buf);
		if (nntptrace >= 1)
			printf("NNTP %s Connect failed: Cannot open %s\n",psocket(&fsocket), buf);
		rmlock(Newsdir, "nntp");
		goto quit;
	}
	rewind(fp);
/*	for(pos=0L; fgets(buf,NNTPMAXLEN,fp) != NULL;pos=ftell(fp)) { */
	for(; fgets(buf,NNTPMAXLEN,fp) != NULL;) {
		if((cp = strchr(buf,' ')) == NULL)
			continue;	/* something wrong with this line, skip it */
		*cp = '\0';
		if(stricmp(buf,np->name) == 0) {
			rip(cp+1);
			lastdate = strdup(cp+1);
			break;
		}
	}
	fclose(fp);
	rmlock(Newsdir, "nntp");

	if(lastdate == NULL)
		lastdate = strdup("700101 000000");
	/* snapshot the time for use later in re-writing nntp.dat */
	time(&t);
	ltm = localtime(&t);
				
	/* Get a list of new message-id's */
	if (np->groups) {
		if (nntptrace >= 3)
			printf("==>NEWNEWS %s %s\n", np->groups, lastdate);
		fprintf(network,"NEWNEWS %s %s\n", np->groups, lastdate);
	} else {
		if (nntptrace >= 3)
			printf("==>NEWNEWS %s %s\n", Nntpgroups != NULL ? Nntpgroups : "*", lastdate);
		fprintf(network,"NEWNEWS %s %s\n",Nntpgroups != NULL ? Nntpgroups : "*", lastdate);
	}
	free(lastdate);
	/* Get the response */
	if((i = getreply(network)) != 230) { /* protocol error */
		logmsg(fileno(network),"NNTP %s protocol error (response was %d)",psocket(&fsocket),i);
		if (nntptrace >= 1)
			printf("NNTP %s protocol error (response was %d)\n",psocket(&fsocket),i);
		goto quit;
	}
	if((tmpf = tmpfile()) == NULL) {
		if (nntptrace >= 1)
			printf("NNTP %s Cannot open temp file\n", psocket(&fsocket));
		goto quit;
	}
	if(gettxt(network,tmpf) == -1) {
		logmsg(fileno(network), "NNTP %s giving up: gettxt() failure",psocket(&fsocket));
		if (nntptrace >= 1)
			printf("NNTP %s giving up: gettxt() failure\n",psocket(&fsocket));
		fclose(tmpf);
		goto quit;
	}

	/* Open the history file */
	if (mlock(Newsdir, "history")) {
		if (nntptrace >= 1)
			printf("NNTP %s giving up: couldn't lock history file\n", psocket(&fsocket));
		fclose(tmpf);
		goto quit;
	}
	sprintf(buf,"%s/history",Newsdir);
	if((fp = fopen(buf,APPEND_TEXT)) == NULL) {
		logmsg(fileno(network),"NNTP %s Connect failed: Cannot open %s",psocket(&fsocket), buf);
		if (nntptrace >= 1)
			printf("NNTP %s Connect failed: Cannot open %s\n",psocket(&fsocket), buf);
		fclose(tmpf);
		goto quit;
	}
	/* search through the history file for matching message id's */
	rewind(tmpf);
	while(fgets(tbuf,NNTPMAXLEN,tmpf) != NULL) {
		i = 0;
		rewind(fp);
		while(fgets(buf,NNTPMAXLEN,fp) != NULL) {
			if(stricmp(buf,tbuf) == 0) {
				i = 1;
				break;
			}
			kwait(NULL);
		}
		if(i == 0) {		/* not found, get the article */
			if(getarticle(network,tbuf) == -1) {
				logmsg(fileno(network),"NNTP %s Giving up: could not get article",psocket(&fsocket));
				if (nntptrace >= 2)
					printf("NNTP %s Giving up: could not get article\n",psocket(&fsocket));
				fclose(fp);
				rmlock(Newsdir, "history");
				fclose(tmpf);
				goto quit;
			}
			fprintf(fp,"%s",tbuf); /* add the new message id */
		}
	}
	fclose(fp);
	rmlock(Newsdir, "history");
	fclose(tmpf);
	if (nntptrace >= 3)
		printf("==>QUIT\n");
	fprintf(network,"QUIT\n");
	/* Eat the response */
	getreply(network);
	/* NOW, update the nntp.dat file */
	if (mlock(Newsdir, "nntp")) {
		if (nntptrace >= 2)
			printf("NNTP %s Could not lock nntp.dat for update\n", psocket(&fsocket));
		goto quit;
	}
	sprintf(buf,"%s/nntp.dat",Newsdir);
	fp = fopen(buf,READ_TEXT);
	sprintf(buf, "%s/nntp.tmp",Newsdir);
	if ((tmpf = fopen(buf, WRITE_TEXT)) == NULL)
		if (nntptrace >= 1)
			printf("NNTP %s Cannot create temp file '%s'\n", psocket(&fsocket), buf);
	if (fp == NULL || tmpf == NULL) {
		logmsg(fileno(network),"NNTP %s Could not update %s", psocket(&fsocket), buf);
		if (nntptrace >= 2)
			printf("NNTP %s Could not update %s\n",psocket(&fsocket), buf);
		if (fp)
			fclose(fp);
		if (tmpf)
			fclose(tmpf);
		rmlock(Newsdir, "nntp");
		goto quit;
	}
	while (fgets(tbuf, sizeof(tbuf), fp))
		if (strnicmp(tbuf, np->name, strlen(np->name)))
			fputs(tbuf, tmpf);
	fprintf(tmpf,"%s %02d%02d%02d %02d%02d%02d\n",np->name,ltm->tm_year%100,ltm->tm_mon+1,
		ltm->tm_mday,ltm->tm_hour,ltm->tm_min,ltm->tm_sec);
	fclose(fp);
	fclose(tmpf);
	sprintf(buf, "%s/nntp.dat", Newsdir);
	sprintf(tbuf, "%s/nntp.tmp", Newsdir);
	unlink(buf);
	rename(tbuf, buf);
	rmlock(Newsdir, "nntp");
quit:
	if (nntptrace >= 3)
		printf("NNTP daemon exiting\n");
	fclose(network);
	/* Restart timer */
	start_timer(&np->nntpcli_t);
	return;
}

static int
gettxt(network,fp)
FILE *network;
FILE *fp;
{
	char buf[NNTPMAXLEN];
	int nlines;
	for (nlines = 0; fgets(buf,NNTPMAXLEN,network) != NULL; ++nlines) {
		if (nntptrace >= 4)
			printf("<==%s", buf);
		if(strcmp(buf,".\n") == 0) {
			if (nntptrace >= 3)
				printf("NNTP received %d lines\n", nlines);
			return 0;
			}
		/* check for escaped '.' characters */
		if(strcmp(buf,"..\n") == 0)
			fputs(".\n",fp);
		else
			fputs(buf,fp);
	}
	if (nntptrace >= 1)
		printf("NNTP receive error after %d lines\n", nlines);
	return -1;
}

static int
getreply(network)
FILE *network;
{
	char buf[NNTPMAXLEN];
	int response;
	while(fgets(buf,NNTPMAXLEN,network) != NULL) {
		/* skip informative messages and blank lines */
		if(buf[0] == '\0' || buf[0] == '1')
			continue;
		sscanf(buf,"%d",&response);
		if (nntptrace >= 3)
			printf("<==%s\n", buf);
		return response;
	}
	if (nntptrace >= 3)
		printf("==No response\n");
	return -1;
}

static int
getarticle(network,msgid)
FILE *network;
char *msgid;
{
	char buf[NNTPMAXLEN], froml[NNTPMAXLEN], newgl[NNTPMAXLEN];
	FILE *fp, *tmpf;
	int r;
	char *cp;
	extern int Smtpquiet;

	if (nntptrace >= 3)
		printf("==>ARTICLE %s", msgid);
	fprintf(network,"ARTICLE %s", msgid);
	r = getreply(network);
	if(r == -1 || r >= 500)
		return -1;
	if(r >= 400)
		return 0;
	if((tmpf = tmpfile()) == NULL) {
		if (nntptrace >= 1)
			printf("NNTP Cannot open temp file for article\n");
		return -1;
	}
	if(gettxt(network,tmpf) == -1) {
		fclose(tmpf);
		return -1;
	}
	/* convert the article into mail format */
	rewind(tmpf);
	froml[0] = '\0';
	newgl[0] = '\0';
	while(fgets(buf,NNTPMAXLEN,tmpf) != NULL) {
		if(strncmp(buf,"From: ",6) == 0) {
			struct timeb t;
			ftime(&t);
			rip(&buf[6]);
			sprintf(froml,"From %s %ld\n",&buf[6], t.time);
			if(newgl[0] != '\0')
				break;
		}
		if(strncmp(buf,"Newsgroups: ",12) == 0) {
			strcpy(newgl,&buf[12]);
			if(froml[0] != '\0')
				break;
		}
		/* invalid article - missing 'From:' line or 'Newsgroups:' line */
		if(strcmp(buf,"\n") == 0 && (froml[0] == '\0' || newgl[0] == '\0')) {
/*			fclose(fp); */
			fclose(tmpf);
			return 0;
		}
	}
	sprintf(buf,"%s/",News_spool ? News_spool : Mailspool);
	for(cp=newgl;;++cp) {
		if(*cp == '.') {
#ifdef __TURBOC__
			mkdir(buf); /* create a subdirectory, if necessary */
#else
			mkdir(buf,0755); /* create a subdirectory, if necessary */
#endif
			strcat(buf,"/");
			continue;
		}
		if(*cp == ',' || *cp == '\n') {
			char tempdir[80], prefix[20], *p;
			strcpy(tempdir, buf);
			if ((p = strrchr(tempdir, '/')) != NULL) {
				*p++ = '\0';
				strcpy(prefix, p);
			}
			if (mlock(tempdir, prefix)) {
				if (nntptrace >= 2)
					printf("NNTP group '%s' is locked\n", buf);
				return -1;
			}
			strcat(buf,".txt");
			/* open the mail file */
			if (nntptrace >= 3)
				printf("Writing article to '%s'\n", buf);
			if((fp = fopen(buf,APPEND_TEXT)) != NULL) {
				fputs(froml,fp);
				rewind(tmpf);
				while(fgets(buf,NNTPMAXLEN,tmpf) != NULL) {
					/* for UNIX mail compatiblity */
					if(strncmp(buf,"From ",5) == 0)
						putc('>',fp);
					fputs(buf,fp);
				}
				putc('\n',fp);
				fclose(fp);
			}
			rmlock(tempdir, prefix);
			if (*cp == '\n') 
				break;
			else
				sprintf(buf,"%s/",News_spool ? News_spool : Mailspool);
			continue;
		}
		buf[strlen(buf)+1] = '\0';
		buf[strlen(buf)] = strchr(validchars, tolower(*cp)) ? *cp : '_';
	}
	fclose(tmpf);
	strcpy(buf,msgid);		/* Get a copy we can munge */
	rip(buf);			/* remove trailing new-line */
	rip(newgl);			/* ditto */
#ifdef	notdef
	printf("New news arrived: %s, article %s%c\n",newgl,buf,Smtpquiet?' ':'\007');
#else
	printf("New news arrived: %s, article %s\n",newgl,buf);
#endif
	return 0;
}
