/*
 *	DOMAIN.C -- domain name system stub resolver
 *
 *	Original code by Phil Karn, KA9Q.
 *
 *	Apr 90	Bill Simpson added address->name resolution, time-to-live,
 *	thru	memory caching, generalized multi-record multi-type searches,
 *	Oct 90	and many minor changes to conform more closely to the RFCs.
 *	Feb 91	Bill Simpson added "query" command and TYPE_ANY processing.
 *	Jul 91	Bill Simpson added "more" sessions for query and cache list.
 */

#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "ip.h"
#include "netuser.h"
#include "session.h"
#include "socket.h"
#include "cmdparse.h"
#include "commands.h"
#include "files.h"
#include "main.h"
#include "domain.h"

#undef	DEBUG				/* for certain trace messages */
#undef	DEBUG_PAIN			/* for painful debugging */

static struct rr *Dcache = NULL;	/* Cache of resource records */
static int Dcache_size = 20;		/* size limit */
static time_t Dcache_time = 0L; 	/* timestamp */

static int Dfile_clean = FALSE; 	/* discard expired records (flag) */
static int Dfile_reading = 0;		/* read interlock (count) */
static int Dfile_writing = 0;		/* write interlock (count) */

struct proc *Dfile_updater = NULL;
static int32 Dfile_wait_absolute = 0L;	/* timeout Clock time */
static int Dfile_wait_relative = 300;	/* timeout file activity (seconds) */

static struct dserver *Dservers = NULL; /* List of potential servers */
static int Dserver_retries = 2;		/* Attempts to reach servers */

static char *Dsuffix = NULL;	/* Default suffix for names without periods */
static int Dtrace = FALSE;
static char *Dtypes[] = {
	"",
	"A",
	"NS",
	"MD",
	"MF",
	"CNAME",
	"SOA",
	"MB",
	"MG",
	"MR",
	"NULL",
	"WKS",
	"PTR",
	"HINFO",
	"MINFO",
	"MX",
	"TXT"
};
static int Ndtypes = 17;
static char delim[] = " \t\r\n";

static int docache(int argc,char *argv[],void *p);
static int dosuffix(int argc,char *argv[],void *p);

static int docacheclean(int argc,char *argv[],void *p);
static int docachelist(int argc,char *argv[],void *p);
static int docachesize(int argc,char *argv[],void *p);
static int docachewait(int argc,char *argv[],void *p);

static void dlist_add(struct dserver *dp);
static void dlist_drop(struct dserver *dp);
static int dodnsadd(int argc,char *argv[],void *p);
static int dodnsdrop(int argc,char *argv[],void *p);
static int dodnslist(int argc,char *argv[],void *p);
static int dodnsquery(int argc,char *argv[],void *p);
static int dodnsretry(int argc,char *argv[],void *p);
static int dodnstrace(int argc,char *argv[],void *p);

static char * dtype(int value);
static int check_ttl(struct rr *rrlp);
static int compare_rr(struct rr *search_rrp,struct rr *target_rrp);
static int compare_rr_list(struct rr *rrlp,struct rr *target_rrp);
static struct rr *copy_rr(struct rr *rrp);
static struct rr *copy_rr_list(struct rr *rrlp);
static struct rr *make_rr(int source,
	char *dname,uint16 class,uint16 type,int32 ttl,uint16 rdl,void *data);

static void dcache_add(struct rr *rrlp);
static void dcache_drop(struct rr *rrp);
static struct rr *dcache_search(struct rr *rrlp);
static void dcache_update(struct rr *rrlp);

static struct rr *get_rr(FILE *fp, struct rr *lastrrp);
static void put_rr(FILE *fp,struct rr *rrp);
static struct rr *dfile_search(struct rr *rrlp);
static void dfile_update(int s,void *unused,void *p);

static void dumpdomain(struct dhdr *dhp,int32 rtt);
static int dns_makequery(uint16 op,struct rr *rrp,
	uint8 *buffer,uint16 buflen);
static int dns_query(struct rr *rrlp);

static int isaddr(char *s);
static char *checksuffix(char *dname);
static struct rr *resolver(struct rr *rrlp);


/**
 **	Domain Resolver Commands
 **/

static struct cmds Dcmds[] = {
	"addserver",	dodnsadd,	0, 2, "add <hostid>",
	"dropserver",	dodnsdrop,	0, 2, "drop <hostid>",
	"list",		dodnslist,	0, 0, NULL,
	"query",	dodnsquery,   512, 2, "query <hostid>",
	"retry",	dodnsretry,	0, 0, NULL,
	"suffix",	dosuffix,	0, 0, NULL,
	"trace",	dodnstrace,	0, 0, NULL,
	"cache",	docache,	0, 0, NULL,
	NULL,
};

static struct cmds Dcachecmds[] = {
	"clean",	docacheclean,	0, 0, NULL,
	"list",		docachelist,  512, 0, NULL,
	"size",		docachesize,	0, 0, NULL,
	"wait",		docachewait,	0, 0, NULL,
	NULL,
};

int
dodomain(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Dcmds,argc,argv,p);
}

static int
docache(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Dcachecmds,argc,argv,p);
}

static int
dosuffix(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		if(Dsuffix != NULL)
			printf("%s\n",Dsuffix);
		return 0;
	}
	free(Dsuffix);
	Dsuffix = strdup(argv[1]);
	return 0;
}

static int
docacheclean(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool( &Dfile_clean, "discard expired records", argc,argv );
}

static int
docachelist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct rr *rrp;
	struct session *sp;
	int row = 25;

	if((sp = newsession(Cmdline,DCLIST,1)) == NULL){
		return -1;
	}

	(void)dcache_search(NULL); /* update ttl */

	/* Put tty into raw mode so single-char responses will work */
	sp->ttystate.echo = sp->ttystate.edit = 0;

	for(rrp=Dcache;rrp!=NULL;rrp=rrp->next)
	{
		put_rr(stdout,rrp);
		if(--row == 0){
			row = keywait("--More--",0);
			switch(row){
			case -1:
			case 'q':
			case 'Q':
				rrp = NULL;
				break;
			case '\n':
			case '\r':
				row = 1;
				break;
			case ' ':
			default:
				row = 25;
			};
		}
	}
	fflush(stdout);
	keywait(NULL,1);
	freesession(sp);
	return 0;
}

