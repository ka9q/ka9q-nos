/* POP Server state machine - see RFC 937
 *
 *  also see other credits in popcli.c
 *  10/89 Mike Stockett wa7dyx
 *  Modified 5/27/90 by Allen Gwinn, N5CKP, for later NOS releases.
 *  Added to NOS by PA0GRI 2/6/90 (and linted into "standard" C)
 */

#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
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
#include "proc.h"
#include "files.h"
#include "pop.h"

extern char Nospace[];

static struct pop_scb *create_scb(void);
static void delete_scb(struct pop_scb *scb);
static void popserv(int s,void *unused,void *p);
static int poplogin(char *pass,char *username);
static void rrip(register char *s);
int newmail(struct pop_scb *);
int isdeleted(struct pop_scb *,int);

/* I don't know why this isn't static, it isn't called anywhere else {was} */
void pop_sm(struct pop_scb *scb);

static int Spop = -1; /* prototype socket for service */

/* Start up POP receiver service */
int
pop1(argc,argv,p)

int argc;
char *argv[];
void *p;

{
	struct sockaddr_in lsocket;
	int s;

	if (Spop != -1) {
		return 0;
	}

	ksignal(Curproc,0);		/* Don't keep the parser waiting */
	chname(Curproc,"POP listener");

	lsocket.sin_family = AF_INET;
	lsocket.sin_addr.s_addr = INADDR_ANY;
	if(argc < 2)
		lsocket.sin_port = IPPORT_POP;
	else
		lsocket.sin_port = atoi(argv[1]);

	Spop = socket(AF_INET,SOCK_STREAM,0);

	bind(Spop,(struct sockaddr *)&lsocket,sizeof(lsocket));

	listen(Spop,1);

	for (;;) {
		if((s = accept(Spop,NULL,(int *)NULL)) == -1)
			break;	/* Service is shutting down */

		/* Spawn a server */

		newproc("POP server",2048,popserv,s,NULL,NULL,0);
	}
	return 0;
}

/* Shutdown POP service (existing connections are allowed to finish) */

int
pop0(argc,argv,p)
int argc;
char *argv[];
void *p;

{
	close_s(Spop);
	Spop = -1;
	return 0;
}

static void
popserv(s,unused,p)
int s;
void *unused;
void *p;
{
	struct pop_scb *scb;
	FILE *network;

	sockowner(s,Curproc);		/* We own it now */
	logmsg(s,"open POP");
	network = fdopen(s,"r+t");

	if((scb = create_scb()) == NULL) {
		printf(Nospace);
		logmsg(s,"close POP - no space");
		fclose(network);
		return;
	}

	scb->network = network;
	scb->state  = AUTH;

	(void) fprintf(network,greeting_msg,Hostname);

loop:	if (fgets(scb->buf,BUF_LEN,network) == NULL){
		/* He closed on us */

		goto quit;
	}
	scb->count = strlen(scb->buf);
	rip(scb->buf);
	if (strlen(scb->buf) == 0)		/* Ignore blank cmd lines */
		goto loop;
	pop_sm(scb);
	if (scb->state == DONE)
		goto quit;

	goto loop;

quit:
	logmsg(fileno(scb->network),"close POP");
	fclose(scb->network);
	delete_scb(scb);
}


/* Create control block, initialize */

static struct
pop_scb *create_scb()
{
	register struct pop_scb *scb;

	if((scb = (struct pop_scb *)callocw(1,sizeof (struct pop_scb))) == NULL)
		return NULL;

	scb->username[0] = '\0';
	scb->msg_status = NULL;
	scb->wf = NULL;

	scb->count = scb->folder_file_size = scb->msg_num = 0;

	scb->folder_modified = FALSE;
	return scb;
}


/* Free resources, delete control block */

static void
delete_scb(scb)
register struct pop_scb *scb;
{

	if (scb == NULL)
		return;
	if (scb->wf != NULL)
		fclose(scb->wf);
	if (scb->msg_status  != NULL)
		free(scb->msg_status);

	free(scb);
}

/* replace terminating end of line marker(s) (\r and \n) with null */
static void
rrip(s)
register char *s;
{
	register char *cp;

	if((cp = strchr(s,'\r')) != NULL)
		*cp = '\0';
	if((cp = strchr(s,'\n')) != NULL)
		*cp = '\0';
}

/* --------------------- start of POP server code ------------------------ */

#define	BITS_PER_WORD		16

#define isSOM(x)		((strncmp(x,"From ",5) == 0))

/* Command string specifications */

static char	ackd_cmd[] = "ACKD",
		acks_cmd[] = "ACKS",
