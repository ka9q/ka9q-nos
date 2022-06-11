/* Parse command line, set up command arguments Unix-style, and call function.
 * Note: argument is modified (delimiters are overwritten with nulls)
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Improved error handling by Brian Boesch of Stanford University
 * Feb '91 - Bill Simpson
 *		bit16cmd for PPP
 * Mar '91 - Glenn McGregor
 *		handle string escaped sequences
 */
#include <stdio.h>
#include "global.h"
#include "proc.h"
#include "cmdparse.h"

struct boolcmd {
	char *str;	/* Token */
	int val;	/* Value */
};

static struct boolcmd Boolcmds[] = {
	"y",		1,	/* Synonyms for "true" */
	"yes",		1,
	"true",		1,
	"on",		1,
	"1",		1,
	"set",		1,
	"enable",	1,

	"n",		0,	/* Synonyms for "false" */
	"no",		0,
	"false",	0,
	"off",		0,
	"0",		0,
	"clear",	0,
	"disable",	0,
	NULL
};

static char *stringparse(char *line);

static char *
stringparse(line)
char *line;
{
	register char *cp = line;
	unsigned long num;

	while ( *line != '\0' && *line != '\"' ) {
		if ( *line == '\\' ) {
			line++;
			switch ( *line++ ) {
			case 'n':
				*cp++ = '\n';
				break;
			case 't':
				*cp++ = '\t';
				break;
			case 'v':
				*cp++ = '\v';
				break;
			case 'b':
				*cp++ = '\b';
				break;
			case 'r':
				*cp++ = '\r';
				break;
			case 'f':
				*cp++ = '\f';
				break;
			case 'a':
				*cp++ = '\a';
				break;
			case '\\':
				*cp++ = '\\';
				break;
			case '\?':
				*cp++ = '\?';
				break;
			case '\'':
				*cp++ = '\'';
				break;
			case '\"':
				*cp++ = '\"';
				break;
			case 'x':
				num = strtoul( --line, &line, 16 );
				*cp++ = (char) num;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				num = strtoul( --line, &line, 8 );
				*cp++ = (char) num;
				break;
			case '\0':
				return NULL;
			default:
				*cp++ = *(line - 1);
				break;
			};
		} else {
			*cp++ = *line++;
		}
	}

	if ( *line == '\"' )
		line++; 	/* skip final quote */
	*cp = '\0';		/* terminate string */
	return line;
}

int
cmdparse(cmds,line,p)
struct cmds cmds[];
register char *line;
void *p;
{
	struct cmds *cmdp;
	char *argv[NARG],*cp;
	char **pargv;
	int argc,i;

	/* Remove cr/lf */
	rip(line);

	for(argc = 0;argc < NARG;argc++)
		argv[argc] = NULL;

	for(argc = 0;argc < NARG;){
		register int qflag = FALSE;

		/* Skip leading white space */
		while(*line == ' ' || *line == '\t')
			line++;
		if(*line == '\0')
			break;
		/* return if comment character first non-white */
		if ( argc == 0  &&  *line == '#' )
			return 0;
		/* Check for quoted token */
		if(*line == '"'){
			line++;	/* Suppress quote */
			qflag = TRUE;
		}
		argv[argc++] = line;	/* Beginning of token */

		if(qflag){
			/* Find terminating delimiter */
			if((line = stringparse(line)) == NULL){
				return -1;
			}
		} else {
			/* Find space or tab. If not present,
			 * then we've already found the last
			 * token.
			 */
			if((cp = strchr(line,' ')) == NULL
			 && (cp = strchr(line,'\t')) == NULL){
				break;
			}
			*cp++ = '\0';
			line = cp;
		}
	}
	if (argc < 1) {		/* empty command line */
		argc = 1;
		argv[0] = "";
	}
	/* Look up command in table; prefix matches are OK */
	for(cmdp = cmds;cmdp->name != NULL;cmdp++){
		if(strncmp(argv[0],cmdp->name,strlen(argv[0])) == 0)
			break;
	}
	if(cmdp->name == NULL) {
		if(cmdp->argc_errmsg != NULL)
			printf("%s\n",cmdp->argc_errmsg);
		return -1;
	}
	if(argc < cmdp->argcmin) {
		/* Insufficient arguments */
		printf("Usage: %s\n",cmdp->argc_errmsg);
		return -1;
	}
	if(cmdp->func == NULL)
		return 0;
	if(cmdp->stksize == 0){
		return (*cmdp->func)(argc,argv,p);
	} else {
		/* Make private copy of argv and args,
		 * spawn off subprocess and return.
		 */
		pargv = (char **)callocw(argc,sizeof(char *));
		for(i=0;i<argc;i++)
			pargv[i] = strdup(argv[i]);
		newproc(cmdp->name,cmdp->stksize,
		(void (*)())cmdp->func,argc,pargv,p,1);
		return 0;
	}
}

