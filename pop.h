/*
			filename --  POP.H

			author   --  Mike Stockett, WA7DYX

			additional hacking by Allen Gwinn, N5CKP
*/

#ifndef TRUE
#define TRUE		1
#define FALSE		0
#endif

#define BUF_LEN		128

#include <fcntl.h>

/* ---------------- common server data structures ---------------- */

/* POP server control block */

struct pop_scb {
	FILE *network;		/* Network stream for this connection */
	char	state;		/* server state */
#define 		   LSTN		0
#define 		   AUTH		1
#define 		   MBOX		2
#define 		   ITEM		3
#define			   NEXT		4
#define 		   DONE		5
	char	buf[BUF_LEN],	/* input line buffer */
		count,		/* line buffer length */
		username[64];	/* user/folder name */
	FILE	*wf;		/* work folder file pointer */
	int	folder_len,	/* number of msgs in current folder */
		msg_num;	/* current msg number */
	long	msg_len;	/* length of current msg */
	int	msg_status_size; /* size of the message status array */
	long	curpos,		/* current msg's position in file */
		folder_file_size, /* length of the current folder file, in bytes */
		nextpos;	/* next msg's position in file */
	unsigned int	folder_modified, /*  mail folder contents modified flag */
		*msg_status;	/* message status array pointer */
};

/* Response messages */

static char	count_rsp[]    = "#%d messages in this folder\n",
		error_rsp[]    = "- ERROR: %s\n",
		greeting_msg[] = "+ POP2 %s\n",
/*		length_rsp[]   = "=%ld bytes in this message\n", */
		length_rsp[]   = "=%ld characters in Message #%d\n",
		msg_line[]     = "%s\n",
		no_mail_rsp[]  = "+ No mail, sorry\n",
		no_more_rsp[]  = "=%d No more messages in this folder\n",
		signoff_msg[]  = "+ Bye, thanks for calling\n";

/* ------------------------ end of header file ---------------------------- */

