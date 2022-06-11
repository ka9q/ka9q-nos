/* Routines common to both the FTP client and server
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "proc.h"
#include "ftp.h"
#include "cmdparse.h"
#include "md5.h"

#define	MD5BLOCK	64	/* Preferred MD5 block size */

/* Send a file (opened by caller) on a network socket.
 * Normal return: count of bytes sent
 * Error return: -1
 */
long
sendfile(fp,network,mode,verb)
FILE *fp;		/* File to be sent */
FILE *network;		/* Network stream to be sent on */
enum ftp_type mode;	/* Transfer mode */
enum verb_level verb;	/* Verbosity level */
{
	long total = 0;
	long hmark = 0;
	char *buf;
	int cnt;
	char cmdbuf[50];

	if(verb >= V_STAT){
		sprintf(cmdbuf,"repeat socket %d",fileno(network));
		cmdparse(Cmds,cmdbuf,NULL);
	}
	switch(mode){
	default:
	case LOGICAL_TYPE:
	case IMAGE_TYPE:
		fmode(network,STREAM_BINARY);
		break;
	case ASCII_TYPE:
		fmode(network,STREAM_ASCII);
		break;
	}
	buf = mallocw(BUFSIZ);
	for(;;){
		if((cnt = fread(buf,1,BUFSIZ,fp)) == 0){
			break;
		}
		total += cnt;
		if(fwrite(buf,1,cnt,network) != cnt){
			total = -1;
			break;
		}
		while(verb == V_HASH && total >= hmark+1000){
			putchar('#');
			hmark += 1000;
		}
	}
	free(buf);
	if(verb == V_HASH)
		putchar('\n');
	return total;
}
/* Receive a file (opened by caller) from a network stream
 * Normal return: count of bytes received
 * Error return: -1
 */
long
recvfile(fp,network,mode,verb)
FILE *fp;
FILE *network;
enum ftp_type mode;
enum verb_level verb;
{
	int cnt;
	long total = 0;
	long hmark = 0;
	char *buf;
	char cmdbuf[50];

	if(verb >= V_STAT){
		sprintf(cmdbuf,"repeat socket %d",fileno(network));
		cmdparse(Cmds,cmdbuf,NULL);
	}
	if(fp == NULL)
		fp = stdout;	/* Default */
	switch(mode){
	default:
	case LOGICAL_TYPE:
	case IMAGE_TYPE:
		fmode(network,STREAM_BINARY);
		break;
	case ASCII_TYPE:
		fmode(network,STREAM_ASCII);
		break;
	}
	buf = mallocw(BUFSIZ);
	while((cnt = fread(buf,1,BUFSIZ,network)) != 0){
		total += cnt;
		while(verb == V_HASH && total >= hmark+1000){
			putchar('#');
			hmark += 1000;
		}
		if(fwrite(buf,1,cnt,fp) != cnt){
			total = -1;
			break;
		}
		/* Detect an abnormal close */
		if(socklen(fileno(network),0) == -1){
			total = -1;
			break;
		}
	}
	free(buf);
	if(verb == V_HASH)
		putchar('\n');
	return total;
}
/* Determine if a file appears to be binary (i.e., non-text).
 * Return 1 if binary, 0 if ascii text after rewinding the file pointer.
 *
 * Used by FTP to warn users when transferring a binary file in text mode.
 */
int
isbinary(fp)
FILE *fp;
{
	int c,i;
	int rval;

	rval = 0;
	for(i=0;i<512;i++){
		if((c = getc(fp)) == EOF)
			break;
		if(c & 0x80){
			/* High bit is set, probably not text */
			rval = 1;
			break;
		}
	}
	/* Assume it was at beginning */
	fseek(fp,0L,SEEK_SET);
	return rval;
}
/* Compute MD5 hash of local file */
int
md5hash(fp,hash,ascii)
FILE *fp;
uint8 hash[16];
int ascii;
{
	MD5_CTX md;
	char *buf;
	int len;

	MD5Init(&md);

	if(ascii){
		/* Text file, hash canonical form (newlines = crlf) */
		char *cp;
		int c;

		cp = buf = malloc(MD5BLOCK);
		len = 0;
		while((c = fgetc(fp)) != EOF){
			if(c == '\n'){
				*cp++ = '\r';
				if(++len == MD5BLOCK){
					MD5Update(&md,(unsigned char *)buf,len);
					kwait(NULL);
					cp = buf;
					len = 0;
				}
			}
			*cp++ = c;
			if(++len == MD5BLOCK){
				MD5Update(&md,(unsigned char *)buf,len);
				kwait(NULL);
				cp = buf;
				len = 0;
			}
		}
		if(len != 0)
			MD5Update(&md,(unsigned char *)buf,len);
		free(buf);
	} else {
		buf = malloc(BUFSIZ);
		while((len = fread(buf,1,BUFSIZ,fp)) != 0){
			MD5Update(&md,(unsigned char *)buf,len);
			kwait(NULL);
		}
		free(buf);
	}
	MD5Final(hash,&md);
	return 0;
}