static int
docachesize(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int newsize;
	int oldsize;
	int result;

	newsize = oldsize = Dcache_size;
	result = setint( &newsize, "memory cache size", argc,argv );

	if(newsize > 0){
		Dcache_size = newsize;
		if(newsize < oldsize){
			(void)dcache_search(NULL); /* update size */
		}
	}
	return result;
}

static int
docachewait(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setint( &Dfile_wait_relative, "time before file update (seconds)", argc,argv );
}

static void
dlist_add(dp)
register struct dserver *dp;
{
	dp->prev = NULL;
	dp->next = Dservers;
	if(Dservers != NULL)
		Dservers->prev = dp;
	Dservers = dp;
}

static void
dlist_drop(dp)
register struct dserver *dp;
{
	if(dp->prev != NULL)
		dp->prev->next = dp->next;
	else
		Dservers = dp->next;
	if(dp->next != NULL)
		dp->next->prev = dp->prev;
}

static int
dodnsadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int32 address;

	if((address = resolve(argv[1])) == 0L){
		printf("Resolver %s unknown\n",argv[1]);
		return 1;
	}
	return add_nameserver(address);
}
int
add_nameserver(address)
int32 address;
{
	struct dserver *dp;

	dp = (struct dserver *)callocw(1,sizeof(struct dserver));
	dp->address = address;
	dp->srtt = INITRTT;
	dp->mdev = 0;
	dp->timeout = 2 * dp->mdev + dp->srtt + 3;
	dlist_add(dp);
	return 0;
}

static int
dodnsdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct dserver *dp;
	int32 addr;

	addr = resolve(argv[1]);
	for(dp = Dservers;dp != NULL;dp = dp->next)
		if(addr == dp->address)
			break;

	if(dp == NULL){
		printf("Not found\n");
		return 1;
	}

	dlist_drop(dp);
	free(dp);
	return 0;
}

static int
dodnslist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct dserver *dp;

	printf("Server address          srtt    mdev   timeout   queries responses\n");
	for(dp = Dservers;dp != NULL;dp = dp->next){
		printf("%-20s%8lu%8lu%10lu%10lu%10lu\n",
		 inet_ntoa(dp->address),
		 dp->srtt,dp->mdev,dp->timeout,
		 dp->queries,dp->responses);
	}
	return 0;
}

static int
dodnsquery(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct rr *rrp;
	struct rr *result_rrlp;
	char *sname;
	struct session *sp;
	int row = 25;

	if((sp = newsession(Cmdline,DQUERY,1)) == NULL){
		return -1;
	}

	if ( isaddr( argv[1] ) ) {
		result_rrlp = inverse_a( aton( argv[1] ) );
	} else {
		sname = checksuffix( argv[1] );
		rrp = make_rr(RR_QUERY,sname,CLASS_IN,TYPE_ANY,0,0,NULL);
		FREE(sname);

		dns_query(rrp);
		result_rrlp = dcache_search(rrp);
		free_rr(rrp);
	}

	/* Put tty into raw mode so single-char responses will work */
	sp->ttystate.echo = sp->ttystate.edit = 0;

	for( rrp=result_rrlp; rrp!=NULL; rrp=rrp->next)
	{
		put_rr(stdout,rrp);
		if(--row == 0){
			row = keywait("--More--",0);
			switch(row){
			case -1:
			case 'q':
			case 'Q':
				rrp = NULL;
				break;
			case '\n':
			case '\r':
				row = 1;
				break;
			case ' ':
			default:
				row = 25;
			};
		}
	}
	fflush(stdout);
	free_rr(result_rrlp);
	keywait(NULL,1);
	freesession(sp);
	return 0;
}

static int
dodnsretry(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setint( &Dserver_retries, "server retries", argc,argv );
}

static int
dodnstrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Dtrace,"server trace",argc,argv);
}


/**
 **	Domain Resource Record Utilities
 **/

static char *
dtype(value)
int value;
{
	static char buf[10];

	if (value < Ndtypes)
		return Dtypes[value];

	sprintf( buf, "{%d}", value);
	return buf;
}

/* check list of resource records for any expired ones.
 * returns number of expired records.
 */
static int
check_ttl(rrlp)
register struct rr *rrlp;
{
	int count = 0;

	while(rrlp != NULL){
		if(rrlp->ttl == 0L)
			count++;
		rrlp = rrlp->next;
	}
	return count;
}

/* Compare two resource records.
 * returns 0 if match, nonzero otherwise.
 */
static int
compare_rr(search_rrp,target_rrp)
register struct rr *search_rrp,*target_rrp;
{
	int i;

	if(search_rrp == NULL || target_rrp == NULL)
		return -32765;

	if(search_rrp->class != target_rrp->class)
		return -32763;

	if(search_rrp->type != TYPE_ANY
	&& search_rrp->type != target_rrp->type
	&& (search_rrp->source != RR_QUERY
	 || (target_rrp->type != TYPE_CNAME
	  && target_rrp->type != TYPE_PTR)))
		return -32761;

	if(search_rrp->source != RR_INQUERY){
		if((i = strlen(search_rrp->name)) != strlen(target_rrp->name))
			return -32759;
		if((i = strnicmp(search_rrp->name,target_rrp->name,i)) != 0)
			return i;

		/* match negative records so that they are replaced */
		if(target_rrp->rdlength == 0)
			return 0;
	}

	/* if a query has gotten this far, match it */
	if(search_rrp->source == RR_QUERY)
		return 0;

	/* ensure negative records don't replace older records */
	if(search_rrp->rdlength == 0)
		return -32757;

	/* match expired records so that they are replaced */
	if(search_rrp->source != RR_INQUERY){
		if(target_rrp->ttl == 0L)
			return 0;
	}

	/* Note: rdlengths are not compared because they vary depending
	 * on the representation (ASCII or encoded) this record was
	 * generated from.
	 */

	switch(search_rrp->type){
	case TYPE_A:
		i = search_rrp->rdata.addr != target_rrp->rdata.addr;
		break;
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
	case TYPE_TXT:
		i = stricmp(search_rrp->rdata.data,target_rrp->rdata.data);
		break;
	case TYPE_HINFO:
		i = strcmp(search_rrp->rdata.hinfo.cpu,target_rrp->rdata.hinfo.cpu) ||
			strcmp(search_rrp->rdata.hinfo.os,target_rrp->rdata.hinfo.os);
		break;
	case TYPE_MX:
		i = stricmp(search_rrp->rdata.mx.exch,target_rrp->rdata.mx.exch);
		break;
	case TYPE_SOA:
		i = search_rrp->rdata.soa.serial != target_rrp->rdata.soa.serial;
		break;
	default:
		i = -32755;	/* unsupported */
	}
	return i;
}

