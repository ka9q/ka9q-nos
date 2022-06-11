#ifndef	_SMTP_H
#define	_SMTP_H

#define SMTPTRACE			/* enable tracing for smtp */
#define MAXSESSIONS	10		/* most connections allowed */
#define JOBNAME		13		/* max size of a job name with null */
#define	LINELEN		256
#define SLINELEN	64
#define MBOXLEN		8		/* max size of a mail box name */

/* types of address used by smtp in an address list */
#define BADADDR	0
#define LOCAL	1
#define DOMAIN	2

/* a list entry */
struct list {
	struct list *next;
	char *val;
	char type;
};

/* Per-session control block  used by smtp server */
struct smtpsv {
	FILE *network;		/* The network stream for this connection */
	char *system;		/* Name of remote system */
	char *from;		/* sender address */
	struct list *to;	/* Linked list of recipients */
	FILE *data;		/* Temporary input file pointer */
};

/* used by smtpcli as a queue entry for a single message */
struct smtp_job {
	struct 	smtp_job *next;	/* pointer to next mail job for this system */
	char	jobname[9];	/* the prefix of the job file name */
	char	*from;		/* address of sender */
	struct list *to;	/* Linked list of recipients */
};

/* control structure used by an smtp client session */
struct smtpcli {
	FILE *network;		/* The network stream for this connection */
	int32	ipdest;		/* address of forwarding system */
	char	*destname;	/* domain address of forwarding system */
	char	*wname;		/* name of workfile */
	char	*tname;		/* name of data file */
	char	buf[LINELEN];	/* Output buffer */
	char	cnt;		/* Length of input buffer */
	FILE	*tfile;
	struct	smtp_job *jobq;
	struct	list 	*errlog;	
	int lock;		/* In use */
};

/* smtp server routing mode */
#define	QUEUE	1

extern int Smtpmode;
extern char *Mailspool;
extern char *Maillog;
extern char *Mailqdir;		/* Outgoing spool directory */
extern char *Routeqdir;	/* spool directory for a router program */
extern char *Mailqueue;	/* Prototype of work file */
extern char *Maillock;		/* Mail system lock */
extern char *Alias;		/* File of local aliases */

/* In smtpserv.c: */
char *ptime(long *t);
long get_msgid(void);
char *getname(char *cp);
int validate_address(char *s);
int queuejob(FILE *dfile,char *host,struct list *to,char *from);
struct list *addlist(struct list **head,char *val,int type);
int mdaemon(FILE *data,char *to,struct list *lp,int bounce);

/* In smtpcli.c: */
int smtptick(int32 target);
int mlock(char *dir,char *id);
int rmlock(char *dir,char *id);
void del_list(struct list *lp);
int32 mailroute(char *dest);

#endif	/* _SMTP_H */

