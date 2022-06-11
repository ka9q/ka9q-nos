/* Some of the code in this file was originally based on the following file:
 * gateway.c : Paul Healy, EI9GL, 900818
 *
 * Rewrote forwarding mechanism to use "X-Forwarded-To" paradigm instead of
 * "X-BBS-To", added timer support, etc.  Anders Klemets, SM0RGV, 901009.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "global.h"
#include "config.h"
#include "bm.h"
#include "mailbox.h"
#include "smtp.h"
#include "cmdparse.h"
#include "proc.h"
#include "socket.h"
#include "timer.h"
#include "usock.h"
#include "netuser.h"
#include "ax25.h"
#include "netrom.h"
#include "nr4.h"
#include "files.h"

#define ISPROMPT(s)	(strlen(s) > 1 && s[strlen(s)-2] == '>')
static struct timer fwdtimer;

static char *findident(char *str, int n, char *result);
static void sendmsg(struct mbx *m,int msgn);
static char *mbxtime(char *line);
static int fwdinit(struct mbx *m);
static char *fwdanybbs(struct mbx *m);
static int timeok(char *line);
static void fwdtick(void *v);
static int isconnbbs(struct mbx *m);
static void startfwd(int a,void *v1,void *v2);
static int openconn(int argc,char *argv[],void *p);
static int sendmsgtobbs(struct mbx *m,int msgn,char *dest,int bulletin);
static int makecl(struct mbx *m,int msgn,char *dest,char *line,char *subj,int bulletin);
static char *grabtext(char *from,char *to,int marker);

/***************************************************************************
   findident copies the 'n'th alphanumeric sequence from 'str' to result.
   It returns a ptr to result. It returns "\0" for missing identifier etc.
   Uses isalnum macro to decide on alphanumeric/non-alnum status.
*/
static char *
findident(str, n, result)
char *str, *result;
int n;
{
   int count; /* current identifier */
   count = 0;
   *result = '\0';
   while ( (count<n) && (*str!='\0') ) { /* Process alnum or non alnum seq */
      while ( (*str!='\0') && (!isalnum(*str)) ) /* Get rid of ';:.@%"# etc */
         str++;
      if ( (str!='\0') && isalnum(*str) ) { /* this is an alnum seq */
         count++;
         while ( (*str!='\0') && (isalnum(*str) || (*str=='_')) )
            if (count==n)
               *result++ = *str++;
            else str++;
         if (count==n)
            *result = '\0';
         }
      }
   return result;
}
/**************************************************************************/
static void
sendmsg(m,msgn)
struct mbx *m;
int msgn;
{
	char buf[LINELEN], tb[LINELEN], *cp;
	int len, rec = 0;
	long cnt;
	fseek(m->mfile,m->mbox[msgn].start,0);
	cnt = m->mbox[msgn].size;

	/* If the data part of the message starts with "R:" the RFC-822
	 * headers will not be forwarded. Instead we will add an R:
	 * line of our own.
	 */
	for(;;) {
		if(fgets(buf,sizeof(buf),m->mfile) == NULL)
			break;
		cnt -= strlen(buf);
		if(rec == 1) {	/* look at the line following Received: */
		     ++rec;
		     if((cp = strchr(buf,';')) != NULL){
			  strcpy(tb,cp+1); 	/* get the date of receipt */
			  ++rec;
		     }
		}
		/* The first Received: line is the one that we have added */
		if(!rec && htype(buf) == RECEIVED)
		     ++rec;
		if(*buf == '\n') {
		     if(rec == 3 && cnt > 1) {
			  fread(buf,1,2,m->mfile);
			  cnt -= 2;
			  if(strncmp(buf,"R:",2) == 0) {
			       pax25(buf,Mycall);
			       if((cp = strchr(buf,'-')) != NULL)
				    *cp = '\0';	/* remove SSID */
			       fprintf(m->user,"R:%s @%s %s (%s)\nR:",
					mbxtime(tb),buf,Hostname,Version);
			       break;
			  }
		     }
		     /* Start over, forwarding the RFC-822 headers */
		     fseek(m->mfile,m->mbox[msgn].start,0);
		     cnt = m->mbox[msgn].size;
		     rec = 0;
		     break;
		}
	   }
	while(rec != 3) {	/* Forward the RFC-822 headers */
		if(fgets(buf,sizeof(buf),m->mfile) == NULL)
			break;
		cnt -= strlen(buf);
		switch(htype(buf)) {
		case XFORWARD: /* Do not forward the "X-Forwarded-To:" lines */
		case STATUS:   /* Don't forward the "Status:" line either */
		     break;
		default:
		     fputs(buf,m->user);
		}
		if(*buf == '\n')	/* last header line */
			break;
	}
	do {	/* the rest of the message is treated below */
		len = min(cnt,sizeof(buf)-1);
		if(fread(buf,1,len,m->mfile) != len)
			break;
		cnt -= len;
		buf[len] = '\0';
		fputs(buf,m->user);
	} while(cnt);
}