static int
compare_rr_list(rrlp,target_rrp)
register struct rr *rrlp,*target_rrp;
{
	while(rrlp != NULL){
		if(compare_rr(rrlp,target_rrp) == 0)
			return 0;
#ifdef DEBUG_PAIN
		if(Dtrace)
			printf("%15d %s\n",
				compare_rr(rrlp,target_rrp),
				target_rrp->name);
#endif
		rrlp = rrlp->next;
	}
	return -32767;
}

/* Make a new copy of a resource record */
static struct rr *
copy_rr(rrp)
register struct rr *rrp;
{
	register struct rr *newrr;

	if(rrp == NULL)
		return NULL;

	newrr = (struct rr *)callocw(1,sizeof(struct rr));
	newrr->source =	rrp->source;
	newrr->name =	strdup(rrp->name);
	newrr->type =	rrp->type;
	newrr->class =	rrp->class;
	newrr->ttl =	rrp->ttl;
	if((newrr->rdlength = rrp->rdlength) == 0)
		return newrr;

	switch(rrp->type){
	case TYPE_A:
		newrr->rdata.addr = rrp->rdata.addr;
		break;
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
	case TYPE_TXT:
		newrr->rdata.name = strdup(rrp->rdata.name);
		break;
	case TYPE_HINFO:
		newrr->rdata.hinfo.cpu = strdup(rrp->rdata.hinfo.cpu);
		newrr->rdata.hinfo.os = strdup(rrp->rdata.hinfo.os);
		break;
	case TYPE_MX:
		newrr->rdata.mx.pref = rrp->rdata.mx.pref;
		newrr->rdata.mx.exch = strdup(rrp->rdata.mx.exch);
		break;
	case TYPE_SOA:
		newrr->rdata.soa.mname = 	strdup(rrp->rdata.soa.mname);
		newrr->rdata.soa.rname = 	strdup(rrp->rdata.soa.rname);
		newrr->rdata.soa.serial = 	rrp->rdata.soa.serial;
		newrr->rdata.soa.refresh = 	rrp->rdata.soa.refresh;
		newrr->rdata.soa.retry = 	rrp->rdata.soa.retry;
		newrr->rdata.soa.expire = 	rrp->rdata.soa.expire;
		newrr->rdata.soa.minimum = 	rrp->rdata.soa.minimum;
		break;
	}
	return newrr;
}

static struct rr *
copy_rr_list(rrlp)
register struct rr *rrlp;
{
	register struct rr **rrpp;
	struct rr *result_rrlp;

	rrpp = &result_rrlp;
	while(rrlp != NULL){
		*rrpp = copy_rr(rrlp);
		rrpp = &(*rrpp)->next;
		rrlp = rrlp->next;
	}
	*rrpp = NULL;
	return result_rrlp;
}

/* Free (list of) resource records */
void
free_rr(rrlp)
register struct rr *rrlp;
{
	register struct rr *rrp;

	while((rrp = rrlp) != NULL){
		rrlp = rrlp->next;

		free(rrp->comment);
		free(rrp->name);
		if(rrp->rdlength > 0){
			switch(rrp->type){
			case TYPE_A:
				break;	/* Nothing allocated in rdata section */
			case TYPE_CNAME:
			case TYPE_MB:
			case TYPE_MG:
			case TYPE_MR:
			case TYPE_NS:
			case TYPE_PTR:
			case TYPE_TXT:
				free(rrp->rdata.name);
				break;
			case TYPE_HINFO:
				free(rrp->rdata.hinfo.cpu);
				free(rrp->rdata.hinfo.os);
				break;
			case TYPE_MX:
				free(rrp->rdata.mx.exch);
				break;
			case TYPE_SOA:
				free(rrp->rdata.soa.mname);
				free(rrp->rdata.soa.rname);
				break;
			}
		}
		free(rrp);
	}
}

static struct rr *
make_rr(source,dname,dclass,dtype,ttl,rdl,data)
int source;
char *dname;
uint16 dclass;
uint16 dtype;
int32 ttl;
uint16 rdl;
void *data;
{
	register struct rr *newrr;

	newrr = (struct rr *)callocw(1,sizeof(struct rr));
	newrr->source = source;
	newrr->name = strdup(dname);
	newrr->class = dclass;
	newrr->type = dtype;
	newrr->ttl = ttl;
	if((newrr->rdlength = rdl) == 0)
		return newrr;

	switch(dtype){
	case TYPE_A:
	  {
		register int32 *ap = (int32 *)data;
		newrr->rdata.addr = *ap;
		break;
	  }
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
	case TYPE_TXT:
	  {
		newrr->rdata.name = strdup((char *)data);
		break;
	  }
	case TYPE_HINFO:
	  {
		register struct hinfo *hinfop = (struct hinfo *)data;
		newrr->rdata.hinfo.cpu = strdup(hinfop->cpu);
		newrr->rdata.hinfo.os = strdup(hinfop->os);
		break;
	  }
	case TYPE_MX:
	  {
		register struct mx *mxp = (struct mx *)data;
		newrr->rdata.mx.pref = mxp->pref;
		newrr->rdata.mx.exch = strdup(mxp->exch);
		break;
	  }
	case TYPE_SOA:
	  {
		register struct soa *soap = (struct soa *)data;
		newrr->rdata.soa.mname = 	strdup(soap->mname);
		newrr->rdata.soa.rname = 	strdup(soap->rname);
		newrr->rdata.soa.serial = 	soap->serial;
		newrr->rdata.soa.refresh = 	soap->refresh;
		newrr->rdata.soa.retry = 	soap->retry;
		newrr->rdata.soa.expire = 	soap->expire;
		newrr->rdata.soa.minimum = 	soap->minimum;
		break;
	  }
	}
	return newrr;
}


/**
 **	Domain Cache Utilities
 **/

