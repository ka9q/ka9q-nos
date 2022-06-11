#ifndef	_COMMANDS_H
#define	_COMMANDS_H

/* In n8250.c, amiga.c: */
int doasystat(int argc,char *argv[],void *p);
int fp_attach (int argc,char *argv[],void *p);

/* In alloc.c: */
int domem(int argc,char *argv[],void *p);

/* In amiga.c: */
int doamiga(int argc,char *argv[],void *p);

/* In arpcmd.c: */
int doarp(int argc,char *argv[],void *p);

/* In asy.c: */
int asy_attach(int argc,char *argv[],void *p);

/* In ax25cmd.c: */
int doax25(int argc,char *argv[],void *p);
int doaxheard(int argc,char *argv[],void *p);
int doaxdest(int argc,char *argv[],void *p);
int doconnect(int argc,char *argv[],void *p);

/* In bootp.c */
int dobootp(int argc,char *argv[],void *p);

/* In bootpd.c */
int bootpdcmd(int argc,char *argv[],void *p);

/* In dialer.c: */
int dodialer(int argc,char *argv[],void *p);

/* In dirutil.c: */
int docd(int argc,char *argv[],void *p);
int dodir(int argc,char *argv[],void *p);
int domkd(int argc,char *argv[],void *p);
int dormd(int argc,char *argv[],void *p);

/* In domain.c: */
int dodomain(int argc,char *argv[],void *p);

/* In drsi.c: */
int dodrstat(int argc,char *argv[],void *p);
int dr_attach(int argc,char *argv[],void *p);
int dodr(int argc,char *argv[],void *p);

/* In eagle.c: */
int eg_attach(int argc,char *argv[],void *p);
int doegstat(int argc,char *argv[],void *p);

/* In ec.c: */
int doetherstat(int argc,char *argv[],void *p);
int ec_attach(int argc,char *argv[],void *p);

/* In fax.c: */
int dofax(int argc,char *argv[],void *p);
int fax1(int argc,char *argv[],void *p);
int fax0(int argc,char *argv[],void *p);

/* In finger.c: */
int dofinger(int argc,char *argv[],void *p);

/* In fingerd.c: */
int finstart(int argc,char *argv[],void *p);
int fin0(int argc,char *argv[],void *p);

/* In ftpcli.c: */
int doftp(int argc,char *argv[],void *p);
int doabort(int argc,char *argv[],void *p);

/* In ftpserv.c: */
int ftpstart(int argc,char *argv[],void *p);
int ftp0(int argc,char *argv[],void *p);

/* In hapn.c: */
int dohapnstat(int argc,char *argv[],void *p);
int hapn_attach(int argc,char *argv[],void *p);

/* In hop.c: */
int dohop(int argc,char *argv[],void *p);

/* In hs.c: */
int dohs(int argc,char *argv[],void *p);
int hs_attach(int argc,char *argv[],void *p);

/* In icmpcmd.c: */
int doicmp(int argc,char *argv[],void *p);

/* In iface.c: */
int doifconfig(int argc,char *argv[],void *p);
int dodetach(int argc,char *argv[],void *p);

/* In ipcmd.c: */
int doip(int argc,char *argv[],void *p);
int doroute(int argc,char *argv[],void *p);

/* In ipsec.c: */
int dosec(int argc,char *argv[],void *p);

/* In ksp.c: */
int doksp(int argc,char *argv[],void *p);

/* In ksubr.c: */
int ps(int argc,char *argv[],void *p);

/* In lterm.c: */
int dolterm(int argc,char *argv[],void *p);

/* In main.c: */
int dodelete(int argc,char *argv[],void *p);
int dorename(int argc,char *argv[],void *p);
int doexit(int argc,char *argv[],void *p);
int dohostname(int argc,char *argv[],void *p);
int dolog(int argc,char *argv[],void *p);
int dohelp(int argc,char *argv[],void *p);
int doattach(int argc,char *argv[],void *p);
int doparam(int argc,char *argv[],void *p);
int dopage(int argc,char *argv[],void *p);
int domode(int argc,char *argv[],void *p);
int donothing(int argc,char *argv[],void *p);
int donrstat(int argc,char *argv[],void *p);
int doescape(int argc,char *argv[],void *p);
int doremote(int argc,char *argv[],void *p);
int doboot(int argc,char *argv[],void *p);
int dorepeat(int argc,char *argv[],void *p);
int dodebug(int argc,char *argv[],void *p);
int dowipe(int argc,char *argv[],void *p);

