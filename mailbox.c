/* There are only two functions in this mailbox code that depend on the
 * underlying protocol, namely mbx_getname() and dochat(). All the other
 * functions can hopefully be used without modification on other stream
 * oriented protocols than AX.25 or NET/ROM.
 *
 * SM0RGV 890506, most work done previously by W9NK
 *
 *** Changed 900114 by KA9Q to use newline mapping features in stream socket
 *	interface code; everything here uses C eol convention (\n)
 *
 *	Numerous new commands and other changes by SM0RGV, 900120
 */
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#ifdef	UNIX
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "global.h"
#include "config.h"
#include "timer.h"
#include "proc.h"
#include "socket.h"
#include "usock.h"
#include "session.h"
#include "smtp.h"
#include "dirutil.h"
#include "telnet.h"
#include "ftp.h"
#include "ftpserv.h"
#include "commands.h"
#include "netuser.h"
#include "files.h"
#include "bm.h"
#include "mailbox.h"
#include "ax25mail.h"
#include "nr4mail.h"
#include "cmdparse.h"

/*
#define MBDEBUG
*/

struct mbx *Mbox[NUMMBX];
static char *Motd = NULL;
static int Attended = TRUE;	/* default to attended mode */
unsigned Maxlet = BM_NLET;

char Noperm[] = "Permission denied.\n";
char Nosock[] = "Can't create socket\n";

static char Mbbanner[] = "[NET-H$]\nWelcome %s to the %s TCP/IP Mailbox (%s)\n%s";
static char Mbmenu[] = "Current msg# %d : A,B,C,D,E,F,G,H,I,J,K,L,N,R,S,T,U,V,W,Z,? >\n";
static char Longmenu1[] = "(?)help    (A)rea     (B)ye      (C)hat     (D)ownload (E)scape   (F)inger\n";
static char Longmenu2[] = "(G)ateway  (H)elp     (I)nfo     (J)heard   (K)ill     (L)ist     (N)etrom\n";
static char Longmenu3[] = "(R)ead     (S)end     (T)elnet   (U)pload   (V)erbose  (W)hat     (Z)ap\n";
static char Loginbanner[] = "\nKA9Q NOS (%s)\n\n";
static char Howtoend[] = "Terminate with /EX or ^Z in first column (^A aborts):\n";

static int doarea(int argc,char *argv[],void *p);
static int mbx_getname(struct mbx *m);

/************************************************************************/
/*			C O M M A N D S					*/
/************************************************************************/

static int doattend(int argc,char *argv[],void *p);
static int domaxmsg(int argc,char *argv[],void *p);
static int domotd(int argc,char *argv[],void *p);
static int dotimeout(int argc,char *argv[],void *p);

/* mbox subcommand table */
static struct cmds Mbtab[] = {
	"attend",	doattend,	0, 0, NULL,
#ifdef	AX25
	"kick",		dombkick,	0, 0, NULL,
#endif
	"maxmsg",	domaxmsg,	0, 0, NULL,
	"motd",		domotd,		0, 0, NULL,
	"status",	domboxdisplay,	0, 0, NULL,
#ifdef	AX25
	"timer",	dombtimer,	0, 0, NULL,
#endif
	"tiptimeout",	dotimeout,	0, 0, NULL,
	NULL,
};


int
dombox(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc == 1)
		return domboxdisplay(argc,argv,p);
	return subcmd(Mbtab,argc,argv,p);
}

/* if unattended mode is set, ax25, telnet and maybe other sessions will
 * be restricted.
 */
static int
doattend(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Attended,"Attended flag",argc,argv);
}

static int
domaxmsg(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Maxlet,"Maximum messages per area",argc,argv);
}

static int
domotd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc > 2) {
		printf("Usage: mbox motd \"<your message>\"\n");
		return 0;
	}

	if(argc < 2) {
		if(Motd != NULL)
			puts(Motd);
	}
	else {
		if(Motd != NULL){
			free(Motd);
			Motd = NULL;	/* reset the pointer */
		}
		if(!strlen(argv[1]))
			return 0;		/* clearing the buffer */
		Motd = mallocw(strlen(argv[1])+5);/* allow for the EOL char */
		strcpy(Motd, argv[1]);
		strcat(Motd, "\n");		/* add the EOL char */
	}
	return 0;
}

int
domboxdisplay(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int i, j, len;
	struct mbx *m;
	struct sockaddr fsocket;
	static char *states[] = {"LOGIN","CMD","SUBJ","DATA","REVFWD",
				"TRYING","FORWARD"};

	printf("User       State    S#  Where\n");

	for (i = 0; i < NUMMBX; i++){
		if((m = Mbox[i]) != NULL){
			len = MAXSOCKSIZE;
			j = getpeername(fileno(m->user),&fsocket,&len);
			printf("%-11s%-9s%-4u%s\n",m->name,
			 states[m->state],fileno(m->user),
			 j != -1 ? psocket(&fsocket): "");
		}
	}
	return 0;
}

static int
dotimeout(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setuns(&Tiptimeout,"Tip connection timeout",argc,argv);
}


/**********************************************************************/

void
listusers(network)
FILE *network;
{
	FILE *outsave;

	fprintf(network,"\nCurrent remote users:\n");
	outsave = stdout;
	stdout = network;
	domboxdisplay(0,NULL,NULL);
	stdout = outsave;
}

struct mbx *
newmbx()
{
	int i;
	struct mbx *m;

	for(i = 0; i < NUMMBX; i++){
		if(Mbox[i] == NULL){
			m = Mbox[i] = (struct mbx *)callocw(1,sizeof(struct mbx));
			m->mbnum = i;
			return m;
		}
	}
	/* If we get here, there are no free mailbox sessions */
	return NULL;
}

static int
mbx_getname(m)
struct mbx *m;
{
#ifdef	AX25
	char *cp;
#endif
	union sp sp;
	struct sockaddr tmp;
	char buf[MBXLINE];
	int len = MAXSOCKSIZE;
	int anony = 0;
	int oldmode;