static void
dcache_add(rrlp)
register struct rr *rrlp;
{
	register struct rr *last_rrp;
	struct rr *save_rrp;

	if(rrlp == NULL)
		return;

	save_rrp = rrlp;
	last_rrp = NULL;
	while(rrlp != NULL){
		rrlp->last = last_rrp;
		last_rrp = rrlp;
		rrlp = rrlp->next;
	}
	last_rrp->next = Dcache;
	if(Dcache != NULL)
		Dcache->last = last_rrp;
	Dcache = save_rrp;
}

static void
dcache_drop(rrp)
register struct rr *rrp;
{
	if(rrp->last != NULL)
		rrp->last->next = rrp->next;
	else
		Dcache = rrp->next;
	if(rrp->next != NULL)
		rrp->next->last = rrp->last;
	rrp->last =
	rrp->next = NULL;
}

/* Search cache for resource records, removing them from the cache.
 * Also, timeout cache entries, and trim cache to size.
 * (Calling with NULL is legal -- will timeout & trim only.)
 * Note that an answer from the cache cannot be authoritative, because
 * we cannot guarantee that all the entries remain from a previous request.
 * Returns RR list, or NULL if no record found.
 */
static struct rr *
dcache_search(rrlp)
struct rr *rrlp;
{
	register struct rr *rrp, *test_rrp;
	struct rr **rrpp, *result_rrlp;
	int32 elapsed;
	time_t now;
	int count = 0;

#ifdef DEBUG
	if(Dtrace && rrlp != NULL){
		printf("dcache_search: searching for %s\n",rrlp->name);
	}
#endif

	elapsed = (int32)(time(&now) - Dcache_time);
	Dcache_time = now;

	rrpp = &result_rrlp;
	for(rrp = Dcache; (test_rrp = rrp) != NULL;){
		rrp = rrp->next;
					/* timeout entries */
		if(test_rrp->ttl > 0L
		&& (test_rrp->ttl -= elapsed) <= 0L)
			test_rrp->ttl = 0L;

		if(compare_rr_list(rrlp,test_rrp) == 0){
			dcache_drop( *rrpp = test_rrp );
			rrpp = &(*rrpp)->next;
		} else if(test_rrp->source == RR_FILE && ++count > Dcache_size){
			dcache_drop(test_rrp);
			free_rr(test_rrp);
		}
	}
	*rrpp = NULL;
	return result_rrlp;
}

/* Move a list of resource records to the cache, removing duplicates. */
static void
dcache_update(rrlp)
register struct rr *rrlp;
{
	if(rrlp == NULL)
		return;

	free_rr(dcache_search(rrlp));	/* remove duplicates, first */
	dcache_add(rrlp);
}


/**
 **	File Utilities
 **/

static struct rr *
get_rr(fp,lastrrp)
FILE *fp;
struct rr *lastrrp;
{
	char *line,*lp,*strtok();
	struct rr *rrp;
	char *name,*ttl,*class,*type,*data;
	int i;

	line = mallocw(256);
	if(fgets(line,256,fp) == NULL){
		free(line);
		return NULL;
	}

	rrp = (struct rr *)callocw(1,sizeof(struct rr));
	rrp->source = RR_FILE;

	if(line[0] == '\0' || line[0] == '#' || line[0] == ';'){
		rrp->comment = line;
		return rrp;
	}

	if(!isspace(line[0]) || lastrrp == NULL){
		name = strtok(line,delim);
		lp = NULL;
	} else {	/* Name field is missing */
		name = lastrrp->name;
		lp = line;
	}
	if(name == NULL || (i = strlen(name)) == 0){
		rrp->comment = strdup("\n");
		free(line);
		return rrp;
	}

	if(name[i-1] != '.'){
		/* Tack on a trailing period if it's not there */
		/* !!! need to implement $ORIGIN suffix here */
		rrp->name = mallocw(i+2);
		strcpy(rrp->name,name);
		strcat(rrp->name,".");
	} else
		rrp->name = strdup(name);

	ttl = strtok(lp,delim);

	if(ttl == NULL || (!isdigit(ttl[0]) && ttl[0] != '-')){
		/* Optional ttl field is missing */
		rrp->ttl = TTL_MISSING;
		class = ttl;
	} else {
		rrp->ttl = atol(ttl);
		class = strtok(NULL,delim);
	}

	if(class == NULL){
		/* we're in trouble, but keep processing */
		rrp->class = CLASS_MISSING;
		type = class;
	} else if(class[0] == '<'){
		rrp->class = atoi(&class[1]);
		type = strtok(NULL,delim);
	} else if(stricmp(class,"IN") == 0){
		rrp->class = CLASS_IN;
		type = strtok(NULL,delim);
	} else {
		/* Optional class field is missing; assume IN */
		rrp->class = CLASS_IN;
		type = class;
	}

	if(type == NULL){
		/* we're in trouble, but keep processing */
		rrp->type = TYPE_MISSING;
		data = type;
	} else if(type[0] == '{'){
		rrp->type = atoi(&class[1]);
		data = strtok(NULL,delim);
	} else {
		rrp->type = TYPE_MISSING;
		for(i=1;i<Ndtypes;i++){
			if(stricmp(type,Dtypes[i]) == 0){
				rrp->type = i;
				data = strtok(NULL,delim);
				break;
			}
		}
	}

	if(rrp->type == TYPE_MISSING){
		data = NULL;
	}

	if(data == NULL){
		/* Empty record, just return */
		free(line);
		return rrp;
	}
	switch(rrp->type){
	case TYPE_A:
		rrp->rdlength = 4;
		rrp->rdata.addr = aton(data);
		break;
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
	case TYPE_TXT:
		rrp->rdlength = strlen(data);
		rrp->rdata.name = strdup(data);
		break;
	case TYPE_HINFO:
		rrp->rdlength = strlen(data);
		rrp->rdata.hinfo.cpu = strdup(data);
		if((data = strtok(NULL,delim)) != NULL){
			rrp->rdlength += strlen(data);
			rrp->rdata.hinfo.os = strdup(data);
		}
		break;
	case TYPE_MX:
		rrp->rdata.mx.pref = atoi(data);
		rrp->rdlength = 2;

		/* Get domain name of exchanger */
		if((data = strtok(NULL,delim)) != NULL){
			rrp->rdlength += strlen(data);
			rrp->rdata.mx.exch = strdup(data);
		}
		break;
	case TYPE_SOA:
		/* Get domain name of master name server */
		rrp->rdlength = strlen(data);
		rrp->rdata.soa.mname = strdup(data);

		/* Get domain name of irresponsible person */
		if((data = strtok(NULL,delim)) != NULL){
			rrp->rdata.soa.rname = strdup(data);
			rrp->rdlength += strlen(data);
		}
		data = strtok(NULL,delim);
		rrp->rdata.soa.serial = atol(data);
		data = strtok(NULL,delim);
		rrp->rdata.soa.refresh = atol(data);
		data = strtok(NULL,delim);
		rrp->rdata.soa.retry = atol(data);
		data = strtok(NULL,delim);
		rrp->rdata.soa.expire = atol(data);
		data = strtok(NULL,delim);
		rrp->rdata.soa.minimum = atol(data);
		rrp->rdlength += 20;
		break;
	}

