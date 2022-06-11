/*************************************************/
/* Center for Information Technology Integration */
/*           The University of Michigan          */
/*                    Ann Arbor                  */
/*                                               */
/* Dedicated to the public domain.               */
/* Send questions to info@citi.umich.edu         */
/*                                               */
/* BOOTP is documented in RFC 951 and RFC 1048   */
/*************************************************/



#include <stdio.h>
#include <sys\types.h>
#include <sys\stat.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
#include "global.h"
#include "config.h"
#include "cmdparse.h"
#include "bootpd.h"
#include "netuser.h"
#include "iface.h"
#include "udp.h"
#include "arp.h"

#define BP_DEFAULT_TAB "bootptab"
#define BP_DEFAULT_LOG "bootplog"
#define BP_DEFAULT_DIR "bpfiles"
#define BP_DEFAULT_FILE "boot"

static char    *bootptab = BP_DEFAULT_TAB;
static FILE    *bootfp;                 /* bootptab fp */
static long     modtime;          	/* last modification time of bootptab */

static char    bootplog[64] = BP_DEFAULT_LOG;
static int	LogInFile = 0;	 	/* Should bp_log log in a file? */
static int	LogOnScreen = 0;	/* Should bp_log log on screen? */

static char    *line;            /* line buffer for reading bootptab */
static int     linenum;          /* current ilne number in bootptab */

extern int	Nhosts;          /* number of hosts in host structure */
extern struct host hosts[MHOSTS];

extern char    homedir[64];      /* bootfile homedirectory */
extern char    defaultboot[64];  /* default file to boot */
extern int32   bp_DefaultDomainNS[BP_MAXDNS]; /* default domain name server */
extern int	Nhosts;
extern struct udp_cb *Bootpd_cb;





static int bp_Homedir(int argc,char *argv[],void *p);
static int bp_DefaultFile(int argc,char *argv[],void *p);
static int bp_DynamicRange(int argc,char *argv[],void *p);
static int bp_donothing(int argc,char *argv[],void *p);
static int bp_Host(int argc,char *argv[],void *p);
static int bp_rmHost(int argc,char *argv[],void *p);
static int bp_DomainNS(int argc,char *argv[],void *p);
static int bp_Start(int argc,char *argv[],void *p);
static int bp_Stop(int argc,char *argv[],void *p);
static int bp_logFile(int argc,char *argv[],void *p);
static int bp_logScreen(int argc,char *argv[],void *p);
static void dumphosts(void);

void bootpd(struct iface *iface, struct udp_cb *sock, int cnt);

static struct cmds BootpdCmds[] = {
        "",             bp_donothing,           0, 0, NULL,
	"start", 	bp_Start,		0, 0, NULL,
	"stop",		bp_Stop,		0, 0, NULL,
	"dns",		bp_DomainNS,		0, 0, NULL,
        "dynip",    	bp_DynamicRange,     	0, 0, NULL,
	"host",		bp_Host,		0, 0, NULL,
	"rmhost",	bp_rmHost,		0, 0, NULL,
        "homedir",      bp_Homedir,          	0, 0, NULL,
        "defaultfile",  bp_DefaultFile,      	0, 0, NULL,
	"logfile",	bp_logFile,		0, 0, NULL,
	"logscreen",	bp_logScreen,		0, 0, NULL,
        NULL,       NULL,                 0, 0, NULL
};



int 
bootpdcmd (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	return subcmd (BootpdCmds, argc, argv, p);
}


/* Start up bootp service */
static int
bp_Start (argc,argv,p)
int argc;
char *argv[];
void *p;
{

        struct socket lsock;
        time_t tloc;
	char *usage = "bootpd start\n";

	if (argc != 1) {
		printf (usage);
		return (-1);
	}

	time(&tloc);
        bp_log ("\n\n####BOOTP server starting at %s\n", ctime(&tloc));

        lsock.address = INADDR_ANY;
        lsock.port = IPPORT_BOOTPS;

        /* This way is better than recvfrom because it passes the iface in bootpd call */
       /* Listen doesn't work for datagrams. */

        if (Bootpd_cb == NULL) {
                if ((Bootpd_cb = open_udp(&lsock, bootpd)) == NULL) {
			printf ("bootpd: can't open_udp\n");	
			return (-1);
		}
        }

        /*
         * Read the bootptab file once immediately upon startup.
         */

        da_init();

        readtab();

        return (0);
}



