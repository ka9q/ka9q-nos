/*
 *	Simple mail user interface for KA9Q IP/TCP package.
 *	A.D. Barksdale Garbee II, aka Bdale, N3EUA
 *	Copyright 1986 Bdale Garbee, All Rights Reserved.
 *	Permission granted for non-commercial copying and use, provided
 *	this notice is retained.
 *	Copyright 1987 1988 Dave Trulli NN2Z, All Rights Reserved.
 *	Permission granted for non-commercial copying and use, provided
 *	this notice is retained.
 *
 *	Ported to NOS at 900120 by Anders Klemets SM0RGV.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "global.h"
#include "ftpserv.h"
#include "smtp.h"
#include "proc.h"
#include "usock.h"
#include "socket.h"
#include "telnet.h"
#include "timer.h"
#include "session.h"
#include "files.h"

#define		SETVBUF
#if	defined(UNIX) || defined(MICROSOFT)
#include	<sys/types.h>
#endif
/*
#if	defined(UNIX) || defined(MICROSOFT) || defined(__TURBOC__)
#include	<sys/stat.h>
#endif
#ifdef AZTEC
#include <stat.h>
#endif
*/
#include <fcntl.h>
#include "bm.h"
#include "mailbox.h"

#ifdef SETVBUF
#define		MYBUF	1024
#endif

extern long ftell();
static char Badmsg[] = "Invalid Message number %d\n";
static char Nomail[] = "No messages\n";
static char Noaccess[] = "Unable to access %s\n";
static int readnotes(struct mbx *m,FILE *ifile,int update);
static long isnewmail(struct mbx *m);
static int initnotes(struct mbx *m);
static int lockit(struct mbx *m);
static long fsize(char *name);
static void mfclose(struct mbx *m);
static int tkeywait(char *prompt,int flush);

static int
initnotes(m)
struct mbx *m;
{
	FILE	*tmpfile();
	FILE	*ifile;
	register struct	let *cmsg;
	char buf[256];
	int 	i, ret;

	sprintf(buf,"%s/%s.txt",Mailspool,m->area);
	if ((ifile = fopen(buf,READ_TEXT)) == NULL)
		return 0;
	fseek(ifile,0L,2);	 /* go to end of file */
	m->mboxsize = ftell(ifile);
	rewind(ifile);
	if(!stricmp(m->area,m->name)) /* our private mail area */
		m->mysize = m->mboxsize;
	if ((m->mfile = tmpfile()) == NULL) {
		(void) fclose(ifile);
		return -1;
	}
#ifdef	SETVBUF
	if (m->stdinbuf == NULL)
		m->stdinbuf = mallocw(MYBUF);
	setvbuf(ifile, m->stdinbuf, _IOFBF, MYBUF);
	if (m->stdoutbuf == NULL)
		m->stdoutbuf = mallocw(MYBUF);
	setvbuf(m->mfile, m->stdoutbuf, _IOFBF, MYBUF);
#endif
	m->nmsgs = 0;
	m->current = 0;
	m->change = 0;
	m->newmsgs = 0;
	m->anyread = 0;
	/* Allocate space for reading messages */
	free(m->mbox);
	m->mbox = (struct let *)callocw(Maxlet+1,sizeof(struct let));
	ret = readnotes(m,ifile,0);
	(void) fclose(ifile);
#ifdef SETVBUF
	free(m->stdinbuf);
	m->stdinbuf = NULL;
#endif
	if (ret != 0)
		return -1;
	for (cmsg = &m->mbox[1],i = 1; i <= m->nmsgs; i++, cmsg++)  
		if ((cmsg->status & BM_READ) == 0) {
			m->newmsgs++;
			if (m->current == 0)
				m->current = i;
		}
	/* start at one if no new messages */
	if (m->current == 0)
		m->current++;

	return 0;
}

/* readnotes assumes that ifile is pointing to the first
 * message that needs to be read.  For initial reads of a
 * notesfile, this will be the beginning of the file.  For
 * rereads when new mail arrives, it will be the first new
 * message.
 */
