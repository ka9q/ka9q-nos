#ifndef	_FTPCLI_H
#define	_FTPCLI_H

#include <stdio.h>

#ifndef	_FTP_H
#include "ftp.h"
#endif

#ifndef _SESSION_H
#include "session.h"
#endif

#define	LINELEN	256		/* Length of user command buffer */

/* Per-session FTP client control block */
struct ftpcli {
	FILE *control;
	FILE *data;

	char state;
#define	COMMAND_STATE	0	/* Awaiting user command */
#define	SENDING_STATE	1	/* Sending data to user */
#define	RECEIVING_STATE	2	/* Storing data from user */

	uint16 verbose;		/* Transfer verbosity level */
	int batch;		/* Command batching flag */
	int abort;		/* Aborted transfer flag */
	int update;		/* Compare with MD5 during mput/mget */
	char type;		/* Transfer type */
	char typesent;		/* Last type command sent to server */
	int logbsize;		/* Logical byte size for logical type */
	FILE *fp;		/* File descriptor being transferred */

	char buf[LINELEN];	/* Command buffer */
	char line[LINELEN];	/* Last response from server */
	struct session *session;
};
#endif	/* _FTPCLI_H */