#ifdef POP_FOLDERS
		fold_cmd[] = "FOLD ",
#endif
		login_cmd[] = "HELO ",
		nack_cmd[] = "NACK",
		quit_cmd[] = "QUIT",
		read_cmd[] = "READ",
		retr_cmd[] = "RETR";

void
pop_sm(scb)
struct pop_scb *scb;
{
	char password[40];
	void state_error(struct pop_scb *,char *);
	void open_folder(struct pop_scb *);
	void do_cleanup(struct pop_scb *);
	void read_message(struct pop_scb *);
	void retrieve_message(struct pop_scb *);
	void deletemsg(struct pop_scb *,int);
	void get_message(struct pop_scb *,int);
	void print_message_length(struct pop_scb *);
	void close_folder(struct pop_scb *);
#ifdef POP_FOLDERS
	void select_folder(struct pop_scb *);
#endif

	if (scb == NULL)	/* be certain it is good -- wa6smn */
		return;

	switch(scb->state) {
	case AUTH:
		if (strncmp(scb->buf,login_cmd,strlen(login_cmd)) == 0){
			sscanf(scb->buf,"HELO %s%s",scb->username,password);

			if (!poplogin(scb->username,password)) {
				logmsg(fileno(scb->network),"POP access DENIED to %s",
					    scb->username);
				state_error(scb,"Access DENIED!!");
				return;
			}

			logmsg(fileno(scb->network),"POP access granted to %s",
				    scb->username);
			open_folder(scb);
		} else if (strncmp(scb->buf,quit_cmd,strlen(quit_cmd)) == 0){
			do_cleanup(scb);
		} else
			state_error(scb,"(AUTH) Expected HELO or QUIT command");
		break;

	case MBOX:
		if (strncmp(scb->buf,read_cmd,strlen(read_cmd)) == 0)
			read_message(scb);

#ifdef POP_FOLDERS
		else if (strncmp(scb->buf,fold_cmd,strlen(fold_cmd)) == 0)
			select_folder(scb);

#endif

		else if (strncmp(scb->buf,quit_cmd,strlen(quit_cmd)) == 0) {
			do_cleanup(scb);
		} else
			state_error(scb,
#ifdef POP_FOLDERS
				    "(MBOX) Expected FOLD, READ, or QUIT command");
#else
				    "(MBOX) Expected READ or QUIT command");
#endif
		break;

	case ITEM:
		if (strncmp(scb->buf,read_cmd,strlen(read_cmd)) == 0)
			read_message(scb);

#ifdef POP_FOLDERS

		else if (strncmp(scb->buf,fold_cmd,strlen(fold_cmd)) == 0)
			select_folder(scb);
#endif

		else if (strncmp(scb->buf,retr_cmd,strlen(retr_cmd)) == 0)
			retrieve_message(scb);
		else if (strncmp(scb->buf,quit_cmd,strlen(quit_cmd)) == 0)
			do_cleanup(scb);
		else
			state_error(scb,
#ifdef POP_FOLDERS
			   "(ITEM) Expected FOLD, READ, RETR, or QUIT command");
#else
			   "(ITEM) Expected READ, RETR, or QUIT command");
#endif
		break;

	case NEXT:
		if (strncmp(scb->buf,ackd_cmd,strlen(ackd_cmd)) == 0){
				/* ACKD processing */
			deletemsg(scb,scb->msg_num);
			scb->msg_num++;
			get_message(scb,scb->msg_num);
		} else if (strncmp(scb->buf,acks_cmd,strlen(acks_cmd)) == 0){
				/* ACKS processing */
			scb->msg_num++;
			get_message(scb,scb->msg_num);
		} else if (strncmp(scb->buf,nack_cmd,strlen(nack_cmd)) == 0){
				/* NACK processing */
			fseek(scb->wf,scb->curpos,SEEK_SET);
		} else {
			state_error(scb,"(NEXT) Expected ACKD, ACKS, or NACK command");
			return;
		}

		print_message_length(scb);
		scb->state  = ITEM;
		break;

	case DONE:
		do_cleanup(scb);
		break;

	default:
		state_error(scb,"(TOP) State Error!!");
		break;
	}
}

void
do_cleanup(scb)
struct pop_scb *scb;
{
	void close_folder(struct pop_scb *);

	close_folder(scb);
	(void) fprintf(scb->network,signoff_msg);
	scb->state = DONE;
}

void
state_error(scb,msg)
struct pop_scb *scb;
char *msg;
{
	(void) fprintf(scb->network,error_rsp,msg);
	scb->state = DONE;
}