static int
readnotes(m,ifile,update)
struct mbx *m;
FILE *ifile ;
int update;	/* true if this is not the initial read of the notesfile */
{
	char 	tstring[LINELEN];
	long	cpos;
	register struct	let *cmsg;
	register char *line;

	cmsg = (struct let *)NULL;
	line = tstring;
	while(fgets(line,LINELEN,ifile) != NULL) {
		/* scan for begining of a message */
		if(strncmp(line,"From ",5) == 0) {
			kwait(NULL);
			cpos = ftell(m->mfile);
			fputs(line,m->mfile);
			if (m->nmsgs == Maxlet) {
				printf("Mail box full: > %d messages\n",Maxlet);
				mfclose(m);
				return -1;
			}
			m->nmsgs++;
			cmsg = &m->mbox[m->nmsgs];
			cmsg->start = cpos;
			if(!update)
				cmsg->status = 0;
			cmsg->size = strlen(line);
			while (fgets(line,LINELEN,ifile) != NULL) {
				if (*line == '\n') { /* done header part */
					cmsg->size++;
					putc(*line, m->mfile);
					break;
				}
				if (htype(line) == STATUS) {
					if (line[8] == 'R') 
						cmsg->status |= BM_READ;
					continue;
				}
				cmsg->size += strlen(line);
				if (fputs(line,m->mfile) == EOF) {
					printf("tmp file: %s",sys_errlist[errno]);
					mfclose(m);
					return -1;
				}

			}
		} else if (cmsg) {
			cmsg->size += strlen(line);
			fputs(line,m->mfile);
		}
	}
	return 0;
}

/* list headers of a notesfile a message */
int
dolistnotes(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	register struct	let *cmsg;
	register char	*cp, *s;
	char	smtp_date[SLINELEN], smtp_from[SLINELEN];
	char	smtp_subject[SLINELEN], tstring[LINELEN], type;
	int	start, stop;
	long	size;
	char	*area;

	m = (struct mbx *) p;
	if (m->mfile == NULL) {
		printf(Nomail);
		return 0;
	}

	area = strdup(m->area);
	while((cp = strchr(area,'/')) != NULL)
		*cp = '.';
	printf("Mail area: %s  %d message%s -  %d new\n\n",area,m->nmsgs,
		m->nmsgs == 1 ? " " : "s ", m->newmsgs);
	free(area);

	stop = m->nmsgs;
	if(m->stype == 'L') {		/* LL (List Latest) command */
	     if(argc > 1)
		  start = stop - atoi(argv[1]) + 1;
	     else
		  start = stop;
	}
	else {
	     if(argc > 1)
		  start = atoi(argv[1]);
	     else
		  start = 1;
	     if(argc > 2)
		  stop = atoi(argv[2]);
	}
	if(stop > m->nmsgs)
		stop = m->nmsgs;
	if(start < 1 || start > stop) {
		printf("Invalid range.\n");
		return 0;
	}
	for (cmsg = &m->mbox[start]; start <= stop; start++, cmsg++) {
		*smtp_date = '\0';
		*smtp_from = '\0';
		*smtp_subject = '\0';
		type = ' ';
		fseek(m->mfile,cmsg->start,0);
		size = cmsg->size;
		while (size > 0 && fgets(tstring,sizeof(tstring),m->mfile)
		       != NULL) {
			if (*tstring == '\n')	/* end of header */
				break;
			size -= strlen(tstring);
			rip(tstring);
			/* handle continuation later */
			if (*tstring == ' '|| *tstring == '\t')
				continue;
			switch(htype(tstring)) {
			case FROM:
				cp = getaddress(tstring,0);
				sprintf(smtp_from,"%.30s",
					cp != NULL ? cp : "");
				break;
			case SUBJECT:
				sprintf(smtp_subject,"%.34s",&tstring[9]);
				break;
			case DATE:
				if ((cp = strchr(tstring,',')) == NULL)
					cp = &tstring[6];
				else
					cp++;
				/* skip spaces */
				while (*cp == ' ') cp++;
				if(strlen(cp) < 17)
					break; 	/* not a valid length */
				s = smtp_date;
				/* copy day */
				if (atoi(cp) < 10 && *cp != '0') {
					*s++ = ' ';
				} else
					*s++ = *cp++;
				*s++ = *cp++;

				*s++ = ' ';
				*s = '\0';
				while (*cp == ' ')
					cp++;
				strncat(s,cp,3);	/* copy month */
				cp += 3;
				while (*cp == ' ')
					cp++;
				/* skip year */
				while (isdigit(*cp))
					cp++;
				/* copy time */
				strncat(s,cp,6); /* space hour : min */
				break;
			case BBSTYPE:
				type = tstring[16];
				break;
			case NOHEADER:
				break;
			}
		}
		if((type == m->stype && m->stype != ' ') || m->stype == ' '
		   || m->stype == 'L')
		     printf("%c%c%c%3d %-27.27s %-12.12s %5ld %.25s\n",
			     (start == m->current ? '>' : ' '),
			     (cmsg->status & BM_DELETE ? 'D' : ' '),
			     (cmsg->status & BM_READ ? 'Y' : 'N'),
			     start, smtp_from, smtp_date,
			     cmsg->size, smtp_subject);
	}
	return 0;
}

