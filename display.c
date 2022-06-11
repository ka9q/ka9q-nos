/* ANSI display emulation
 *
 * This file emulates the IBM ANSI terminal display. It maintains a
 * display buffer and descriptor for each virtual display, of which there
 * can be many. All writes occur first into this display buffer, and then
 * any particular display buffer can be copied onto the real screen.
 * This allows many background tasks to run without blocking even though
 * only one task's display is actually being shown.
 *
 * This display driver is substantially faster than even the NANSI.SYS
 * loadable screen driver, particularly when large blocks are written.
 *
 * Extensions to handle displaying multiple small virtual windows should
 * be pretty straightforward.
 *
 * Copyright 1992 Phil Karn, KA9Q
 * 
 */
#include <conio.h>
#include <string.h>
#include <sys/stat.h>
#include "global.h"
#include "display.h"
#include "proc.h"

#define	DCOL	67
#define	DSIZ	(81-DCOL)

uint8 fgattr[] = { 0, 4, 2, 14, 1, 5, 3, 7 };	/* Foreground attribs */
uint8 bgattr[] = { 0, 4, 2, 6, 1, 5, 3, 7 };	/* Background attribs */

static void dclrscr(struct display *dp);
static void desc(struct display *dp,uint8 c);
static void darg(struct display *dp,uint8 c);
static void dchar(struct display *dp,uint8 c);
static void dclreol(struct display *dp,int row,int col);
static void dattrib(struct display *dp,int val);
static uint8 *bufloc(struct display *dp,int row,int col);
static void dinsline(struct display *dp);
static void ddelline(struct display *dp);
static void ddelchar(struct display *dp);
static void dinsert(struct display *dp);
static void dclreod(struct display *dp,int row,int col);

extern struct proc *Display;

/* Create a new virtual display.
 * The "noscrol" flag, if set, causes lines to "wrap around" from the bottom
 * to the top of the screen instead of scrolling the entire screen upwards
 * with each new line. This can be handy for packet trace screens.
 */
struct display *
newdisplay(rows,cols,noscrol,sflimit)
int rows,cols;	/* Size of new screen. 0,0 defaults to whole screen */
int noscrol;	/* 1: old IBM-style wrapping instead of scrolling */
int sflimit;	/* Scrollback file size, lines */
{
	struct display *dp;
	struct text_info text_info;
	int i;

	gettextinfo(&text_info);
	if(rows == 0)
		rows = text_info.screenheight;
	if(cols == 0)
		cols = text_info.screenwidth;

	dp = (struct display *)calloc(1,sizeof(struct display) +
	 2*rows*cols + rows*sizeof(struct dirty) + cols);
	dp->cookie = D_COOKIE;
	dp->buf = (uint8 *)(dp + 1);
	dp->dirty = (struct dirty *)(dp->buf + 2*rows*cols);
	dp->tabstops = (uint8 *)(dp->dirty + rows);
	dp->rows = rows;
	dp->cols = cols;

	/* Set default tabs every 8 columns */
	for(i=0;i<cols;i+= 8)
		dp->tabstops[i] = 1;
	/* Default scrolling region is all but last line of display,
	 * which is reserved for a status display
	 */
	dp->slast = rows - 2;

	dp->attrib = 0x7;	/* White on black, no blink or intensity */
	dclrscr(dp);		/* Start with a clean slate */
	dclreol(dp,rows-1,0);	/* Clear status line too */
	dp->flags.dirty_cursor = 1;
	dp->flags.no_scroll = noscrol;
	if(sflimit != 0 && (dp->sfile = tmpfile()) == NULL)
		sflimit = 0;	/* Out of handles? */

	dp->sflimit = sflimit;
	return dp;
}

/* Close a display - simply get rid of the memory */
void
closedisplay(dp)
struct display *dp;
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;
	if(dp->sfile != NULL)
		fclose(dp->sfile);
	free(dp);
}

/* Write buffer to status line. Works independently of the ANSI
 * machinery so as to not upset a possible escape sequence in
 * progress. Maximum of one line allowed, no control sequences
 */
