/* Standard I/O routines with socket support
 * Replaces those in Borland C++ library
 * Copyright 1992 Phil Karn, KA9Q
 */
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#define __IN_OPEN	1	/* Less stringent open() proto in io.h */
#include <io.h>
#include "global.h"
#include "stdio.h"
#include "mbuf.h"
#include "proc.h"
#include "usock.h"
#include "socket.h"
#include "display.h"
#include "asy.h"

#define	_CREAT(a,b)	_creat((a),(b))
#define _OPEN(a,b)	_open((a),(b))
#define	_CLOSE(a)	_close((a))
#define	_READ(a,b,c)	_read((a),(b),(c))
#define	_WRITE(a,b,c)	_write((a),(b),(c))
#define	_LSEEK(a,b,c)	_lseek((a),(b),(c))
#define	_DUP(a)		dup((a))

long _lseek(int fd,long offset,int whence);

static void _fclose(FILE *fp);
static struct mbuf *_fillbuf(FILE *fp,int cnt);
static FILE *_fcreat(void);

FILE *_Files;
int _clrtmp = 1;
extern unsigned *Refcnt;

/* Open a file and associate it with a (possibly specified) stream */
FILE *
freopen(
char *filename,
char *mode,
FILE *fp
){
	int modef;
	int textmode = 0;
	int create = 0;
	int append = 0;
	int fd;
	struct stat statbuf;

	if(strchr(mode,'r') != NULL){
		modef = O_RDONLY;
	} else if(strchr(mode,'w') != NULL){
		create = 1;
		modef = O_WRONLY;
	} else if(strchr(mode,'a') != NULL){
		modef = O_WRONLY;
		append = 1;
		if(stat(filename,&statbuf) == -1 && errno == ENOENT)
			create = 1;	/* Doesn't exist, so create */
	} else
		return NULL;	/* No recognizable mode! */

	if(strchr(mode,'+') != NULL)
		modef = O_RDWR;	/* Update implies R/W */

	if(strchr(mode,'t') != NULL)
		textmode = 1;
	
	if(create)
		fd = _CREAT(filename,S_IREAD|S_IWRITE);
	else
		fd = _OPEN(filename,modef);
	if(fd == -1)
		return NULL;

	if(fp != NULL){
		_fclose(fp);
	} else {
		if((fp = _fcreat()) == NULL){
			_CLOSE(fd);
			if(create)
				unlink(filename);
			return NULL;
		}
	}
	fp->fd = fd;
	fp->offset = 0;
	fp->type = _FL_FILE;
	fp->bufmode = _IOFBF;
	fp->ptr = strdup(filename);
	fp->flags.ascii = textmode;
	fp->flags.append = append;
	fp->bufsize = BUFSIZ;
	seteol(fp,Eol);
	return fp;
}
/* Associate a file or socket descripter (small integer) with a stream */
FILE *
fdopen(
int handle,
char *mode
){
	FILE *fp;
	int textmode = 0;
	int append = 0;

	if(handle == -1)
		return NULL;

	if(strchr(mode,'a') != NULL)
		append = 1;

	if(strchr(mode,'t') != NULL)
		textmode = 1;
	
	if((fp = _fcreat()) == NULL)
		return NULL;

	fp->fd = handle;
	fp->bufmode = _IOFBF;
	fp->type = _fd_type(handle);
	fp->flags.ascii = textmode;
	fp->flags.append = append;

	fp->bufsize = BUFSIZ;
	/* set default eol sequence, can be overridden by user */
	switch(fp->type){
	case _FL_SOCK:
		seteol(fp,eolseq(handle));	/* Socket eol seq */
		break;
	case _FL_FILE:
		seteol(fp,Eol);	/* System end-of-line sequence */
		break;
	}
	fp->refcnt = 1;

	return fp;
}
/* Create a stream in pipe mode (whatever is written can be
 * read back). These always work in binary mode.
 */