	sp.sa = &tmp;
	sp.sa->sa_family = AF_LOCAL;	/* default to AF_LOCAL */
	getpeername(fileno(m->user),&tmp,&len);
	m->path = mallocw(MBXLINE);
	/* This is one of the two parts of the mbox code that depends on the
	 * underlying protocol. We have to figure out the name of the
	 * calling station. This is only practical when AX.25 or NET/ROM is
	 * used. Telnet users have to identify themselves by a login procedure.
	 */
	switch(sp.sa->sa_family){
#ifdef	AX25
	case AF_NETROM:
	case AF_AX25:
		/* NETROM and AX25 socket address structures are "compatible" */
		pax25(m->name,sp.ax->ax25_addr);
		cp = strchr(m->name,'-');
		if(cp != NULL)			/* get rid of SSID */
			*cp = '\0';
		/* SMTP wants the name to be in lower case */
		cp = m->name;
		while(*cp){
			if(isupper(*cp))
				*cp = tolower(*cp);
			++cp;
		}
		anony = 1;
		/* Try to find the privileges of this user from the userfile */
		if((m->privs = userlogin(m->name,buf,&m->path,MBXLINE,&anony)) == -1)
			if((m->privs = userlogin("bbs",buf,&m->path,MBXLINE,&anony)) == -1)
				if((m->privs = userlogin("anonymous",buf,&m->path,MBXLINE,
					 &anony)) == -1){
						m->privs = 0;
						free(m->path);
						m->path = NULL;
				}
		if(m->privs & EXCLUDED_CMD)
			return -1;
		return 0;
#endif
	case AF_LOCAL:
	case AF_INET:
		m->state = MBX_LOGIN;
		printf(Loginbanner,Hostname);
		for(;;){
			fputs("login: ",stdout);
			if(mbxrecvline(m->user,m->name,sizeof(m->name),-1) == EOF)
				return -1;
			if(*m->name == '\0')
				continue;
			printf("Password: %c%c%c",IAC,WILL,TN_ECHO);
			oldmode = fmode(m->user,STREAM_BINARY);
			if(mbxrecvline(m->user,buf,MBXLINE,-1) == EOF)
				return -1;
			printf("%c%c%c",IAC,WONT,TN_ECHO);
			fmode(m->user,oldmode);
			putchar('\n');
#ifdef	notdef
			/* This is needed if the password was send before the
			 * telnet no-echo options were received. We neeed to
			 * flush the eold sequence from the input buffers, sigh
			 */
			if(socklen(fileno(m->user),0))/* discard any remaining input */
				recv_mbuf(fileno(m->user),NULL,0,NULL,0);
#endif
			if((m->privs = userlogin(m->name,buf,&m->path,MBXLINE,&anony))
			 != -1){
				if(anony)
					logmsg(fileno(m->user),"MBOX login: %s Password: %s",m->name,buf);
				else
					logmsg(fileno(m->user),"MBOX login: %s",m->name);
				if(m->privs & EXCLUDED_CMD)
					return -1;
				return 0;
			}
			printf("Login incorrect\n");
			*m->name = '\0';	/* wipe any garbage */
		}
	}
	return 0;
}

/* Incoming mailbox session */
void
mbx_incom(s,t,p)
int s;
void *t;
void *p;
{
	struct mbx *m;
	struct usock *up;
	char *buf[3];
	int rval;
	FILE *network;

	sockowner(s,Curproc);	/* We own it now */
	if(p == NULL)
		network = fdopen(s,"r+t");
	else
		network = (FILE *)p;
	
	/* Secede from the parent's sockets, and use the network socket that
	 * was passed to us for both input and output. The reference
	 * count on this socket will still be 1; this allows the domboxbye()
	 * command to work by closing that socket with a single call.
	 * If we return, the socket will be closed automatically.
	 */
	fclose(stdin);
	stdin = fdup(network);
	fclose(stdout);
	stdout = fdup(network);

	logmsg(fileno(network),"open MBOX");
	if((m = newmbx()) == NULL){
		printf("Too many mailbox sessions\n");
		return;
	}
	m->user = network;
	m->escape = 24;		/* default escape character is Ctrl-X */
	m->type = (int) t;
	/* get the name of the remote station */
	if(mbx_getname(m) == -1) {
		exitbbs(m);
		return;
	}

	m->state = MBX_CMD;	/* start in command state */

	/* Now say hi */
	printf(Mbbanner,m->name,Hostname,Version,
		Motd != NULL ? Motd : "");
	/* Enable our local message area */
	buf[1] = m->name;
	doarea(2,buf,m);
	printf(Mbmenu,m->current);
	while(mbxrecvline(network,m->line,MBXLINE,-1) != EOF){
		if((rval = mbx_parse(m)) == -2)
			break;
		if(rval == 1)
			printf("Bad syntax.\n");
		if(!(m->sid & MBX_SID) && isnewprivmail(m) > 0L)
			printf("You have new mail.\n");
		scanmail(m);
		printf((m->sid & MBX_SID) ? ">\n" : Mbmenu, m->current);
		m->state = MBX_CMD;
	}
	exitbbs(m);
	/* nasty hack! we may have screwed up reference count */
	/* by invoking newproc("smtp_send",....); Fudge it!   */
	if((up = itop(fileno(stdout))) != NULL)
		up->refcnt = 1;
	fclose(stdout);
}

void
exitbbs(m)
struct mbx *m;
{
	closenotes(m);
	free(m->to);
	free(m->tofrom);
	free(m->origto);
	free(m->tomsgid);
	free(m->path);
	free(m->mbox);
	Mbox[m->mbnum] = NULL;
	free(m);
}

/**********************************************************************/

static int dochat(int argc,char *argv[],void *p);
static int dodownload(int argc,char *argv[],void *p);
static int dombupload(int argc,char *argv[],void *p);
static int dowhat(int argc,char *argv[],void *p);
static int dozap(int argc,char *argv[],void *p);
static int dosend(int argc,char *argv[],void *p);
static int dosid(int argc,char *argv[],void *p);
static int dosysop(int argc,char *argv[],void *p);
static int dostars(int argc,char *argv[],void *p);
static int dombhelp(int argc,char *argv[],void *p);
static int dombtelnet(int argc,char *argv[],void *p);
static int dombfinger(int argc,char *argv[],void *p);
static void gw_alarm(void *p);
static void gw_input(int s,void *notused,void *p);
static void gw_superv(int null,void *proc,void *p);
static int mbx_to(int argc,char *argv[],void *p);
static int mbx_data(struct mbx *m,struct list *cclist,char *extra);
static int msgidcheck(char *string);
static int uuencode(FILE *infile,FILE *outfile,char *infilename);