/* Parse a line for date and time in Arpanet format
 * (Day, day Month year hh:mm:ss Zone) and return it in mailbox format
 * (yymmdd/hhmmz)
 */
static char *
mbxtime(line)
char *line;
{
     extern char *Months[];
     static char buf[13];
     char *cp;
     int i, day;
     cp = line;
     while(isspace(*cp))	/* skip initial blanks */
	  ++cp;
     if(*cp == '\0')
	  return NULL;
     if(strlen(cp) < 22)
	  return NULL;
     cp += 5;
     day = atoi(cp);
     if(*(++cp) != ' ')
	  ++cp;
     ++cp;
     for(i=0; i < 12; ++i)
	  if(strnicmp(Months[i],cp,3) == 0)
	       break;
     if(i == 12)
	  return NULL;
     sprintf(buf,"%02d%02d%02d/%02d%02d%c",atoi(cp + 4),i+1,day,atoi(cp + 7),
	     atoi(cp + 10), (strnicmp(cp + 16,"GMT",3) &&
			     strnicmp(cp + 16,"UTC",3)) ? ' ' : 'z');
     return buf;
}
     
static char *
grabtext(from, to, marker)
char *from, *to;
int marker;
{
   while (*from!=marker)
      *to++ = *from++;
   *to = '\0';
   return from+1;
}