	/* !!! need to handle trailing comments */
	free(line);
	return rrp;
}

/* Print a resource record */
static void
put_rr(fp,rrp)
FILE *fp;
struct rr *rrp;
{
	char * stuff;

	if(fp == NULL || rrp == NULL)
		return;

	if(rrp->name == NULL && rrp->comment != NULL){
		fprintf(fp,"%s",rrp->comment);
		return;
	}

	fprintf(fp,"%s",rrp->name);
	if(rrp->ttl != TTL_MISSING)
		fprintf(fp,"\t%ld",rrp->ttl);
	if(rrp->class == CLASS_IN)
		fprintf(fp,"\tIN");
	else
		fprintf(fp,"\t<%u>",rrp->class);

	stuff = dtype(rrp->type);
	fprintf(fp,"\t%s",stuff);
	if(rrp->rdlength == 0){
		/* Null data portion, indicates nonexistent record */
		/* or unsupported type.  Hopefully, these will filter */
		/* as time goes by. */
		fprintf(fp,"\n");
		return;
	}
	switch(rrp->type){
	case TYPE_A:
		fprintf(fp,"\t%s\n",inet_ntoa(rrp->rdata.addr));
		break;
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
	case TYPE_TXT:
		/* These are all printable text strings */
		fprintf(fp,"\t%s\n",rrp->rdata.data);
		break;
	case TYPE_HINFO:
		fprintf(fp,"\t%s\t%s\n",
		 rrp->rdata.hinfo.cpu,
		 rrp->rdata.hinfo.os);
		break;
	case TYPE_MX:
		fprintf(fp,"\t%u\t%s\n",
		 rrp->rdata.mx.pref,
		 rrp->rdata.mx.exch);
		break;
	case TYPE_SOA:
		fprintf(fp,"\t%s\t%s\t%lu\t%lu\t%lu\t%lu\t%lu\n",
		 rrp->rdata.soa.mname,rrp->rdata.soa.rname,
		 rrp->rdata.soa.serial,rrp->rdata.soa.refresh,
		 rrp->rdata.soa.retry,rrp->rdata.soa.expire,
		 rrp->rdata.soa.minimum);
		break;
	default:
		fprintf(fp,"\n");
		break;
	}
}

/* Search local database for resource records.
 * Returns RR list, or NULL if no record found.
 */
static struct rr *
dfile_search(rrlp)
struct rr *rrlp;
{
	register struct rr *frrp;
	struct rr **rrpp, *result_rrlp, *oldrrp;
	int32 elapsed;
	FILE *dbase;
	struct stat dstat;

#ifdef DEBUG
	if(Dtrace){
		printf("dfile_search: searching for %s\n",rrlp->name);
	}
#endif

	while(Dfile_writing > 0)
		kwait(&Dfile_reading);
	Dfile_reading++;

	if((dbase = fopen(Dfile,READ_TEXT)) == NULL){
		Dfile_reading--;
		return NULL;
	}
	if(fstat(fileno(dbase),&dstat) != 0){
		printf("dfile_search: can't get file status\n");
		fclose(dbase);
		Dfile_reading--;
		return NULL;
	}
	if((elapsed = (int32)(Dcache_time - (time_t)dstat.st_ctime)) < 0L)
		elapsed = -elapsed;	/* arbitrary time mismatch */

	result_rrlp = NULL;		/* for contiguous test below */
	oldrrp = NULL;
	rrpp = &result_rrlp;
	while((frrp = get_rr(dbase,oldrrp)) != NULL){
		free_rr(oldrrp);
		if(frrp->type != TYPE_MISSING
		&& frrp->rdlength > 0
		&& compare_rr_list(rrlp,frrp) == 0){
			if(frrp->ttl > 0L
			&& (frrp->ttl -= elapsed) <= 0L)
				frrp->ttl = 0L;
			*rrpp = frrp;
			rrpp = &(*rrpp)->next;
			oldrrp = copy_rr(frrp);
		} else {
			oldrrp = frrp;
			/*
				All records of the same name and the same type
				are contiguous.  Therefore, for a single query,
				we can stop searching.  Multiple queries must
				read the whole file.
			*/
			if(rrlp->type != TYPE_ANY
			&& rrlp->next == NULL
			&& result_rrlp != NULL)
				break;
		}
		if(!main_exit)
			kwait(NULL);	/* run multiple sessions */
	}
	free_rr(oldrrp);
	*rrpp = NULL;

	fclose(dbase);

	if(--Dfile_reading <= 0){
		Dfile_reading = 0;
		ksignal(&Dfile_writing,0);
	}

	return result_rrlp;
}

/* Process which will add new resource records from the cache
 * to the local file, eliminating duplicates while it goes.
 */
