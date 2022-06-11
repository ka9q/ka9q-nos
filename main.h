#ifndef _MAIN_H
#define _MAIN_H

#ifndef _PROC_H
#include "proc.h"
#endif

extern char Badhost[];
extern char *Hostname;
extern char Nospace[];			/* Generic malloc fail message */

extern struct proc *Cmdpp;
extern struct proc *Display;
extern int main_exit;			/* from main program (flag) */

#endif _MAIN_H
