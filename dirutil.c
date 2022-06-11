/* dirutil.c - MS-DOS directory reading routines
 *
 * Bdale Garbee, N3EUA, Dave Trulli, NN2Z, and Phil Karn, KA9Q
 * Directory sorting by Mike Chepponis, K3MC
 * New version using regs.h by Russell Nelson.
 * Rewritten for Turbo-C 2.0 routines by Phil Karn, KA9Q 25 March 89
 */

#include <stdio.h>
#include <dir.h>
#include <dos.h>
#include <stdlib.h>
#include "global.h"
#include "dirutil.h"
#include "proc.h"
#include "commands.h"

struct dirsort {
	struct dirsort *next;
	struct ffblk de;
};
#define	NULLSORT (struct dirsort *)0

static void commas(char *dest);
static int fncmp(char *a, char *b);
static void format_fname_full(FILE *file,struct ffblk *sbuf,int full,
	int n);
static void free_clist(struct dirsort *this);

#ifdef	notdef
static int getdir_nosort(char *path,int full,FILE *file);
#endif
static int nextname(int command, char *name, struct ffblk *sbuf);
static void print_free_space(FILE *file,int n);
static void undosify(char *s);
static char *wildcardize(char *path);

#define REGFILE	(FA_HIDDEN|FA_SYSTEM|FA_DIREC)

#define	insert_ptr(list,new)	(new->next = list,list = new)


/* Create a directory listing in a temp file and return the resulting file
 * descriptor. If full == 1, give a full listing; else return just a list
 * of names.
 */
FILE *
dir(path,full)
char *path;
int full;
{
	FILE *fp;

	if((fp = tmpfile()) != NULL){
		getdir(path,full,fp);
		rewind(fp);
	}
	return fp;
}

/* find the first or next file and lowercase it. */
static int
nextname(command, name, sbuf)
int command;
char *name;
struct ffblk *sbuf;
{
	int found;

	switch(command){
	case 0:
		found = findfirst(name,sbuf,REGFILE);
		break;
	default:
		found = findnext(sbuf);
	}
	found = found == 0;
	if(found)
		strlwr(sbuf->ff_name);

	return found;
}

/* wildcard filename lookup */
int
filedir(name,times,ret_str)
char *name;
int times;
char *ret_str;
{
	static struct ffblk sbuf;
	int rval;

	switch(times){
	case 0:
		rval = findfirst(name,&sbuf,REGFILE);
		break;
	default:
		rval = findnext(&sbuf);
		break;
	}
	if(rval == -1){
		ret_str[0] = '\0';
	} else {
		/* Copy result to output */
		strcpy(ret_str, sbuf.ff_name);
	}
	return rval;
}
/* do a directory list to the stream 
 * full = 0 -> short form, 1 is long
*/
int
getdir(path,full,file)
char *path;
int full;
FILE *file;
{
	struct ffblk sbuf;
	int command = 0;
	int n = 0;
	struct dirsort *head, *here, *new;

	path = wildcardize(path);

	head = NULLSORT;	/* No head of chain yet... */
	for(;;){
		if (!nextname(command, path, &sbuf))
			break;
		command = 1;	/* Got first one already... */
		if (sbuf.ff_name[0] == '.')	/* drop "." and ".." */
			continue;

		new = (struct dirsort *) mallocw(sizeof(struct dirsort));
		new->de = sbuf;	/* Copy contents of directory entry struct */

		/* insert it into the list */
		if (!head || fncmp(new->de.ff_name, head->de.ff_name) < 0) {
			insert_ptr(head, new);
		} else {
			register struct dirsort *this;
			for (this = head;
			    this->next != NULLSORT;
			    this = this->next)
				if (fncmp(new->de.ff_name, this->next->de.ff_name) < 0)
					break;
			insert_ptr(this->next, new);
		}
	} /* infinite FOR loop */

	for (here = head; here; here = here->next)
		format_fname_full(file,&here->de,full,++n);

	/* Give back all the memory we temporarily needed... */
	free_clist(head);

	if(full)
		print_free_space(file, n);

	return 0;
}
static int
fncmp(a,b)
register char *a, *b;
{
        int i;

	for(;;){
		if (*a == '.')
			return -1;
		if (*b == '.')
			return 1;
		if ((i = *a - *b++) != 0)
			return i;
		if (!*a++)
			return -1;
	}
}
/* Change working directory */
int
docd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char dirname[128];

	if(argc > 1){
		if(chdir(argv[1]) == -1){
			printf("Can't change directory\n");
			return 1;
		}
	}
	if(getcwd(dirname,128) != NULL){
		undosify(dirname);
		printf("%s\n",dirname);
	}
	return 0;
}
/* List directory to console */
int
dodir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *path;

	if(argc >= 2)
		path = argv[1];
	else
		path = "*.*";

	getdir(path,1,stdout);
	return 0;
}
/* Create directory */
int
domkd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(mkdir(argv[1]) == -1)
		perror("Can't mkdir");
	return 0;
}
/* Remove directory */
int
dormd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(rmdir(argv[1]) == -1)
		perror("Can't rmdir");
	return 0;
}