FILE *
pipeopen(void)
{
	FILE *fp;

	if((fp = _fcreat()) == NULL)
		return NULL;

	fp->fd = -1;
	fp->type = _FL_PIPE;
	fp->bufmode = _IOFBF;
	fp->bufsize = BUFSIZ;

	strcpy(fp->eol,"\r\n");
	return fp;
}
/* Open an asynch port for direct I/O. This must have already been attached
 * as a NOS interface. All packet-mode I/O is suspended until this stream
 * is closed.
 */
FILE *
asyopen(
char *name,	/* Name of interface */
char *mode	/* Usual fopen-style mode (used only for text/binary) */
){
	FILE *fp;
	int dev;
	int textmode = 0;

	if((dev = asy_open(name)) == -1)
		return NULL;
	if((fp = _fcreat()) == NULL)
		return NULL;

	if(strchr(mode,'t') != NULL)
		textmode = 1;

	fp->fd = dev;
	fp->type = _FL_ASY;
	fp->bufmode = _IOFBF;
	fp->flags.ascii = textmode;

	fp->bufsize = BUFSIZ;
	strcpy(fp->eol,"\r\n");
	return fp;
}
/* Create a new display screen and associate it with a stream. */
FILE *
displayopen(
char *mode,
int noscrol,
int sfsize
){
	FILE *fp;
	int textmode = 0;

	if(strchr(mode,'t') != NULL)
		textmode = 1;

	if((fp = _fcreat()) == NULL)
		return NULL;

	fp->fd = -1;
	fp->type = _FL_DISPLAY;
	fp->bufmode = _IOFBF;
	fp->flags.ascii = textmode;

	fp->ptr = newdisplay(0,0,noscrol,sfsize);
	fp->bufsize = BUFSIZ;
	strcpy(fp->eol,"\r\n");
	return fp;
}


/* Read string from stdin into buf until newline, which is not retained */
char *
gets(char *s)
{
	int c;
	char *cp;

	cp = s;
	for(;;){
		if((c = getc(stdin)) == EOF)
			return NULL;

		if(c == '\n')
			break;

		if(s != NULL)
			*cp++ = c;
	}
	if(s != NULL)
		*cp = '\0';
	return s;
}

/* Read a line from a stream into a buffer, retaining newline */
char *
fgets(
char *buf,	/* User buffer */
int len,	/* Length of buffer */
FILE *fp	/* Input stream */
){
	int c;
	char *cp;

	cp = buf;
	while(len-- > 1){	/* Allow room for the terminal null */
		if((c = getc(fp)) == EOF){
			return NULL;
		}
		if(buf != NULL)
			*cp++ = c;
		if(c == '\n')
			break;
	}
	if(buf != NULL)
		*cp = '\0';
	return buf;
}

/* Do printf on a stream */
int
fprintf(FILE *fp,char *fmt,...)
{
	va_list args;
	int len;

	va_start(args,fmt);
	len = vfprintf(fp,fmt,args);
	va_end(args);
	return len;
}
/* Printf on standard output stream */
int
printf(char *fmt,...)
{
	va_list args;
	int len;

	va_start(args,fmt);
	len = vfprintf(stdout,fmt,args);
	va_end(args);
	return len;
}
/* The guts of printf, uses variable arg version of sprintf */
int
vprintf(char *fmt, va_list args)
{
	return vfprintf(stdout,fmt,args);
}

/* There's a more efficient version of vfprintf() in vfprintf.c
 * that avoids the malloc(BUFSIZ) call by calling the internal 
 * Borland __vprinter() function directly.
 */