/* Makes a command line and returns -1 if the message cannot be sent. */
static int
makecl(m, msgn, dest, line, subj, bulletin)
struct mbx *m;
int msgn;		/* Message number */
char *dest;		/* Destination address to use instead of To: line */
char *line, *subj;	/* Buffers to keep command line and subject */
int bulletin;		/* True if message is in public message area */
{
   char bid[LINELEN], to[LINELEN], atbbs[LINELEN], from[LINELEN],
	buf[LINELEN], *cp;
   if(m->mfile == NULL)
	return -1;
   if(!bulletin && (m->mbox[msgn].status & BM_READ))
	return -1;	/* the message was already read */
   fseek(m->mfile,m->mbox[msgn].start,0);
   *bid = *to = *atbbs = *from = '\0';
   if(subj != NULL)
	*subj = '\0';
   m->stype = bulletin ? 'B' : 'P';	/* default to SB or SP */
   while (fgets(buf,sizeof(buf),m->mfile) != NULL) {
      if (buf[0] == '\n')
         break; /* envelope finished */
      switch (htype(buf)) {
      case TO:
	    /* The following code tries to parse "To: " lines where the
	     * address looks like any of the following: "to@atbbs",
	     * "<to@atbbs>", "<to%atbbs@host>" and with possible spaces
	     * surrounding the '<>' characters.
	     */
	    if((cp = getaddress(buf,0)) == NULL)
		break;
	    strcpy(to,cp);
	    if((cp = strchr(to,'%')) != NULL) { /* look for a '%' */
		 strcpy(atbbs,cp + 1);
		 *cp = '\0';	/* "to" ends at the '%' character */
	    }
	    else {	/* no '%' but maybe a '@'? */
		 if((cp = strchr(to,'@')) != NULL) {
		      strcpy(atbbs,cp + 1);
		      *cp = '\0';	/* "to" ends at the '@' character */
		 }
	    }
	    if(*atbbs != '\0')		/* either '%' or '@' found */
		 /* cut "atbbs" at the first '@' character */
		 for(cp = atbbs; *cp != '\0'; ++cp)
		      if(*cp == '@') {
			   *cp = '\0';
			   break;
		      }
	    /* "to" or "atbbs" should not be more than 6 characters (ALEN).
	     * If "to" is too long, it might simply be because the area name
	     * is longer than 6 characters, but it might also be because
	     * the address on the To: line is in an obscure format that we
	     * failed to parse (eg '!' character notation.)
	     */
	    if(strlen(to) > ALEN) {
		/* Play safe and set "to" and "atbbs" to the area name */
		strcpy(to,m->area);
		strcpy(atbbs,m->area);
      	    }
	    if(*atbbs == '\0')
		strcpy(atbbs,to);
      	    to[ALEN] = '\0';
	    /* Only if the BBS supports "hierarchical routing designators"
	     * is the atbbs field allowd to be longer than 6 characters and
	     * have dots in it.
	     */
	    if((m->sid & MBX_HIER_SID) == 0) {
		 atbbs[ALEN] = '\0';	/* 6 character limit */
		 if((cp = strchr(atbbs,'.')) != NULL)
		      *cp = '\0';	/* cut "atbbs" at first dot */
	    }
            break;
      case MSGID:
	    /* The following code distinguishes between two different types
	     * of Message-IDs: <abcde@callsign.bbs> and <abcde@host.domain>.
	     * The first type is converted to $abcde and the second to
	     * $abcde_host.domain. This preserves compability with BBSes.
	     */
	    if((cp = getname(buf)) == NULL)
		 break;
	    bid[0] = '$';
	    strcpy(&bid[1],cp);
      	    cp = strchr(bid,'@');
	    /* A trailing ".bbs" indicates that the Message-ID was generated
	     * from a BBS style message, and not a RFC-822 message.
	     */
	    if(cp != NULL && stricmp(&bid[strlen(bid) - 4], ".bbs") == 0)
		*cp = '\0';
	    else
		*cp = '_';
	    bid[13] = '\0';	/* BIDs should be no longer than 13 bytes */
      	    break;
      case SUBJECT:
	    if(subj != NULL)
      		(void) grabtext(buf+9, subj, '\n');
            break;
      case FROM:
	    if((cp = getaddress(buf,0)) != NULL) {
		findident(cp, 1, from);		/* cp points to from@domain */
		from[ALEN] = '\0';	/* 6 character limit */
	    }
            break;
      case XFORWARD:
	    if((cp = getaddress(buf,0)) == NULL)
		 break;
	    if(stricmp(m->name,cp) == 0)
		/* This message has already been forwarded, abort */
		return -1;
	    break;
      case BBSTYPE:
	    m->stype = buf[16];
	    break;
      default:
	    break;
      }
   }
   /* Check for an invalid RFC-822 header */
   if((to[0] == '\0' && ((dest != NULL && *dest == '\0') ||
      dest == NULL)) || from[0] == '\0')
	return -1;

   if(line != NULL) {
	if(dest != NULL && *dest != '\0'){
	     /* strip off hierarchical routing designators from the predefined
	      * destination address if they are not supported
	      */
	     if((m->sid & MBX_HIER_SID) == 0 && (cp = strchr(dest,'.')) !=
	       NULL)
		*cp = '\0';
	     sprintf(line, "S%c %s < %s ", m->stype, dest, from);
	}
	else
	     sprintf(line, "S%c %s @ %s < %s ", m->stype, to, atbbs, from);
	if(bulletin & (m->sid & MBX_SID))
	     strcat(line,bid);
	strcat(line,"\n");
   }
   return 0;
}