/*  save msg on stream - if noheader set don't output the header */
int
msgtofile(m,msg,tfile,noheader)
struct mbx *m;
int msg;
FILE *tfile;   /* already open for write */
int noheader;
{
	char	tstring[LINELEN];
	long 	size;

	if (m->mfile == NULL) {
		printf(Nomail);
		return -1;
	}
	fseek(m->mfile,m->mbox[msg].start,0);
	size = m->mbox[msg].size;

	if (noheader) {
		/* skip header */
		while (size > 0 && fgets(tstring,sizeof(tstring),m->mfile)
		       != NULL) {
			size -= strlen(tstring);
			if (*tstring == '\n')
				break;
		}
	}
	while (size > 0 && fgets(tstring,sizeof(tstring),m->mfile)
	       != NULL) {
		size -= strlen(tstring);
		fputs(tstring,tfile);
		if (ferror(tfile)) {
			printf("Error writing mail file\n");
			return -1;
		}
	}
	return 0;
}

/*  dodelmsg - delete message in current notesfile */
int
dodelmsg(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	int msg,i;
	m = (struct mbx *) p;
	if (m->mfile == NULL) {
		printf(Nomail);
		return 0;
	}
	for(i = 1; i < argc; ++i) {
		msg = atoi(argv[i]);
		if(msg < 0 || msg > m->nmsgs) {
			printf(Badmsg,msg);
			continue;
		}
		/* Check if we have permission to delete others mail */
		if(!(m->privs & FTP_WRITE) && stricmp(m->area,m->name)) {
			printf(Noperm);
			return 0;
		}
		m->mbox[msg].status |= BM_DELETE;
		printf("Msg %d Killed.\n", msg);
		m->change = 1;
	}
	return 0;
}
/* close the temp file while coping mail back to the mailbox */
int
closenotes(m)
struct mbx *m;
{
	register struct	let *cmsg;
	register char *line;
	char tstring[LINELEN], buf[256];
	long size;
	int i, nostatus = 0, nodelete;
	FILE	*nfile;

	if (m->mfile == NULL)
		return 0;

	if(!m->change) {		/* no changes were made */
		mfclose(m);
		m->mboxsize = 0;
		return 0;
	}
	/* If this area is a public message area, then we will not add a
	 * Status line to indicate that the message has been read.
	 */
	nostatus = isarea(m->area);

	/* Don't delete messages from public message areas unless you are
	 * a BBS.
	 */
	if(nostatus)
		nodelete = !(m->privs & SYSOP_CMD);
	else
		nodelete = 0;