#ifndef	__TURBOC__
/* The guts of printf, uses variable arg version of sprintf */
int
vfprintf(FILE *fp,char *fmt, va_list args)
{
	int len,cnt,withargs;
	char *buf;

	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	if(strchr(fmt,'%') == NULL){
		/* Common case optimization: no args, so we don't
		 * need vsprintf()
		 */
		withargs = 0;
		buf = fmt;
	} else {
		/* Use a default value that is hopefully longer than the
		 * biggest output string we'll ever print (!)
		 */
		withargs = 1;
		buf = mallocw(BUFSIZ);
		vsprintf(buf,fmt,args);
	}
	len = strlen(buf);
	cnt = fwrite(buf,1,len,fp);
	if(cnt != len)
		cnt = -1;
	if(withargs)
		free(buf);
	return cnt;
}
#endif	/* __TURBOC__ */
/* put a char to a stream */ 
int
fputc(int c,FILE *fp)
{
	int nbytes;
	struct mbuf *bp;
	int eol;

	if(c == '\n' && fp->flags.ascii){
		nbytes = strlen(fp->eol);
		eol = 1;
	} else {
		nbytes = 1;
		eol = 0;
	}
	bp = fp->obuf;
	if(bp != NULL && bp->size - bp->cnt < nbytes && fflush(fp) == EOF)
		return EOF;
	if(fp->obuf == NULL)
		fp->obuf = ambufw(max(nbytes,fp->bufsize));

	bp = fp->obuf;
	if(eol)
		memcpy(&bp->data[bp->cnt],fp->eol,nbytes);
	else
		bp->data[bp->cnt] = c;
	bp->cnt += nbytes;

	if(bp->cnt == bp->size || (fp->bufmode == _IONBF)
	 || ((fp->bufmode == _IOLBF) && eol)){
		if(fflush(fp) == EOF)
			return EOF;
	}
	return c;
}
/* put a string to a stream */
int
fputs(char *buf,FILE *fp)
{
	int cnt,len;

	len = strlen(buf);
	cnt = fwrite(buf,1,len,fp);
	if(cnt != len)
		return EOF;
	return buf[len-1];
}

/* Put a string to standard output */
int
puts(char *s)
{
	if(fputs(s,stdout) == EOF)
		return EOF;
	putchar('\n');
	return 1;
}

/* Read a character from the stream */
int
fgetc(FILE *fp)
{
	int c;

	if(fp == NULL || fp->cookie != _COOKIE)
		return EOF;
	c = _fgetc(fp);
	if(!fp->flags.ascii || c == EOF || c != fp->eol[0])
		return c;
	/* First char of newline sequence encountered */
	if(fp->eol[1] == '\0')
		return '\n';	/* Translate 1-char eol sequence */
	/* Try to read next input character */
	if((c = _fgetc(fp)) == EOF)
		return fp->eol[0];	/* Got a better idea? */
	if(c == fp->eol[1]){
		/* Translate two-character eol sequence into newline */
		return '\n';
	} else {
		/* CR-NUL sequence on Internet -> bare CR (kludge?) */
		if(c != '\0')
			ungetc(c,fp);
		/* Otherwise return first char unchanged */
		return fp->eol[0];
	}
}
/* Read a character from a stream without newline processing */
int
_fgetc(FILE *fp)
{
	struct mbuf *bp;

	if(fp == NULL || fp->cookie != _COOKIE)
		return EOF;
	fflush(fp);
	if((bp = fp->ibuf) == NULL || bp->cnt == 0)
		if(_fillbuf(fp,1) == NULL)
			return EOF;
	if(fp->type == _FL_PIPE)
		ksignal(&fp->obuf,1);
	return PULLCHAR(&fp->ibuf);
}

/* Flush output on a stream. All actual output is done here. */
int
fflush(FILE *fp)
{
	struct mbuf *bp;
	int cnt;

	if(fp == NULL || fp->cookie != _COOKIE){
		flushall();
		return 0;
	}
	if(fp->obuf == NULL)
		return 0;	/* Nothing to do */

	bp = fp->obuf;
	fp->obuf = NULL;
	switch(fp->type){
	case _FL_ASY:
		while(bp != NULL){
			asy_write(fp->fd,bp->data,bp->cnt);
			bp = free_mbuf(&bp);
		}
		return 0;		
	case _FL_PIPE:
		append(&fp->ibuf,&bp);
		ksignal(&fp->ibuf,1);
		while(len_p(fp->ibuf) >= BUFSIZ)
			kwait(&fp->obuf);	/* Hold at hiwat mark */	
		return 0;
	case _FL_SOCK:
		return send_mbuf(fp->fd,&bp,0,NULL,0);
	case _FL_FILE:
		do {
			if(fp->flags.append)
				_LSEEK(fp->fd,0L,SEEK_END);
			else
				_LSEEK(fp->fd,fp->offset,SEEK_SET);
			cnt = _WRITE(fp->fd,bp->data,bp->cnt);
			if(cnt > 0)
				fp->offset += cnt;
			if(cnt != bp->cnt){
				fp->flags.err = 1;
				free_p(&bp);
				return EOF;
			}
			bp = free_mbuf(&bp);
		} while(bp != NULL);
		return 0;
	case _FL_DISPLAY:
		do {
			displaywrite(fp->ptr,bp->data,bp->cnt);
			bp = free_mbuf(&bp);
		} while(bp != NULL);
		return 0;
	}
	return 0;	/* Can't happen */
}