void
statwrite(dp,col,buf,cnt,attrib)
struct display *dp;	/* Virtual screen pointer */
int col;		/* Starting column of write */
void *buf;		/* Data to be written */
int cnt;		/* Count */
int attrib;		/* Screen attribute to be used */
{
	uint8 *buf1 = buf;
	uint8 *sp = bufloc(dp,dp->slast+1,col);
	struct dirty *dirtp = &dp->dirty[dp->slast+1];

	/* Clip debug area if activated */
	if(Kdebug && cnt > DCOL - col - 1)
		cnt = DCOL - col - 1;
	else if(cnt > dp->cols-col)
		cnt = dp->cols - col;	/* Limit write to line length */

	while(cnt-- != 0){
		if(sp[0] != *buf1 || sp[1] != attrib){
			if(col < dirtp->lcol)
				dirtp->lcol = col; 
			if(col > dirtp->rcol)
				dirtp->rcol = col;
			sp[0] = *buf1;
			sp[1] = attrib;
		}
		buf1++;
		sp += 2;
		col++;
	}
}
/* Write data to the virtual display. Does NOT affect the real screen -
 * dupdate(dp) must be called to copy the virtual screen to the real
 * screen.
 */
void
displaywrite(dp,buf,cnt)
struct display *dp;	/* Virtual screen pointer */
void *buf;		/* Data to be written */
int cnt;		/* Count */
{
	uint8 c;
	char *bufp = buf;

	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	while(cnt-- != 0){
		c = *bufp++;
		switch(dp->state){
		case DISP_ESCAPE:
			desc(dp,c);
			break;
		case DISP_ARG:
			darg(dp,c);
			break;
		case DISP_NORMAL:
			dchar(dp,c);
			break;
		}
	}
	ksignal(dp,1);
}
/* Make the real screen look like the virtual one. It attempts to do as
 * little work as possible unless the "dirty screen" flag is set -- then
 * the entire screen is updated. (This is useful when switching between
 * virtual display screens.)
 *
 * Note the different row and column numbering conventions -- I start
 * at zero, the puttext() and gotoxy() library functions start at 1.
 */
void
dupdate(dp)
struct display *dp;	/* Virtual screen pointer */
{
	int row,rows;
	long sp;
	uint8 *lbuf;
	struct dirty *dirtp;
	long offset;

	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	offset = dp->flags.scrollbk ? dp->sfoffs : 0;

	if(offset > 0 && dp->flags.dirty_screen){
		/* Display from scrollback file */
		sp = dp->sfseek - 2*offset*dp->cols;
		if(sp < 0)
			sp += 2*dp->sfsize*dp->cols;	/* Wrap back */
		rows = min(offset,dp->slast+1);	/* # rows to read */
		lbuf = malloc(2*dp->cols*rows);
		fseek(dp->sfile,sp,SEEK_SET);
		/* row = actual # rows read */
		row = fread(lbuf,2*dp->cols,rows,dp->sfile);
		if(row != 0)
			puttext(1,1,dp->cols,row,lbuf);
		if(row != rows){
			/* Hit end of file; rewind and read the rest */
			fseek(dp->sfile,0L,SEEK_SET);
			fread(lbuf,2*dp->cols,rows-row,dp->sfile);
			puttext(1,row+1,dp->cols,rows,lbuf);
		}
		free(lbuf);
	}
	/* Display from memory image of current screen (if visible) */
	for(row = offset,dirtp = &dp->dirty[row];
	 row<=dp->slast;row++,dirtp++){
		if(dp->flags.dirty_screen){
			/* Force display of all columns */
			dirtp->lcol = 0;
			dirtp->rcol = dp->cols-1;
		}
		if(dirtp->lcol <= dirtp->rcol){
			puttext(dirtp->lcol+1,row+1,dirtp->rcol+1,row+1,
			 bufloc(dp,row-offset,dirtp->lcol));
			dirtp->lcol = dp->cols-1;
			dirtp->rcol = 0;
		}
	}
	/* Display unscrolled status region */
	for(row=dp->slast+1,dirtp = &dp->dirty[row];row<dp->rows;row++,dirtp++){
		if(dp->flags.dirty_screen){
			dirtp->lcol = 0;
			dirtp->rcol = dp->cols-1;
		}
		if(dirtp->lcol <= dirtp->rcol){
			puttext(dirtp->lcol+1,row+1,dirtp->rcol+1,row+1,
			 bufloc(dp,row,dirtp->lcol));
			dirtp->lcol = dp->cols-1;
			dirtp->rcol = 0;
		}
	}
	if(dp->flags.dirty_screen || (dp->flags.dirty_cursor)){
		/* Update cursor */
		if(dp->row+offset <= dp->slast){
			gotoxy(dp->col+1,dp->row+1+offset);
			_setcursortype(_NORMALCURSOR);
		} else {
			/* Turn off cursor entirely */
			_setcursortype(_NOCURSOR);
		}
	}
	dp->flags.dirty_cursor = 0;
	dp->flags.dirty_screen = 0;
}
void
dscrollmode(dp,flag)
struct display *dp;
int flag;
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	if(flag != dp->flags.scrollbk){
		dp->flags.scrollbk = flag;
		if(dp->sfoffs != 0)
			dp->flags.dirty_screen = 1;
		alert(Display,1);
	}
}