static struct cmds Mbcmds[] = {
	"",		doreadnext,	0, 0, NULL,
	"area",		doarea,		0, 0, NULL,
	"send",		dosend,		0, 0, NULL,
	"read",		doreadmsg,	0, 2, "R numbers",
	"verbose",	doreadmsg,	0, 2, "V numbers",
#ifdef	AX25
	"jheard",	doaxheard,	0, 0, NULL,
#endif
	"kill",		dodelmsg,	0, 2, "K numbers",
	"list",		dolistnotes,	0, 0, NULL,
	"escape",	dombescape,	0, 0, NULL,
	"download",	dodownload,	0, 2, "D[U] filename",
	"upload",	dombupload,	0, 2, "U filename",
	"what",		dowhat,		0, 0, NULL,
	"zap",		dozap,		0, 2, "Z filename",
#ifdef AX25
	"gateway",	dogateway,	0, 3, "G interface callsigns",
#endif
	"telnet",	dombtelnet,	0, 2, "T hostname",
	"finger",	dombfinger,	0, 0, NULL,
#ifdef	NETROM
	"netrom",	dombnetrom,	0, 0, NULL,
#endif
	"chat",		dochat,		0, 0, NULL,
	"bye",		domboxbye,	0, 0, NULL,
	"help",		dombhelp,	0, 0, NULL,
	"info",		dombhelp,	0, 0, NULL,
	"?",		dombhelp,	0, 0, NULL,
	"[",		dosid,		0, 0, NULL,
#ifdef	AX25
	"f>",		dorevfwd,	0, 0, NULL,
#endif
	"@",		dosysop,	0, 0, NULL,
	"***",		dostars,	0, 0, NULL,
	NULL,	NULL,		0, 0, "Huh?",
};

/* "twocmds" defines the MBL/RLI two-letter commands, eg. "SB", "SP" and so on.
 * They have to be treated specially since cmdparse() wants a space between
 * the actual command and its arguments.
 * "SP FOO" is converted to "s  foo" and the second command letter is saved
 * in m->stype. Longer commands like "SEND" are unaffected, except for
 * commands starting with "[", i.e. the SID, since we don't know what it will
 * look like.
 */
static char twocmds[] = "slrd[";	/* S,L,R,D are two-letter commands */
int
mbx_parse(m)
struct mbx *m;
{
	char *cp;
	int i;
	char *newargv[2];
	/* Translate entire buffer to lower case */
	for (cp = m->line; *cp != '\0'; ++cp)
		if(isupper(*cp))
			*cp = tolower(*cp);
	/* Skip any spaces at the begining */
	for(cp = m->line;isspace(*cp);++cp)
		;
	m->stype = ' ';
	if(*cp != '\0' && *(cp+1) != '\0')
	for(i=0; i<strlen(twocmds); ++i){
		if(*cp == twocmds[i] && (isspace(*(cp+2)) || *(cp+2) == '\0'
		 || *cp == '[')){
			if(islower(*(++cp)))
				m->stype = toupper(*cp); /* Save the second character */
			else
				m->stype = *cp;
			*cp = ' ';
			break;
		}
	}
	/* See if the input line consists solely of digits */
	cp = m->line;
	for(cp = m->line;isspace(*cp);++cp)
		;
	newargv[1] = cp;
	for(;*cp != '\0' && isdigit(*cp);++cp)
		;
	if(*cp == '\0' && strlen(newargv[1]) > 0) {
		newargv[0] = "read";
		return doreadmsg(2,newargv,(void *)m);
	}
	else
		return cmdparse(Mbcmds,m->line,(void *)m);
}

/* This works like recvline(), but telnet options are answered and the
 * terminating newline character is not put into the buffer. If the
 * incoming character equals the value of escape, any queued input is
 * flushed and -2 returned.
 */
int
mbxrecvline(network,buf,len,escape)
FILE *network;
char *buf;
int len;
int escape;
{
	int c, cnt = 0, opt;
	if(buf == NULL)
		return 0;
	fflush(stdout);
	while((c = getc(network)) != EOF){
		if(c == IAC){		/* Telnet command escape */
			if((c = getc(network)) == EOF)
				break;
			if(c > 250 && c < 255 && (opt = getc(network)) != EOF){
#ifdef	foo
				switch(c){
				case WILL:
					printf("%c%c%c",IAC,DONT,opt);
					break;
				case WONT:
					printf("%c%c%c",IAC,DONT,opt);
					break;
				case DO:
					printf("%c%c%c",IAC,WONT,opt);
					break;
				case DONT:
					printf("%c%c%c",IAC,WONT,opt);
				}
#endif
/* to be fixed 			fflush(stdout);*/
				continue;
			}
			if(c != IAC && (c = fgetc(network)) == EOF)
				break;
		}
		/* ordinary character */
		if(c == '\r' || c == '\n')
			break;
		if(c == escape){
			if(socklen(fileno(network),0)) /* discard any remaining input */
				recv_mbuf(fileno(network),NULL,0,NULL,0);
			cnt = -2;
			break;
		}
		*buf++ = c;
		++cnt;
		if(cnt == len - 1)
			break;
	}
	if(c == EOF && cnt == 0)
		return -1;
	*buf = '\0';
	return cnt;
}

int
domboxbye(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;

	m = (struct mbx *)p;
	/* Now say goodbye */
	printf("Thank you %s, for calling the %s Tcp/Ip Mailbox.\n",m->name,
		Hostname);
	if(m->type == TIP)
		printf("Please hang up now.\n");

	return -2;	/* signal that exitbbs() should be called */
}
static int
dombhelp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char buf[255];
	int i;
	FILE *fp;
	if(*argv[0] == '?') {
		fputs(Longmenu1,stdout);
		fputs(Longmenu2,stdout);
		fputs(Longmenu3,stdout);
		return 0;
	}
	buf[0] = '\0';
	if(argc > 1)
		for(i=0; Mbcmds[i].name != NULL; ++i)
			if(!strncmp(Mbcmds[i].name,argv[1],strlen(argv[1]))) {
				sprintf(buf,"%s/%s.hlp",Helpdir,Mbcmds[i].name);
				break;
			}
	if(buf[0] == '\0')
		if(*argv[0] == 'i')			/* INFO command */
			sprintf(buf,"%s/info.hlp",Helpdir);
		else
			sprintf(buf,"%s/help.hlp",Helpdir);
	if((fp = fopen(buf,READ_TEXT)) != NULL) {
		sendfile(fp,Curproc->output,ASCII_TYPE,0);
		fclose(fp);
	}
	else
		printf("No help available. (%s not found)\n",buf);
	return 0;
}

