/* Miscellaneous integer and IP address format conversion subroutines
 * Copyright 1991 Phil Karn, KA9Q
 */
#define LINELEN 256
#include <ctype.h>
#include <stdio.h>
#include "global.h"
#include "netuser.h"

int Net_error;

/* Convert Internet address in ascii dotted-decimal format (44.0.0.1) to
 * binary IP address
 */
int32
aton(s)
register char *s;
{
	int32 n;

	register int i;

	n = 0;
	if(s == NULL)
		return 0;
	for(i=24;i>=0;i -= 8){
		/* Skip any leading stuff (e.g., spaces, '[') */
		while(*s != '\0' && !isdigit(*s))
			s++;
		if(*s == '\0')
			break;
		n |= (int32)atoi(s) << i;
		if((s = strchr(s,'.')) == NULL)
			break;
		s++;
	}
	return n;
}
/* Convert an internet address (in host byte order) to a dotted decimal ascii
 * string, e.g., 255.255.255.255\0
 */
char *
inet_ntoa(a)
int32 a;
{
	static char buf[16];

	sprintf(buf,"%u.%u.%u.%u",
		hibyte(hiword(a)),
		lobyte(hiword(a)),
		hibyte(loword(a)),
		lobyte(loword(a)) );
	return buf;
}
/* Convert hex-ascii string to long integer */
long
htol(s)
char *s;
{
	long ret;
	char c;

	ret = 0;
	while((c = *s++) != '\0'){
		if(c == 'x')
			continue;	/* Ignore 'x', e.g., '0x' prefixes */
		if(c >= '0' && c <= '9')
			ret = ret*16 + (c - '0');
		else if(c >= 'a' && c <= 'f')
			ret = ret*16 + (10 + c - 'a');
		else if(c >= 'A' && c <= 'F')
			ret = ret*16 + (10 + c - 'A');
		else
			break;
	}
	return ret;
}
char *
pinet(s)
struct socket *s;
{
	static char buf[30];

	sprintf(buf,"%s:%u",inet_ntoa(s->address),s->port);
	return buf;
}

