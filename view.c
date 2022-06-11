/* Random access file viewer. PC specific */

#include <stdio.h>
#include <conio.h>
#include "global.h"
#include "session.h"
#include "tty.h"
#include "commands.h"
#include "socket.h"

#include <dos.h>

static long lineseek(FILE *fp,long offset,int nlines,int width);
static int ctlproc(int c);

int
doview(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	FILE *fp;

	if((fp = fopen(argv[1],READ_TEXT)) == NULL){
		printf("Can't read %s\n",argv[1]);
		return 1;
	}
	newproc("view",512,view,0,(void *)fp,strdup(Cmdline),0);
	return 0;	
}
/* Random-access file display program. Used both to read local
 * files with the "view" command, and by the FTP client to view
 * directory listings, temporary copies of read files, etc.
 *
 */
void
view(s,p1,p2)
int s;		/* If non-zero, poll interval for a changing file */
void *p1;	/* Open file pointer to read from */
void *p2;	/* If non-null, name to give to session. We free it */
{
	struct session *sp;
	FILE *fp;
	char *name;
	int c;
	long offset = 0;
	int row,col;
	int cols;
	int rows;
	int32 polldelay = 0;
	struct text_info text_info;

	gettextinfo(&text_info);
	cols = text_info.screenwidth;
	rows = text_info.screenheight-1;	/* Allow for status line */

	fp = (FILE *)p1;
	if(p2 != NULL)
		name = (char *)p2;
	else
		name = fpname(fp);

	if((sp = newsession(name,VIEW,1)) == NULL)
		return;

	if(p2 != NULL)
		free(name);

	if(s != 0)
		polldelay = s;
	sp->ctlproc = ctlproc;
	/* Put tty into raw mode so single-char responses will work */
	sp->ttystate.echo = sp->ttystate.edit = 0;
	for(;;){
		fseek(fp,offset,SEEK_SET);
		putchar(FF);	/* Clear screen */
		/* Display a screen's worth of data, keeping track of
		 * cursor location so we know when the screen is full
		 */
		col = row = 0;
		while((c = getc(fp)),c != EOF){
			switch(c){
			case '\n':
				row++;
				col = 0;
				break;
			case '\t':
				if(col < cols - 8)
					col = (col + 8) & ~7;
				break;
			default:
				col++;
				break;
			}
			if(col >= cols){
				/* Virtual newline caused by wraparound */
				col = 0;
				row++;
			}
			if(row >= rows)
				break;	/* Screen now full */
			putchar(c);
		}
#ifdef	notdef
		if(feof(fp) && offset != 0){
			/* Hit end of file. Back up proper number of
			 * lines and try again.
			 */
			offset = lineseek(fp,offset,row-rows,cols);
			continue;
		}
#endif
		fflush(stdout);
		/* If we hit the end of the file and the file may be
		 * growing, then set an alarm to time out the getchar()
		 */
		do {
			if(feof(fp) && polldelay != 0){
				kalarm(polldelay);
			}
			c = getchar();	/* Wait for user keystroke */
			kalarm(0L);	/* Cancel alarm */
			if(c != -1 || errno != EALARM)
				break;	/* User hit key */
			/* Alarm timeout; see if more data arrived by
			 * clearing the EOF flag, trying to read
			 * another byte, and then testing EOF again
			 */
			clearerr(fp);
			(void)getc(fp);
			c = ' ';	/* Simulate a no-op keypress */
		} while(feof(fp));
		switch(c){
		case 'h':	/* Home */
		case 'H':
		case '<':	/* For emacs users */
			offset = 0;
			break;
		case 'e':	/* End */
		case '>':	/* For emacs users */
			fseek(fp,0L,SEEK_END);
			offset = lineseek(fp,ftell(fp),-rows,cols);
			break;
		case CTLD:	/* Down one half screen (for VI users) */
			if(!feof(fp))
				offset = lineseek(fp,offset,rows/2,cols);
			break;
		case CTLU:	/* Up one half screen (for VI users) */
			offset = lineseek(fp,offset,-rows/2,cols);
			break;
		case 'd':	/* down line */
		case CTLN:	/* For emacs users */
		case 'j':	/* For vi users */
			if(!feof(fp))
				offset = lineseek(fp,offset,1,cols);
			break;
		case 'D':	/* Down page */
		case CTLV:	/* For emacs users */
			if(!feof(fp))
				offset = lineseek(fp,offset,rows,cols);
			break;
		case 'u':	/* up line */
		case CTLP:	/* for emacs users */
		case 'k':	/* for vi users */
			offset = lineseek(fp,offset,-1,cols);
			break;
		case 'U':	/* Up page */
		case 'v':	/* for emacs users */
			offset = lineseek(fp,offset,-rows,cols);
			break;
		case CTLC:
		case 'q':
		case 'Q':
		case ESC:
			goto done;
		default:
			break;	/* Redisplay screen */
		}
	}
done:	fclose(fp);
	freesession(sp);
}
/* Given a starting offset into an open file stream, scan forwards
 * or backwards the specified number of lines and return a pointer to the
 * new offset.
 */
