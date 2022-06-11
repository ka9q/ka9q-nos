#ifndef	_CMDPARSE_H
#define	_CMDPARSE_H

#define	NARG		20	/* Max number of args to commands */

struct cmds {
	char *name;		/* Name of command */
	int (*func)(int argc,char *argv[],void *p);
				/* Function to execute command */
	int stksize;		/* Size of stack if subprocess, 0 if synch */
	int  argcmin;		/* Minimum number of args */
	char *argc_errmsg;	/* Message to print if insufficient args */
};
extern struct cmds Cmds[],Startcmds[],Stopcmds[],Attab[];

/* In cmdparse.c: */
int cmdparse(struct cmds cmds[],char *line,void *p);
int subcmd(struct cmds tab[],int argc,char *argv[],void *p);
int setbool(int *var,char *label,int argc,char *argv[]);
int bit16cmd(uint16 *bits, uint16 mask, char *label, int argc, char *argv[]);
int setint(int *var,char *label,int argc,char *argv[]);
int setlong(int32 *var,char *label,int argc,char *argv[]);
int setshort(unsigned short *var,char *label,int argc,char *argv[]);
int setuns(unsigned *var,char *label,int argc,char *argv[]);

#endif	/* _CMDPARSE_H */