static int /* 0 = ok, -1 = problem so disc */
sendmsgtobbs(m, msgn, dest, bulletin)
struct mbx *m;
int msgn;
char *dest;		/* Optional destination address to override To: line */
int bulletin;
{
   int result = -1;
   char line[64], subj[256];
   if(makecl(m, msgn, dest, line, subj, bulletin) == -1)
	return 0;	/* do not forward this particular message */
   fputs(line,stdout);		 /* Send mail offer to bbs */
   rip(line);
   fflush(m->user);
   if (fgets(m->line, MBXLINE,m->user) != NULL ) {
      if (m->line[0] == 'O' || m->line[0] == 'o' || (m->sid & MBX_SID) == 0) {
	 /* Got 'OK' or any line if the bbs is unsofisticated */
         printf("%s\n", subj);
	 sendmsg(m,msgn);	/* send the message */
         puts("/EX"); /* was 0x1a */
         fflush(m->user);
      	 /* get F> for a good deliver */
      	 while (fgets(m->line, MBXLINE,m->user) != NULL )
		if (ISPROMPT(m->line)) {
			logmsg(fileno(m->user),"MBOX bbs mail sent: %s ", line);
			if(bulletin)
				m->mbox[msgn].status |= BM_FORWARDED;
			else
				m->mbox[msgn].status |= BM_DELETE;
			m->change = 1;
			result = 0;
			break;
		}
      }
      else { /* OK response not received from bbs */
	   if (m->line[0] == 'N' || m->line[0] == 'n') { /* 'NO' respone */
          	logmsg(fileno(m->user),"MBOX bbs mail refused: %s\n     %s",line,m->line);
		/* Mark refused message as forwarded if it is a bulletin.
		 * The message was probably a duplicate. Non-bulletin
		 * messages are sent without BID, so they cannot be dected
		 * as duplicates. The reason why it was refused is probably
		 * because the address was invalid. Retry later.
		 */
		if(bulletin){
			m->mbox[msgn].status |= BM_FORWARDED;
			m->change = 1;
		}
          }
      	  /* should get a F> here */
          while (fgets(m->line, MBXLINE,m->user) != NULL )
      		if (ISPROMPT(m->line)) {
      			result = 0;
			break;
      		}
      }
   } /* OK or NO here */
   return result;
}

/* This is the main entry point for reverse forwarding. It is also used
 * for normal, "forward", forwarding.
 */
int
dorevfwd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char oldarea[64], *cp;
	struct mbx *m;
	int i, bulletin, err = 0;
	m = (struct mbx *)p;
	logmsg(fileno(m->user),"MBOX forwarding mail to: %s ", m->name);
	/* indicate we are doing reverse forwarding, if we are not already
	 * doing normal forwarding.
	 */
	if(m->state != MBX_FORWARD)
		m->state = MBX_REVFWD;
	if(fwdinit(m) != -1) {
		strcpy(oldarea,m->area);
		while(!err && fgets(m->line,MBXLINE,m->tfile) != NULL) {
			if(*m->line == '-')	/* end of record reached */
				break;
			rip(m->line);		/* adds extra null at end */
			cp = strchr(m->line,' '); /* remove trailing blanks */
			if(cp != NULL)
				*cp = '\0';
			if((cp = strchr(m->line,'\t')) != NULL)
				*cp = '\0';
			if(*m->line == '\0' || *m->line == '.')
				continue;
			changearea(m,m->line);
			bulletin = isarea(m->line);	/* public area */
			/* get the optional destination field, cp will point
			 * at null byte if it is missing.
			 */
			cp = &m->line[strlen(m->line)] + 1;
			while(*cp != '\0' && isspace(*cp)) /* strip blanks */
			     ++cp;
			for(i=1; i<=m->nmsgs; i++)
				if(sendmsgtobbs(m, i, cp, bulletin) == -1) {
					err = 1;	/* abort */
					break;
				}
		}
		fclose(m->tfile);
		m->tfile = NULL;
		if(*oldarea != '\0')
			changearea(m,oldarea);
	}
	if(m->state == MBX_FORWARD)
		return 0;
	printf("*** Done\n");
	if((m->sid & MBX_RLI_SID))	/* disconnect if it is a W0RLI bbs */
		return domboxbye(0,NULL,m);
	return 0;
}