	/* See if any messages have been forwarded, otherwise just close
	 * the file and return since there is nothing to write back.
	 */
	if(nostatus && nodelete) {
		for(i=1; i <= m->nmsgs; ++i)
			if(m->mbox[i].status & BM_FORWARDED)
				break;
		if(i > m->nmsgs) {
			mfclose(m);
			m->mboxsize = 0;
			return 0;
		}
	}
	line = tstring;
	scanmail(m);
	if(lockit(m))
		return -1;
	sprintf(buf,"%s/%s.txt",Mailspool,m->area);
	if ((nfile = fopen(buf,WRITE_TEXT)) == NULL) {
		printf(Noaccess,buf);
		mfclose(m);
		m->mboxsize = 0;
		rmlock(Mailspool,m->area);
		return -1;
	}
	/* copy tmp file back to notes file */
	for (cmsg = &m->mbox[1],i = 1; i <= m->nmsgs; i++, cmsg++) {
		fseek(m->mfile,cmsg->start,0);
		size = cmsg->size;
		/* It is not possible to delete messages if nodelete is set */
		if ((cmsg->status & BM_DELETE) && !nodelete)
			continue;
		/* copy the header */
		while (size > 0 && fgets(line,LINELEN,m->mfile) != NULL) {
			size -= strlen(line);
			if (*line == '\n') {
				if (cmsg->status & BM_FORWARDED)
					fprintf(nfile,"%s%s\n",Hdrs[XFORWARD],
						m->name);
				if ((cmsg->status & BM_READ) != 0 && !nostatus)
					fprintf(nfile,"%sR\n",Hdrs[STATUS]);
				fprintf(nfile,"\n");
				break;
			}
			fputs(line,nfile);
			/* kwait(NULL);  can cause problems if exiting NOS */
		}
		while (size > 0 && fgets(line,LINELEN,m->mfile) != NULL) {
			fputs(line,nfile);
			size -= strlen(line);
			/* kwait(NULL);   dont want no damaged files */
			if (ferror(nfile)) {
				printf("Error writing mail file\n");
				(void) fclose(nfile);
				mfclose(m);
				m->mboxsize = 0;
				rmlock(Mailspool,m->area);
				return -1;
			}
		}
	}
	m->nmsgs = 0;
	if (!stricmp(m->name,m->area))
		m->mysize = ftell(nfile); /* Update the size of our mailbox */
	/* remove a zero length file */
	if (ftell(nfile) == 0L)
		(void) unlink(buf);
	(void) fclose(nfile);
	mfclose(m);
	m->mboxsize = 0;
	rmlock(Mailspool,m->area);
	kwait(NULL);
	return 0;
}

