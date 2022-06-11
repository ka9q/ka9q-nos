#ifndef	_DIRUTIL_H
#define	_DIRUTIL_H

#ifndef	_GLOBAL_H
#include "global.h"
#endif

/* In dirutil.c */
FILE *dir(char *path,int full);
int filedir(char *name,int times,char *ret_str);
int getdir(char *path,int full,FILE *file);

/* In pathname.c: */
char *pathname(char *cd,char *path);

#endif /* _DIRUTIL_H */