static int
dochat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char buf[8], *newargv[3];

	if(Attended){
		newargv[0] = "telnet";
		newargv[1] = Hostname;
		sprintf(buf,"%d",IPPORT_TTYLINK);
		newargv[2] = buf;
		return dombtelnet(3,newargv,p);
	}
	else {
		printf("Sorry - the system is unattended.\007\n");
	}
	/* It returns only after a disconnect or refusal */
	return 0;
}

static int
dosend(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int cccnt = 0, fail = 0;
	char *host, *cp, fullfrom[MBXLINE], sigwork[LINELEN], *rhdr = NULL;
	struct list *ap, *cclist = NULL;
	struct mbx *m;
	FILE *fp;

	m = (struct mbx *)p;
	if((m->stype != 'R' || (m->sid & MBX_SID)) && mbx_to(argc,argv,m)
	   == -1){
		if(m->sid & MBX_SID)
			printf("NO - syntax error\n");
		else {
			printf("S command syntax error - format is:\n");
			printf("  S[F] name [@ host] [< from_addr] [$bulletin_id]\n");
			printf("  SR [number]\n");
		}
		return 0;
	}
	if(m->stype != 'R' && msgidcheck(m->tomsgid)) {
		if(m->sid & MBX_SID)
			fputs("NO - ",stdout);
		printf("Already have %s\n",m->tomsgid);
		return 0;
	}
	if(m->stype == 'R' && !(m->sid & MBX_SID) &&
	   mbx_reply(argc,argv,m,&cclist,&rhdr) == -1)
		return 0;
	if((cp = rewrite_address(m->to)) != NULL)
	     if(strcmp(m->to,cp) != 0){
		  m->origto = m->to;
		  m->to = cp;
	     }
	     else
		  free(cp);
	if((m->origto != NULL || m->stype == 'R') && !(m->sid & MBX_SID))
		printf("To: %s\n", m->to);
	if(validate_address(m->to) == 0){
		if(m->sid & MBX_SID)
			printf("NO - bad address\n");
		else
			printf("Bad user or host name\n");
		free(rhdr);
		del_list(cclist);
		/* We don't free any more buffers here. They are freed upon
		 * the next call to mbx_to() or to domboxbye()
		 */
		return 0;
	}
	/* Display the Cc: line (during SR command) */
	for(ap = cclist; ap != NULL; ap = ap->next) {
		if(cccnt == 0){
			printf("%s",Hdrs[CC]);
			cccnt = 4;
		}
		else {
			fputs(", ",stdout);
			cccnt += 2;
		}
		if(cccnt + strlen(ap->val) > 80 - 3) {
			fputs("\n    ",stdout);
			cccnt = 4;
		}
		fputs(ap->val,stdout);
		cccnt += strlen(ap->val);
	}
	if(cccnt)
		putchar('\n');
	m->state = MBX_SUBJ;
	if(m->stype != 'R' || (m->sid & MBX_SID) != 0) {
		printf((m->sid & MBX_SID) ? "OK\n" : "Subject: ");
		if(mbxrecvline(m->user,m->line,MBXLINE,-1) == -1)
			return 0;
	}
	else				/* Replying to a message */
		printf("Subject: %s\n",m->line);
	if(mbx_data(m,cclist,rhdr) == -1){
		free(rhdr);
		del_list(cclist);
		puts("Can't create temp file for mail");
		return 0;
	}
	free(rhdr);
	m->state = MBX_DATA;
	if((m->sid & MBX_SID) == 0 && m->stype != 'F')
		printf("Enter message.  %s",Howtoend);

	if(m->stype != 'F' || (m->sid & MBX_SID) != 0)
		while(mbxrecvline(m->user,m->line,MBXLINE,-1) != -1){
			if(m->line[0] == 0x01){  /* CTRL-A */
				fclose(m->tfile);
				puts("Aborted.");
				del_list(cclist);
				return 0;
			}
			if(m->line[0] != CTLZ && stricmp(m->line, "/ex"))
				fprintf(m->tfile,"%s\n",m->line);
			else
				break;	/* all done */
		}
	else {
		fprintf(m->tfile,"----- Forwarded message -----\n\n");
		msgtofile(m,m->current,m->tfile,0);
		fprintf(m->tfile,"----- End of forwarded message -----\n");
	}

	/* Insert customised signature if one is found */
	if(!(m->sid & MBX_SID)) {	/* not a forwarding BBS */
	     sprintf(sigwork,"%s/%s.sig",Signature,
		     m->tofrom ? m->tofrom : m->name);
	     if((fp = fopen(sigwork,READ_TEXT)) != NULL){
		  while(fgets(sigwork,LINELEN,fp) != NULL)
			fputs(sigwork,m->tfile);
		  fclose(fp);
	     }
	}

	if((host = strrchr(m->to,'@')) == NULL) {
		host = Hostname;	/* use our hostname */
		if(m->origto != NULL) {
			/* rewrite_address() will be called again by our
			 * SMTP server, so revert to the original address.
			 */
		 	free(m->to);
			m->to = m->origto;
			m->origto = NULL;
		}
	}
	else
		host++;	/* use the host part of address */

	/* make up full from name for work file */
	if(m->tofrom != NULL)
		sprintf(fullfrom,"%s%%%s.bbs@%s",m->tofrom, m->name, Hostname);
	else
		sprintf(fullfrom,"%s@%s",m->name,Hostname);
	if(cclist != NULL && stricmp(host,Hostname) != 0) {
		fseek(m->tfile,0L,0);	/* reset to beginning */
		fail = queuejob(m->tfile,Hostname,cclist,fullfrom);
		del_list(cclist);
		cclist = NULL;
	}
	addlist(&cclist,m->to,0);
	fseek(m->tfile,0L,0);
	fail += queuejob(m->tfile,host,cclist,fullfrom);
	del_list(cclist);
	fclose(m->tfile);
	if(fail)
	     puts("Couldn't queue message for delivery");
	else
	     if(m->tomsgid != NULL &&
		(fp = fopen(Historyfile,APPEND_TEXT)) != NULL) {
		  fprintf(fp,"%s\n",m->tomsgid); /* Save BID in history file */
		  fclose(fp);
	     }
	smtptick(0L);		/* wake SMTP to send that mail */
	return 0;
}

