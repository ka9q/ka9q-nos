/* Convert relative to absolute pathnames
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "dirutil.h"

static void crunch(char *buf,char *path);

/* Given a working directory and an arbitrary pathname, resolve them into
 * an absolute pathname. Memory is allocated for the result, which
 * the caller must free
 */
char *
pathname(cd,path)
char *cd;	/* Current working directory */
char *path;	/* Pathname argument */
{
	register char *buf;
#ifdef	MSDOS
	char *cp,c;
	char *tbuf;
	int tflag = 0;
#endif

	if(cd == NULL || path == NULL)
		return NULL;

#ifdef	MSDOS
	/* If path has any backslashes, make a local copy with them
	 * translated into forward slashes
	 */
	if(strchr(path,'\\') != NULL){
		tflag = 1;
		cp = tbuf = mallocw(strlen(path));
		while((c = *path++) != '\0'){
			if(c == '\\')
				*cp++ = '/';
			else
				*cp++ = c;
		}
		*cp = '\0';
		path = tbuf;
	}
#endif

	/* Strip any leading white space on args */
	while(*cd == ' ' || *cd == '\t')
		cd++;
	while(*path == ' ' || *path == '\t')
		path++;

	/* Allocate and initialize output buffer; user must free */
	buf = mallocw((unsigned)strlen(cd) + strlen(path) + 10);	/* fudge factor */
	buf[0] = '\0';

	/* Interpret path relative to cd only if it doesn't begin with "/" */
	if(path[0] != '/')
		crunch(buf,cd);

	crunch(buf,path);

	/* Special case: null final path means the root directory */
	if(buf[0] == '\0'){
		buf[0] = '/';
		buf[1] = '\0';
	}
#ifdef	MSDOS
	if(tflag)
		free(tbuf);
#endif
	return buf;
}

/* Process a path name string, starting with and adding to
 * the existing buffer
 */
static void
crunch(buf,path)
char *buf;
register char *path;
{
	register char *cp;
	

	cp = buf + strlen(buf);	/* Start write at end of current buffer */
	
	/* Now start crunching the pathname argument */
	for(;;){
		/* Strip leading /'s; one will be written later */
		while(*path == '/')
			path++;
		if(*path == '\0')
			break;		/* no more, all done */
		/* Look for parent directory references, either at the end
		 * of the path or imbedded in it
		 */
		if(strcmp(path,"..") == 0 || strncmp(path,"../",3) == 0){
			/* Hop up a level */
			if((cp = strrchr(buf,'/')) == NULL)
				cp = buf;	/* Don't back up beyond root */
			*cp = '\0';		/* In case there's another .. */
			path += 2;		/* Skip ".." */
			while(*path == '/')	/* Skip one or more slashes */
				path++;
		/* Look for current directory references, either at the end
		 * of the path or imbedded in it
		 */
		} else if(strcmp(path,".") == 0 || strncmp(path,"./",2) == 0){
			/* "no op" */
			path++;			/* Skip "." */
			while(*path == '/')	/* Skip one or more slashes */
				path++;
		} else {
			/* Ordinary name, copy up to next '/' or end of path */
			*cp++ = '/';
			while(*path != '/' && *path != '\0')
				*cp++ = *path++;
		}
	}
	*cp++ = '\0';
}