void
dhome(dp)
struct display *dp;
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	if(dp->sfoffs != dp->sfsize){
		dp->sfoffs = dp->sfsize;
		dp->flags.dirty_screen = 1;
		alert(Display,1);
	}
}
void
dend(dp)
struct display *dp;
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	if(dp->sfoffs != 0){
		dp->sfoffs = 0;
		dp->flags.dirty_screen = 1;
		alert(Display,1);
	}
}
void
dpgup(dp)
struct display *dp;
{
	long newoffs;

	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	newoffs = dp->sfoffs + dp->slast + 1;
	newoffs = min(newoffs,dp->sfsize);
	if(newoffs != dp->sfoffs){
		dp->sfoffs = newoffs;
		dp->flags.dirty_screen = 1;
		alert(Display,1);
	}
}
void
dpgdown(dp)
struct display *dp;
{
	long newoffs;

	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	newoffs = dp->sfoffs - (dp->slast + 1);
	newoffs = max(0,newoffs);
	if(newoffs != dp->sfoffs){
		dp->sfoffs = newoffs;
		dp->flags.dirty_screen = 1; 
		alert(Display,1);
	}
}
void
dcursup(dp)
struct display *dp;
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	if(dp->sfoffs < dp->sfsize){
		dp->sfoffs++;
		dp->flags.dirty_screen = 1; 
		alert(Display,1);
	}
}
void
dcursdown(dp)
struct display *dp;
{
	if(dp == NULL || dp->cookie != D_COOKIE)
		return;

	if(dp->sfoffs != 0){
		dp->sfoffs--;
		dp->flags.dirty_screen = 1; 
		alert(Display,1);
	}
}

/* Process incoming character while in ESCAPE state */
static void
desc(dp,c)
struct display *dp;
uint8 c;
{
	int i;

	switch(c){
	case 'O':
	case '[':	/* Always second char of ANSI escape sequence */
		/* Get ready for argument list */
		dp->state = DISP_ARG;
		dp->argi = 0;
		for(i=0;i<MAXARGS;i++)
			dp->arg[i] = 0;

		break;
	case '7':	/* Save cursor location (VT-100) */
		dp->savcol = dp->col;
		dp->savrow = dp->row;
		dp->state = DISP_NORMAL;
		break;
	case '8':	/* Restore cursor location (VT-100) */
		dp->col = dp->savcol;
		dp->row = dp->savrow;
		dp->flags.dirty_cursor = 1;
		dp->state = DISP_NORMAL;
		break;
	case ESC:
		break;	/* Remain in ESCAPE state */
	case 'H':	/* Set tab stop at current position (VT-100) */
		dp->tabstops[dp->col] = 1;
		break;
	default:
		dp->state = DISP_NORMAL;
		dchar(dp,c);
	}
}