/* Stop bootp service */
static int
bp_Stop (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	time_t now;
	char *usage = "bootpd stop\n";

	if (argc != 1) {
		printf (usage);
		return -1;
	}

	time (&now);

        Nhosts = 0;
        da_shut();
        readtab_shut();
        del_udp (Bootpd_cb);
        Bootpd_cb = NULL;

	bp_log ("Bootpd shutdown %s", ctime (&now));
        return (0);
};



static int
bp_logFile (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	int i;
	time_t now;
	char *usage = "bootpd logfile [<file_name> | default] [on | off] \n"; 

	time (&now);

	if (argc == 1) {
		if (LogInFile)
                	printf ("Bootpd logging to file '%s' turned on.\n", bootplog);
		else 
                	printf ("Bootpd logging to file '%s' turned off.\n", bootplog);
	}
	else {
		for (i = 1; i < argc; i++) {

			if (strcmp ("?", argv[i]) == 0) 
				printf (usage);

			else if (strcmp ("off", argv[i]) == 0) {
				bp_log ("Stopping file logging at %s", ctime(&now));
				LogInFile = 0;
			}
			else if (strcmp ("on", argv[i]) == 0) {
				LogInFile = 1;
				bp_log ("Starting file logging at %s", ctime(&now));
			}
			else if (strcmp ("default", argv[i]) == 0) {
				strcpy (bootplog, BP_DEFAULT_LOG);
				bp_log ("File for logging set to %s\n", bootplog);
			}
			else {
				strcpy (bootplog, argv[1]);
				bp_log ("File for logging set to %s\n", bootplog);
			}	
		}
	}
	return 0;
}


static int
bp_logScreen (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	char *usage = "bootpd logscreen [on | off]\n";	

        if (argc == 1)
		if (LogOnScreen)	
                	printf ("Bootpd logging on screen turned on.\n");
		else 
                	printf ("Bootpd logging on screen turned off.\n");

        else if (argc == 2)  {
                if  (strcmp ("on", argv[1]) == 0)
                        LogOnScreen = 1;
                else if  (strcmp ("off", argv[1]) == 0)
                        LogOnScreen = 0;
		else printf (usage);
	}
	else printf (usage);
	return 0;
}




static int
bp_DomainNS (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	int a0, a1, a2, a3;
	int i;
	char *usage = "bootpd dns [<IP addr of domain name server>...]\n";

	if (argc == 1) {
		printf ("Bootp domain name servers: ");
		for (i=0; (i < BP_MAXDNS) && (bp_DefaultDomainNS[i] != 0); i++) 
			printf (" %s", inet_ntoa (bp_DefaultDomainNS[i]));
		printf ("\n");
		return (0);
	}

	if (argc > 1) {
		if ((argc == 2) && (strcmp ("?", argv[1]) == 0)) {
			printf (usage);
			return 0;
		}
			
		/* A list of name servers has been given */
		/* reset the domain name server list */
		for (i= 0; i < BP_MAXDNS; i++) 
			bp_DefaultDomainNS[i] = 0;

		/* get ip address */
		for (i = 1; (i < argc) && (i < BP_MAXDNS); i++) {
                	if (4 != sscanf (argv[i], "%d.%d.%d.%d", &a0, &a1, &a2, &a3)) {
                       	 	printf("bad internet address: %s\n", argv[1], linenum);
				return  -1;
                	}
	        	bp_DefaultDomainNS[i-1] = aton(argv[i]);
		}
	}
	/* record for the loggers sake */
	bp_log ("Bootp domain name servers: ");
	for (i=0; (i < BP_MAXDNS) && (bp_DefaultDomainNS[i] != 0); i++) 
		bp_log (" %s", inet_ntoa (bp_DefaultDomainNS[i]));
	bp_log ("\n");
	return 0;
}



static int
bp_rmHost (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	int i;
	struct host *hp = NULL;
	struct host *cp = NULL;
	char *usage = "bootpd rmhost <host name>\n";


	if (argc == 2) {
		
		/* Find the host record */
		for (i=0; i < Nhosts; i++) {
			if (strcmp (hosts[i].name, argv[1]) == 0) {
				hp = &(hosts[i]);
				break;
			}
		}	
		/* Return if not found */
		if (hp == NULL) {
			printf ("Host %s not in host tables.\n", argv[1]);
			return -1;
		}
		bp_log ("Host %s removed from host table\n", hp->name);
		cp = &(hosts [Nhosts - 1]);
		if (hp < cp) 
			memcpy(hp,cp,sizeof(struct host));
		Nhosts--;
		return 0;
	}
	else printf (usage);
	return 0;
}