/* Read the forward file for a record for the connected BBS. If found,
 * return 1 if this is the right time to forward, m->tfile is left pointing
 * at the first message area to be forwarded.
 */
static int
fwdinit(m)
struct mbx *m;
{
	char host[80];
	int start = 1;
	if((m->tfile = fopen(Forwardfile,READ_TEXT)) == NULL)
		return -1;
	while(fgets(m->line,MBXLINE,m->tfile) != NULL) {
		if(*m->line == '\n')
			continue;
		/* lines starting with '-' separate the forwarding records */
		if(*m->line == '-') {
			start = 1;
			continue;
		}
		if(start) {
			start = 0;
			/* get the name of this forwarding record */
			findident(m->line,1,host);
			if(stricmp(m->name,host) == 0) {
				if(!timeok(m->line))
					break;
				/* eat the connect command line */
				fgets(m->line,MBXLINE,m->tfile);
				return 0;
			}
		}
	}
	fclose(m->tfile);
	m->tfile = NULL;
	return -1;
}
/* Read the forward file for a record for the connected BBS. If found,
 * determine if this is the right time to forward, and return the command
 * line to establish a forwarding connection. m->tfile is left pointing
 * at the first message area to be forwarded.
 */
static char *
fwdanybbs(m)
struct mbx *m;
{
	char host[80];
	int start = 1;
	if(m->tfile == NULL && (m->tfile = fopen(Forwardfile,READ_TEXT))
					== NULL)
		return NULL;
	while(fgets(m->line,MBXLINE,m->tfile) != NULL) {
		if(*m->line == '\n')
			continue;
		/* lines starting with '-' separate the forwarding records */
		if(*m->line == '-') {
			start = 1;
			continue;
		}
		if(start) {
			start = 0;
			/* get the name of this forwarding record */
			findident(m->line,1,host);
			strcpy(m->name,host);
			if(!timeok(m->line))
				continue;	/* too late or too early */
			/* get the connect command line */
			fgets(m->line,MBXLINE,m->tfile);
			return strdup(m->line);
		}
	}
	fclose(m->tfile);
	m->tfile = NULL;
	return NULL;
}

/* get any groups of four digits that specify the begin and ending hours of
 * forwarding. Returns 1 if forwarding may take place.
 */
static int
timeok(line)
char *line;
{
	char hours[80], *now;
	long t;
	int t1, t2, pos = 2;
	findident(line,pos++,hours);
	if(*hours == '\0')
		return 1;	/* no digits default to 0023, ie. anytime */
	time(&t);
	now = ctime(&t) + 11;
	*(now + 2) = '\0';
	while(*hours != '\0') {
		t1 = (*hours - '0') * 10 + (*(hours+1) - '0');
		t2 = (*(hours+2) - '0') * 10 + (*(hours+3) - '0');
		if(atoi(now) >= t1 && atoi(now) <= t2)
			return 1;		/* right in time */
		findident(line,pos++,hours);	/* get next group if any */
	}
	return 0;	/* too early or too late */
}

int
dombtimer(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		printf("Forwarding timer: %lu/%lu\n",
		read_timer(&fwdtimer)/1000L,
		dur_timer(&fwdtimer)/1000L);
		return 0;
	}
	fwdtimer.func = (void (*)())fwdtick;/* what to call on timeout */
	fwdtimer.arg = NULL;		/* dummy value */
	set_timer(&fwdtimer,atol(argv[1])*1000L); /* set timer duration */
	start_timer(&fwdtimer);		/* and fire it up */
	return 0;
}

int
dombkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	fwdtick(NULL);
	return 0;
}