/* Process characters after a ESC[ sequence */
static void
darg(dp,c)
struct display *dp;
uint8 c;
{
	int i;

	switch(c){
	case ESC:
		dp->state = DISP_ESCAPE;
		return;
	case '?':	/* Ignored */
	case '=':
		return;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		/* Collect decimal number */
		dp->arg[dp->argi] = 10*dp->arg[dp->argi] + (c - '0');
		return;
	case ';':	/* Next argument is beginning */
		if(dp->argi <= MAXARGS - 1)
			dp->argi++;
		dp->arg[dp->argi] = 0;
		return;
	case '@':	/* Open up space for character */
		dinsert(dp);
		break;
	case 'A':	/* Cursor up */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one line */
		if(dp->arg[0] <= dp->row)
			dp->row -= dp->arg[0];
		else
			dp->row = 0;
		dp->flags.dirty_cursor = 1;
		break;
	case 'B':	/* Cursor down */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one line */
		dp->row += dp->arg[0];
		if(dp->row > dp->slast)
			dp->row = dp->slast;
		dp->flags.dirty_cursor = 1; 
		break;
	case 'C':	/* Cursor right */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one column */
		dp->col += dp->arg[0];
		if(dp->col >= dp->cols)
			dp->col = dp->cols - 1;
		dp->flags.dirty_cursor = 1;
		break;
	case 'D':	/* Cursor left */
		if(dp->arg[0] == 0)
			dp->arg[0] = 1;	/* Default is one column */
		if(dp->arg[0] <= dp->col)
			dp->col -= dp->arg[0];
		else
			dp->col = 0;
		dp->flags.dirty_cursor = 1;
		break;
	case 'f':
	case 'H':	/* Cursor motion - limit to scrolled region */
		i = (dp->arg[0] == 0) ? 0 : dp->arg[0] - 1;
		if(i > dp->slast)
			i = dp->slast;
		dp->row = i;

		i = (dp->arg[1] == 0) ? 0 : dp->arg[1] - 1;
		if(i >= dp->cols)
			i = dp->cols - 1;
		dp->col = i;
		dp->state = DISP_NORMAL;
		dp->flags.dirty_cursor = 1;
		break;
	case 'h':	/* Set mode */
		switch(dp->arg[0]){
		case 7:	/* Turn on wrap mode */
			dp->flags.no_line_wrap = 0;
			break;
		}
		break;
	case 'J':	/* Clear screen */
		switch(dp->arg[0]){
		case 2:
			dclrscr(dp);	/* Clear entire screen, home cursor */
			break;
		case 0:
			dclreod(dp,dp->row,dp->col);	/* Clear to end of screen (VT-100) */
			break;
		}
		break;
	case 'K':	/* Erase to end of current line */
		dclreol(dp,dp->row,dp->col);
		break;
	case 'L':	/* Add blank line */
		dinsline(dp);
		break;		
	case 'l':	/* Clear mode */
		switch(dp->arg[0]){
		case 7:	/* Turn off wrap mode */
			dp->flags.no_line_wrap = 1;
			break;
		}
		break;
	case 'M':	/* Delete line */
		ddelline(dp);
		break;
	case 'm':	/* Set screen attributes */
		for(i=0;i<=dp->argi;i++){
			dattrib(dp,dp->arg[i]);
		}
		break;
	case 'P':	/* Delete character */
		ddelchar(dp);
		break;
	case 's':	/* Save cursor position */
		dp->savcol = dp->col;
		dp->savrow = dp->row;
		break;
	case 'u':	/* Restore cursor position */
		dp->col = dp->savcol;
		dp->row = dp->savrow;
		dp->flags.dirty_cursor = 1;
		break;
	case 'g':
		switch(dp->arg[0]){
		case 0:
			dp->tabstops[dp->col] = 0;
			break;
		case 3:
			memset(dp->tabstops,0,dp->cols);
			break;
		}
		break;
	}
	dp->state = DISP_NORMAL;
}
/* Clear from specified location to end of screen, leaving cursor as is */
static void
dclreod(dp,row,col)
struct display *dp;
int row,col;
{
	dclreol(dp,row,col);	/* Clear current line */
	for(row = row + 1;row <= dp->slast;row++)
		dclreol(dp,row,0);	/* Clear all lines below */
}
/* Insert space at cursor, moving all chars on right to right one position */
static void
dinsert(dp)
struct display *dp;
{
	int i = 2*(dp->cols - dp->col - 1);
	uint8 *cp = bufloc(dp,dp->row,dp->col);
	struct dirty *dirtp = &dp->dirty[dp->row];

