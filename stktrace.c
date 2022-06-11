/* This file contains code to print function/arg stack tracebacks
 * at run time, which is extremely useful for finding heap free() errors.
 *
 * This code is highly specific to Borland C and the 80x6 machines.
 *
 * April 10, 1992 P. Karn
 */

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <string.h>
#include <time.h>
#include "global.h"
#include "proc.h"

struct symtab {
	struct symtab *next;
	unsigned short seg;
	unsigned short offs;
	char *name;
};
static struct symtab *Symtab;
static void rdsymtab(int unused,void *name,void *p);
static void clrsymtab(void);
static struct symtab *findsym(void (*)());
static int scompare();
static void paddr(void (*pc)());

static unsigned short Codeseg;

void
stktrace()
{
	int i,j;
	unsigned short far *context;
	unsigned short far *cnext;
	unsigned short far *ctmp;
	void (*pc)();
	int nargs;
	struct proc *rdproc;
	struct symtab *sp;
	extern char **_argv;
	char *mapname;
	char *cp;
	FILE *fp,*outsave;
	time_t t;

	/* Temporarily redirect stdout to file */
	if((fp = fopen("stktrace.out","at")) == NULL)
		return;	/* Give up */
	outsave = Curproc->output;
	Curproc->output = fp;

	time(&t);
	printf("stktrace from proc %s at %s",Curproc->name,ctime(&t));
	Codeseg = _psp + 0x10;
#ifdef	notdef
	printf("Code base segment: %x\n",Codeseg);
#endif
	/* Construct name of map file */
	mapname = malloc(strlen(_argv[0]) + 5);
	strcpy(mapname,_argv[0]);
	if((cp = strrchr(mapname,'.')) != NULL)
		*cp = '\0';
	strcat(mapname,".map");

	/* Read the symbol table in another process to avoid overstressing
	 * the stack in this one
	 */
	rdproc = newproc("rdsymtab",512,rdsymtab,1,mapname,NULL,0);
	kwait(rdproc);
	free(mapname);

	context = MK_FP(_SS,_BP);
	pc = stktrace;

	for(i=0;i<20;i++){
		paddr(pc);
		sp = findsym(pc);
		if(sp != NULL)
			printf(" %s+%x",sp->name,FP_OFF(pc) - sp->offs);

		if(FP_OFF(context) == 0){
			/* No context left, we're done */
			putchar('\n');
			break;
		}
		cnext = MK_FP(FP_SEG(context),*context);
		/* Compute number of args to display */
		if(FP_OFF(cnext) != 0){
			nargs = cnext - context - (1 + sizeof(pc)/2);
			if(nargs > 20)
				nargs = 20; /* limit to reasonable number */
		} else {
			/* No higher level context, so just print an
			 * arbitrary fixed number of args
			 */
			nargs = 6;
		}		
		/* Args start after saved BP and return address */
		ctmp = context + 1 + sizeof(pc)/2;
		printf("(");
		for(j=0;j<nargs;j++){
			fprintf(fp,"%x",*ctmp);
			if(j < nargs-1)
				putchar(' ');
			else
				break;
			ctmp++;
		}
		printf(")\n");
#ifdef	notdef
		if(strcmp(cp,"_main") == 0)
			break;
#endif

#ifdef	LARGECODE
		pc = MK_FP(context[2],context[1]);
#else
		pc = (void (*)())MK_FP(FP_SEG(pc),context[1]);
#endif
		context = cnext;
	}
	clrsymtab();
	fclose(fp);
	Curproc->output = outsave;
}
static struct symtab *
findsym(pc)
void (*pc)();
{
	struct symtab *sp,*spprev;
	unsigned short seg,offs;
	
#ifdef	LARGECODE
	seg = FP_SEG(pc) - Codeseg;
#else
	seg = 0;	/* Small code, no segment */
#endif
	offs = FP_OFF(pc);
	spprev = NULL;
	for(sp = Symtab;sp != NULL;spprev = sp,sp = sp->next){
		if(sp->seg > seg || (sp->seg == seg && sp->offs > offs)){
			break;
		}
	}
	return spprev;
}
static void
clrsymtab()
{
	struct symtab *sp,*spnext;

	for(sp = Symtab;sp != NULL;sp = spnext){
		spnext = sp->next;
		free(sp->name);
		free(sp);
	}
	Symtab = NULL;
}
static void
rdsymtab(unused,name,p)
int unused;
void *name;
void *p;
{
	char *buf;
	FILE *fp;
	unsigned short seg;
	unsigned short offs;
	struct symtab *sp;
	struct symtab **spp;
	int size = 0;
	int i;

	if((fp = fopen(name,"rt")) == NULL){
		printf("can't read %s\n",name);
		return;
	}
	buf = (char *)malloc(128);
	while(fgets(buf,128,fp),!feof(fp)){
		rip(buf);
		if(strcmp(buf,"  Address         Publics by Value") == 0)
			break;
	}
	if(feof(fp)){
		printf("Can't find header line in %s\n",name);
		free(buf);
		return;
	}
	Symtab = NULL;
	while(fgets(buf,128,fp),!feof(fp)){
		rip(buf);
		if(sscanf(buf,"%x:%x",&seg,&offs) != 2)
			continue;
		sp = (struct symtab *)malloc(sizeof(struct symtab));
		sp->offs = offs;
		sp->seg = seg;
		sp->name = strdup(buf+17);
		sp->next = Symtab;
		Symtab = sp;
		size++;
	}
	fclose(fp);
	free(buf);
#ifdef	notdef
	printf("Symbols read: %d\n",size);
#endif
	/* Sort the symbols using the quicksort library function */
	spp = malloc(size*sizeof(struct symtab *));
	for(i=0,sp = Symtab;sp != NULL;i++,sp = sp->next)
		spp[i] = sp;
	qsort(spp,size,sizeof(struct symtab *),scompare);
	/* Now put them back in the linked list */
	Symtab = NULL;
	for(i=size-1;i >= 0;i--){
		sp = spp[i];
		sp->next = Symtab;
		Symtab = sp;
	}
	free(spp);
#ifdef	notdef
	for(sp = Symtab;sp != NULL;sp = sp->next)
		printf("%x:%x	%s\n",sp->seg,sp->offs,sp->name);
#endif
}
static int
scompare(a,b)
struct symtab **a,**b;
{
	if((*a)->seg > (*b)->seg)
		return 1;
	if((*a)->seg < (*b)->seg)
		return -1;
	if((*a)->offs > (*b)->offs)
		return 1;
	if((*a)->offs < (*b)->offs)
		return -1;
	return 0;
}
/* Print a code address according to the memory model */
static void
paddr(pc)
void (*pc)();
{
#ifdef	LARGECODE
	printf("%04x:%04x",FP_SEG(pc) - Codeseg,FP_OFF(pc));
#else
	printf("%04x",FP_OFF(pc);
#endif	
}