/* called when the forward timer expires or explicitly by dombkick() */
static void
fwdtick(v)
void *v;
{
	char *cc, *cp;
	struct mbx *m;
	int i, bulletin, skip = 0;
	/* restart the timer */
	start_timer(&fwdtimer);
	if((m = newmbx()) == NULL)
		return;
	m->user = stdout;
	m->state = MBX_TRYING;
	while((cc = fwdanybbs(m)) != NULL) {
		if(isconnbbs(m)) /* already connected to this BBS, skip it */
			skip = 1;
		while(fgets(m->line,MBXLINE,m->tfile) != NULL) {
			if(*m->line == '-') {	/* end of record reached */
				skip = 0;
				break;
			}
			if((cp = strchr(m->line,' ')) != NULL)
				*cp = '\0';
			if((cp = strchr(m->line,'\t')) != NULL)
				*cp = '\0';
			if(skip || *m->line == '\0' || *m->line == '.')
				continue;
			rip(m->line);
			changearea(m,m->line);
			bulletin = isarea(m->line);	/* public area */
			/* check if there are any messages in this area
			 * that need to be forwarded.
			 */
			for(i=1; i<=m->nmsgs; i++)
				if(makecl(m, i, NULL, NULL, NULL,
				   bulletin) == 0) {
					newproc("Mbox forwarding", 2048,
						startfwd, 0, (void *)cc,
						(void *)strdup(m->name),0);
					skip = 1;
					cc = NULL;
					break;
				}
		}
		free(cc);
	}
	exitbbs(m);
}

/* returns 1 if m->name matches the name of another connected mailbox. */
static int
isconnbbs(m)
struct mbx *m;
{
	int i;
	for(i = 0; i < NUMMBX; ++i)
		if(Mbox[i] != NULL && Mbox[i] != m &&
			stricmp(m->name,Mbox[i]->name) == 0)
				return 1;
	return 0;
}

/* possible commands on the command line in the forwarding file */
static struct cmds cfwdcmds[] = {
	"tcp",		openconn,	0, 0, NULL,
	"telnet",	openconn,	0, 0, NULL,
#ifdef AX25
	"ax25",		openconn,	0, 0, NULL,
	"connect",	openconn,	0, 0, NULL,
#endif
#ifdef NETROM
	"netrom",	openconn,	0, 0, NULL,
#endif
	NULL
};

/* this function is called whenever the forwarding timer expires */
static void
startfwd(a,v1,v2)
int a;
void *v1, *v2;
{
	struct mbx *m;
	char *cc;
	cc = (char *) v1;
	if((m = newmbx()) == NULL) {
		free(cc);
		free(v2);
		return;
	}
	strcpy(m->name,(char *)v2);
	free(v2);
	m->state = MBX_TRYING;
	/* open the connection, m->user will be the new stream */
	if(cmdparse(cfwdcmds,cc,(void *)m) == -1) {
		free(cc);
		exitbbs(m);
		return;
	}
	free(cc);
	m->state = MBX_FORWARD;
	sockowner(fileno(m->user),Curproc);
	
	/* m->user will be closed automatically when this process exits */
	stdin = stdout = m->user;

	if(fwdinit(m) == -1) {
		/* it is probably not the right time to forward anymore */
		exitbbs(m);
		return;
	}
	/* read the connect script. Lines starting with a dot will be sent
	 * to the remote BBS.
	 */
	while(fgets(m->line,MBXLINE,m->tfile) != NULL)
		if(*m->line == '.')
			fputs(m->line + 1,stdout);
		else
			break;
	fflush(m->user);
	fclose(m->tfile);
	m->tfile = NULL;

	/* read the initial output from the bbs, looking for the SID */
	for(;;) {
		if(fgets(m->line,MBXLINE,m->user) == NULL) {
			exitbbs(m);
			return;
		}
		if(ISPROMPT(m->line))
			break;
		if(*m->line == '[') {		/* parse the SID */
			rip(m->line);
			mbx_parse(m);
			continue;
		}
	}
	/* Now sync the two ends as telnet password messes them up */
	if(socklen(fileno(m->user),0))		/* discard any remaining input */
		recv_mbuf(fileno(m->user),NULL,0,NULL,0);

	/* send our SID if the peer announced its SID */
	if(m->sid & MBX_SID) {
		puts("[NET-HMR$]");
		fflush(m->user);
		for(;;) {
			if(fgets(m->line,MBXLINE,m->user) == NULL) {
				exitbbs(m);
				return;
			}
			if(ISPROMPT(m->line))
				break;
		}
	}
	/* start the actual forwarding */
	dorevfwd(0,NULL,(void *)m);
	/* ask for reverse forwarding or just disconnect */
	if(((m->sid & MBX_SID) && puts("F>") == -1) ||
	   (m->sid & MBX_SID) == 0) {
		exitbbs(m);
		fclose(stdout);
		return;
	}
	fflush(m->user);
	/* parse the commands that are are received during reverse
	 * forwarding.
	 */
	while(fgets(m->line,MBXLINE,m->user) != NULL) {
		rip(m->line);
		if(mbx_parse(m) == 2)	/* got the "*** Done" command */
			break;
		puts("F>");
		fflush(m->user);
	}
	exitbbs(m);
	fclose(stdout);
}

