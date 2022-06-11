/* This file MUST be compiled with the -zC_TEXT so that it will go
 * into the same code segment with the Borland C library. It
 * references the internal Borland function __vprinter(), which is
 * defined to be near. It seems that the Borland linker does NOT detect
 * cross-segment calls to near functions, so omitting this option will
 * cause the executable to crash!
 */

#include "global.h"
#include "stdio.h"
#include <stdarg.h>

int pascal near __vprinter(
	unsigned pascal near (*)(void *,unsigned, FILE*),
	FILE *, char *, void _ss *);
static unsigned pascal near
fputter(void *ptr,unsigned n,FILE *fp)
{
	return fwrite(ptr,1,n,fp);
}


int
vfprintf(FILE *fp,char *fmt, va_list args)
{


	if(fp == NULL || fp->cookie != _COOKIE)
		return -1;
	return __vprinter(fputter,fp,fmt,(void _ss *)args);
}