	if(i != 0)
		memmove(cp+2,cp,i);	/* handles overlapping blocks */
	*cp++ = ' ';
	*cp = dp->attrib;
	/* Dirty everything from the cursor to the right edge */
	if(dp->col < dirtp->lcol)
		dirtp->lcol = dp->col;
	dirtp->rcol = dp->cols-1;
}
/* Delete character at cursor, moving chars to right left one position */
static void
ddelchar(dp)
struct display *dp;
{
	uint8 *cp = bufloc(dp,dp->row,dp->col);
	int i = 2*(dp->cols-dp->col-1);
	struct dirty *dirtp = &dp->dirty[dp->row];

	/* Copy characters to right one space left */
	if(i != 0)
		memmove(cp,cp+2,i);	/* memmove handles overlapping blocks */
	/* Clear right most character on line */
	cp[i] = ' ';
	cp[i+1] = dp->attrib;
	/* Dirty everything from the cursor to the right edge */
	if(dp->col < dirtp->lcol)
		dirtp->lcol = dp->col;
	dirtp->rcol = dp->cols-1;
}
/* Delete line containing cursor, moving lines below up one line */
static void
ddelline(dp)
struct display *dp;
{
	uint8 *cp1,*cp2;
	int row;
	struct dirty *dirtp;

	for(row=dp->row,dirtp = &dp->dirty[row];row < dp->slast;row++,dirtp++){
		cp1 = bufloc(dp,row,0);
		cp2 = bufloc(dp,row+1,0);
		memcpy(cp1,cp2,dp->cols*2);
		/* Dirty entire line */
		dirtp->lcol = 0;
		dirtp->rcol = dp->cols-1;
	}
	/* Clear bottom line */
	dclreol(dp,dp->slast,0);
}		
/* Insert blank line where cursor is. Push existing lines down one */
static void
dinsline(dp)
struct display *dp;
{
	uint8 *cp1,*cp2;
	int row;
	struct dirty *dirtp;

	/* Copy lines down */
	for(row = dp->slast,dirtp = &dp->dirty[row];row > dp->row;row--){
		cp1 = bufloc(dp,row-1,0);
		cp2 = bufloc(dp,row,0);
		memcpy(cp2,cp1,2*dp->cols);
		/* Dirty entire line */
		dirtp->lcol = 0;
		dirtp->rcol = dp->cols-1;
	}
	/* Clear current line */
	dclreol(dp,dp->row,0);
}

/* Process an argument to an attribute set command */
static void
dattrib(dp,val)
struct display *dp;
int val;
{
	switch(val){
	case 0:	/* Normal white on black */
		dp->attrib = 0x7;
		break;
	case 1:	/* High intensity */
		dp->attrib |= 0x8;
		break;
	case 5:	/* Blink on */
		dp->attrib |= 0x80;
		break;
	case 7:	/* Reverse video (black on white) */
		dp->attrib = 0x70;
		break;
	default:
		if(val >= 30 && val < 38){
			/* Set foreground color */
			dp->attrib = (dp->attrib & ~0x7) | fgattr[val - 30];
		} else if(val >= 40 && val < 48){
			/* Set background color */
			dp->attrib = (dp->attrib & ~0x70) | ((bgattr[val - 40]) << 4);
		}
		break;
	}
 }