static void
dfile_update(s,unused,p)
int s;
void *unused;
void *p;
{
	struct rr **rrpp, *rrlp, *oldrrp;
	char *newname;
	FILE *old_fp, *new_fp;
	struct stat old_stat, new_stat;

	logmsg(-1,"update Domain.txt initiated");

	/* Produce output on command session rather than the one
	 * that invoked us
	 */
	fclose(stdin);
	stdin = fdup(Cmdpp->input);
	fclose(stdout);
	stdout = fdup(Cmdpp->output);

	newname = strdup(Dfile);
	strcpy(&newname[strlen(newname)-3],"tmp");

	while(Dfile_wait_absolute != 0L && !main_exit){
		register struct rr *frrp;
		int32 elapsed;

		while(Dfile_wait_absolute != 0L){
			elapsed = Dfile_wait_absolute - secclock();
			Dfile_wait_absolute = 0L;
			if(elapsed > 0L && !main_exit){
				kalarm(elapsed*1000L);
				kwait(&Dfile_wait_absolute);
				kalarm(0L);
			}
		}

		logmsg(-1,"update Domain.txt");

		/* create new file for copy */
		if((new_fp = fopen(newname,WRITE_TEXT)) == NULL){
			printf("dfile_update: can't create %s!\n",newname);
			break;
		}
		if(fstat(fileno(new_fp),&new_stat) != 0){
			printf("dfile_update: can't get new_file status!\n");
			fclose(new_fp);
			break;
		}

		kwait(NULL);	/* file operations can be slow */

		/* timeout the cache one last time before writing */
		(void)dcache_search(NULL);

		/* copy new RRs out to the new file */
		/* (can't wait here, the cache might change) */
		rrpp = &rrlp;
		for(frrp = Dcache; frrp != NULL; frrp = frrp->next ){
			switch(frrp->source){
			case RR_QUESTION:
			case RR_ANSWER:
			case RR_AUTHORITY:
			case RR_ADDITIONAL:
				*rrpp = copy_rr(frrp);
				if(frrp->type != TYPE_MISSING
				&& frrp->rdlength > 0)
					put_rr(new_fp,frrp);
				rrpp = &(*rrpp)->next;
				frrp->source = RR_FILE;
				break;
			}
		}
		*rrpp = NULL;

		/* open up the old file, concurrently with everyone else */
		if((old_fp = fopen(Dfile,READ_TEXT)) == NULL){
			/* great! no old file, so we're ready to go. */
			fclose(new_fp);
			rename(newname,Dfile);
			free_rr(rrlp);
			break;
		}
		if(fstat(fileno(old_fp),&old_stat) != 0){
			printf("dfile_update: can't get old_file status!\n");
			fclose(new_fp);
			fclose(old_fp);
			free_rr(rrlp);
			break;
		}
		if((elapsed = (int32)(new_stat.st_ctime - old_stat.st_ctime)) < 0L)
			elapsed = -elapsed;	/* file times are inconsistant */

		/* Now append any non-duplicate records */
		oldrrp = NULL;
		while((frrp = get_rr(old_fp,oldrrp)) != NULL){
			free_rr(oldrrp);
			if(frrp->name == NULL
			&& frrp->comment != NULL)
				put_rr(new_fp,frrp);
			if(frrp->type != TYPE_MISSING
			&& frrp->rdlength > 0
			&& compare_rr_list(rrlp,frrp) != 0){
				if(frrp->ttl > 0L
				&& (frrp->ttl -= elapsed) <= 0L)
					frrp->ttl = 0L;
				if(frrp->ttl != 0 || !Dfile_clean)
					put_rr(new_fp,frrp);
			}
			oldrrp = frrp;
			if(!main_exit)
				kwait(NULL);	/* run in background */
		}
		free_rr(oldrrp);
		fclose(new_fp);
		fclose(old_fp);
		free_rr(rrlp);

		/* wait for everyone else to finish reading */
		Dfile_writing++;
		while(Dfile_reading > 0)
			kwait(&Dfile_writing);

		unlink(Dfile);
		rename(newname,Dfile);

		Dfile_writing = 0;
		ksignal(&Dfile_reading,0);
	}
	free(newname);

	logmsg(-1,"update Domain.txt finished");
	Dfile_updater = NULL;
}


/**
 **	Domain Server Utilities
 **/

