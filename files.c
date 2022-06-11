/* System-dependent definitions of various files, spool directories, etc */
#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "netuser.h"
#include "files.h"
#include "md5.h"

#ifdef	MSDOS
char System[] = "MSDOS";
char *Startup = "/autoexec.net";	/* Initialization file */
char *Userfile = "/ftpusers";	/* Authorized FTP users and passwords */
char *Maillog = "/spool/mail.log";	/* mail log */
char *Mailspool = "/spool/mail";	/* Incoming mail */
char *Mailqdir = "/spool/mqueue";		/* Outgoing mail spool */
char *Mailqueue = "/spool/mqueue/*.wrk";	/* Outgoing mail work files */
char *Routeqdir = "/spool/rqueue";		/* queue for router */
char *Alias = "/alias";		/* the alias file */
char *Dfile = "/domain.txt";	/* Domain cache */
char *Fdir = "/finger";		/* Finger info directory */
char *Arealist = "/spool/areas";/* List of message areas */
char *Helpdir = "/spool/help";	/* Mailbox help file directory */
char *Rewritefile = "/spool/rewrite"; /* Address rewrite file */
char *Newsdir = "/spool/news";		/* News messages and NNTP data */
char *Popusers = "/popusers";		/* POP user and passwd file */
char *Signature = "/spool/signatur"; /* Mail signature file directory */
char *Forwardfile = "/spool/forward.bbs"; /* Mail forwarding file */
char *Historyfile = "/spool/history"; /* Message ID history file */
char *Tmpdir = "/tmp";
char Eol[] = "\r\n";
#define	SEPARATOR	"/"
#endif

#ifdef	UNIX
char System[] = "UNIX";
char *Startup = "./startup.net";	/* Initialization file */
char *Config = "./config.net";	/* Device configuration list */
char *Userfile = "./ftpusers";
char *Mailspool = "./mail";
char *Maillog = "./mail.log";	/* mail log */
char *Mailqdir = "./mqueue";
char *Mailqueue = "./mqueue/*.wrk";
char *Routeqdir = "./rqueue";		/* queue for router */
char *Alias = "./alias";	/* the alias file */
char *Dfile = "./domain.txt";	/* Domain cache */
char *Fdir = "./finger";		/* Finger info directory */
char *Arealist = "./areas";		/* List of message areas */
char *Helpdir = "./help";	/* Mailbox help file directory */
char *Rewritefile = "./rewrite"; /* Address rewrite file */
char *Newsdir = "./news";		/* News messages and NNTP data */
char *Popusers = "./popusers";		/* POP user and passwd file */
char *Signature = "./signatur"; /* Mail signature file directory */
char *Forwardfile = "./forward.bbs"; /* Mail forwarding file */
char *Historyfile = "./history"; /* Message ID history file */
Char *Tmpdir = "/tmp";
#define	SEPARATOR	"/"
char Eol[] = "\n";
#endif

#ifdef	AMIGA
char System[] = "AMIGA";
char *Startup = "TCPIP:net-startup";
char *Config = "TCPIP:config.net";	/* Device configuration list */
char *Userfile = "TCPIP:ftpusers";
char *Mailspool = "TCPIP:spool/mail";
char *Maillog = "TCPIP:spool/mail.log";
char *Mailqdir = "TCPIP:spool/mqueue";
char *Mailqueue = "TCPIP:spool/mqueue/#?.wrk";
char *Routeqdir = "TCPIP:spool/rqueue";		/* queue for router */
char *Alias = "TCPIP:alias";	/* the alias file */
char *Dfile = "TCPIP:domain.txt";	/* Domain cache */
char *Fdir = "TCPIP:finger";		/* Finger info directory */
char *Arealist = "TCPIP:spool/areas";	/* List of message areas */
char *Helpdir = "TCPIP:spool/help";	/* Mailbox help file directory */
char *Rewritefile = "TCPIP:spool/rewrite"; /* Address rewrite file */
char *Newsdir = "TCPIP:spool/news";	/* News messages and NNTP data */
char *Popusers = "TCPIP:/popusers";	/* POP user and passwd file */
char *Signature = "TCPIP:spool/signatur"; /* Mail signature file directory */
char *Forwardfile = "TCPIP:spool/forward.bbs"; /* Mail forwarding file */
char *Historyfile = "TCPIP:spool/history"; /* Message ID history file */
Char *Tmpdir = "TCPIP:tmp";
#define	SEPARATOR	"/"
char Eol[] = "\r\n";
#endif