static long
lineseek(fp,start,nlines,width)
FILE *fp;	/* Open file stream */
long start;	/* Offset to start searching backwards from */
int nlines;	/* Number of lines to move forward (+) or back (-) */
int width;	/* Screen width (max line size) */
{
	long offset;
	long *pointers;
	int col = 0;
	int c;
	int newlines = 0;

	if(nlines == 0)
		return start;	/* Nothing to do */

	if(nlines > 0){		/* Look forward requested # of lines */
		fseek(fp,start,SEEK_SET);
		col = 0;
		while((c = getc(fp)),c != EOF){
			switch(c){
			case '\n':
				newlines++;
				col = 0;
				break;
			case '\t':
				if(col < width - 8)
					col = (col + 8) & ~7;
				break;
			default:
				col++;
				break;
			}
			if(col >= width){
				/* Virtual newline caused by wraparound */
				col = 0;
				newlines++;
			}
			if(newlines >= nlines)
				break;	/* Found requested count */
		}
		return ftell(fp);	/* Could be EOF */
	}
	/* Backwards scan (the hardest)
	 * Start back up at most (width + 2) chars/line from the start.
	 * This handles full lines followed by expanded newline
	 * sequences
	 */
	nlines = -nlines;
	offset = (width + 2)*(nlines + 1);
	if(offset > start)
		offset = 0;	/* Go to the start of the file */
	else
		offset = start - offset;
	fseek(fp,offset,SEEK_SET);

	/* Keep a circular list of the last 'nlines' worth of offsets to
	 * each line, starting with the first
	 */
	pointers = (int32 *)calloc(sizeof(long),nlines);
	pointers[newlines++ % nlines] = offset;

	/* Now count newlines up but not including the original
	 * starting point
	 */
	col = 0;
	for(;;){
		c = getc(fp);
		switch(c){
		case EOF:
			goto done;
		case '\n':
			col = 0;
			offset = ftell(fp);
			if(offset >= start)
				goto done;
			pointers[newlines++ % nlines] = offset;
			break;
		case '\t':
			if(col < width - 8)
				col = (col + 8) & ~7;
			break;
		default:
			col++;
			break;
		}
		if(col >= width){
			/* Virtual newline caused by wraparound */
			col = 0;
			offset = ftell(fp);
			if(offset >= start)
				goto done;
			pointers[newlines++ % nlines] = offset;
		}
	}
	done:;
	if(newlines >= nlines){
		/* Select offset pointer nlines back */
		offset = pointers[newlines % nlines];
	} else {
		/* The specified number of newlines wasn't seen, so
		 * go to the start of the file
		 */
		offset = 0;
	}
	free(pointers);
	return offset;
}

/* Handle special keystrokes */
static int
ctlproc(c)
int c;
{
	switch(c){
	case 256 + 71:	/* HOME */
		putc('h',Current->input);
		break;
	case 256 + 72:	/* Cursor up */
		putc('u',Current->input);
		break;
	case 256 + 73:	/* Page up */
		putc('U',Current->input);
		break;
	case 256 + 79:	/* End */
		putc('e',Current->input);
		break;
	case 256 + 80:	/* Cursor down */
		putc('d',Current->input);
		break;
	case 256 + 81:	/* Page down */
		putc('D',Current->input);
		break;
	default:
		return c;
	}
	fflush(Current->input);
	return 0;
}