static int
dosid(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	char *cp;

	m = (struct mbx *)p;
	if(argc == 1)
		return 1;
	if(argv[1][strlen(argv[1]) - 1] != ']') /* must be an SID */
		return 1;
	m->sid = MBX_SID;
	/* Now check to see if this is an RLI board.
	 * As usual, Hank does it a bit differently from
	 * the rest of the world.
	 */
	if(m->stype == 'R' && strncmp(argv[1],"li",2) == 0)/* [RLI] at a minimum */
		m->sid |= MBX_RLI_SID;
	/* Check to see if the BBS supports a kludge called "hierarchical
	 * routing designators."
	 *
	 * No need to check for ']' -- it must be there or this is not
	 * a valid mbox id -- it is checked earlier (fix de OH3LKU)
	 */
	if((cp = strchr(argv[1],'-')) != NULL
	 && (cp=strchr(cp+1,'h')) != NULL
	 && strchr(cp+1,'$'))
		m->sid |= MBX_HIER_SID;	
	return 0;
}

int
dombescape(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;

	m = (struct mbx *)p;
	if(argc < 2){
		printf("The escape character is: ");
		if(m->escape < 32)
			printf("CTRL-%c\n",m->escape+'A'-1);
		else
			printf("'%c'\n",m->escape);
		return 0;
	}
	if(strlen(argv[1]) > 1)
		if(isdigit(*argv[1]))
			m->escape = (char) atoi(argv[1]);
		else
			return 1;
	else
		m->escape = *argv[1];
	return 0;
}

static int
dodownload(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	FILE *fp;
	char *file;

	m = (struct mbx *)p;
	file = pathname(m->path,argv[1]);
	if(!permcheck(m->path,m->privs,RETR_CMD,file)){
		printf(Noperm);
		return 0;
	}
	if((fp = fopen(file,READ_TEXT)) == NULL)
		printf("Can't open \"%s\": %s\n",file,sys_errlist[errno]);
	else
		if(m->stype == 'U'){			/* uuencode ? */
			fclose(fp);
			fp = fopen(file,READ_BINARY);	/* assume non-ascii */
			uuencode(fp,m->user,file);
		} else
			sendfile(fp,m->user,ASCII_TYPE,0);
	free(file);
	fclose(fp);
	return 0;
}

static int
dombupload(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	FILE *fp;
	char *file, buf[LINELEN];

	m = (struct mbx *)p;
	file = pathname(m->path,argv[1]);
	if(!permcheck(m->path,m->privs,STOR_CMD,file)){
		printf(Noperm);
		return 0;
	}
	if((fp = fopen(file,WRITE_TEXT)) == NULL){
		printf("Can't create \"%s\": %s\n",file,sys_errlist[errno]);
		free(file);
		return 0;
	}
	logmsg(fileno(m->user),"MBOX upload: %s",file);
	printf("Send file,  %s",Howtoend);
	for(;;){
		if(mbxrecvline(m->user,buf,LINELEN,-1) == -1){
			unlink(file);
			break;
		}
		if(buf[0] == 0x01){  /* CTRL-A */
			unlink(file);
			printf("Aborted.\n");
			break;
		}
		if(buf[0] == CTLZ || !stricmp("/ex",buf))
			break;
		fputs(buf,fp);
#if !defined(UNIX) && !defined(__TURBOC__) && !defined(AMIGA)
		/* Needed only if the OS uses a CR/LF
		 * convention and putc doesn't do
		 * an automatic translation
		 */
		if(putc('\r',fp) == EOF)
			break;
#endif
		if(putc('\n',fp) == EOF)
			break;
	}
	free(file);
	fclose(fp);
	return 0;
}

static int
dowhat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	FILE *fp;
	char *file;

	m = (struct mbx *)p;
	if(argc < 2)
		file = strdup(m->path);
	else
		file = pathname(m->path,argv[1]);
	if(!permcheck(m->path,m->privs,RETR_CMD,file)){
		printf(Noperm);
		return 0;
	}
	if((fp = dir(file,1)) == NULL)
		printf("Can't read directory: \"%s\": %s\n",file,sys_errlist[errno]);
	else
		sendfile(fp,m->user,ASCII_TYPE,0);
	free(file);
	fclose(fp);
	return 0;
}

static int
dozap(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	char *file;

	m = (struct mbx *)p;
	file = pathname(m->path,argv[1]);
	if(!permcheck(m->path,m->privs,DELE_CMD,file)){
		printf(Noperm);
		return 0;
	}
	if(unlink(file))
		printf("Zap failed: %s\n",sys_errlist[errno]);
	logmsg(fileno(m->user),"MBOX Zap: %s",file);
	free(file);
	return 0;
}

static int
dosysop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	int c;
	extern struct cmds Cmds[];

	m = (struct mbx *) p;
	if(!(m->privs & SYSOP_CMD)){
		printf(Noperm);
		return 0;
	}
	dombescape(1,NULL,p);
	for(;;){
		printf("Net> ");
		fflush(stdout);
		c = mbxrecvline(stdin,m->line,MBXLINE,m->escape);
		if(c == EOF || c == -2)
			break;
		logmsg(fileno(m->user),"MBOX sysop: %s",m->line);
		cmdparse(Cmds,m->line,NULL);
	}
	return 0;
}

/* Handle the "*** Done" command when reverse forwarding ends or the
 * "*** LINKED to" command.
 */
static int
dostars(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	int anony = 1;
	m = (struct mbx *)p;
	/* The "*** LINKED to" command is only allowed to stations with
	 * SYSOP privileges to prevent others from obtaining the same.
	 */
	if((m->privs & SYSOP_CMD) && argc == 4 && !strcmp(argv[1],"linked")) {
		strcpy(m->name,argv[3]);
		/* Try to find the privileges of this user from the userfile */
		if((m->privs = userlogin(m->name,NULL,&m->path,MBXLINE,
					 &anony)) == -1)
		     if((m->privs = userlogin("bbs",NULL,&m->path,
				      MBXLINE,&anony)) == -1)
			  if((m->privs = userlogin("anonymous",NULL,
					   &m->path,MBXLINE,&anony)) == -1){
						m->privs = 0;
						free(m->path);
						m->path = NULL;
			  }
		printf("Oh, hello %s.\n",m->name);
		if(m->privs & EXCLUDED_CMD)
			return domboxbye(0,NULL,p);
		changearea(m,m->name);
		return 0;
	}
	if(argc > 1 && (m->sid & MBX_SID))	/* "*** Done" or similar */
		return 2;
	return -1;
}