static void
dumpdomain(dhp,rtt)
struct dhdr *dhp;
int32 rtt;
{
	struct rr *rrp;
	char * stuff;

	printf("response id %u (rtt %lu sec) qr %u opcode %u aa %u tc %u rd %u ra %u rcode %u\n",
	 dhp->id,(long)rtt / 1000L,
	 dhp->qr,dhp->opcode,dhp->aa,dhp->tc,dhp->rd,
	 dhp->ra,dhp->rcode);
	printf("%u questions:\n",dhp->qdcount);
	for(rrp = dhp->questions; rrp != NULL; rrp = rrp->next){
		stuff = dtype(rrp->type);
		printf("%s type %s class %u\n",rrp->name,
		 stuff,rrp->class);
	}
	printf("%u answers:\n",dhp->ancount);
	for(rrp = dhp->answers; rrp != NULL; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	printf("%u authority:\n",dhp->nscount);
	for(rrp = dhp->authority; rrp != NULL; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	printf("%u additional:\n",dhp->arcount);
	for(rrp = dhp->additional; rrp != NULL; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	fflush(stdout);
}

static int
dns_makequery(op,srrp,buffer,buflen)
uint16 op;	/* operation */
struct rr *srrp;/* Search RR */
uint8 *buffer;	/* Area for query */
uint16 buflen;	/* Length of same */
{
	uint8 *cp;
	char *cp1;
	char *dname, *sname;
	uint16 parameter;
	uint16 dlen,len;

	cp = buffer;
	/* Use millisecond clock for timestamping */
	cp = put16(cp,(uint16)msclock());
	parameter = (op << 11)
			| 0x0100;	/* Recursion desired */
	cp = put16(cp,parameter);
	cp = put16(cp,1);
	cp = put16(cp,0);
	cp = put16(cp,0);
	cp = put16(cp,0);

	sname = strdup(srrp->name);
	dname = sname;
	dlen = strlen(dname);
	for(;;){
		/* Look for next dot */
		cp1 = strchr(dname,'.');
		if(cp1 != NULL)
			len = cp1-dname;	/* More to come */
		else
			len = dlen;	/* Last component */
		*cp++ = len;		/* Write length of component */
		if(len == 0)
			break;
		/* Copy component up to (but not including) dot */
		strncpy((char *)cp,dname,len);
		cp += len;
		if(cp1 == NULL){
			*cp++ = 0;	/* Last one; write null and finish */
			break;
		}
		dname += len+1;
		dlen -= len+1;
	}
	free(sname);
	cp = put16(cp,srrp->type);
	cp = put16(cp,srrp->class);
	return cp - buffer;
}

/* domain server resolution loop
 * returns: any answers in cache.
 *	(future features)
 *	multiple queries.
 *	inverse queries.
 * return value: 0 if something added to cache, -1 if error
 */
static int
dns_query(rrlp)
struct rr *rrlp;
{
	struct mbuf *bp;
	struct dhdr *dhp;
	struct dserver *dp;	/* server list */
	int32 rtt,abserr;
	int tried = 0;		/* server list has been retried (count) */

	if((dp = Dservers) == NULL)
		return -1;

	for(;;){
		uint8 *buf;
		int len;
		struct sockaddr_in server_in;
		int s;
		int rval;

		dp->queries++;

		s = socket(AF_INET,SOCK_DGRAM,0);
		server_in.sin_family = AF_INET;
		server_in.sin_port = IPPORT_DOMAIN;
		server_in.sin_addr.s_addr = dp->address;

		if(Dtrace){
			printf("dns_query: querying server %s for %s\n",
			 inet_ntoa(dp->address),rrlp->name);
		}

		buf = mallocw(512);
		len = dns_makequery(0,rrlp,buf,512);
		if(sendto(s,buf,len,0,(struct sockaddr *)&server_in,sizeof(server_in)) == -1)
			perror("domain sendto");
		FREE(buf);
		kalarm(max(dp->timeout,100));
		/* Wait for something to happen */
		rval = recv_mbuf(s,&bp,0,NULL,0);
		kalarm(0L);
		close_s(s);

		if(Dtrace){
			if(errno == 0)
				printf("dns_query: received message length %d\n",rval);
			else
				perror("dns_query");
		}

		if(rval > 0)
			break;

		if(errno == EABORT)
			return -1;		/* Killed by "reset" command */

		/* Timeout; back off this one and try another server */
		dp->timeout <<= 1;
		if((dp = dp->next) == NULL){
			dp = Dservers;
			if(Dserver_retries > 0 && ++tried > Dserver_retries)
				return -1;
		}
	}

	/* got a response */
	dp->responses++;
	dhp = (struct dhdr *) mallocw(sizeof(struct dhdr));
	ntohdomain(dhp,&bp);	/* Convert to local format */

	/* Compute and update the round trip time */
	rtt = (int32) ((uint16)msclock() - dhp->id);
	abserr = rtt > dp->srtt ? rtt - dp->srtt : dp->srtt - rtt;
	dp->srtt = ((AGAIN-1) * dp->srtt + rtt + (AGAIN/2)) >> LAGAIN;
	dp->mdev = ((DGAIN-1) * dp->mdev + abserr + (DGAIN/2)) >> LDGAIN;
	dp->timeout = 4 * dp->mdev + dp->srtt;

	/* move to top of list for next time */
	if(dp->prev != NULL){
		dlist_drop(dp);
		dlist_add(dp);
	}

	if(Dtrace)
		dumpdomain(dhp,rtt);

	/* Add negative reply to answers.  This assumes that there was
	 * only one question, which is true for all questions we send.
	 */
	if(dhp->aa && (dhp->rcode == NAME_ERROR || dhp->ancount == 0)){
		register struct rr *rrp;
		long ttl = 600L; /* Default TTL for negative records */

		/* look for SOA ttl */
		for(rrp = dhp->authority; rrp != NULL; rrp = rrp->next){
			if(rrp->type == TYPE_SOA)
				ttl = rrp->ttl;
		}

		/* make the questions the negative answers */
		for(rrp = dhp->questions; rrp != NULL; rrp = rrp->next)
			rrp->ttl = ttl;
	} else {
		free_rr(dhp->questions);
		dhp->questions = NULL;
	}

	/* post in reverse order to maintain original order */
	dcache_update(dhp->additional);
	dcache_update(dhp->authority);
	dcache_update(dhp->answers);
	dcache_update(dhp->questions);

	Dfile_wait_absolute = secclock() + Dfile_wait_relative;
	if(Dfile_updater == NULL){
		Dfile_updater = newproc("domain update",
			512,dfile_update,0,NULL,NULL,0);
	}

#ifdef DEBUG
	if(Dtrace)
		keywait(NULL,1);	/* so we can look around */
#endif
	free(dhp);
	return 0;
}


/**
 **	Resolver Utilities
 **/

/* Return TRUE if string appears to be an IP address in dotted decimal;
 * return FALSE otherwise (i.e., if string is a domain name)
 */
static int
isaddr(s)
register char *s;
{
	char c;

	if(s == NULL)
		return TRUE;	/* Can't happen */

	while((c = *s++) != '\0'){
		if(c != '[' && c != ']' && !isdigit(c) && c != '.')
			return FALSE;
	}
	return TRUE;
}

/* Return "normalized" domain name, with default suffix and trailing '.'
 */
static char *
checksuffix(dname)
char *dname;
{
	char *sname, *tname;

	sname = strdup(dname);
	if(strchr(sname,'.') == NULL && Dsuffix != NULL){
		/* Append default suffix */
		tname = mallocw(strlen(sname)+strlen(Dsuffix)+2);
		sprintf(tname,"%s.%s",sname,Dsuffix);
		free(sname);
		sname = tname;
	}
	if(sname[strlen(sname)-1] != '.'){
		/* Append trailing dot */
		tname = mallocw(strlen(sname)+2);
		sprintf(tname,"%s.",sname);
		free(sname);
		sname = tname;
	}
	return sname;
}

/* Search for resource records.
 * Returns RR list, or NULL if no record found.
 */
static struct rr *
resolver(rrlp)
register struct rr *rrlp;
{
	register struct rr *result_rrlp;

	if((result_rrlp = dcache_search(rrlp)) == NULL){
		result_rrlp = dfile_search(rrlp);
	}
	if(result_rrlp == NULL || check_ttl(result_rrlp) != 0){
		dcache_add(result_rrlp); 	/* save any expired RRs */
		if(dns_query(rrlp) == -1)
			return NULL;
		result_rrlp = dcache_search(rrlp);
	}
	dcache_add(copy_rr_list(result_rrlp));
	return result_rrlp;
}

/* general entry point for address -> domain name resolution.
 * Returns RR list, or NULL if no record found.
 */
struct rr *
inverse_a(ip_address)
int32 ip_address;
{
	struct rr *prrp;
	struct rr *result_rrlp;
	char pname[30];

	if(ip_address == 0L)
		return NULL;

	sprintf( pname, "%u.%u.%u.%u.IN-ADDR.ARPA.",
			lobyte(loword(ip_address)),
			hibyte(loword(ip_address)),
			lobyte(hiword(ip_address)),
			hibyte(hiword(ip_address)) );

	prrp = make_rr(RR_QUERY,pname,CLASS_IN,TYPE_PTR,0,0,NULL);

	prrp->next = 		/* make list to speed search */
		make_rr(RR_INQUERY,NULL,CLASS_IN,TYPE_A,0,4,&ip_address);

	result_rrlp = resolver(prrp);

	free_rr(prrp);
	return result_rrlp;
}

/* general entry point for domain name -> resource resolution.
 * Returns RR list, or NULL if no record found.
 */
struct rr *
resolve_rr(dname,dtype)
char *dname;
uint16 dtype;
{
	struct rr *qrrp;
	struct rr *result_rrlp;
	char *sname;
	int looping = MAXCNAME;

	if(dname == NULL)
		return NULL;

	sname = checksuffix(dname);
	qrrp = make_rr(RR_QUERY,sname,CLASS_IN,dtype,0,0,NULL);
	FREE(sname);

	while(looping > 0){
		if((result_rrlp=resolver(qrrp)) == NULL
		|| result_rrlp->type == dtype)
			break;
#ifdef DEBUG
		if(Dtrace)
			put_rr(stdout,result_rrlp);
#endif
		/* Should be CNAME or PTR record */
		/* Replace name and try again */
		free(qrrp->name);
		qrrp->name = strdup(result_rrlp->rdata.name);
		free_rr(result_rrlp);
		result_rrlp = NULL;
		looping--;
	}
	free_rr(qrrp);
	return result_rrlp;
}

/* main entry point for address -> domain name resolution.
 * Returns string, or NULL if no name found.
 */
char *
resolve_a(ip_address,shorten)
int32 ip_address;		/* search address */
int shorten;			/* return only first part of name (flag)*/
{
	struct rr *save_rrlp, *rrlp;
	char *result = NULL;

	for( rrlp = save_rrlp = inverse_a(ip_address);
	     rrlp != NULL && result == NULL;
	     rrlp = rrlp->next ){
		if(rrlp->rdlength > 0){
			switch(rrlp->type){
			case TYPE_PTR:
				result = strdup(rrlp->rdata.name);
				break;
			case TYPE_A:
				result = strdup(rrlp->name);
				break;
			}
		}
	}
	free_rr(save_rrlp);

	if(result != NULL && shorten){
		int dot;
		char *shortened;

		if((dot = strcspn(result, ".")) == 0){
			shortened = mallocw(dot+1);
			strncpy(shortened, result, dot);
			shortened[dot] = '\0';
			free(result);
			result = shortened;
		}
	}
	return result;
}

/* Main entry point for domain name -> address resolution.
 * Returns 0 if name is currently unresolvable.
 */
int32
resolve(name)
char *name;
{
	register struct rr *rrlp;
	int32 ip_address = 0;

	if(name == NULL)
		return 0;

	if(isaddr(name))
		return aton(name);

	if((rrlp = resolve_rr(name,TYPE_A)) != NULL
	 && rrlp->rdlength > 0)
		ip_address = rrlp->rdata.addr;

	/* multi-homed hosts are handled here */
	if(rrlp != NULL && rrlp->next != NULL) {
		register struct rr *rrp;
		register struct route *rp;
		uint16 cost = MAXINT16;
		rrp = rrlp;
		/* choose the best of a set of routes */
		while(rrp != NULL) {
			if(rrp->rdlength > 0
			 && (rp = rt_lookup(rrp->rdata.addr)) != NULL
			 && rp->metric <= cost) {
				ip_address = rrp->rdata.addr;
				cost = rp->metric;
			}
			rrp = rrp->next;
		}
	}

	free_rr(rrlp);
	return ip_address;
}


/* Main entry point for MX record lookup.
 * Returns 0 if name is currently unresolvable.
 */
int32
resolve_mx(name)
char *name;
{
	register struct rr *rrp, *arrp;
	char *sname, *tmp, *cp;
	int32 addr, ip_address = 0;
	uint16 pref = MAXINT16;

	if(name == NULL)
		return 0;

	if(isaddr(name)){
		if((sname = resolve_a(aton(name),FALSE)) == NULL)
			return 0;
	}
	else
		sname = strdup(name);

	cp = sname;
	while(1){
		rrp = arrp = resolve_rr(sname,TYPE_MX);
		/* Search this list of rr's for an MX record */
		while(rrp != NULL){
			if(rrp->rdlength > 0 && rrp->rdata.mx.pref <= pref &&
			   (addr = resolve(rrp->rdata.mx.exch)) != 0L){
				pref = rrp->rdata.mx.pref;
				ip_address = addr;
			}
			rrp = rrp->next;
		}
		free_rr(arrp);
		if(ip_address != 0)
			break;
		/* Compose wild card one level up */
		if((cp = strchr(cp,'.')) == NULL)
			break;
		tmp = mallocw(strlen(cp)+2);
		sprintf(tmp,"*%s",cp);		/* wildcard expansion */
		free(sname);
		sname = tmp;
		cp = sname + 2;
	}
	free(sname);
	return ip_address;
}

/* Search for local records of the MB, MG and MR type. Returns list of
 * matching records.
 */
struct rr *
resolve_mailb(name)
char *name;		/* local username, without trailing dot */
{
	register struct rr *result_rrlp;
	struct rr *rrlp;
	char *sname;

	/* Append trailing dot */
	sname = mallocw(strlen(name)+2);
	sprintf(sname,"%s.",name);
	rrlp = make_rr(RR_QUERY,sname,CLASS_IN,TYPE_MB,0,0,NULL);
	rrlp->next = make_rr(RR_QUERY,sname,CLASS_IN,TYPE_MG,0,0,NULL);
	rrlp->next->next = make_rr(RR_QUERY,sname,CLASS_IN,TYPE_MR,0,0,NULL);
	FREE(sname);
	if((result_rrlp = dcache_search(rrlp)) == NULL){
		result_rrlp = dfile_search(rrlp);
	}
	free_rr(rrlp);
	if(Dsuffix != NULL){
		rrlp = result_rrlp;
		while(rrlp != NULL){	/* add domain suffix to data */
			if(rrlp->rdlength > 0 &&
			   rrlp->rdata.name[rrlp->rdlength-1] != '.'){
				sname = mallocw(rrlp->rdlength +
					strlen(Dsuffix)+2);
				sprintf(sname,"%s.%s",rrlp->rdata.name,Dsuffix);
				free(rrlp->rdata.name);
				rrlp->rdata.name = sname;
				rrlp->rdlength = strlen(sname);
			}
			rrlp = rrlp->next;
		}
	}
	dcache_add(copy_rr_list(result_rrlp));
	return result_rrlp;
}