#ifdef POP_FOLDERS

select_folder(scb)
struct pop_scb	*scb;
{
	sscanf(scb->buf,"FOLD %s",scb->username);

	if (scb->wf != NULL)
		close_folder(scb);

	open_folder(scb);
}

#endif


void
close_folder(scb)
struct pop_scb *scb;
{
	char folder_pathname[64];
	char line[BUF_LEN];
	FILE *fd;
	int deleted = FALSE;
	int msg_no = 0;
	struct stat folder_stat;

	if (scb->wf == NULL)
		return;

	if (!scb->folder_modified) {
		/* no need to re-write the folder if we have not modified it */

		fclose(scb->wf);
		scb->wf = NULL;

		free(scb->msg_status);
		scb->msg_status = NULL;
		return;
	}


	sprintf(folder_pathname,"%s/%s.txt",Mailspool,scb->username);

	if (newmail(scb)) {
		/* copy new mail into the work file and save the
		   message count for later */

		if ((fd = fopen(folder_pathname,"r")) == NULL) {
			state_error(scb,"Unable to add new mail to folder");
			return;
		}

		fseek(scb->wf,0,SEEK_END);
		fseek(fd,scb->folder_file_size,SEEK_SET);
		while (!feof(fd)) {
			fgets(line,BUF_LEN,fd);
			fputs(line,scb->wf);
		}

		fclose(fd);
	}

	/* now create the updated mail folder */

	if ((fd = fopen(folder_pathname,"w")) == NULL){
		state_error(scb,"Unable to update mail folder");
		return;
	}

	rewind(scb->wf);
	while (!feof(scb->wf)){
		fgets(line,BUF_LEN,scb->wf);

		if (isSOM(line)){
			msg_no++;
			if (msg_no <= scb->folder_len)
				deleted = isdeleted(scb,msg_no);
			else
				deleted = FALSE;
		}

		if (deleted)
			continue;

		fputs(line,fd);
	}

	fclose(fd);

	/* trash the updated mail folder if it is empty */

	if ((stat(folder_pathname,&folder_stat) == 0) && (folder_stat.st_size == 0))
		unlink(folder_pathname);

	fclose(scb->wf);
	scb->wf = NULL;

	free(scb->msg_status);
	scb->msg_status = NULL;
}

void
open_folder(scb)
struct pop_scb	*scb;
{
	char folder_pathname[64];
	char line[BUF_LEN];
	FILE *fd;
	FILE *tmpfile();
	struct stat folder_stat;


	sprintf(folder_pathname,"%s/%s.txt",Mailspool,scb->username);
	scb->folder_len       = 0;
	scb->folder_file_size = 0;
	if (stat(folder_pathname,&folder_stat)){
		 (void) fprintf(scb->network,no_mail_rsp);
		 return;
	}

	scb->folder_file_size = folder_stat.st_size;
	if ((fd = fopen(folder_pathname,"r")) == NULL){
		state_error(scb,"Unable to open mail folder");
		return;
	}

	if ((scb->wf = tmpfile()) == NULL) {
		state_error(scb,"Unable to create work folder");
		return;
	}

	while(!feof(fd)) {
		fgets(line,BUF_LEN,fd);

		/* scan for begining of a message */

		if (isSOM(line))
			scb->folder_len++;

		/* now put  the line in the work file */

		fputs(line,scb->wf);
	}

	fclose(fd);

	scb->msg_status_size = (scb->folder_len) / BITS_PER_WORD;

	if ((((scb->folder_len) % BITS_PER_WORD) != 0) ||
	    (scb->msg_status_size == 0))
		scb->msg_status_size++;

	if ((scb->msg_status = (unsigned int *) callocw(scb->msg_status_size,
				sizeof(unsigned int))) == NULL) {
		state_error(scb,"Unable to create message status array");
		return;
	}

	(void) fprintf(scb->network,count_rsp,scb->folder_len);

	scb->state  = MBOX;
}

void
read_message(scb)
struct pop_scb	*scb;
{
	void get_message(struct pop_scb *,int);
	void print_message_length(struct pop_scb *);

	if (scb == NULL)	/* check for null -- wa6smn */
		return;
	if (scb->buf[sizeof(read_cmd) - 1] == ' ')
		scb->msg_num = atoi(&(scb->buf[sizeof(read_cmd) - 1]));
	else
		scb->msg_num++;

	get_message(scb,scb->msg_num);
	print_message_length(scb);
	scb->state  = ITEM;
}

void
retrieve_message(scb)
struct pop_scb	*scb;
{
	char line[BUF_LEN];
	long cnt;