static int
doarea(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	FILE *fp;

	m = (struct mbx *) p;
	if(argc < 2){
		printf("Current message area is: %s\n",m->area);
		printf("Available areas are:\n%-15s  Your private mail area\n",
		  m->name);
		if((fp = fopen(Arealist,READ_TEXT)) == NULL)
			return 0;
		sendfile(fp,m->user,ASCII_TYPE,0);
		fclose(fp);
		return 0;
	}
	if((m->privs & SYSOP_CMD) || strcmp(m->name,argv[1]) == 0){
		changearea(m,argv[1]);
		if(m->nmsgs){
			if(!strcmp(m->name,m->area))
				printf("You have ");
			else
				printf("%s: ",m->area);
			printf("%d message%s -  %d new.\n", m->nmsgs,
			  m->nmsgs == 1 ? " " : "s ", m->newmsgs);
		}
		return 0;
	}
	if(isarea(argv[1])) {
		changearea(m,argv[1]);
		printf("%s: %d message%s.\n", m->area, m->nmsgs,
		  m->nmsgs == 1 ? "" : "s");
	}
	else
		printf("No such message area: %s\n",argv[1]);
	return 0;
}

/* subroutine to do the actual switch from one area to another */
void
changearea(m,area)
struct mbx *m;
char *area;
{
	closenotes(m);
	m->nmsgs = m->newmsgs = m->current = 0;
	strcpy(m->area,area);
	scanmail(m);
}