/* Set the end-of-line sequence on a stream */
int
seteol(FILE *fp,char *seq)
{
	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	if(seq != NULL)
		strncpy(fp->eol,seq,sizeof(fp->eol));
	else
		*fp->eol = '\0';
	return 0;
}
/* Enable/disable eol translation, return previous state */
int
fmode(FILE *fp,int mode)
{
	int prev;

	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	fflush(fp);
	prev = fp->flags.ascii;
	fp->flags.ascii = mode;
	return prev;
}
/* Control blocking behavior for fread on network, pipe and asy streams */
int
fblock(FILE *fp,int mode)
{
	int prev;

	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	prev = fp->flags.partread;
	fp->flags.partread = mode;
	return prev;
}

int
fclose(FILE *fp)
{
	if(fp == NULL || fp->cookie != _COOKIE){
		return -1;
	}
	if(--fp->refcnt != 0)
		return 0;	/* Others are still using it */
	_fclose(fp);
	if(fp->prev != NULL)
		fp->prev->next = fp->next;
	else
		_Files = fp->next;

	if(fp->next != NULL)
		fp->next->prev = fp->prev;
	free(fp);
	return 0;
}
int
fseek(
FILE *fp,
long offset,
int whence
){
	struct stat statbuf;

	if(fp == NULL || fp->cookie != _COOKIE || fp->type != _FL_FILE){
		errno = EINVAL;
		return -1;
	}
	/* Optimize for do-nothing seek */ 
#ifdef	notdef
	if(whence == SEEK_SET && fp->offset == offset)
		return 0;
#endif
	fflush(fp);	/* Flush output buffer */
	/* On relative seeks, adjust for data in input buffer */
	switch(whence){
	case SEEK_SET:
		fp->offset = offset;	/* Absolute seek */
		break;
	case SEEK_CUR:
		/* Relative seek, adjusting for buffered data */
		fp->offset += offset - len_p(fp->ibuf);
		break;
	case SEEK_END:
		/* Find out how big the file currently is */
		if(fstat(fp->fd,&statbuf) == -1)
			return -1;	/* "Can't happen" */
		fp->offset = statbuf.st_size + offset;
		break;
	}
	/* Toss input buffer */
	free_p(&fp->ibuf);
	fp->ibuf = NULL;
	fp->flags.eof = 0;
	return 0;
}
long
ftell(FILE *fp)
{
	if(fp == NULL || fp->cookie != _COOKIE || fp->type != _FL_FILE)
		return -1;
	fflush(fp);
	return fp->offset - len_p(fp->ibuf);
}