/* Display character */
static void
dchar(dp,c)
struct display *dp;
uint8 c;
{
	uint8 *cp;
	int row,rowchange;
	struct dirty *dirtp;

	rowchange = 0;
	switch(c){
	case ESC:
		dp->state = DISP_ESCAPE;
		return;
	case CTLQ:	/*****/
	case '\0':	/* Ignore nulls and bells */
	case BELL:
		break;
	case '\b':	/* Backspace */
		if(dp->col > 0){
			dp->col--;
			dp->flags.dirty_cursor = 1;
		}
		break;
	case FF:	/* Page feed */
		dclrscr(dp);
		break;
	case '\t':	/* Tab */
		while(dp->col < dp->cols-1){
			if(dp->tabstops[++dp->col])
				break;
		}
		dp->flags.dirty_cursor = 1;
		break;
	case '\n':	/* Move cursor down one row */
		dp->row++;
		rowchange = 1;
		dp->flags.dirty_cursor = 1;
		break;
	case '\r':	/* Move cursor to beginning of current row */
		dp->col = 0;
		dp->flags.dirty_cursor = 1;
		break;
	default:	/* Display character on screen */
		/* Compute location in screen buffer memory */
		cp = bufloc(dp,dp->row,dp->col);
		/* Normal display */
		if(c != *cp || cp[1] != dp->attrib){
			dirtp = &dp->dirty[dp->row];
			if(dp->col < dirtp->lcol)
				dirtp->lcol = dp->col;
			if(dp->col > dirtp->rcol)
				dirtp->rcol = dp->col;
		}
		*cp++ = c;
		*cp = dp->attrib;
		dp->flags.dirty_cursor = 1;
		/* Update cursor position, wrapping if necessary */
		if(++dp->col == dp->cols){
			if(dp->flags.no_line_wrap){
				dp->col--;
			} else {
				dp->col = 0;
				dp->row++;
				rowchange = 1;
			}
		}
	}
	/* Scroll screen if necessary */
	if(rowchange && dp->row > dp->slast){
		dp->row--;
		/* Scroll screen up */
		dp->scroll = (dp->scroll + 1) % (dp->slast + 1);
		if(!dp->flags.no_scroll){
			for(row=0,dirtp=&dp->dirty[row];row <=dp->slast;row++,dirtp++){
				dirtp->lcol = 0;
				dirtp->rcol = dp->cols-1;
			}
		}
		if(dp->sfile != NULL){
			uint8 *cp;

			/* When scrolled back, leave screen stationary */
			if(dp->flags.scrollbk && dp->sfoffs != dp->sflimit)
				dp->sfoffs++;

			/* Copy scrolled line to scrollback file */
			cp = bufloc(dp,dp->row,0);
			fseek(dp->sfile,dp->sfseek,SEEK_SET);
			fwrite(cp,2,dp->cols,dp->sfile);
			dp->sfseek += 2*dp->cols;
			if(dp->sfseek >= 2*dp->cols*dp->sflimit)
				dp->sfseek = 0;
			if(dp->sfsize < dp->sflimit)
				dp->sfsize++;
		}
		dclreol(dp,dp->row,0);
	}
}

/* Clear from specified location to end of line. Cursor is not moved */
static void
dclreol(dp,row,col)
struct display *dp;
int row,col;
{
	uint8 *cp = bufloc(dp,row,col);
	struct dirty *dirtp = &dp->dirty[row];
	int i;

	for(i=dp->cols - col;i!=0;i--){
		*cp++ = ' ';
		*cp++ = dp->attrib;
	}
	/* Dirty from current column to right edge */
	if(col < dirtp->lcol)
		dirtp->lcol = col;
	dirtp->rcol = dp->cols-1;
}
/* Move cursor to top left corner, clear screen */
static void
dclrscr(dp)
struct display *dp;
{
	dclreod(dp,0,0);
	dp->row = dp->col = 0;
	dp->scroll = 0;
	dp->flags.dirty_cursor = 1;
}
/* Return pointer into screen buffer for specified cursor location.
 * Not guaranteed to be valid past the end of the current line due to
 * scrolling
 */
static uint8 *
bufloc(dp,row,col)
struct display *dp;
int row,col;
{
#ifndef	notdef
	if(row < 0 || row >= dp->rows || col < 0 || col >= dp->cols){
		stktrace();
		cprintf("panic: bufloc(%p,%d,%d)\n",dp,row,col);
		exit(1);
	}
#endif
	if(row <= dp->slast)
		row = (row + dp->scroll) % (dp->slast + 1);
	return dp->buf + 2*(col + dp->cols*row);
}
/* Immediately display short debug string on lower right corner of display */
void
debug(s)
char *s;
{
	int i;
	static uint8 msg[2*DSIZ];

	if(msg[1] != 0x7){
		/* One time initialization to blanks with white-on-black */
		for(i=0;i<DSIZ;i++){
			msg[2*i] = ' ';
			msg[2*i+1] = 0x7;
		}
	}
	if(s == NULL)
		return;
	for(i=0;i<DSIZ && *s != '\0';i++)
		msg[2*i] = (uint8) *s++;

	for(;i<DSIZ;i++)
		msg[2*i] = ' ';

	puttext(DCOL,25,80,25,msg);
}