#ifdef	MAC
char System[] = "MACOS";
char *Startup ="Mikes Hard Disk:net.start";
char *Config = "Mikes Hard Disk:config.net";	/* Device configuration list */
char *Userfile = "Mikes Hard Disk:ftpusers";
char *Mailspool = "Mikes Hard Disk:spool:mail:";
char *Maillog = "Mikes Hard Disk:spool:mail.log:";
char *Mailqdir = "Mikes Hard Disk:spool:mqueue:";
char *Mailqueue = "Mikes Hard Disk:spool:mqueue:*.wrk";
char *Routeqdir = "Mikes Hard Disk:spool/rqueue:";	/* queue for router */
char *Alias = "Mikes Hard Disk:alias";	/* the alias file */
char *Dfile = "Mikes Hard Disk:domain:txt";	/* Domain cache */
char *Fdir = "Mikes Hard Disk:finger";		/* Finger info directory */
char *Arealist = "Mikes Hard Disk:spool/areas";	/* List of message areas */
char *Helpdir = "Mikes Hard Disk:spool/help"; /* Mailbox help file directory */
char *Rewritefile = "Mikes Hard Disk:spool/rewrite"; /* Address rewrite file */
char *Newsdir = "Mikes Hard Disk:spool/news"; /* News messages and NNTP data */
char *Popusers = "Mikes Hard Disk:/popusers";	/* POP user and passwd file */
char *Signature = "Mikes Hard Disk:spool/signatur"; /* Mail signature file directory */
char *Forwardfile = "Mikes Hard Disk:spool/forward.bbs"; /* Mail forwarding file */
char *Historyfile = "Mikes Hard Disk:spool/history"; /* Message ID history file */
Char *Tmpdir = "Mikes Hard Disk:tmp";
#define	SEPARATOR	"/"
char Eol[] = "\r";
#endif

static char *rootdir = "";

/* Establish a root directory other than the default. Can only be called
 * once, at startup time
 */
void
initroot(root)
char *root;
{
	rootdir = strdup( root );

	Startup = rootdircat(Startup);
	Userfile = rootdircat(Userfile);
	Maillog = rootdircat(Maillog);
	Mailspool = rootdircat(Mailspool);
	Mailqdir = rootdircat(Mailqdir);
	Mailqueue = rootdircat(Mailqueue);
	Routeqdir = rootdircat(Routeqdir);
	Alias = rootdircat(Alias);
	Dfile = rootdircat(Dfile);
	Fdir = rootdircat(Fdir);
	Arealist = rootdircat(Arealist);
	Helpdir = rootdircat(Helpdir);
	Rewritefile = rootdircat(Rewritefile);
	Newsdir = rootdircat(Newsdir);
	Signature = rootdircat(Signature);
	Forwardfile = rootdircat(Forwardfile);
	Historyfile = rootdircat(Historyfile);
}

/* Concatenate root, separator and arg strings into a malloc'ed output
 * buffer, then remove repeated occurrences of the separator char
 */
char *
rootdircat(filename)
char *filename;
{
	char *out = filename;

	if ( strlen(rootdir) > 0 ) {
		char *separator = SEPARATOR;

		out = mallocw( strlen(rootdir)
				+ strlen(separator)
				+ strlen(filename) + 1);

		strcpy(out,rootdir);
		strcat(out,separator);
		strcat(out,filename);
		if(*separator != '\0'){
			char *p1, *p2;

			/* Remove any repeated occurrences */
			p1 = p2 = out;
			while(*p2 != '\0'){
				*p1++ = *p2++;
				while(p2[0] == p2[-1] && p2[0] == *separator)
					p2++;
			}
			*p1 = '\0';
		}
	}
	return out;
}