int
ungetc(int c,FILE *fp)
{
	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;

	if(c == '\n' && fp->flags.ascii){
		pushdown(&fp->ibuf,fp->eol,strlen(fp->eol));
	} else {
		pushdown(&fp->ibuf,&c,1);
	}
	return c;
}
size_t
fwrite(
void *ptr,
size_t size,
size_t n,
FILE *fp
){
	struct mbuf *bp;
	uint8 *icp,*ocp;
	size_t bytes;
	size_t cnt;
	size_t asize;
	int room;
	int newlines = 0;
	int eollen = 1;
	int doflush = 0;
	
	if(fp == NULL || fp->cookie != _COOKIE || size == 0)
		return 0;
	icp = ptr;
	if(n == 1)	/* Avoid multiply in common case when n==1 */
		bytes = size;
	else
		bytes = size*n;

	/* Optimization for large binary file writes */
	if(fp->type == _FL_FILE && !fp->flags.ascii && bytes >= fp->bufsize){
		fflush(fp);
		if(fp->flags.append)
			_LSEEK(fp->fd,0L,SEEK_END);
		else
			_LSEEK(fp->fd,fp->offset,SEEK_SET);
		cnt = _WRITE(fp->fd,icp,bytes);
		if(cnt > 0)
			fp->offset += cnt;
		if(cnt != bytes)
			return cnt/size;
		return n;
	}
	if(fp->flags.ascii){
		/* Count the newlines in the input buffer */
		newlines = memcnt(ptr,'\n',bytes);
		if(newlines != 0){
			eollen = strlen(fp->eol);
			if(fp->bufmode == _IOLBF)
				doflush = 1;
		}
	}
	while(bytes != 0){
		bp = fp->obuf;
		if(bp != NULL && bp->cnt + eollen > bp->size){
			/* Current obuf is full; flush it */
			if(fflush(fp) == EOF)
				return (bytes - n*size)/size;
		}
		if((bp = fp->obuf) == NULL){
			/* Allocate a new output buffer. The size is the
			 * larger of the buffer size or the amount of data
			 * we have to write (including any expanded newlines)
			 */
			asize = bytes+(eollen-1)*newlines;
			asize = max(fp->bufsize,asize);
			bp = fp->obuf = ambufw(asize);
		}
		if(fp->flags.ascii && newlines != 0){
			/* Copy text to buffer, expanding newlines */
			ocp = bp->data + bp->cnt;
			room = bp->size - bp->cnt;
			for(;room >= eollen && bytes != 0;icp++,bytes--){
				if(*icp == '\n'){
					memcpy(ocp,fp->eol,eollen);
					ocp += eollen;
					room -= eollen;
					newlines--;
				} else {
					*ocp++ = *icp;
					room--;
				}
			}
			bp->cnt = ocp - bp->data;
		} else {
			/* Simply copy binary data to buffer */
			cnt = min(bp->size - bp->cnt,bytes);
			memcpy(bp->data+bp->cnt,icp,cnt);
			bp->cnt += cnt;
			icp += cnt;
			bytes -= cnt;
		}
	}
	/* The final flush. Flush if the stream is unbuffered,
	 * the output buffer is full, or the stream is line buffered
	 * and we've written at least one newline (not necessarily the
	 * last character)
	 */
	if(fp->bufmode == _IONBF || bp->cnt == bp->size || doflush){
		if(fflush(fp) == EOF)
			return (bytes - n*size)/size;
	}
	return n;
}
static struct mbuf *
_fillbuf(FILE *fp,int cnt)
{
	struct mbuf *bp;
	int i;

	if(fp->ibuf != NULL)
		return fp->ibuf;	/* Stuff already in the input buffer */

	switch(fp->type){
	case _FL_ASY:
		fp->ibuf = ambufw(BUFSIZ);
		i = asy_read(fp->fd,fp->ibuf->data,BUFSIZ);
		if(i < 0)
			return NULL;
		fp->ibuf->cnt = i;
		return fp->ibuf;
	case _FL_PIPE:
		while(fp->ibuf == NULL)
			if((errno = kwait(&fp->ibuf)) != 0)	/* Wait for something */
				return NULL;
		return fp->ibuf;
	case _FL_SOCK:
		/* Always grab everything available from a socket */
		if(recv_mbuf(fp->fd,&fp->ibuf,0,NULL,0) <= 0
		 && errno != EALARM){
			fp->flags.eof = 1;
		}
		return fp->ibuf;
	case _FL_FILE:
		/* Read from file */
		cnt = max(fp->bufsize,cnt);
		bp = ambufw(cnt);		
		_LSEEK(fp->fd,fp->offset,SEEK_SET);
		cnt = _READ(fp->fd,bp->data,cnt);
		if(cnt < 0)
			fp->flags.err = 1;
		if(cnt == 0)
			fp->flags.eof = 1;
		if(cnt <= 0){
			free_p(&bp);	/* Nothing successfully read */
			return NULL;
		}
		fp->offset += cnt;	/* Update pointer */
		/* Buffer successfully read, store it */
		bp->cnt = cnt;
		fp->ibuf = bp;
		return bp;
	case _FL_DISPLAY:	/* Displays are write-only */
		return NULL;
	}
	return NULL;	/* Can't happen */
}
size_t
fread(
void *ptr,
size_t size,
size_t n,
FILE *fp
){
	struct mbuf *bp;
	size_t bytes;
	size_t cnt;
	int c;
	size_t tot = 0;
	uint8 *ocp;
	uint8 *cp;

	if(fp == NULL || fp->cookie != _COOKIE || size == 0)
		return 0;
	fflush(fp);
	bytes = n*size;

	ocp = ptr;
	while(bytes != 0){
		/* Optimization for large binary file reads */
		if(fp->ibuf == NULL
		 && fp->type == _FL_FILE && !fp->flags.ascii
		 && bytes >= BUFSIZ){
			_LSEEK(fp->fd,fp->offset,SEEK_SET);
			tot = _READ(fp->fd,ocp,bytes);
			if(tot > 0)
				fp->offset += tot;
			if(tot != bytes)
				return tot/size;
			return n;
		}
		/* Replenish input buffer if necessary */
		if(fp->ibuf == NULL){
			if(tot != 0 && fp->flags.partread){
				/* Would block for more data */
				return tot/size;	
			}
		 	if(_fillbuf(fp,bytes) == NULL){
				/* eof or error */
				return tot/size;
			}
		}
		/* In this pass, read the lesser of the buffer size,
		 * the requested amount, or the amount up to the next
		 * eol sequence (if ascii mode)
		 */
		bp = fp->ibuf;
		cnt = min(bp->cnt,bytes);
		if(fp->flags.ascii
		 && (cp = memchr(bp->data,fp->eol[0],cnt)) != NULL)
			cnt = min(cnt,cp - bp->data);
		if(cnt != 0){
			cnt = pullup(&fp->ibuf,ocp,cnt);
			ocp += cnt;
			tot += cnt;
			bytes -= cnt;
		} else {
			/* Hit a eol sequence, use fgetc to translate */
			if((c = fgetc(fp)) == EOF)
				return tot/size;

			*ocp++ = c;
			tot++;
			bytes--;
		}
	}
	if(fp->type == _FL_PIPE)
		ksignal(&fp->obuf,1);
	return n;
}
void
perror(const char *s)
{
	fprintf(stderr,"%s: errno %d",s,errno);
	if(errno < sys_nerr)
		fprintf(stderr,": %s\n",sys_errlist[errno]);
	else if(EMIN <= errno && errno <= EMAX)
		fprintf(stderr,": %s\n",Sock_errlist[errno-EMIN]);
	else
		fprintf(stderr,"\n");
}
int
setvbuf(
FILE *fp,
char *buf,	/* Ignored; we alloc our own */
int type,
int size
){
	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	fflush(fp);
	if(size == 0)
		type = _IONBF;
	switch(type){
	case _IOFBF:
		fp->bufsize = size;
		break;
	case _IOLBF:
		fp->bufsize = size;
		break;
	case _IONBF:
		fp->bufsize = 1;
		break;
	default:
		return -1;	/* Invalid */
	}
	fp->bufmode = type;
	return 0;
}
void
setbuf(FILE *fp,char *buf)
{
	if(buf == NULL)
		setvbuf(fp,NULL,_IONBF,0);
	else
		setvbuf(fp,buf,_IOFBF,BUFSIZ);
}
FILE *
tmpfile(void)
{
	static int num;
	struct stat statbuf;
	FILE *fp;
	char *fname;
	char *tmpdir;
	char *cp;

	/* Determine directory to use. First look for $TMP environment
	 * variable, then use the compiled-in-default, then use the
	 * current directory.
	 */
	if((cp = getenv("TMP")) != NULL
	 && stat(cp,&statbuf) == 0 && (statbuf.st_mode & S_IFDIR)){
		fname = malloc(strlen(cp) + 11);
		tmpdir = malloc(strlen(cp) + 2);
		strcpy(tmpdir,cp);
		strcat(tmpdir,"/");
	} else if(stat(Tmpdir,&statbuf) == 0 && (statbuf.st_mode & S_IFDIR)){
		fname = malloc(strlen(Tmpdir) + 11);
		tmpdir = malloc(strlen(Tmpdir) + 2);
		strcpy(tmpdir,Tmpdir);
		strcat(tmpdir,"/");
	} else {
		fname = malloc(10);
		tmpdir = strdup("");
	}
	for(;;){
		sprintf(fname,"%stemp.%03d",tmpdir,num);
		if(stat(fname,&statbuf) == -1 && errno == ENOENT)
			break;
		num++;
	}
	free(tmpdir);
	fp = fopen(fname,"w+b");
	free(fname);
	if(fp != NULL)
		fp->flags.tmp = 1;
	return fp;
}
/* Do everything to close a stream except freeing the descriptor
 * The reference count is left unchanged, and the descriptor is still
 * on the list
 */
