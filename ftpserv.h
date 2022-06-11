#ifndef	_FTPSERV_H
#define	_FTPSERV_H

#include <stdio.h>

#ifndef _SOCKADDR_H
#include "sockaddr.h"
#endif

#ifndef	_FTP_H
#include "ftp.h"
#endif

extern char *Userfile;	/* List of user names and permissions */

struct ftpserv {
	FILE *control;		/* Control stream */
	FILE *data;		/* Data stream */
	enum ftp_type type;	/* Transfer type */
	int logbsize;		/* Logical byte size for logical type */

	FILE *fp;		/* File descriptor being transferred */
	struct sockaddr_in port;/* Remote port for data connection */
	char *username;		/* Arg to USER command */
	char *path;		/* Allowable path prefix */
	int perms;		/* Permission flag bits */
				/* (See FILES.H for definitions) */
	char *cd;		/* Current directory name */
};

/* FTP commands */
enum ftp_cmd {
	USER_CMD,
	ACCT_CMD,
	PASS_CMD,
	TYPE_CMD,
	LIST_CMD,
	CWD_CMD,
	DELE_CMD,
	NAME_CMD,
	QUIT_CMD,
	RETR_CMD,
	STOR_CMD,
	PORT_CMD,
	NLST_CMD,
	PWD_CMD,
	XPWD_CMD,
	MKD_CMD,
	XMKD_CMD,
	XRMD_CMD,
	RMD_CMD,
	STRU_CMD,
	MODE_CMD,
	SYST_CMD,
	XMD5_CMD,
	XCWD_CMD,
};
int permcheck(char *path,int perms,int op,char *file);

#endif	/* _FTPSERV_H */
