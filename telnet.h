#ifndef	_TELNET_H
#define	_TELNET_H

#ifndef _SESSION_H
#include "session.h"
#endif

#ifndef _SOCKADDR_H
#include "sockaddr.h"
#endif

#define	LINESIZE	256	/* Length of local editing buffer */

/* Telnet command characters */
#define	IAC		255	/* Interpret as command */
#define	WILL		251
#define	WONT		252
#define	DO		253
#define	DONT		254

/* Telnet options */
#define	TN_TRANSMIT_BINARY	0
#define	TN_ECHO			1
#define	TN_SUPPRESS_GA		3
#define	TN_STATUS		5
#define	TN_TIMING_MARK		6
#define	NOPTIONS		6

/* Telnet protocol control block */
struct telnet {
	char local[NOPTIONS];	/* Local option settings */
	char remote[NOPTIONS];	/* Remote option settings */
	struct session *session;	/* Pointer to session structure */
	char eolmode;		/* Control translation of enter key */
};

extern int Refuse_echo;
extern int Tn_cr_mode;

/* In telnet.c: */
int tel_connect(struct session *sp,struct sockaddr *fsocket,int len);
void tel_output(int unused,void *p1,void *p2);
void tnrecv(struct telnet *tn);
void doopt(struct telnet *tn,int opt);
void dontopt(struct telnet *tn,int opt);
void willopt(struct telnet *tn,int opt);
void wontopt(struct telnet *tn,int opt);
void answer(struct telnet *tn,int r1,int r2);

/* In ttylink.c: */
void ttylhandle(int s,void *unused,void *p);

#endif	/* _TELNET_H */