static void
_fclose(FILE *fp)
{
	struct stat statbuf;
	char *buf;
	long i;
	int n;

	if(fp == NULL || fp->cookie != _COOKIE)
		return;
	if(_clrtmp && fp->flags.tmp){
		/* Wipe temp file for security */
		rewind(fp);
		fstat(fileno(fp),&statbuf);
		buf = malloc(BUFSIZ);
		memset(buf,0,BUFSIZ);
		i = statbuf.st_size;
		while(i > 0){
			n = fwrite(buf,1,min(i,BUFSIZ),fp);
			kwait(NULL);
			if(n < BUFSIZ)
				break;
			i -= n;
		}
		free(buf);
	}
	fflush(fp);
	switch(fp->type){
	case _FL_ASY:
		asy_close(fp->fd);
		break;
	case _FL_SOCK:
		close_s(fp->fd);
		break;
	case _FL_FILE:
		_CLOSE(fp->fd);
		fp->offset = 0;
		break;
	case _FL_DISPLAY:
		closedisplay(fp->ptr);
		fp->ptr = NULL;
		break;
	}
	free_p(&fp->obuf);	/* Should be NULL anyway */
	fp->obuf = NULL;
	free_p(&fp->ibuf);
	fp->ibuf = NULL;
	if(fp->flags.tmp)
		unlink(fp->ptr);
	free(fp->ptr);
	fp->ptr = NULL;
	fp->flags.err = fp->flags.eof = fp->flags.ascii = 0;
	fp->flags.append = fp->flags.tmp = fp->flags.partread = 0;
	fp->fd = -1;
}
/* allocate a new file pointer structure, init a few fields and put on list */
static FILE *
_fcreat(void)
{
	FILE *fp;

	if((fp = (FILE *)calloc(1,sizeof(FILE))) == NULL)
		return NULL;

	fp->cookie = _COOKIE;
	fp->refcnt = 1;
	fp->next = _Files;
	_Files = fp;
	if(fp->next != NULL)
		fp->next->prev = fp;
	return fp;
}