/*
 * Printout the hosts table.
*/
static void
dumphosts()
{
        int i;
        struct host *hp;
	struct arp_type *at;

        printf ("\n\nStatus of host table\n");

        if (Nhosts == 0) {
                printf ("     No hosts in host table\n");
                return;
        }
        for (i = 0; i <= Nhosts-1; i++) {
                hp = &hosts[i];
		at = &Arp_type[hp->htype];
		
                printf ("%s  %s  %s  %s  '%s'\n",
                        hp->name, ArpNames[hp->htype], (*at->format)(bp_ascii, hp->haddr),
                        inet_ntoa ((int32)hp->iaddr.s_addr),
                        hp->bootfile);

        }
}


static int
bp_Host (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	struct host *hp;
	int a0, a1, a2, a3;
	struct arp_type *at;
	char *usage = "bootpd host [<hostname> <hardware type> <hardware addr> <ip addr> [boot file]]\n";

	switch (argc) {
	case 1:	
		dumphosts();
		break;
	case 5:
	case 6:
	
		hp = &hosts[Nhosts];
		
		/* get host name */
		strncpy (hp->name, argv[1], sizeof (hp->name));

		/* get hardware type */
		/* This code borrowed from Phil Karn's arpcmd.c */
		/* This is a kludge. It really ought to be table driven */
		switch(tolower(argv[2][0])){
			case 'n':       /* Net/Rom pseudo-type */
				hp->htype = ARP_NETROM;
				break;
			case 'e': /* "ether" */
				hp->htype = ARP_ETHER;
				break;
			case 'a': /* "ax25" */
				hp->htype = ARP_AX25;
				break;
			case 'm': /* "mac appletalk" */
				hp->htype = ARP_APPLETALK;
				break;
			default:
				printf("unknown hardware type \"%s\"\n",argv[2]);
				return -1;
		}

		at = &Arp_type[hp->htype];
		if(at->scan == NULL){
			return 1;
		}
		/* Destination address */
		(*at->scan)(hp->haddr,argv[3]);


		/* get ip address */
                if (4 != sscanf (argv[4], "%d.%d.%d.%d", &a0, &a1, &a2, &a3))
                {
                        printf("bad internet address: %s\n", argv[1], linenum);
                        return (0);
                }
	        hp->iaddr.s_addr = aton(argv[4]);

		/* get the bootpfile */
		if (argc == 6) strncpy (hp->bootfile, argv[5], sizeof (hp->bootfile));
		else hp->bootfile[0] = 0;

                bp_log ("Host added: %s  %s  %s  %s  '%s'\n",
                        hp->name, ArpNames[hp->htype], (*at->format)(bp_ascii, hp->haddr),
                        inet_ntoa ((int32)hp->iaddr.s_addr),
                        hp->bootfile);


		Nhosts++;
		break;
	
	default:
		printf (usage);
		break;
	}
	return 0;
}



static int	
bp_Homedir (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	char *usage = "bootpd homedir [<name of home directory> | default]\n";

	if (argc == 1) 	
		printf ("Bootp home directory: '%s'\n", homedir);
	else if (argc == 2) {
		if (strcmp (argv[1], "?") == 0)
			printf (usage);
		else if (strcmp (argv[1], "default") == 0) {
			strcpy (homedir, BP_DEFAULT_DIR);
			bp_log ("Bootp home directory set to: '%s'\n", homedir);
		}
		else {
			strcpy (homedir, argv[1]);
			bp_log ("Bootp home directory set to: '%s'\n", homedir);
		}
	}
	else printf (usage);
	return (0);
};



static int
bp_DefaultFile (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	char *usage = "bootpd defaultfile [<name of default boot file> | default]\n";

        if (argc == 1)
                printf ("Bootp default boot file:  '%s'\n", defaultboot);
        else if (argc == 2) {
		if (strcmp (argv[1], "?") == 0)
                        printf (usage);
                else if (strcmp (argv[1], "default") == 0)
                        strcpy (defaultboot, BP_DEFAULT_FILE);
                else {
                        strcpy (defaultboot, argv[1]);
                	bp_log ("Bootp default boot file set to:  '%s'\n", defaultboot);
		}
        }
	else
                printf (usage);

	return  (0);
};


