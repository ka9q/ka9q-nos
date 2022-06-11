/*
 *	POP2 Client routines.  Originally authored by Mike Stockett
 *	  (WA7DYX).
 *	Modified 12 May 1991 by Mark Edwards (WA6SMN) to use new timer
 *	facilities in NOS0423.  Fixed type mismatches spotted by C++.
 *	Modified 27 May 1990 by Allen Gwinn (N5CKP) for compatibility
 *	  with later releases (NOS0522).
 *	Added into NOS by PA0GRI (and linted into "standard" C)
 *
 *	Some code culled from previous releases of SMTP.
 *
 *	Client routines for Simple Mail Transfer Protocol ala RFC821
 *	A.D. Barksdale Garbee II, aka Bdale, N3EUA
 *	Copyright 1986 Bdale Garbee, All Rights Reserved.
 *	Permission granted for non-commercial copying and use, provided
 *	  this notice is retained.
 * 	Modified 14 June 1987 by P. Karn for symbolic target addresses,
 *	  also rebuilt locking mechanism
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
#include "dirutil.h"
#include "files.h"

extern char Badhost[];

#define BUF_LEN		257

/* POP client control block */

struct pop_ccb {
	FILE *network;		/* Network stream for this connection */
	char	state;		/* client state */
#define	   CALL		0
#define	   NMBR		3
#define	   SIZE		5
#define	   XFER		8
#define	   EXIT		10
	char	buf[BUF_LEN],	/* tcp input buffer */
		count;		/* input buffer length */
	int	folder_len;	/* number of msgs in current folder */
	long	msg_len;	/* length of current msg */
	int	msg_num;	/* current message number */
} *ccb;

static int Popquiet = 0;

static struct timer  popcli_t;
static int32 mailhost;
static char	mailbox_name[10],
		mailbox_pathname[BUF_LEN],
		username[20],
		password[20],
		Workfile_name[] ="mbox.pop";

static int domailbox(int argc,char *argv[],void *p);
static int domailhost(int argc,char *argv[],void *p);
static int douserdata(int argc,char *argv[],void *p);
static int doquiet(int argc,char *argv[],void *p);
static int dotimer(int argc,char *argv[],void *p);
static struct pop_ccb 	*new_ccb(void);
static void delete_ccb(void);
static void pop_send(int unused,void *cb1,void *p);
static int popkick(int argc,char *argv[],void *p);

/* I don't know why this isn't static, it isn't called anywhere else {was} */
int poptick(void);

static struct cmds Popcmds[] = {
	"mailbox",	domailbox,	0,	0,	NULL,
	"mailhost",	domailhost,	0,	0,	NULL,
	"kick",		popkick,	0,	0,	NULL,
	"quiet",	doquiet,	0,	0,	NULL,
	"timer",	dotimer,	0,	0,	NULL,
	"userdata",	douserdata,	0,	0,	NULL,
	NULL,
};


/* Command string specifications */

static char ackd_cmd[] = "ACKD\n",
#ifdef POP_FOLDERS
	fold_cmd[] = "FOLD %s\n",
#endif
	login_cmd[] = "HELO %s %s\n",
	/* nack_cmd[]      = "NACK\n",     /* Not implemented */
	quit_cmd[]      = "QUIT\n",
	read_cur_cmd[]  = "READ\n",
	retr_cmd[]      = "RETR\n";

/* Response string keys */

static char *greeting_rsp  = "+ POP2 ";

FILE	*fd;

int
dopop(argc,argv,p)
int 	argc;
char 	*argv[];
void 	*p;
{
	return subcmd(Popcmds,argc,argv,p);
}

static int
domailbox(argc,argv,p) 
int argc;
char *argv[];
void *p;
{
	if(argc < 2) {
		if(mailbox_name[0] == '\0')
			printf("maibox name not set yet\n");
		else
			printf("%s\n",mailbox_name);
	} else {
		strncpy(mailbox_name,argv[1],10);
	}

	return 0;
}

