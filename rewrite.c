#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "global.h"
#include "files.h"
#include "mailbox.h"
#include "smtp.h"
#include "bm.h"

char *Hdrs[] = {
	"Approved: ",
	"From: ",
	"To: ",
	"Date: ",
	"Message-Id: ",
	"Subject: ",
	"Received: ",
	"Sender: ",
	"Reply-To: ",
	"Status: ",
	"X-BBS-Msg-Type: ",
	"X-Forwarded-To: ",
	"Cc: ",
	"Return-Receipt-To: ",
	"Apparently-To: ",
	"Errors-To: ",
	"Organization: ",
	NULL
};

/* Read the rewrite file for lines where the first word is a regular
 * expression and the second word are rewriting rules. The special
 * character '$' followed by a digit denotes the string that matched
 * a '*' character. The '*' characters are numbered from 1 to 9.
 * Example: the line "*@*.* $2@$1.ampr.org" would rewrite the address
 * "foo@bar.xxx" to "bar@foo.ampr.org".
 * $H is replaced by our hostname, and $$ is an escaped $ character.
 * If the third word on the line has an 'r' character in it, the function
 * will recurse with the new address.
 */
char *
rewrite_address(addr)
char *addr;
{
	char *argv[10], buf[MBXLINE], *cp, *cp2, *retstr;
	int cnt;
	FILE *fp;
	if ((fp = fopen(Rewritefile,READ_TEXT)) == NULL)
		return NULL;
	memset(argv,0,10*sizeof(char *));
	while(fgets(buf,MBXLINE,fp) != NULL) {
		if(*buf == '#')		/* skip commented lines */
			continue;
		if((cp = strchr(buf,' ')) == NULL) /* get the first word */
			if((cp = strchr(buf,'\t')) == NULL)
				continue;
		*cp = '\0';
		if((cp2 = strchr(buf,'\t')) != NULL){
			*cp = ' ';
			cp = cp2;
			*cp = '\0';
		}
		if(!wildmat(addr,buf,argv))
			continue;		/* no match */
		rip(++cp);
		cp2 = retstr = (char *) callocw(1,MBXLINE);
		while(*cp != '\0' && *cp != ' ' && *cp != '\t')
			if(*cp == '$') {
				if(isdigit(*(++cp)))
					if(argv[*cp - '0'-1] != '\0')
						strcat(cp2,argv[*cp - '0'-1]);
				if(*cp == 'h' || *cp == 'H') /* Our hostname */
					strcat(cp2,Hostname);
				if(*cp == '$')	/* Escaped $ character */
					strcat(cp2,"$");
				cp2 = retstr + strlen(retstr);
				cp++;
			}
			else
				*cp2++ = *cp++;
		for(cnt=0; argv[cnt] != NULL; ++cnt)
			free(argv[cnt]);
		fclose(fp);
		/* If there remains an 'r' character on the line, repeat
		 * everything by recursing.
		 */
		if(strchr(cp,'r') != NULL || strchr(cp,'R') != NULL) {
			if((cp2 = rewrite_address(retstr)) != NULL) {
				free(retstr);
				return cp2;
			}
		}
		return retstr;
	}
	fclose(fp);
	return NULL;
}

/* Parse a string in the "Text: <user@host>" or "Text: user@host (Text)"
 * format for the address user@host.
 */
char *
getaddress(string,cont)
char *string;
int cont;		/* true if string is a continued header line */
{
	char *cp, *ap = NULL;
	int par = 0;
	if((cp = getname(string)) != NULL) /* Look for <> style address */
	     return cp;
	cp = string;
	if(!cont)
	     if((cp = strchr(string,':')) == NULL)	/* Skip the token */
		  return NULL;
	     else
		  ++cp;
	for(; *cp != '\0'; ++cp) {
	     if(par && *cp == ')') {
		  --par;
		  continue;
	     }
	     if(*cp == '(')		/* Ignore text within parenthesis */
		  ++par;
	     if(par)
		  continue;
	     if(*cp == ' ' || *cp == '\t' || *cp == '\n' || *cp == ',') {
		  if(ap != NULL)
		       break;
		  continue;
	     }
	     if(ap == NULL)
		  ap = cp;
	}
	*cp = '\0';
	return ap;
}
/* return the header type */
int
htype(s)
char *s;
{
	register char *p;
	register int i;

	p = s;
	/* check to see if there is a ':' before and white space */
	while (*p != '\0' && *p != ' ' && *p != ':')
		p++;
	if (*p != ':')
		return NOHEADER;

	for (i = 0; Hdrs[i] != NULL; i++) {
		if (strnicmp(Hdrs[i],s,strlen(Hdrs[i])) == 0)
			return i;
	}
	return UNKNOWN;
}