static int
bp_DynamicRange (argc, argv, p)
int argc;
char *argv[];
void *p;
{
	int i0, i1, i2, i3;
	int32 start, end;
	struct iface *iface;
	char *usage = "bootpd dynip [<net name> | <netname>  <IP address> <IP address> | <netname> off]\n";

	if (argc == 1) {
		da_status (NULL);
		return 0;
	}
	if ((argc == 2) && (strcmp ("?", argv[1]) == 0)) {
			printf (usage);
			return 0;
	}

	/* get the interface */
	iface = if_lookup (argv[1]);
	if (iface == NULL) {
		printf ("network '%s' not found\n", argv[1]);
		return  (-1);
	}
	if (argc == 2) {
		da_status (iface);
		return 0;
	}
	if (argc == 3) {
		if (strcmp ("off", argv[2]) == 0) 
			da_done_net (iface);
		else printf (usage);
	}
	else if (argc == 4) {
		
	        /* get ip address */
                /* check the ip address - isaddr isn't a ka9q function */
                if ((4 != sscanf (argv[2], "%d.%d.%d.%d", &i0, &i1, &i2, &i3)) || 
			(i0 > 255) || (i1 > 255) || (i2 > 255) || (i3 > 255)
		)
                {
                	printf("bad internet address: %s\n", argv[2], linenum);
                        return (-1);
                }

                if ((4 != sscanf (argv[3], "%d.%d.%d.%d", &i0, &i1, &i2, &i3)) || 
			(i0 > 255) || (i1 > 255) || (i2 > 255) || (i3 > 255)
		)
                {
                	printf("bad internet address: %s\n", argv[3], linenum);
                        return (-1);
                }

               	start = aton(argv[2]);
            	end = aton(argv[3]);

		da_serve_net (iface, start, end);

	}
	else {
		printf (usage);
		return (0);
	}

	return (0);
};


static int
bp_donothing (argc, argv, p)
int argc;
char *argv[];
void *p;
{
        return (0);
}


/*
 * Read bootptab database file.  Avoid rereading the file if the
 * write date hasn't changed since the last time we read it.
 */
int
readtab()
{
        struct stat st;

        /* If the file hasn't been opened, open it. */
        if (bootfp == 0) {
                if ((bootfp = fopen(bootptab, "r")) == NULL) {
                        bp_log("Can't open bootptab file: %s\n", bootptab);
                        return (-1);
                }
        }

        /* Only reread if modified */
        stat (bootptab, &st);
        if (st.st_mtime == modtime && st.st_nlink) {
                return (0); /* hasnt been modified or deleted yet */
        }
        /* It's been changed, reread. */

        if ((bootfp = fopen(bootptab, "r")) == NULL) {
                bp_log("Can't open %s\n", bootptab);
                return (-1);
        }
        fstat(fileno(bootfp), &st);
        bp_log("(re)reading %s\n", bootptab);
        modtime = st.st_mtime;

        /*
         * read and parse each line in the file.
         */

	line = mallocw(BUFSIZ);	
	
	while (fgets(line, BUFSIZ, bootfp) != NULL) {
		linenum++;


		if ((line[0] == 0) || (line[0] == '#') || (line[0] == ' '))
			continue;

                if (cmdparse (BootpdCmds, line, NULL) ==  -1)
                         continue;

        }
        fclose(bootfp);
	free (line);
	return (0);
}

void
readtab_shut()
{
	modtime = 0;
}

/*
 * log an error message
 *
 */
void
bp_log(char *fmt,...)
{
        FILE *fp;
	va_list ap;

        if (LogOnScreen) {
		va_start(ap,fmt);
		vprintf(fmt, ap);
		va_end(ap);
		fflush (stdout);
	}
        if (LogInFile) {
                if ((fp = fopen(bootplog, "a+")) == NULL) {
                        printf ("Cannot open bootplog.\n");
                        return;
                }
		va_start(ap,fmt);
		vfprintf(fp, fmt, ap);
		va_end(ap);
                fflush(fp);
                fclose(fp);
        }
}