static int
domailhost(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int32 n;

	if(argc < 2) {
		printf("%s\n",inet_ntoa(mailhost));
	} else
		if((n = resolve(argv[1])) == 0) {
			printf(Badhost,argv[1]);
			return 1;
		} else
			mailhost = n;

	return 0;
}

static int
doquiet(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Popquiet,"POP quiet",argc,argv);
}

static int
douserdata(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if (argc < 2)
		printf("%s\n",username);
	else if (argc != 3) {
		printf("Usage: pop userdata <username> <password>\n");
		return 1;
	} else {
		sscanf(argv[1],"%18s",username);
		sscanf(argv[2],"%18s",password);
	}

	return 0;
}

/* Set scan interval */

static int
dotimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2) {
		printf("%lu/%lu\n",
			read_timer(&popcli_t) /1000L,
			dur_timer(&popcli_t)/ 1000L);
		return 0;
	}

	popcli_t.func  = (void (*)())poptick;  		/* what to call on timeout */
	popcli_t.arg   = NULL;				/* dummy value */
	set_timer(&popcli_t, atol(argv[1])*1000L); 	/* set timer duration */
	start_timer(&popcli_t);				/* and fire it up */
	return 0;
}

static int
popkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	poptick();
	return 0;
}

int
poptick()
{
	if (ccb == NULL) {

		/* Don't start if any of the required parameters have not been specified */

		if (mailhost == 0) {
			printf("mailhost not defined yet.(pop mailhost <host>)\n");
			return 0;
		}

		if (mailbox_name[0] == '\0') {
			printf("mailbox name not defined yet.(pop mailbox <name>)\n");
			return 0;
		}

		if (username[0] == '\0') {
			printf("username not defined yet. (pop user <name> <pass>)\n");
			return 0;
		}

		if (password[0] == '\0') {
			printf(" Unknown password\n");
			return 0;
		}

		if ((ccb = new_ccb()) == NULL) {
			fprintf(stderr,"*** Unable to allocate CCB");
			return 0;
		}

		newproc("Auto-POP Client",1024,pop_send,0,ccb,NULL,0);
	}

	/* Restart timer */

	start_timer(&popcli_t);
	return 0;
}

/* this is the master state machine that handles a single SMTP transaction */
/* it is called with a queue of jobs for a particular host. */

static void
pop_send(unused,cb1,p) 
int unused;
void *cb1;
void *p;
{
	char *cp;
	struct sockaddr_in fsocket;
	struct pop_ccb	*ccb;
	void pop_csm(struct pop_ccb *);
	void quit_session(struct pop_ccb *);
	int s;

	ccb = (struct pop_ccb *)cb1;
	fsocket.sin_family = AF_INET;
	fsocket.sin_addr.s_addr = mailhost;
	fsocket.sin_port = IPPORT_POP;

	s = socket(AF_INET,SOCK_STREAM,0);

	ccb->state = CALL;

	if (connect(s,(struct sockaddr *)&fsocket,SOCKSIZE) == 0) {
		logmsg(s,"Connected to mailhost %s", inet_ntoa(mailhost));
		ccb->network = fdopen(s,"r+t");
	} else {
		cp = sockerr(s);
		logmsg(s,"Connect to mailhost %s failed: %s", inet_ntoa(mailhost),
		    (cp != NULL)? cp: "");
	}

	while(1) {
		if (fgets(ccb->buf,BUF_LEN,ccb->network) == NULL)
			goto quit;

		rip(ccb->buf);
		pop_csm(ccb);
		if (ccb->state == EXIT)
			goto quit;
	}
quit:
	logmsg(s,"Connection closed to mailhost %s", inet_ntoa(mailhost));
	fclose(ccb->network);
	if (fd != NULL)
		fclose(fd);
	delete_ccb();
}

/* free the message struct and data */

