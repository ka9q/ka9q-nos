#ifndef _BM_H
#define _BM_H

/* bm.h -- definitions for bmutil.c that aren't included elsewhere */

#ifndef _SMTP_H
#include "smtp.h"
#endif

#ifndef	_MAILBOX_H
#include "mailbox.h"
#endif

/* Header types */
#define	NOHEADER 	-1
#define APPROVED	0
#define	FROM		1
#define	TO		2
#define	DATE		3
#define	MSGID		4
#define	SUBJECT		5
#define	RECEIVED	6
#define	SENDER		7
#define	REPLYTO		8
#define STATUS		9
#define BBSTYPE		10
#define XFORWARD	11
#define CC		12
#define RRECEIPT	13
#define APPARTO		14
#define ERRORSTO	15
#define ORGANIZATION	17
#define	UNKNOWN		18

/* number of columns and lines on a standard display, e.g. vt100 */
#define	MAXCOL		80
#define MAXLIN		24

/* message status */
#define	BM_DELETE	1
#define	BM_READ		2
#define	BM_FORWARDED	4

#define BM_NLET		200	/* default size of letter array */

#define SLINELEN	64
#define LINELEN		256

extern unsigned Maxlet;		/* max messages */
extern char *fgets();
extern char *Hdrs[];

void scanmail(struct mbx *m);
int msgtofile(struct mbx *m,int msg,FILE *tfile,int noheader);
int dolistnotes(int argc,char *argv[],void *p);
int isarea(char *name);
int dodelmsg(int argc,char *argv[],void *p);
int doreadmsg(int argc,char *argv[],void *p);
int doreadnext(int argc,char *argv[],void *p);
int mbx_reply(int argc,char *argv[],struct mbx *m,struct list **cclist,char **rhdr);
int closenotes(struct mbx *m);
long isnewprivmail(struct mbx *m);
int htype(char *s);
char *getaddress(char *string,int cont);

#endif  /* _BM_H */