	if (scb == NULL)	/* check for null -- wa6smn */
		return;
	if (scb->msg_len == 0) {
		state_error(scb,"Attempt to access a DELETED message!");
		return;
	}

	cnt  = scb->msg_len;
	while(!feof(scb->wf) && (cnt > 0)) {
		fgets(line,BUF_LEN,scb->wf);
		rrip(line);

		(void) fprintf(scb->network,msg_line,line);
		cnt -= (strlen(line)+2);	/* Compensate for CRLF */
	}

	scb->state = NEXT;
}

void
get_message(scb,msg_no)
struct pop_scb	*scb;
int msg_no;
{
	char line[BUF_LEN];

	if (scb == NULL)	/* check for null -- wa6smn */
		return;
	scb->msg_len = 0;
	if (msg_no > scb->folder_len) {
		scb->curpos  = 0;
		scb->nextpos = 0;
		return;
	} else {
		/* find the message and its length */

		rewind(scb->wf);
		while (!feof(scb->wf) && (msg_no > -1)) {
			if (msg_no > 0)
				scb->curpos = ftell(scb->wf);
			
			fgets(line,BUF_LEN,scb->wf);
			rrip(line);

			if (isSOM(line))
				msg_no--;

			if (msg_no != 0)
				continue;

			scb->nextpos  = ftell(scb->wf);
			scb->msg_len += (strlen(line)+2);	/* Add CRLF */
		}
	}

	if (scb->msg_len > 0)
		fseek(scb->wf,scb->curpos,SEEK_SET);

	/* we need the pointers even if the message was deleted */

	if  (isdeleted(scb,scb->msg_num))
		scb->msg_len = 0;
}

static int
poplogin(username,pass)
char *pass;
char *username;
{
	char buf[80];
	char *cp;
	char *cp1;
	FILE *fp;

	if((fp = fopen(Popusers,"r")) == NULL) {
		/* User file doesn't exist */
		printf("POP users file %s not found\n",Popusers);
		return(FALSE);
	}

	while(fgets(buf,sizeof(buf),fp),!feof(fp)) {
		if(buf[0] == '#')
			continue;	/* Comment */

		if((cp = strchr(buf,':')) == NULL)
			/* Bogus entry */
			continue;

		*cp++ = '\0';		/* Now points to password */
		if(strcmp(username,buf) == 0)
			break;		/* Found user name */
	}

	if(feof(fp)) {
		/* User name not found in file */

		fclose(fp);
		return(FALSE);
	}
	fclose(fp);

	if ((cp1 = strchr(cp,':')) == NULL)
		return(FALSE);

	*cp1 = '\0';
	if(strcmp(cp,pass) != 0) {
		/* Password required, but wrong one given */

		return(FALSE);
	}

	/* whew! finally made it!! */

	return(TRUE);
}

int
isdeleted(scb,msg_no)
struct pop_scb *scb;
int msg_no;
{
	unsigned int mask = 1,offset;

	msg_no--;
	offset = msg_no / BITS_PER_WORD;
	mask <<= msg_no % BITS_PER_WORD;
	return (((scb->msg_status[offset]) & mask)? TRUE:FALSE);
}

void
deletemsg(scb,msg_no)
struct pop_scb *scb;
int msg_no;
{
	unsigned int mask = 1,offset;

	if (scb == NULL)	/* check for null -- wa6smn */
		return;
	msg_no--;
	offset = msg_no / BITS_PER_WORD;
	mask <<= msg_no % BITS_PER_WORD;
	scb->msg_status[offset] |= mask;
	scb->folder_modified = TRUE;
}

int
newmail(scb)
struct pop_scb *scb;
{
	char folder_pathname[64];
	struct stat folder_stat;

	sprintf(folder_pathname,"%s/%s.txt",Mailspool,scb->username);

	if (stat(folder_pathname,&folder_stat)) {
		state_error(scb,"Unable to get old mail folder's status");
		return(FALSE);
	} else
		return ((folder_stat.st_size > scb->folder_file_size)? TRUE:FALSE);
}

void
print_message_length(scb)
struct pop_scb *scb;
{
	char *print_control_string;

	if (scb == NULL)	/* check for null -- wa6smn */
		return;
	if (scb->msg_len > 0)
		print_control_string = length_rsp;
	else if (scb->msg_num <= scb->folder_len)
		print_control_string = length_rsp;
	else
		print_control_string = no_more_rsp;

	(void)fprintf(scb->network,print_control_string,scb->msg_len,scb->msg_num);
}