static void
delete_ccb()
{
	if (ccb == NULL)
		return;

	free(ccb);
	ccb = NULL;
}

/* create a new  pop control block */

static struct
pop_ccb *new_ccb()
{
	register struct pop_ccb *ccb;

	if ((ccb = (struct pop_ccb *) callocw(1,sizeof(struct pop_ccb))) == NULL)
		return(NULL);
	return(ccb);
}

/* ---------------------- pop client code starts here --------------------- */

void
pop_csm(ccb)
struct pop_ccb	*ccb;
{
	FILE *mf;

	int mlock (char *,char *);
	int rmlock (char * ,char *);
	void quit_session(struct pop_ccb *);
	/* int mlock(char *dir,char *id);   */
	/* int rmlock(char *dir,char *id);   */


	switch(ccb->state) {
	case CALL:
		if (strncmp(ccb->buf,greeting_rsp,strlen(greeting_rsp)) == 0) {
			 fprintf(ccb->network,login_cmd,username,password);
			ccb->state = NMBR;
		} else
			(void) quit_session(ccb);
		break;

	case NMBR:

		switch (ccb->buf[0]) {
		case '#':
			if ((fd = fopen(Workfile_name,"a+")) == NULL) {
				perror("Unable to open work file");
				quit_session(ccb);
				return;
			}

			fseek(fd,0,SEEK_SET);
			ccb->folder_len = atoi(&(ccb->buf[1]));
			fprintf(ccb->network,read_cur_cmd);
			ccb->state = SIZE;
			break;

		case '+':

			/* If there is no mail (the only time we get a "+"
			 * response back at this stage of the game),
			 * then just close out the connection, because
			 * there is nothing more to do!! */

		default:
			quit_session(ccb);
			break;
		}
	break;

	case SIZE:
		if (ccb->buf[0] == '=') {
			ccb->msg_len = atol(&(ccb->buf[1]));
			if (ccb->msg_len > 0) {
				fprintf(ccb->network,retr_cmd);

				ccb->state = XFER;
			} else {
				logmsg(fileno(ccb->network),"POP client retrieved %d messages",
					    ccb->folder_len);

				/* All done, so do local cleanup */

				if (mlock(Mailspool,mailbox_name)) {
					printf("\n*** Local mailbox locked, new mail in file %s\n",
						 Workfile_name);
					quit_session(ccb);
					return;
				}

				sprintf(mailbox_pathname,"%s/%s.txt",Mailspool,
					mailbox_name);
				if ((mf = fopen(mailbox_pathname,"a+")) == NULL) {
					printf("\n*** Unable to open local mailbox, new mail in file %s\n",
					       Workfile_name);
					quit_session(ccb);
					return;
				}

				fseek(fd,0,SEEK_SET);

				while (!feof(fd)) {
					if(fgets(ccb->buf,BUF_LEN,fd) != NULL) {
						fputs(ccb->buf,mf);
					}
				}
				fclose(mf);
				fclose(fd);
				fd = NULL;
				printf("New mail arrived for %s from mailhost <%s>%c\n",
					mailbox_name, inet_ntoa(mailhost),
					Popquiet ? ' ' : '\007');
				rmlock(Mailspool,mailbox_name);
				unlink(Workfile_name);
				quit_session(ccb);
			}
		} else
			quit_session(ccb);
		break;

		case XFER:
			fprintf(fd,"%s\n",ccb->buf);

			ccb->msg_len -= (long)(strlen(ccb->buf)+2);	/* Add CRLF */

			if (ccb->msg_len > 0)
				return;

			fprintf(ccb->network,ackd_cmd);

			ccb->msg_num++;
			ccb->state = SIZE;
			break;

		case EXIT:
			if (fd != NULL)
				fclose(fd);
			break;

		default:
			break;
	}
}

void
quit_session(ccb)
struct pop_ccb	*ccb;
{
	fprintf(ccb->network,quit_cmd);

	ccb->state  = EXIT;
}