/* Call a subcommand based on the first token in an already-parsed line */
int
subcmd(tab,argc,argv,p)
struct cmds tab[];
int argc;
char *argv[];
void *p;
{
	register struct cmds *cmdp;
	char **pargv;
	int found = 0;
	int i;

	/* Strip off first token and pass rest of line to subcommand */
	if (argc < 2) {
		if (argc < 1)
			printf("SUBCMD - Don't know what to do?\n");
		else
			printf("\"%s\" - takes at least one argument\n",argv[0]);
		return -1;
	}
	argc--;
	argv++;
	for(cmdp = tab;cmdp->name != NULL;cmdp++){
		if(strncmp(argv[0],cmdp->name,strlen(argv[0])) == 0){
			found = 1;
			break;
		}
	}
	if(!found){
		printf("valid subcommands:");
		for(cmdp = tab;cmdp->name != NULL;cmdp++)
			printf(" %s",cmdp->name);
		printf("\n");
		return -1;
	}
	if(argc < cmdp->argcmin){
		if(cmdp->argc_errmsg != NULL)
			printf("Usage: %s\n",cmdp->argc_errmsg);
		return -1;
	}
	if(cmdp->stksize == 0){
		return (*cmdp->func)(argc,argv,p);
	} else {
		/* Make private copy of argv and args */
		pargv = (char **)callocw(argc,sizeof(char *));
		for(i=0;i<argc;i++)
			pargv[i] = strdup(argv[i]);
		newproc(cmdp->name,cmdp->stksize,
		 (void (*)())cmdp->func,argc,pargv,p,1);
		return(0);
	}
}

/* Subroutine for setting and displaying boolean flags */
int
setbool(var,label,argc,argv)
int *var;
char *label;
int argc;
char *argv[];
{
	struct boolcmd *bc;

	if(argc < 2){
		printf("%s: %s\n",label,*var ? "on":"off");
		return 0;
	}
	for(bc = Boolcmds;bc->str != NULL;bc++){
		if(strcmpi(argv[1],bc->str) == 0){
			*var = bc->val;
			return 0;
		}
	}
	printf("Valid options:");
	for(bc = Boolcmds;bc->str != NULL;bc++)
		printf(" %s",bc->str);

	printf("\n");
	return 1;
}


/* Subroutine for setting and displaying bit values */
int
bit16cmd(bits,mask,label,argc,argv)
uint16 *bits;
uint16 mask;
char *label;
int argc;
char *argv[];
{
	int doing = (*bits & mask);
	int result = setbool( &doing, label, argc, argv );

	if ( !result ) {
		if ( doing )
			*bits |= mask;
		else
			*bits &= ~mask;
	}
	return result;
}


/* Subroutine for setting and displaying long variables */
int
setlong(var,label,argc,argv)
long *var;
char *label;
int argc;
char *argv[];
{
	if(argc < 2)
		printf("%s: %ld\n",label,*var);
	else
		*var = atol(argv[1]);

	return 0;
}
/* Subroutine for setting and displaying short variables */
int
setshort(var,label,argc,argv)
unsigned short *var;
char *label;
int argc;
char *argv[];
{
	if(argc < 2)
		printf("%s: %u\n",label,*var);
	else
		*var = atoi(argv[1]);

	return 0;
}
/* Subroutine for setting and displaying integer variables */
int
setint(var,label,argc,argv)
int *var;
char *label;
int argc;
char *argv[];
{
	if(argc < 2)
		printf("%s: %i\n",label,*var);
	else
		*var = atoi(argv[1]);

	return 0;
}

/* Subroutine for setting and displaying unsigned integer variables */
int
setuns(var,label,argc,argv)
unsigned *var;
char *label;
int argc;
char *argv[];
{
	if(argc < 2)
		printf("%s: %u\n",label,*var);
	else
		*var = atoi(argv[1]);

	return 0;
}