/* Returns 1 if name is a public message Area, 0 otherwise */
int
isarea(name)
char *name;
{
	char buf[LINELEN], *cp;
	FILE *fp;
	if((fp = fopen(Arealist,READ_TEXT)) == NULL)
		return 0;
	while(fgets(buf,sizeof(buf),fp) != NULL) {
		/* The first word on each line is all that matters */
		if((cp = strchr(buf,' ')) == NULL)
			if((cp = strchr(buf,'\t')) == NULL)
				continue;
		*cp = '\0';
		if((cp = strchr(buf,'\t')) != NULL)
			*cp = '\0';
		if(stricmp(name,buf) == 0) {	/* found it */
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	return 0;
}

static int
lockit(m)
struct mbx *m;
{
	int c, cnt = 0;

	while(mlock(Mailspool,m->area)) {
		ppause(1000L/MSPTICK);	/* Wait one second */
		if(++cnt == 10) {
			cnt = 0;
			c = tkeywait("Mail file is busy, Abort or Retry ? ",1);
			if (c == 'A' || c == 'a' || c == EOF) {
				mfclose(m);
				return 1;
			}
		}
	}
	return 0;
}

/* read the next message or the current one if new */
int
doreadnext(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	char buf[10], *newargv[2];
	m = (struct mbx *) p;
	if (m->mfile == NULL)
		return 0;
	if ((m->mbox[m->current].status & BM_READ) != 0) {
		if (m->current == 1 && m->anyread == 0)
			;
		else if (m->current < m->nmsgs) {
			m->current++;
		} else {
			printf("Last message\n");
			return 0;
		}
	}
	sprintf(buf,"%d",m->current);
	newargv[0] = "read";
	newargv[1] = buf;
	m->anyread = 1;
	return doreadmsg(2,newargv,p);
}

/*  display message on the crt given msg number */
int
doreadmsg(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	register int c, col, lin;
	char	buf[MAXCOL+2], *cp, *cp2;
	int	msg, cnt, i, usemore, verbose, mbxheader, pathcol;
	int	header, lastheader;
	long 	size;

	m = (struct mbx *) p;
	if (m->mfile == NULL) {
		printf(Nomail);
		return 0;
	}
	if(m->type == TELNET || m->type == TIP)
		usemore = 1;	/* Display More prompt */
	else
		usemore = 0;
	lin = MAXLIN-1;
	for(i = 1; i < argc; ++i) {
		msg = atoi(argv[i]);
		if( msg < 1 || msg > m->nmsgs) {
			printf(Badmsg,msg);
			return 0;
		}
		fseek(m->mfile,m->mbox[msg].start,0);
		size = m->mbox[msg].size;
		m->current = msg;
		header = NOHEADER;
		mbxheader = 0;
		if(*argv[0] == 'v')
			verbose = 1;	/* display all header lines */
		else
			verbose = 0;

		printf("Message #%d %s\n", msg,
			m->mbox[msg].status & BM_DELETE ? "[Deleted]" : "");
		if ((m->mbox[msg].status & BM_READ) == 0) {
			m->mbox[msg].status |= BM_READ;
			m->change = 1;
			m->newmsgs--;
		}
		--lin;
		col = 0;
		while (!feof(m->mfile) && size > 0) {
			for (col = 0;  col < MAXCOL;) {
				c = getc(m->mfile);
				size--;
				if (feof(m->mfile) || size == 0) /* end this line */
					break;
				if (c == '\t') {
					cnt = col + 8 - (col & 7);
					if (cnt >= MAXCOL) /* end this line */
						break;
					while (col < cnt)
						buf[col++] = ' ';
				} else {
					if (c == '\n')
						break;
					buf[col++] = c;
				}
			}
			if(col < MAXCOL)
				buf[col++] = '\n';
			buf[col] = '\0';
			if(mbxheader > 0) {
			     /* Digest R: lines and display as a Path: line */
			     if(strncmp(buf,"R:",2) != 0 ||
				(cp = strchr(buf,'@')) == NULL) {
				  putchar('\n');
				  mbxheader = -1; /* don't get here again */
				  verbose = 1;
			     }
			     else {
				  if(*(++cp) == ':')
				       ++cp;
				  for(cp2 = cp; isalnum(*cp2); ++cp2)  ;
				  *cp2 = '\0';
				  if(mbxheader++ == 1) {
				       fputs("Path: ",stdout);
				       pathcol = 5;
				       --lin;
				  }
				  else {
				       putchar('!');
				       if(++pathcol + strlen(cp) > MAXCOL-3){
					    fputs("\n      ",stdout);
					    pathcol = 5;
					    --lin;
				       }
				  }
				  fputs(cp,stdout);
				  pathcol += strlen(cp);
				  ++lin;	/* to allow for not printing it later */
			     }
			}
			if(col == 1 && !verbose && !mbxheader)
			     /* last header line reached */
			     mbxheader = 1;
			if(verbose)
				fputs(buf,stdout);
			if(!verbose && !mbxheader){
				lastheader = header;
				if(!isspace(*buf))
					header = htype(buf);
				else
					header = lastheader;
				switch(header) {
				case TO:
				case CC:
				case FROM:
				case DATE:
				case SUBJECT:
				case APPARTO:
				case ORGANIZATION:
					fputs(buf,stdout);
					break;
				default:
					++lin;
				}
			}
			col = 0;
			if(usemore && --lin == 0){
				c = tkeywait("--More--",0);
				lin = MAXLIN-1;
				if(c == -1 || c == 'q' || c == 'Q')
					break;
				if(c == '\n' || c == '\r')
					lin = 1;
			}
		}
	}
	return 0;
}

/* Set up m->to when replying to a message. The subject is returned in
 * m->line.
 */
int
mbx_reply(argc,argv,m,cclist,rhdr)
int argc;
char *argv[];
struct mbx *m;
struct list **cclist;	/* Pointer to buffer for pointers to cc recipients */
char **rhdr;		/* Pointer to buffer for extra reply headers */
{
	char subject[MBXLINE], *msgid = NULL, *date = NULL;
	char *cp;
	int msg, lastheader, header = NOHEADER;
	long size;

	/* Free anything that might be allocated
	 * since the last call to mbx_to() or mbx_reply()
	 */
	free(m->to);
	m->to = NULL;
	free(m->tofrom);
	m->tofrom = NULL;
	free(m->tomsgid);
	m->tomsgid = NULL;
	free(m->origto);
	m->origto = NULL;
	subject[0] = '\0';

	if(argc == 1)
	     msg = m->current;
	else
	     msg = atoi(argv[1]);
	if (m->mfile == NULL) {
	     if(m->sid & MBX_SID)
		  fputs("NO - ",stdout);
		puts(Nomail);
		return 0;
	}
	if(msg < 1 || msg > m->nmsgs) {
	     if(m->sid & MBX_SID)
		  fputs("NO - ",stdout);
	     puts(Badmsg);
	     return -1;
	}
	fseek(m->mfile,m->mbox[msg].start,0);
	size = m->mbox[msg].size;
	m->current = msg;
	while(size > 0 && fgets(m->line,MBXLINE-1,m->mfile) != NULL) {
	     size -= strlen(m->line);
	     if(m->line[0] == '\n')	/* end of header */
		  break;
	     rip(m->line);
	     lastheader = header;
	     if(!isspace(m->line[0])) {
		  header = htype(m->line);
		  lastheader = NOHEADER;
	     }
	     switch(header) {
	     case SUBJECT:
		  if(strlen(m->line) > 11 && !strnicmp(&m->line[9],"Re:",3))
		       strcpy(subject,&m->line[9]);
		  else
		       sprintf(subject,"Re: %s",&m->line[9]);
		  break;
	     case FROM:
		  if(m->to == NULL && (cp = getaddress(m->line,0)) !=
		     NULL)
		       m->to = strdup(cp);
		  break;
	     case REPLYTO:
		  if((cp = getaddress(m->line,0)) != NULL) {
		       free(m->to);
		       m->to = strdup(cp);
		  }
		  break;
	     case MSGID:
		  free(msgid);
		  msgid = strdup(&m->line[12]);
		  break;
	     case DATE:
		  free(date);
		  date = strdup(&m->line[6]);
		  break;
	     case TO:
	     case CC:
	     case APPARTO:
		  /* Get addresses on To, Cc and Apparently-To lines */
		  cp = m->line;
		  m->line[strlen(cp)+1] = '\0';	/* add extra null at end */
		  for(;;) {
		       if((cp = getaddress(cp,lastheader == header ||
					   cp != m->line)) == NULL)
			    break;
		       addlist(cclist,cp,0);
		       /* skip to next address, if any */
		       cp += strlen(cp) + 1;
		  }
		  break;
	     }
	}
	if(msgid != NULL || date != NULL) {
	     *rhdr = mallocw(LINELEN);
	     sprintf(*rhdr,"In-Reply-To: your message ");
	     if(date != NULL) {
		  sprintf(m->line,"of %s.\n",date);
		  strcat(*rhdr,m->line);
		  if(msgid != NULL)
		       strcat(*rhdr,"             ");
	     }
	     if(msgid != NULL) {
		  sprintf(m->line,"%s\n",msgid);
		  strcat(*rhdr,m->line);
	     }
	     free(msgid);
	     free(date);
	}
	strcpy(m->line,subject);
	return 0;
}

void
scanmail(m)		 /* Get any new mail */
struct mbx *m;
{
	FILE *nfile;
	int ret, cnt;
	char buf[256];
	long diff;

	if ((diff = isnewmail(m)) == 0L)
		return;
	if(lockit(m))
		return;
	if(m->mfile == NULL || diff < 0L) {
		/* This is the first time scanmail is called, or the
		 * mail file size has decreased. In the latter case,
		 * any changes we did to this area will be lost, but this
		 * is not fatal.
		 */
		initnotes(m);
		rmlock(Mailspool,m->area);
		return;
	}
	sprintf(buf,"%s/%s.txt",Mailspool,m->area);
	if ((nfile = fopen(buf,READ_TEXT)) == NULL)
		printf(Noaccess,buf);
	else {
		/* rewind tempfile */
		fseek(m->mfile,0L,0);
		cnt = m->nmsgs;
		/* Reread all messages since size they may have changed
		 * in size after a X-Forwarded-To line was added.
		 */
		m->nmsgs = 0;
		ret = readnotes(m,nfile,1);   /* get the mail */
		m->newmsgs += m->nmsgs - cnt;
		m->mboxsize = ftell(nfile);
		if(!stricmp(m->name,m->area))
			m->mysize = m->mboxsize;
		(void) fclose(nfile);
		if (ret != 0)
			printf("Error updating mail file\n");
	}
	rmlock(Mailspool,m->area);
}

/* Check the current mailbox to see if new mail has arrived.
 * Returns the difference in size.
 */
static long
isnewmail(m)
struct mbx *m;
{
	char buf[256];
	sprintf(buf,"%s/%s.txt",Mailspool,m->area);
	return fsize(buf) - m->mboxsize;
}

/* Check if the private mail area has changed */
long
isnewprivmail(m)
struct mbx *m;
{
	long cnt;
	char buf[256];
	sprintf(buf,"%s/%s.txt",Mailspool,m->name);
	cnt = m->mysize;
	m->mysize = fsize(buf);
	return m->mysize - cnt; /* != 0 not more than once */
}



/* This function returns the length of a file. The proper thing would be
 * to use stat(), but it fails when using DesqView together with Turbo-C
 * code.
 */
static long
fsize(name)
char *name;
{
	long cnt;
	FILE *fp;
	if((fp = fopen(name,READ_TEXT)) == NULL)
		return -1L;
	fseek(fp,0L,2);
	cnt = ftell(fp);
	fclose(fp);
	return cnt;
}

/* close the temporary mail file */
static void
mfclose(m)
struct mbx *m;
{
	if(m->mfile != NULL)
		fclose(m->mfile);
	m->mfile = NULL;
#ifdef SETVBUF
	free(m->stdoutbuf);
	m->stdoutbuf = NULL;
#endif
}


/* Print prompt and read one character, telnet version */
static int
tkeywait(prompt,flush)
char *prompt;	/* Optional prompt */
int flush;	/* Flush queued input? */
{
	int c, i, oldimode,oldomode;

	if(flush && socklen(fileno(stdin),0) != 0)
		recv_mbuf(fileno(stdin),NULL,0,NULL,0); /* flush */
	if(prompt == NULL)
		prompt = "Hit enter to continue"; 
	printf("%s%c%c%c",prompt,IAC,WILL,TN_ECHO);
	fflush(stdout);

	/* discard the response */

	oldimode = fmode(stdin,STREAM_BINARY);
	oldomode = fmode(stdout,STREAM_BINARY);

	while((c = getchar()) == IAC){
		c = getchar();
		if(c > 250 && c < 255)
			getchar();
	}

	fmode(stdout,oldomode);
	fmode(stdin,oldimode);

	/* Get rid of the prompt */
	for(i=strlen(prompt);i != 0;i--)
		putchar('\b');
	for(i=strlen(prompt);i != 0;i--)
		putchar(' ');
	for(i=strlen(prompt);i != 0;i--)
		putchar('\b');
	printf("%c%c%c",IAC,WONT,TN_ECHO);
	fflush(stdout);
	return c;
}
