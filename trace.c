/* Packet tracing - top level and generic routines, including hex/ascii
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include "global.h"
#include <stdarg.h>
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "commands.h"
#include "trace.h"
#include "session.h"

static void ascii_dump(FILE *fp,struct mbuf **bpp);
static void ctohex(char *buf,uint16 c);
static void fmtline(FILE *fp,uint16 addr,uint8 *buf,uint16 len);
void hex_dump(FILE *fp,struct mbuf **bpp);
static void showtrace(struct iface *ifp);

/* Redefined here so that programs calling dump in the library won't pull
 * in the rest of the package
 */
static char nospace[] = "No space!!\n";

struct tracecmd Tracecmd[] = {
	"input",	IF_TRACE_IN,	IF_TRACE_IN,
	"-input",	0,		IF_TRACE_IN,
	"output",	IF_TRACE_OUT,	IF_TRACE_OUT,
	"-output",	0,		IF_TRACE_OUT,
	"broadcast",	0,		IF_TRACE_NOBC,
	"-broadcast",	IF_TRACE_NOBC,	IF_TRACE_NOBC,
	"raw",		IF_TRACE_RAW,	IF_TRACE_RAW,
	"-raw",		0,		IF_TRACE_RAW,
	"ascii",	IF_TRACE_ASCII,	IF_TRACE_ASCII|IF_TRACE_HEX,
	"-ascii",	0,		IF_TRACE_ASCII|IF_TRACE_HEX,
	"hex",		IF_TRACE_HEX,	IF_TRACE_ASCII|IF_TRACE_HEX,
	"-hex",		IF_TRACE_ASCII,	IF_TRACE_ASCII|IF_TRACE_HEX,
	"off",		0,		0xffff,
	NULL,	0,		0
};


void
dump(
struct iface *ifp,
int direction,
struct mbuf *bp
){
	struct mbuf *tbp;
	uint16 size;
	time_t timer;
	char *cp;
	struct iftype *ift;
	FILE *fp;

	if(ifp == NULL || (ifp->trace & direction) == 0
	 || (fp = ifp->trfp) == NULL)
		return;	/* Nothing to trace */

	ift = ifp->iftype;
	switch(direction){
	case IF_TRACE_IN:
		if((ifp->trace & IF_TRACE_NOBC)
		 && ift != NULL
		 && (ift->addrtest != NULL)
		 && (*ift->addrtest)(ifp,bp) == 0)
			return;		/* broadcasts are suppressed */
		time(&timer);
		cp = ctime(&timer);
		cp[24] = '\0';
		fprintf(fp,"\n%s - %s recv:\n",cp,ifp->name);
		break;
	case IF_TRACE_OUT:
		time(&timer);
		cp = ctime(&timer);
		cp[24] = '\0';
		fprintf(fp,"\n%s - %s sent:\n",cp,ifp->name);
		break;
	}
	if(bp == NULL || (size = len_p(bp)) == 0){
		fprintf(fp,"empty packet!!\n");
		return;
	}
	dup_p(&tbp,bp,0,size);
	if(tbp == NULL){
		fprintf(fp,nospace);
		return;
	}
	if(ift != NULL && ift->trace != NULL)
		(*ift->trace)(fp,&tbp,1);
	if(ifp->trace & IF_TRACE_ASCII){
		/* Dump only data portion of packet in ascii */
		ascii_dump(fp,&tbp);
	} else if(ifp->trace & IF_TRACE_HEX){
		/* Dump entire packet in hex/ascii */
		free_p(&tbp);
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp != NULL)
			hex_dump(fp,&tbp);
		else
			fprintf(fp,nospace);
	}
	free_p(&tbp);
}

/* Dump packet bytes, no interpretation */
void
raw_dump(ifp,direction,bp)
struct iface *ifp;
int direction;
struct mbuf *bp;
{
	struct mbuf *tbp;
	FILE *fp;

	if((fp = ifp->trfp) == NULL)
		return;
	fprintf(fp,"\n******* raw packet dump (%s)\n",
	 ((direction & IF_TRACE_OUT) ? "send" : "recv"));
	dup_p(&tbp,bp,0,len_p(bp));
	if(tbp != NULL)
		hex_dump(fp,&tbp);
	else
		fprintf(fp,nospace);
	fprintf(fp,"*******\n");
	free_p(&tbp);
}

/* Dump an mbuf in hex */
void
hex_dump(fp,bpp)
FILE *fp;
register struct mbuf **bpp;
{
	uint16 n;
	uint16 address;
	uint8 buf[16];

	if(bpp == NULL || *bpp == NULL || fp == NULL)
		return;

	address = 0;
	while((n = pullup(bpp,buf,sizeof(buf))) != 0){
		fmtline(fp,address,buf,n);
		address += n;
	}
}
/* Dump an mbuf in ascii */
static void
ascii_dump(fp,bpp)
FILE *fp;
register struct mbuf **bpp;
{
	int c;
	register uint16 tot;

	if(bpp == NULL || *bpp == NULL || fp == NULL)
		return;

	tot = 0;
	while((c = PULLCHAR(bpp)) != -1){
		if((tot % 64) == 0)
			fprintf(fp,"%04x  ",tot);
		putc(isprint(c) ? c : '.',fp);
		if((++tot % 64) == 0)
			fprintf(fp,"\n");
	}
	if((tot % 64) != 0)
		fprintf(fp,"\n");
}
/* Print a buffer up to 16 bytes long in formatted hex with ascii
 * translation, e.g.,
 * 0000: 30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f  0123456789:;<=>?
 */