int
read(int fd,void *buf,unsigned cnt)
{
	int type = _fd_type(fd);

	if(fd < 0){
		errno = EINVAL;
		return -1;
	}
	switch(type){
	case _FL_FILE:
		return _READ(fd,buf,cnt);
	case _FL_SOCK:
		return recv(fd,buf,cnt,0);
	case _FL_ASY:
		return asy_read(fd,buf,cnt);
	default:
		errno = EINVAL;
		return -1;
	}
}
int
write(int fd,const void *buf,unsigned cnt)
{
	int type = _fd_type(fd);

	if(fd < 0){
		errno = EINVAL;
		return -1;
	}
	switch(type){
	case _FL_FILE:
		return _WRITE(fd,buf,cnt);
	case _FL_SOCK:
		return send(fd,buf,cnt,0);
	case _FL_ASY:
		return asy_write(fd,buf,cnt);
	default:
		errno = EINVAL;
		return -1;
	}
}

/* This entry point is provided for applications that want to call open()
 * directly, instead of using fopen()
 */
int
open(const char *file,int mode)
{
	return _open(file,mode);
}

int
close(int fd)
{
	int type = _fd_type(fd);

	if(fd < 0){
		errno = EINVAL;
		return -1;
	}
	switch(type){
	case _FL_FILE:
		return _CLOSE(fd);
	case _FL_SOCK:
		return close_s(fd);
	case _FL_ASY:
		return asy_close(fd);
	default:
		errno = EINVAL;
		return -1;
	}
}

