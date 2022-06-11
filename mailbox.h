#ifndef _MAILBOX_H
#define _MAILBOX_H
/* Defines for the ax.25 mailbox facility */
#define NUMMBX		10		/* max number of mailbox sessions */
#ifndef _TIMER_H
#include "timer.h"
#endif

#ifndef	_SOCKADDR_H
#include "sockaddr.h"
#endif

/* a mailbox list entry */
struct let {
	long	start;
	long	size;
	int	status;
};

#define MBXLINE		128		/* max length of line */
struct mbx {
	int state ;				/* mailbox state */
#define MBX_LOGIN	0		/* trying to log in */
#define MBX_CMD		1		/* in command mode */
#define MBX_SUBJ	2		/* waiting for a subject line */
#define MBX_DATA	3		/* collecting the message */
#define MBX_REVFWD	4		/* reverse forwarding in progress */
#define MBX_TRYING	5		/* pending forwarding connection */
#define MBX_FORWARD	6		/* established forwarding connection */
	char name[20] ;			/* Name of remote station */
	char *to ;			/* To-address in form user or user@host */
	char *origto ;			/* Original To-address, if rewritten */
	char *tofrom ;			/* Optional <from in to-address */
	char *tomsgid ;			/* Optional $msgid in to-address */
	FILE *tfile ;			/* Temporary file for message, or */
					/* forwarding file. */
	char line[MBXLINE+1] ;	/* Room for null at end */
	int mbnum ;		/* which mailbox session is this? */
	int sid ;		/* Characteristics indicated by the SID */
				/* banner of the attaching station.  If */
				/* no SID was sent, this is zero.  If an */
				/* SID of any kind was received, it is */
				/* assumed that the station supports */
				/* abbreviated mail forwarding mode. */
#define	MBX_SID		0x01	/* Got any SID */
#define	MBX_RLI_SID	0x02	/* This is an RLI BBS, disconnect after F> */
#define MBX_HIER_SID	0x04	/* The BBS supports "hierarchical routing */
				/* designators." */
				/* Space here for others, currently not of */
				/* interest to us. */
	char stype ;		/* BBS send command type (B,P,T, etc.) */
	int type ;		/* Type of session when invoking "chat" */
	FILE *user;		/* User linkage area */
	char escape;		/* Escape character */
	int privs;		/* Privileges (taken from Ftpusers file) */
#define AX25_CMD	8	/* AX.25 gateway operation allowed */
#define TELNET_CMD	16	/* Telnet gateway operation allowed */
#define NETROM_CMD	32	/* NET/ROM gateway operation allowed */
#define SYSOP_CMD	64	/* Remote sysop access allowed */
#define EXCLUDED_CMD	128	/* This user is banned from the BBS */
	char *path;		/* Directory path */
	char *startmsg;		/* Message to be sent at connect through any
				/* of the gateways */
	int current;		/* the current message number */
	int nmsgs;		/* number of messages in this mail box */
	int newmsgs;		/* number of new messages in mail box */
	int change;		/* mail file changed */
	int anyread;		/* true if any message has been read */
	FILE *mfile;		/* mail data file pointer */
	char area[64];		/* name of current mail area */
	long mboxsize;		/* size of mailbox when opened */
	long mysize;		/* size of my private mailbox */
	struct let *mbox;
	char *stdinbuf;		/* the stdio buffer for the mail file */
	char *stdoutbuf;	/* the stdio file io buffer for the temp file */
} ;

/* Structure used for automatic flushing of gateway sockets */
struct gwalarm {
	FILE *s1;
	FILE *s2;
	struct timer t;
};

/* In mailbox.c: */
extern struct mbx *Mbox[NUMMBX] ;
extern char Noperm[];
extern char Nosock[];
extern void (*Listusers)(FILE *network);

int dombescape(int argc,char *argv[],void *p);
int mbxrecvline(FILE *network,char *buf,int len,int escape);
int gw_connect(struct mbx *m,int s,struct sockaddr *fsocket,int len);

void mbx_incom(int s,void *t,void *p);
int domboxdisplay(int argc,char *argv[],void *p);
struct mbx *newmbx(void);
void exitbbs(struct mbx *m);
int domboxbye(int argc,char *argv[],void *p);
int mbx_parse(struct mbx *m);
void changearea(struct mbx *m,char *area);
char *rewrite_address(char *addr);
void listusers(FILE *network);

/* In forward.c: */
int dorevfwd(int argc,char *argv[],void *p);
int dombtimer(int argc,char *argv[],void *p);
int dombkick(int argc,char *argv[],void *p);

/* In tipmail.c */
extern unsigned Tiptimeout;

#endif	/* _MAILBOX_H */