/* open a network connection based upon information in the cc line.
 * m->user is set to the socket number.
 */
static int
openconn(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	struct sockaddr sock;
	uint8 *np;
	char alias[AXBUF];
	union sp sp;
	int len;
	int s;

	m = (struct mbx *)p;
	sp.sa = &sock;
	if(argc < 2)
		return -1;
	switch(*argv[0]) {
	case 't':
		sp.in->sin_family = AF_INET;
		if((sp.in->sin_addr.s_addr = resolve(argv[1])) == 0)
			return -1;
		/* get the optional port number */
		if(argc > 2)
			sp.in->sin_port = atoi(argv[2]);
		else
			sp.in->sin_port = IPPORT_TELNET;
		if((s = socket(AF_INET,SOCK_STREAM,0)) == -1)
			return -1;
		m->user = fdopen(s,"r+t");
		len = sizeof(*sp.in);
		break;
#ifdef AX25
	case 'a':
	case 'c':	/* allow 'c' for 'connect' as well */
		if(argc < 3)
			return -1;
		sp.ax->sax_family = AF_AX25;
		strncpy(sp.ax->iface,argv[1],ILEN); /* the interface name */
		setcall(sp.ax->ax25_addr,argv[2]); /* the remote callsign */
		/* no digipeaters for now, use the "ax25 route add" command */
		if((s = socket(AF_AX25,SOCK_STREAM,0)) == -1)
			return -1;
		m->user = fdopen(s,"r+t");
		len = sizeof(*sp.ax);
		break;
#endif /* AX25 */
#ifdef NETROM
	case 'n':
		sp.nr->nr_family = AF_NETROM;
		len = sizeof(*sp.nr);
		if((s = socket(AF_NETROM,SOCK_SEQPACKET,0)) == -1)
			return -1;
		m->user = fdopen(s,"r+t");
		memcpy(sp.nr->nr_addr.user,Nr4user,AXALEN);
		memcpy(sp.nr->nr_addr.node,Mycall,AXALEN);
		bind(s,sp.sa,len);
		/* See if the requested destination could be an alias, and
		 * use it if it is.  Otherwise assume it is an AX.25
		 * address.
		 */
		if (putalias(alias,argv[1],0) != -1 &&
			(np = find_nralias(alias)) != NULL) {
				memcpy(sp.nr->nr_addr.user,np,AXALEN) ;
				memcpy(sp.nr->nr_addr.node,np,AXALEN) ;
		}
		else {	/* parse ax25 callsign */
		/* Only the user callsign of the remote station is never
		 * used by NET/ROM, but it is needed for the psocket() call.
		 */
			setcall(sp.nr->nr_addr.user,argv[1]);
			setcall(sp.nr->nr_addr.node,argv[1]);
		}
		break;
#endif /* NETROM */
	default:
		return -1;
	}
	if(connect(fileno(m->user),sp.sa,len) == -1) {
		logmsg(fileno(m->user),"MBOX forward failed: %s errno %d",
				sockerr(fileno(m->user)),errno);
		fclose(m->user);
		return -1;
	}
	return 0;
}