static void
fmtline(fp,addr,buf,len)
FILE *fp;
uint16 addr;
uint8 *buf;
uint16 len;
{
	char line[80];
	char *aptr,*cptr;
	uint8 c;

	memset(line,' ',sizeof(line));
	ctohex(line,(uint16)hibyte(addr));
	ctohex(line+2,(uint16)lobyte(addr));
	aptr = &line[6];
	cptr = &line[55];
	while(len-- != 0){
		c = *buf++;
		ctohex(aptr,(uint16)c);
		aptr += 3;
		*cptr++ = isprint(c) ? c : '.';
	}
	*cptr++ = '\n';
	fwrite(line,1,(unsigned)(cptr-line),fp);
}
/* Convert byte to two ascii-hex characters */
static void
ctohex(buf,c)
register char *buf;
register uint16 c;
{
	static char hex[] = "0123456789abcdef";

	*buf++ = hex[hinibble(c)];
	*buf = hex[lonibble(c)];
}

/* Modify or displace interface trace flags */
int
dotrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;
	struct tracecmd *tp;
	struct session *sp;

	if(argc < 2){
		for(ifp = Ifaces; ifp != NULL; ifp = ifp->next)
			showtrace(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc == 2){
		showtrace(ifp);
		return 0;
	}
	/* MODIFY THIS TO HANDLE MULTIPLE OPTIONS */
	if(argc >= 3){
		for(tp = Tracecmd;tp->name != NULL;tp++)
			if(strncmp(tp->name,argv[2],strlen(argv[2])) == 0)
				break;
		if(tp->name != NULL)
			ifp->trace = (ifp->trace & ~tp->mask) | tp->val;
		else
			ifp->trace = htoi(argv[2]);
	}
	if(ifp->trfp != NULL){
		/* Close existing trace file */
		fclose(ifp->trfp);
		ifp->trfp = NULL;
	}
	if(argc >= 4){
		if((ifp->trfp = fopen(argv[3],APPEND_TEXT)) == NULL){
			printf("Can't write to %s\n",argv[3]);
		}
	} else if(ifp->trace != 0){
		/* Create trace session */
		sp = newsession(Cmdline,ITRACE,1);
		sp->cb.p = NULL;
		sp->proc = sp->proc1 = sp->proc2 = NULL;
		ifp->trfp = sp->output;
		showtrace(ifp);
		getchar();	/* Wait for the user to hit something */
		ifp->trace = 0;
		ifp->trfp = NULL;
		freesession(sp);
	}
	return 0;
}
/* Display the trace flags for a particular interface */
static void
showtrace(ifp)
register struct iface *ifp;
{
	char *cp;

	if(ifp == NULL)
		return;
	printf("%s:",ifp->name);
	if(ifp->trace & (IF_TRACE_IN | IF_TRACE_OUT | IF_TRACE_RAW)){
		if(ifp->trace & IF_TRACE_IN)
			printf(" input");
		if(ifp->trace & IF_TRACE_OUT)
			printf(" output");

		if(ifp->trace & IF_TRACE_NOBC)
			printf(" - no broadcasts");

		if(ifp->trace & IF_TRACE_HEX)
			printf(" (Hex/ASCII dump)");
		else if(ifp->trace & IF_TRACE_ASCII)
			printf(" (ASCII dump)");
		else
			printf(" (headers only)");

		if(ifp->trace & IF_TRACE_RAW)
			printf(" Raw output");

		if(ifp->trfp != NULL && (cp = fpname(ifp->trfp)) != NULL)
			printf(" trace file: %s",cp);
		printf("\n");
	} else
		printf(" tracing off\n");
}

/* shut down all trace files */
void
shuttrace()
{
	struct iface *ifp;

	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next){
		fclose(ifp->trfp);
		ifp->trfp = NULL;
	}
}

/* Log messages of the form
 * Tue Jan 31 00:00:00 1987 44.64.0.7:1003 open FTP
 */
void
trace_log(struct iface *ifp,char *fmt, ...)
{
	va_list ap;
	char *cp;
	long t;
	FILE *fp;

	if((fp = ifp->trfp) == NULL)
		return;
	time(&t);
	cp = ctime(&t);
	rip(cp);
	fprintf(fp,"%s - ",cp);
	va_start(ap,fmt);
	vfprintf(fp,fmt,ap);
	va_end(ap);
	fprintf(fp,"\n");
}
int
tprintf(struct iface *ifp,char *fmt, ...)
{
	va_list ap;
	int ret = 0;

	if(ifp->trfp == NULL)
		return -1; 
	va_start(ap,fmt);
	ret = vfprintf(ifp->trfp,fmt,ap);
	va_end(ap);
	return ret;
}