/* Read through FTPUSERS looking for user record
 * Returns line which matches username, or NULL when no match.
 * Each of the other variables must be copied before freeing the line.
 */
char *
userlookup(username,password,directory,permission,ip_address)
char *username;
char **password;
char **directory;
int   *permission;
int32 *ip_address;
{
	FILE *fp;
	char *buf;
	char *cp;

	if((fp = fopen(Userfile,READ_TEXT)) == NULL)
		/* Userfile doesn't exist */
		return NULL;

	buf = mallocw(BUFSIZ);
	while ( fgets(buf,BUFSIZ,fp) != NULL ){
		if(*buf == '#')
			continue;	/* Comment */

		if((cp = strchr(buf,' ')) == NULL)
			/* Bogus entry */
			continue;
		*cp++ = '\0';		/* Now points to password */

		if( stricmp(username,buf) == 0 )
			break;		/* Found user */
	}
	if(feof(fp)){
		/* username not found in file */
		fclose(fp);
		free(buf);
		return NULL;
	}
	fclose(fp);

	if ( password != NULL )
		*password = cp;

	/* Look for space after password field in file */
	if((cp = strchr(cp,' ')) == NULL) {
		/* Invalid file entry */
		free(buf);
		return NULL;
	}
	*cp++ = '\0';	/* Now points to directory field */

	if ( directory != NULL )
		*directory = cp;

	if((cp = strchr(cp,' ')) == NULL) {
		/* Permission field missing */
		free(buf);
		return NULL;
	}
	*cp++ = '\0';	/* now points to permission field */

	if ( permission != NULL )
		*permission = (int)strtol( cp, NULL, 0 );

	if((cp = strchr(cp,' ')) == NULL) {
		/* IP address missing */
		if ( ip_address != NULL )
			*ip_address = 0L;
	} else {
		*cp++ = '\0';	/* now points at IP address field */
		if ( ip_address != NULL )
			*ip_address = resolve( cp );
	}
	return buf;
}

/* Subroutine for logging in the user whose name is name and password is pass.
 * The buffer path should be long enough to keep a line from the userfile.
 * If pwdignore is true, the password check will be overridden.
 * The return value is the permissions field or -1 if the login failed.
 * Path is set to point at the path field, and pwdignore will be true if no
 * particular password was needed for this user.
 */