/* In mailbox.c: */
int dombox(int argc,char *argv[],void *p);

/* In nntpcli.c: */
int donntp(int argc,char *argv[],void *p);

/* In nrcmd.c: */
int donetrom(int argc,char *argv[],void *p);
int nr_attach(int argc,char *argv[],void *p);

/* In pc.c: */
int doshell(int argc,char *argv[],void *p);
int doisat(int argc,char *argv[],void *p);

/* In pc100.h: */
int pc_attach(int argc,char *argv[],void *p);

/* In pktdrvr.c: */
int pk_attach(int argc,char *argv[],void *p);

/* In pi.c: */
int pi_attach(int argc,char *argv[],void *p);
int dopistat(int argc,char *argv[],void *p);

/* In ping.c: */
int doping(int argc,char *argv[],void *p);

/* in popcli.c */
int dopop(int argc,char *argv[],void *p);

/* in popserv.c */
int pop1(int argc,char *argv[],void *p);
int pop0(int argc,char *argv[],void *p);

/* In qtso.c: */
int doqtso(int argc,char *argv[],void *p);

/* In rarp.c: */
int dorarp(int argc,char *argv[],void *p);

/* In ripcmd.c: */
int dorip(int argc,char *argv[],void *p);

/* In ripcmd.c: */
int doaddrefuse(int argc,char *argv[],void *p);
int dodroprefuse(int argc,char *argv[],void *p);
int dorip(int argc,char *argv[],void *p);
int doripadd(int argc,char *argv[],void *p);
int doripdrop(int argc,char *argv[],void *p);
int doripinit(int argc,char *argv[],void *p);
int doripmerge(int argc,char *argv[],void *p);
int doripreq(int argc,char *argv[],void *p);
int doripstat(int argc,char *argv[],void *p);
int doripstop(int argc,char *argv[],void *p);
int doriptrace(int argc,char *argv[],void *p);

/* In sb.c: */
int dosound(int argc,char *argv[],void *p);

/* In scc.c: */
int scc_attach(int argc,char *argv[],void *p);
int dosccstat(int argc,char *argv[],void *p);

/* In session.c: */
int dosession(int argc,char *argv[],void *p);
int go(int argc,char *argv[],void *p);
int doclose(int argc,char *argv[],void *p);
int doreset(int argc,char *argv[],void *p);
int dokick(int argc,char *argv[],void *p);
int dorecord(int argc,char *argv[],void *p);
int dosfsize(int argc,char *argv[],void *p);
int doupload(int argc,char *argv[],void *p);

/* In smisc.c: */
int dis1(int argc,char *argv[],void *p);
int dis0(int argc,char *argv[],void *p);
int echo1(int argc,char *argv[],void *p);
int echo0(int argc,char *argv[],void *p);
int rem1(int argc,char *argv[],void *p);
int rem0(int argc,char *argv[],void *p);
int term1(int argc,char *argv[],void *p);
int term0(int argc,char *argv[],void *p);
int bsr1(int argc,char *argv[],void *p);
int bsr0(int argc,char *argv[],void *p);

/* In smtpcli.c: */
int dosmtp(int argc,char *argv[],void *p);

/* In smtpserv.c: */
int smtp1(int argc,char *argv[],void *p);
int smtp0(int argc,char *argv[],void *p);

/* In sockcmd.c: */
int dosock(int argc,char *argv[],void *p);

/* In stdio.c: */
int dofiles(int argc,char *argv[],void *p);

/* In sw.c: */
int doswatch(int argc,char *argv[],void *p);

/* In tcpcmd.c: */
int dotcp(int argc,char *argv[],void *p);

/* In telnet.c: */
int doecho(int argc,char *argv[],void *p);
int doeol(int argc,char *argv[],void *p);
int dotelnet(int argc,char *argv[],void *p);
int dotopt(int argc,char *argv[],void *p);

/* In tip.c: */
int dotip(int argc,char *argv[],void *p);

/* In ttylink.c: */
int ttylstart(int argc,char *argv[],void *p);
int ttyl0(int argc,char *argv[],void *p);

/* In trace.c: */
int dotrace(int argc,char *argv[],void *p);

/* In udpcmd.c: */
int doudp(int argc,char *argv[],void *p);

/* In view.c: */
int doview(int argc,char *argv[],void *p);
void view(int,void *,void *);

#endif	/* _COMMANDS_H */
