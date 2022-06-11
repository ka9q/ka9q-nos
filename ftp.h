#ifndef	_FTP_H
#define	_FTP_H

/* Definitions common to both FTP servers and clients */
enum ftp_type {
	ASCII_TYPE,
	IMAGE_TYPE,
	LOGICAL_TYPE
};

/* Verbosity levels for sendfile and recvfile in ftpsubr.c */
enum verb_level {
	V_QUIET,	/* Error messages only */
	V_SHORT,	/* Final message only */
	V_NORMAL,	/* display control messages */
	V_HASH,		/* control messages, hash marks */
	V_STAT		/* Full-blown status display */
};
/* In ftpsubr.c: */
long sendfile(FILE *fp,FILE *network,enum ftp_type mode,enum verb_level verb);
long recvfile(FILE *fp,FILE *network,enum ftp_type mode,enum verb_level verb);
int isbinary(FILE *fp);
int md5hash(FILE *fp,uint8 hash[16],int ascii);

#endif	/* _FTP_H */