static int
dombtelnet(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	int s, len, i;
	struct sockaddr dsocket;
	struct sockaddr_in fsocket;

	m = (struct mbx *) p;
	fsocket.sin_family = AF_INET;
	if(argc < 3)
		fsocket.sin_port = IPPORT_TELNET;
	else
		fsocket.sin_port = atoi(argv[2]);

	if((fsocket.sin_addr.s_addr = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 0;
	}
	/* Only local telnets are are allowed to the unprivileged user */
	if(!(m->privs & TELNET_CMD) && !ismyaddr(fsocket.sin_addr.s_addr)){
		printf(Noperm);
		return 0;
	}
	if((s = socket(AF_INET,SOCK_STREAM,0)) == -1){
		printf(Nosock);
		return 0;
	}
	if(fsocket.sin_port == IPPORT_TTYLINK) {
		m->startmsg = mallocw(80);
		len = MAXSOCKSIZE;
		i = getpeername(fileno(m->user),&dsocket,&len);
		sprintf(m->startmsg,"*** Incoming call from %s@%s ***\n",
			m->name,i != -1 ? psocket(&dsocket): Hostname);
	}
	return gw_connect(m,s,(struct sockaddr *)&fsocket,SOCKSIZE);
}

static int
dombfinger(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct mbx *m;
	char *host, *user = NULL, buf[8], *newargv[3];

	if(argc > 2){
		printf("Usage: F user@host  or  F @host  or  F user.\n");
		return 0;
	}
	host = Hostname;
	if(argc == 2){
		if((host = strchr(argv[1], '@')) != NULL){
			*host = '\0';
			host++;
		} else
			host = Hostname;
		user = argv[1];
	}
	m = (struct mbx *) p;
	m->startmsg = mallocw(80);
	if(user != NULL)
		sprintf(m->startmsg,"%s\n",user);
	else
		strcpy(m->startmsg,"\n");
	newargv[0] = "telnet";
	newargv[1] = host;
	sprintf(buf,"%d",IPPORT_FINGER);
	newargv[2] = buf;
	return dombtelnet(3,newargv,p);
}

/* Generic mbox gateway code. It sends and frees the contents of m->startmsg
 * when the connection has been established unless it a null pointer.
 */
int
gw_connect(m,s,fsocket,len)
struct mbx *m;
int s;
struct sockaddr *fsocket;
int len;
{
	int c;
	char *cp;
	struct proc *child;
	struct gwalarm *gwa;
	FILE *network;

	child = newproc("gateway supervisor",256,gw_superv,0,Curproc,m,0);
	printf("Trying %s...  ",psocket(fsocket));
	dombescape(0,NULL,(void *)m);
	fflush(stdout);
	if(connect(s,fsocket,len) == -1){
		cp = sockerr(s);
		printf("Connection failed: ");
		if(cp != NULL)
			printf("%s errno %d\n",cp,errno);
		else
			printf("Escape character sent.\n");
		free(m->startmsg);
		m->startmsg = NULL;
		killproc(child);
		close_s(s);
		return 0;
	}
	network = fdopen(s,"r+t");
	/* The user did not type the escape character */
	killproc(child);
	puts("Connected.");
	
	if(m->startmsg != NULL){
		fputs(m->startmsg,network);
		free(m->startmsg);
		m->startmsg = NULL;
	}
	/* Since NOS does not flush the output socket after a certain
	 * period of time, we have to arrange that ourselves.
	 */
	gwa = (struct gwalarm *) mallocw(sizeof(struct gwalarm));
	gwa->s1 = stdout;
	gwa->s2 = network;
	set_timer(&gwa->t,240L);
	gwa->t.func = gw_alarm;
	gwa->t.arg = (void *) gwa;
	start_timer(&gwa->t);
	/* Fork off the receive process */
	child = newproc("gateway in",1024,gw_input,s,(void *)network,Curproc,0);
	
	for(;;){
		if((c = getchar()) == EOF)
			break;
		if(c == m->escape){
			puts("Disconnecting.");
			if(socklen(fileno(stdin),0))
				recv_mbuf(fileno(stdin),NULL,0,NULL,0);
			break;
		}
		if(putc(c,network) == EOF)
			break;
	}
	stop_timer(&gwa->t);
	free(gwa);
	fclose(network);
	killproc(child); /* get rid of the receive process */
	printf("%c%c%c\n",IAC,WONT,TN_ECHO);
	return 0;
}

static void
gw_input(s,n,p)
int s;
void *n;
void *p;
{
	int c;
	char *cp;
	struct proc *parent;
	FILE *network;

	network = (FILE *)n;
	parent = (struct proc *) p;
	while((c = getc(network)) != EOF)
		putchar((char)c);
	printf("Disconnected ");
	cp = sockerr(fileno(network));
	if(cp != NULL)
		puts(cp);
	/* Tell the parent that we are no longer connected */
	alert(parent,ENOTCONN);
	kwait(Curproc); /* Now wait to be killed */
}

/* Check if the escape character is typed while the parent process is busy
 * doing other things. 
 */
static void
gw_superv(null,proc,p)
int null;
void *proc;
void *p;
{
	struct proc *parent;
	struct mbx *m;
	int c;
	parent = (struct proc *) proc;
	m = (struct mbx *) p;
	while((c = getchar()) != EOF)
		if(c == m->escape){
			/* flush anything in the input queue */
			if(socklen(fileno(stdin),0))
				recv_mbuf(fileno(stdin),NULL,0,NULL,0);
			break;
		}
	alert(parent,EINTR);	 /* Tell the parent to quit */
	kwait(Curproc);		 /* Please kill me */
}

static void
gw_alarm(p)
void *p;
{
	struct gwalarm *gwa = (struct gwalarm *)p;
	char oldbl;
	struct usock *up;

	/* Flush sockets s1 and s2, but first make sure that the socket
	 * is set to non-blocking mode, to prevent the flush from blocking
	 * if the high water mark has been reached.
	 */
	if((up = itop(fileno(gwa->s1))) != NULL) {
		oldbl = up->noblock;
		up->noblock = 1;
		fflush(gwa->s1);
		up->noblock = oldbl;
	}
	if((up = itop(fileno(gwa->s2))) != NULL) {
		oldbl = up->noblock;
		up->noblock = 1;
		fflush(gwa->s2);
		up->noblock = oldbl;
	}
	start_timer(&gwa->t);
}

/* States for send line parser state machine */
#define		LOOK_FOR_USER		2
#define		IN_USER			3
#define		AFTER_USER		4
#define		LOOK_FOR_HOST		5
#define		IN_HOST			6
#define		AFTER_HOST		7
#define		LOOK_FOR_FROM		8
#define		IN_FROM			9
#define		AFTER_FROM		10
#define		LOOK_FOR_MSGID		11
#define		IN_MSGID		12
#define		FINAL_STATE		13
#define		ERROR_STATE		14

/* Prepare the addressee.  If the address is bad, return -1, otherwise
 * return 0
 */
static int
mbx_to(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register char *cp;
	int state, i;
	char *user, *host, *from, *msgid;
	int userlen = 0, hostlen = 0, fromlen = 0, msgidlen = 0;
	struct mbx *m;

	m = (struct mbx *)p;
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

	if(argc == 1)
		return -1;
	i = 1;
	cp = argv[i];
	state = LOOK_FOR_USER;
	while(state < FINAL_STATE){
#ifdef MBDEBUG
		printf("State is %d, char is %c\n", state, *cp);
#endif
		switch(state){
		case LOOK_FOR_USER:
			if(*cp == '@' || *cp == '<' || *cp == '$'){
				state = ERROR_STATE;		/* no user */
			} else {
				user = cp;			/* point at start */
				userlen++;			/* start counting */
				state = IN_USER;
			}
			break;
		case IN_USER:
			switch(*cp){
			case '\0':
				state = AFTER_USER;		/* done with username */
				break;
			case '@':
				state = LOOK_FOR_HOST;		/* hostname should follow */
				break;
			case '<':
				state = LOOK_FOR_FROM;		/* from name should follow */
				break;
			case '$':
				state = LOOK_FOR_MSGID;	/* message id should follow */
				break;
			default:
				userlen++;			/* part of username */
			}
			break;
		case AFTER_USER:
			switch(*cp){
			case '@':
				state = LOOK_FOR_HOST;		/* hostname follows */
				break;
			case '<':
				state = LOOK_FOR_FROM;		/* fromname follows */
				break;
			case '$':
			state = LOOK_FOR_MSGID;	/* message id follows */
				break;
			default:
				state = ERROR_STATE;
			}
			break;
		case LOOK_FOR_HOST:
			if(*cp == '@' || *cp == '<' || *cp == '$'){
				state = ERROR_STATE;
				break;
			}
			if(*cp == '\0')
				break;
			host = cp;
			hostlen++;
			state = IN_HOST;
			break;
		case IN_HOST:
			switch(*cp){
			case '\0':
				state = AFTER_HOST;		/* found user@host */
				break;
			case '@':
				state = ERROR_STATE;		/* user@host@? */
				break;
			case '<':
				state = LOOK_FOR_FROM;		/* fromname follows */
				break;
			case '$':
				state = LOOK_FOR_MSGID;	/* message id follows */
				break;
			default:
				hostlen++;
			}
			break;
		case AFTER_HOST:
			switch(*cp){
			case '@':
				state = ERROR_STATE;		/* user@host @ */
				break;
			case '<':
				state = LOOK_FOR_FROM;		/* user@host < */
				break;
			case '$':
				state = LOOK_FOR_MSGID;	/* user@host $ */
				break;
			default:
				state = ERROR_STATE;		/* user@host foo */
			}
			break;
		case LOOK_FOR_FROM:
			if(*cp == '@' || *cp == '<' || *cp == '$'){
				state = ERROR_STATE;
				break;
			}
			if(*cp == '\0')
				break;
			from = cp;
			fromlen++;
			state = IN_FROM;
			break;
		case IN_FROM:
			switch(*cp){
			case '\0':
				state = AFTER_FROM;		/* user@host <foo */
				break;
			case '<':
				state = ERROR_STATE;		/* user@host <foo< */
				break;
			case '$':
				state = LOOK_FOR_MSGID;	/* message id follows */
				break;
			default:
				fromlen++;
			}
			break;
		case AFTER_FROM:
			switch(*cp){
			case '@':				/* user@host <foo @ */
			case '<':				/* user@host <foo < */
				state = ERROR_STATE;
				break;
			case '$':
				state = LOOK_FOR_MSGID;	/* user@host <foo $ */
				break;
			default:
				state = ERROR_STATE;		/* user@host foo */
			}
			break;
		case LOOK_FOR_MSGID:
			if(*cp == '\0')
				break;
			msgid = cp;
			msgidlen++;
			state = IN_MSGID;
			break;
		case IN_MSGID:
			if(*cp == '\0')
				state = FINAL_STATE;
			else
				msgidlen++;
			break;
		default:
			/* what are we doing in this state? */
			state = ERROR_STATE;
		}
		if(*(cp) == '\0'){
			++i;
			if(i < argc)
			cp = argv[i];
			else break;
		} else
			++cp;
	}
	if(state == ERROR_STATE || state == LOOK_FOR_HOST
	 || state == LOOK_FOR_FROM || state == LOOK_FOR_MSGID)
		return -1;		/* syntax error */

	m->to = mallocw(userlen + hostlen + 2);

	strncpy(m->to, user, userlen);
	m->to[userlen] = '\0';

	if(hostlen){
		m->to[userlen] = '@';
		strncpy(m->to + userlen + 1, host, hostlen);
		m->to[userlen + hostlen + 1] = '\0';
	}
	if(fromlen){
		m->tofrom = mallocw(fromlen + 1);
		strncpy(m->tofrom, from, fromlen);
		m->tofrom[fromlen] = '\0';
	}
	if(msgidlen){
		m->tomsgid = mallocw(msgidlen + 1);
		strncpy(m->tomsgid, msgid, msgidlen);
		m->tomsgid[msgidlen] = '\0';
	}
	return 0;
}

/* This opens the data file and writes the mail header into it.
 * Returns 0 if OK, and -1 if not.
 */
static int
mbx_data(m,cclist,extra)
struct mbx *m;
struct list *cclist;	/* list of carbon copy recipients */
char *extra;		/* optional extra header lines */
{
	time_t t;
	struct list *ap;
	int cccnt = 0;
	
	if((m->tfile = tmpfile()) == NULL)
		return -1;
	time(&t);
	fprintf(m->tfile,Hdrs[RECEIVED]);
	if(m->tofrom != NULL)
		fprintf(m->tfile,"from %s.bbs ",m->name);
	fprintf(m->tfile,"by %s (%s)\n\tid AA%ld ; %s",
		Hostname, Version, get_msgid(), ptime(&t));
	fprintf(m->tfile,"%s%s",Hdrs[DATE],ptime(&t));
	fprintf(m->tfile,Hdrs[MSGID]);
	if(m->tomsgid)
		fprintf(m->tfile,"<%s@%s.bbs>\n", m->tomsgid, m->name);
	else
		fprintf(m->tfile,"<%ld@%s>\n",get_msgid(), Hostname);
	fprintf(m->tfile,Hdrs[FROM]);
	if(m->tofrom)
		fprintf(m->tfile,"%s%%%s.bbs@%s\n",
			m->tofrom, m->name, Hostname);
	else
		fprintf(m->tfile,"%s@%s\n", m->name, Hostname);
	fprintf(m->tfile,"%s%s\n",Hdrs[TO],m->origto != NULL ? m->origto : m->to);
	/* Write Cc: line */
	for(ap = cclist; ap != NULL; ap = ap->next) {
		if(cccnt == 0){
			fprintf(m->tfile,"%s",Hdrs[CC]);
			cccnt = 4;
		}
		else {
		       fprintf(m->tfile,", ");
		       cccnt += 2;
		}
		if(cccnt + strlen(ap->val) > 80 - 3) {
		       fprintf(m->tfile,"\n    ");
		       cccnt = 4;
		}
		fputs(ap->val,m->tfile);
		cccnt += strlen(ap->val);
	}
	if(cccnt)
		fputc('\n',m->tfile);
	fprintf(m->tfile,"%s%s\n",Hdrs[SUBJECT],m->line);
	if(!isspace(m->stype) && ((m->stype != 'R' && m->stype != 'F') ||
	  (m->sid & MBX_SID) !=0))
		  fprintf(m->tfile,"%s%c\n", Hdrs[BBSTYPE],m->stype);
	if(extra != NULL)
		fprintf(m->tfile,extra);
	fprintf(m->tfile,"\n");

	return 0;
}

/* Returns true if string is in history file or if string appears to be a
 * message id generated by our system.
 */
static int
msgidcheck(string)
char *string;
{
     FILE *fp;
     char buf[LINELEN], *cp;
     if(string == NULL)
	  return 0;
     /* BID's that we have generated ourselves are not kept in the history
      * file. Such BID's are in the nnnn_hhhh form, where hhhh is a part of
      * our hostname, truncated so that the BID is no longer than 11
      * characters.
      */
     if((cp = strchr(string,'_')) != NULL && *(cp+1) != '\0' && 
	strnicmp(cp+1,Hostname,strlen(cp+1)) == 0)
	  return 1;

     if((fp = fopen(Historyfile,READ_TEXT)) == NULL)
	  return 0;
     while(fgets(buf,LINELEN,fp) != NULL) {
	  rip(buf);
	  if(stricmp(string,buf) == 0) {	/* found */
	       fclose(fp);
	       return 1;
	  }
     }
     fclose(fp);
     return 0;
}
     

/* uuencode a file -- translated from C++; both versions copyright 1990
   by David R. Evans, G4AMJ/NQ0I
*/

static int
uuencode(infile,outfile,infilename)
FILE *infile;
FILE *outfile;
char *infilename;
{
  int n_read_so_far = 0, n_written_so_far = 0, in_chars, n, mode = 0755;
  int32 cnt = 0;
  unsigned char in[3], out[4], line[100];
#ifdef UNIX
  struct stat stb;
  
  if(stat(infilename,&stb) != -1)
       mode = stb.st_mode & 0777;	/* get real file protection mode */
#endif
  fprintf(outfile, "begin %03o %s\n", mode, infilename);

  /* do the encode */
  for(;;) {
    in_chars = fread(in, 1, 3, infile);
    out[0] = in[0] >> 2;
    out[1] = in[0] << 6;
    out[1] = out[1] >> 2;
    out[1] = out[1] | (in[1] >> 4);
    out[2] = in[1] << 4;
    out[2] = out[2] >> 2;
    out[2] = out[2] | (in[2] >> 6);
    out[3] = in[2] << 2;
    out[3] = out[3] >> 2;
    for (n = 0; n < 4; n++)
      out[n] += ' ';
    n_read_so_far += in_chars;
    for (n = 0; n < 4; n++)
      line[n_written_so_far++] = out[n];
    if (((in_chars != 3) || (n_written_so_far == 60)) && n_read_so_far > 0) {
      line[(n_read_so_far + 2) / 3 * 4] = '\0';
      
      fprintf(outfile,"%c%s\n",n_read_so_far + ' ', line);
      cnt += n_read_so_far;
      n_read_so_far = 0;
      n_written_so_far = 0;
    }
    if (in_chars == 0)
      break;
  }
  if (fprintf(outfile," \nend\nsize %lu\n", cnt) == EOF)
    return 1;
  return 0;
}