void
fcloseall(void)
{
	FILE *fp,*fpnext;

	flushall();
	for(fp = _Files;fp != NULL;fp=fpnext){
		fpnext = fp->next;
		fclose(fp);
	}
}
void
flushall(void)
{
	FILE *fp;

	for(fp = _Files;fp != NULL;fp=fp->next){
		fflush(fp);
	}
}
FILE *
fdup(FILE *fp)
{
	FILE *nfp;

	if(fp == NULL || fp->cookie != _COOKIE)
		return NULL;	/* Invalid arg */
	switch(fp->type){
	case _FL_FILE:
		/* Allocate new file pointer structure so each can
		 * have its own read/write pointer and buffering
		 */
		if((nfp = _fcreat()) == NULL)
			return NULL;
		nfp->fd = _DUP(fp->fd);
		nfp->offset = fp->offset;
		nfp->type = fp->type;
		nfp->bufmode = fp->bufmode;
		nfp->flags = fp->flags;
		strcpy(nfp->eol,fp->eol);
		nfp->bufsize = fp->bufsize;
		nfp->ptr = strdup(fp->ptr);
		fp = nfp;
		break;
	default:	/* These just share the same file pointer */
		fp->refcnt++;
		break;
	}
	return fp;
}
char *
fpname(FILE *fp)
{
	if(fp == NULL || fp->cookie != _COOKIE)
		return NULL;
	if(fp->type == _FL_FILE)
		return fp->ptr;
	return NULL;
}

void
exit(int n)
{
	fcloseall();
	_exit(n);
}

int
dofiles(int argc,char *argv[],void *p)
{
	FILE *fp;
	int i;

	printf("fp       fd   ref  eol   type mod buf  flags\n");
	for(fp = _Files;fp != NULL;fp = fp->next){
		printf("%p ",fp);
		if(fp->fd != -1)
			printf("%-4d",fp->fd);
		else
			printf("    ");
		printf(" %-3d ",fp->refcnt);
		for(i=0;i<EOL_LEN-1;i++){
			if(fp->eol[i] != '\0')
				printf(" %02x",fp->eol[i]);
			else
				printf("   ");
		}
		switch(fp->type){
		case _FL_SOCK:
			printf(" sock");
			break;
		case _FL_FILE:
			printf(" file");
			break;
		case _FL_DISPLAY:
			printf(" disp");
			break;
		case _FL_PIPE:
			printf(" pipe");
			break;
		case _FL_ASY:
			printf(" asy ");
		}
		printf("%4s",fp->flags.ascii ? " txt" : " bin");
		switch(fp->bufmode){
		case _IONBF:
			printf(" none");
			break;
		case _IOLBF:
			printf(" line");
			break;
		case _IOFBF:
			printf(" full");
			break;
		}
		if(fp->flags.eof)
			printf(" EOF");
		if(fp->flags.err)
			printf(" ERR");
		if(fp->flags.append)
			printf(" APND");
		if(fp->flags.tmp)
			printf(" TMP");
		if(fp->type == _FL_FILE && fp->ptr != NULL)
			printf(" (%s seek=%lu)",(char *)fp->ptr,ftell(fp));
		putchar('\n');
	}
	return 0;
}