/*
 * Return a string with commas every 3 positions.
 * the original string is replace with the string with commas.
 *
 * The caller must be sure that there is enough room for the resultant
 * string.
 *
 *
 * k3mc 4 Dec 87
 */
static void
commas(dest)
char *dest;
{
	char *src, *core;	/* Place holder for malloc */
	unsigned cc;		/* The comma counter */
	unsigned len;

	len = strlen(dest);
	/* Make a copy, so we can muck around */
	core = src = strdup(dest);

	cc = (len-1)%3 + 1;	/* Tells us when to insert a comma */

	while(*src != '\0'){
		*dest++ = *src++;
		if( ((--cc) == 0) && *src ){
			*dest++ = ','; cc = 3;
		}
	}
	free(core);
	*dest = '\0';
}
/* fix up the filename so that it contains the proper wildcard set */
static char *
wildcardize(path)
char *path;
{
	struct ffblk sbuf;
	static char ourpath[64];

	/* Root directory is a special case */
	if(path == NULL ||
	   *path == '\0' ||
	   strcmp(path,"\\") == 0 ||
	   strcmp(path,"/") == 0)
		path = "\\*.*";

	/* if they gave the name of a subdirectory, append \*.* to it */
	if (nextname(0, path, &sbuf) &&
	    (sbuf.ff_attrib & FA_DIREC) &&
	    !nextname(1, path, &sbuf)) {

		/* if there isn't enough room, give up -- it's invalid anyway */
		if (strlen(path) + 4 > 63) return path;
		strcpy(ourpath, path);
		strcat(ourpath, "\\*.*");
		return ourpath;
	}
	return path;
}

static void
format_fname_full(file, sbuf, full, n)
FILE *file;
struct ffblk *sbuf;
int full, n;
{
	char line_buf[50];		/* for long dirlist */
	char cbuf[20];			/* for making line_buf */

	strcpy(cbuf,sbuf->ff_name);
	if(sbuf->ff_attrib & FA_DIREC) strcat(cbuf, "/");
	if (full) {
		/* Long form, give other info too */
		sprintf(line_buf,"%-13s",cbuf);
		if(sbuf->ff_attrib & FA_DIREC)
			strcat(line_buf,"           ");/* 11 spaces */
		else {
			sprintf(cbuf,"%ld",sbuf->ff_fsize);
			commas(cbuf);
			sprintf(line_buf+strlen(line_buf),"%10s ",cbuf);
		}
		sprintf(line_buf+strlen(line_buf),"%2d:%02d %2d/%02d/%02d%s",
		  (sbuf->ff_ftime >> 11) & 0x1f,	/* hour */
		  (sbuf->ff_ftime >> 5) & 0x3f,	/* minute */
		  (sbuf->ff_fdate >> 5) & 0xf,	/* month */
		  (sbuf->ff_fdate ) & 0x1f,		/* day */
		  (sbuf->ff_fdate >> 9) + 80,	/* year */
		  (n & 1) ? "   " : "\n");
		fputs(line_buf,file);
	} else {
		fputs(cbuf,file);
		fputs("\n",file);
	}
}
/* Provide additional information only on DIR */
static void
print_free_space(file, n)
FILE *file;
int n;
{
	unsigned long free_bytes, total_bytes;
	char s_free[11], s_total[11];
	char cbuf[20];
	struct dfree dtable;
	unsigned long bpcl;

	if(n & 1)
		fputs("\n",file);

	/* Find disk free space */
	getdfree(0,&dtable);

	bpcl = dtable.df_bsec * dtable.df_sclus;
	free_bytes  = dtable.df_avail * bpcl;
	total_bytes = dtable.df_total * bpcl;

	sprintf(s_free,"%ld",free_bytes);
	commas(s_free);
	sprintf(s_total,"%ld",total_bytes);
	commas(s_total);

	if(n)
		sprintf(cbuf,"%d",n);
	else
		strcpy(cbuf,"No");

	fprintf(file,"%s file%s. %s bytes free. Disk size %s bytes.\n",
		cbuf,(n==1? "":"s"),s_free,s_total);
}
static void
free_clist(this)
struct dirsort *this;
{
	struct dirsort *next;

	while (this != NULLSORT) {
		next = this->next;
		free(this);
		this = next;
	}
}
#ifdef	notdef
static int
getdir_nosort(path,full,file)
char *path;
int full;
FILE *file;
{
	struct ffblk sbuf;
	int command;
	int n = 0;	/* Number of directory entries */

	path = wildcardize(path);
	command = 0;
	while(nextname(command, path, &sbuf)){
		command = 1;	/* Got first one already... */
		if (sbuf.ff_name[0] == '.')	/* drop "." and ".." */
			continue;
		format_fname_full(file, &sbuf, full, ++n);
	}
	if(full)
		print_free_space(file, n);
	return 0;
}
#endif

/* Translate those %$#@!! backslashes to proper form */
static void
undosify(s)
char *s;
{
	while(*s != '\0'){
		if(*s == '\\')
			*s = '/';
		s++;
	}
}
