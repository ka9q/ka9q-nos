#ifndef	_FILES_H
#define	_FILES_H

/* Permission bits in FTPUSERS file */
#define	FTP_READ	1	/* Read files */
#define	FTP_CREATE	2	/* Create new files */
#define	FTP_WRITE	4	/* Overwrite or delete existing files */
#define AX25_CMD	8	/* AX.25 gateway operation allowed */
#define TELNET_CMD	16	/* Telnet gateway operation allowed */
#define NETROM_CMD	32	/* NET/ROM gateway operation allowed */
#define SYSOP_CMD	64	/* Remote sysop access allowed */
#define EXCLUDED_CMD	128	/* This user is banned from the BBS */
#define PPP_ACCESS_PRIV	0x0100	/* Priv bit for PPP connection */
#define PPP_PWD_LOOKUP	0x0200	/* Priv bit for peerID/pass lookup */


/* External definitions for configuration-dependent file names set in
 * files.c
 */
extern char *Startup;	/* Initialization file */
extern char *Userfile;	/* Authorized FTP users and passwords */
extern char *Maillog;	/* mail log */
extern char *Mailspool;	/* Incoming mail */
extern char *Mailqdir;	/* Outgoing mail spool */
extern char *Mailqueue;	/* Outgoing mail work files */
extern char *Routeqdir;	/* queue for router */
extern char *Alias;	/* the alias file */
extern char *Dfile;	/* Domain cache */
extern char *Fdir;	/* Finger info directory */
extern char *Arealist;		/* List of message areas */
extern char *Helpdir;		/* Mailbox help file directory */
extern char *Rewritefile;	/* Address rewrite file */
extern char *Newsdir;		/* News messages and NNTP data */
extern char *Popusers;		/* POP2 user and passwd file */
extern char *Signature;		/* Mail signature file directory */
extern char *Forwardfile;	/* Mail forwarding file */
extern char *Historyfile;	/* Message ID history file */

void initroot(char *root);
char *rootdircat(char *filename);
int userlogin(char *name,char *pass,char **path,int len,int *pwdignore);
char *userlookup(char *username, char **password, char **directory,
			int *permission, int32 *ip_address);
void usercvt(void);

#endif	/* _FILES_H */