int
userlogin(name,pass,path,len,pwdignore)
char *name;
char *pass;
char **path;
int len;			/* Length of buffer pointed at by *path */
int *pwdignore;
{
	char *buf;
	char *password;
	char *directory;
	int permission;
	int anonymous;
	char *cp;
	uint8 hashpass[16],digest[16];
	MD5_CTX md;

	if ( (buf = userlookup( name, &password, &directory,
			&permission, NULL )) == NULL ) {
		return -1;
	}

	anonymous = *pwdignore;
	if(strcmp(password,"*") == 0){
		anonymous = TRUE;	/* User ID is password-free */
	} else {
		if(readhex(hashpass,password,sizeof(hashpass)) != sizeof(hashpass)){
			/* Invalid hashed password in file */
			free(buf);
			return -1;
		}
		MD5Init(&md);
		MD5Update(&md,(unsigned char *)name,strlen(name));
		MD5Update(&md,(unsigned char *)pass,strlen(pass));
		MD5Final(digest,&md);
		if(memcmp(digest,hashpass,sizeof(hashpass)) != 0){
			/* Incorrect password given */
			free(buf);
			return -1;
		}
	}
	if ( strlen( directory ) + 1 > len ) {
		/* not enough room for path */
		free(buf);
		return -1;
	}

#if   defined(AMIGA)
	/*
	 * Well, on the Amiga, a file can be referenced by many names:
	 * device names (DF0:) or volume names (My_Disk:).  This hunk of code
	 * passed the pathname specified in the ftpusers file, and gets the
	 * absolute path copied into the user's buffer.  We really should just
	 * allocate the buffer and return a pointer to it, since the caller
	 * really doesn't have a good idea how long the path string is..
	 */
	if ( (directory = pathname("", directory)) != NULL ) {
		strcpy(*path, directory);
		free(directory);
	} else {
		**path = '\0';
	}
#else
	strcpy(*path,directory);
	/* Convert any backslashes to forward slashes, for backward
	 * compatibility with the old NET
	 */
	while((cp = strchr(*path,'\\')) != NULL)
		*cp = '/';
#endif
	free(buf);
	*pwdignore = anonymous;
	/* Finally return the permission bits */
	return permission;
}
/* MD5 hash plaintext passwords in user file */
void
usercvt()
{
	FILE *fp,*fptmp;
	char *buf;
	uint8 hexbuf[16],digest[16];
	int needsit = 0;
	int len,nlen,plen,i;
	char *pass;
	MD5_CTX md;

	if((fp = fopen(Userfile,READ_TEXT)) == NULL)
		return;		/* Userfile doesn't exist */

	buf = mallocw(BUFSIZ);
	while(fgets(buf,BUFSIZ,fp) != NULL){
		rip(buf);
		len = strlen(buf);
		if(len == 0 || *buf == '#')
			continue;	/* Blank or comment line */

		if((nlen = strcspn(buf,Whitespace)) == len)
			continue;	/* No end to the name! */

		/* Skip whitespace between name and pass */
		for(pass=&buf[nlen];isspace(*pass);pass++)
			;
		if(*pass != '\0' && *pass != '*'
		 && readhex(hexbuf,pass,sizeof(hexbuf)) != 16){
			needsit = 1;
			break;
		}
	}
	if(!needsit){
		/* Everything is in order */
		fclose(fp);
		free(buf);
		return;
	}
	/* At least one entry needs its password hashed */
	rewind(fp);
	fptmp = tmpfile();
	while(fgets(buf,BUFSIZ,fp) != NULL){
		rip(buf);
		if((len = strlen(buf)) == 0 || *buf == '#'
		 || (nlen = strcspn(buf,Whitespace)) == len){
			/* Line is blank, a comment or unparseable;
			 * copy unchanged
			 */
			fputs(buf,fptmp);
			fputc('\n',fptmp);
			continue;
		}
		/* Skip whitespace between name and pass */
		for(pass=&buf[nlen];isspace(*pass);pass++)
			;

		if(*pass == '\0' || *pass == '*'
		 || (plen = strcspn(pass,Whitespace)) == strlen(pass)
		 || readhex(hexbuf,pass,sizeof(hexbuf)) == sizeof(hexbuf)){
			/* Other fields are missing, no password is required,
			 * or password is already hashed; copy unchanged
			 */
			fputs(buf,fptmp);
			fputc('\n',fptmp);
			continue;
		}
		MD5Init(&md);
		MD5Update(&md,(unsigned char *)buf,nlen);	/* Hash name */
		MD5Update(&md,(unsigned char *)pass,plen);	/* Hash password */
		MD5Final(digest,&md);
		fwrite(buf,1,nlen,fptmp);	/* Write name */
		fputc(' ',fptmp);		/* and space */
		for(i=0;i<16;i++)	/* Write hashed password */
			fprintf(fptmp,"%02x",digest[i]);
		fputs(&pass[plen],fptmp);	/* Write remainder of line */
		fputc('\n',fptmp);
	}
	/* Now copy the temp file back into the userfile */
	fclose(fp);
	rewind(fptmp);
	if((fp = fopen(Userfile,WRITE_TEXT)) == NULL){
		printf("Can't rewrite %s\n",Userfile);
		free(buf);
		return;
	}
	while(fgets(buf,BUFSIZ,fptmp) != NULL)
		fputs(buf,fp);
	fclose(fp);
	fclose(fptmp);
	free(buf);
}
