#ifndef	_SESSION_H
#define	_SESSION_H

#include <stdio.h>

#ifndef	_GLOBAL_H
#include "global.h"
#endif

#ifndef	_PROC_H
#include "proc.h"
#endif

#ifndef _HARDWARE_H
#include "hardware.h"
#endif

#ifndef	_FTPCLI_H
#include "ftpcli.h"
#endif

#ifndef	_TELNET_H
#include "telnet.h"
#endif

struct ttystate {
	uint8 *line;		/* Line buffer */
	uint8 *lp;		/* Pointer into same */
	unsigned int echo:1;	/* Keyboard local echoing? */
	unsigned int edit:1;	/* Local editing? */
	unsigned int crnl:1;	/* Translate cr to lf? */
};

/* Session control structure; only one entry is used at a time */
struct session {
	unsigned index;
	enum {
		TELNET,
		FTP,
		AX25TNC,
		FINGER,
		PING,
		NRSESSION,
		COMMAND,
		VIEW,
		HOP,
		TIP,
		PPPPASS,
		DIAL,
		DQUERY,
		DCLIST,
		ITRACE,
		REPEAT,
		FAX
	} type;

	char *name;	/* Name of remote host */
	union {
		struct ftpcli *ftp;
		struct telnet *telnet;
		void *p;
	} cb;
	struct proc *proc;	/* Primary session process (e.g., tn recv) */
	struct proc *proc1;	/* Secondary session process (e.g., tn xmit) */
	struct proc *proc2;	/* Tertiary session process (e.g., upload) */
	FILE *network;		/* Primary network socket (control for FTP) */
	FILE *record;		/* Receive record file */
	FILE *upload;		/* Send file */
	struct ttystate ttystate;
	FILE *input;		/* Input stream */
	FILE *output;		/* Output stream */
	int (*ctlproc)(int);	/* Upcall  for keyboard ctls */
	int (*inproc)(int);	/* Upcall for normal characters */
	struct session *parent;
	enum {
		SCROLL_INBAND,
		SCROLL_LOCAL
	} scrollmode;	/* Cursor control key mode */
};
extern char *Sestypes[];
extern unsigned Nsessions;		/* Maximum number of sessions */
extern long Sfsize;			/* Size of scrollback file, lines */
extern struct session **Sessions;	/* Session descriptors themselves */
extern struct session *Current;		/* Always points to current session */
extern struct session *Lastcurr;	/* Last non-command session */
extern struct session *Command;		/* Pointer to command session */
extern char *Cmdline;			/* Last typed command line */

/* In session.c: */
void freesession(struct session *sp);
int keywait(char *prompt,int flush);
struct session *sessptr(char *cp);
struct session *newsession(char *name,int type,int makecur);
void sesflush(void);
void upload(int unused,void *sp1,void *p);

extern uint16 Lport;
#define	ALERT_EOF	1

#endif  /* _SESSION_H */
