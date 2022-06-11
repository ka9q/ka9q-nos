
/*
 * @(#)wildmat.c 1.3 87/11/06	Public Domain.
 *
From: rs@mirror.TMC.COM (Rich Salz)
Newsgroups: net.sources
Subject: Small shell-style pattern matcher
Message-ID: <596@mirror.TMC.COM>
Date: 27 Nov 86 00:06:40 GMT

There have been several regular-expression subroutines and one or two
filename-globbing routines in mod.sources.  They handle lots of
complicated patterns.  This small piece of code handles the *?[]\
wildcard characters the way the standard Unix(tm) shells do, with the
addition that "[^.....]" is an inverse character class -- it matches
any character not in the range ".....".  Read the comments for more
info.

For my application, I had first ripped off a copy of the "glob" routine
from within the find(1) source, but that code is bad news:  it recurses
on every character in the pattern.  I'm putting this replacement in the
public domain.  It's small, tight, and iterative.  Compile with -DTEST
to get a test driver.  After you're convinced it works, install in
whatever way is appropriate for you.

I would like to hear of bugs, but am not interested in additions; if I
were, I'd use the code I mentioned above.
*/
/*
**  Do shell-style pattern matching for ?, \, [], and * characters.
**  Might not be robust in face of malformed patterns; e.g., "foo[a-"
**  could cause a segmentation violation.
**
**  Written by Rich $alz, mirror!rs, Wed Nov 26 19:03:17 EST 1986.
*/

/*
 * Modified 6Nov87 by John Gilmore (hoptoad!gnu) to return a "match"
 * if the pattern is immediately followed by a "/", as well as \0.
 * This matches what "tar" does for matching whole subdirectories.
 *
 * The "*" code could be sped up by only recursing one level instead
 * of two for each trial pattern, perhaps, and not recursing at all
 * if a literal match of the next 2 chars would fail.
 */

/* Modified by Anders Klemets to take an array of pointers as an optional
   argument. Each part of the string that matches '*' is returned as a
   null-terminated, malloced string in this array.
 */
#include "global.h"
static int Star(char *s,char *p,char **argv);

static int
Star(s,p,argv)
register char *s;
register char *p;
register char **argv;
{
	char *cp = s;
	while (wildmat(cp, p, argv) == FALSE)
		if(*++cp == '\0')
			return -1;
	return cp - s;
}

int
wildmat(s,p,argv)
register char *s;
register char *p;
register char **argv;
{
	register int last;
	register int matched;
	register int reverse;
	register int cnt;

	for(; *p; s++,p++){
		switch(*p){
		case '\\':
			/* Literal match with following character; fall through. */
			p++;
		default:
			if(*s != *p)
				return FALSE;
			continue;
		case '?':
			/* Match anything. */
			if(*s == '\0')
				return FALSE;
			continue;
		case '*':
			/* Trailing star matches everything. */
			if(argv == NULL)
				return *++p ? 1 + Star(s, p, NULL) : TRUE;
			if(*++p == '\0'){
				cnt = strlen(s);
			} else {
				if((cnt = Star(s, p, argv+1)) == -1)
					return FALSE;
			}
			*argv = mallocw(cnt+1);
			strncpy(*argv,s,cnt);
			*(*argv + cnt) = '\0';
			return TRUE;
		case '[':
			/* [^....] means inverse character class. */
			reverse = (p[1] == '^') ? TRUE : FALSE;
			if(reverse)
				p++;
			for(last = 0400, matched = FALSE; *++p && *p != ']'; last = *p){
				/* This next line requires a good C compiler. */
				if(*p == '-' ? *s <= *++p && *s >= last : *s == *p)
					matched = TRUE;
			}
			if(matched == reverse)
				return FALSE;
			continue;
		}
	}
	/* For "tar" use, matches that end at a slash also work. --hoptoad!gnu */
	return *s == '\0' || *s == '/';
}


#ifdef	TEST
#include <stdio.h>

extern char *gets();

main()
{
	char pattern[80];
	char text[80];
	char *argv[80], *cp;
	int cnt;
    
	while (TRUE){
		printf("Enter pattern:  ");
		if(gets(pattern) == NULL)
			break;
		while (TRUE){
			bzero(argv,80*sizeof(char *);
			printf("Enter text:  ");
			if(gets(text) == NULL)
				exit(0);
			if(text[0] == '\0')
				/* Blank line; go back and get a new pattern. */
				break;
			printf("      %d\n", wildmat(text, pattern, argv);
			for(cnt = 0; argv[cnt] != NULL; ++cnt){
				printf("String %d is: '%s'\n",cnt,argv[cnt]);
				free(argv[cnt]);
			}
		}
	}
	exit(0);
}
#endif	/* TEST */
