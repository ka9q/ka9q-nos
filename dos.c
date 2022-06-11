/* This file provides direct calls to the MS-DOS file system primitives
 * _creat(), _open(), _close(), _read(), _write(), _lseek() and dup()
 *
 * These are necessary to allow more open file handles than permitted
 * by the compiled-in table sizes in the Borland C library
 */

#include <io.h>
#include <dos.h>
#include <errno.h>
#include <fcntl.h>
#include "stdio.h"
#include "global.h"

/* Table for mapping MS-DOS errors to UNIX errno values */
static int _ioerr[] = {
    0,			/*  0 - OK		     */
    EINVAL,		/*  1 - e_badFunction	     */
    ENOENT,		/*  2 - e_fileNotFound	     */
    ENOENT,		/*  3 - e_pathNotFound	     */
    EMFILE,		/*  4 - e_tooManyOpen	     */
    EACCES,		/*  5 - e_accessDenied	     */
    EBADF,		/*  6 - e_badHandle	     */
    ENOMEM,		/*  7 - e_mcbDestroyed	     */
    ENOMEM,		/*  8 - e_outOfMemory	     */
    ENOMEM,		/*  9 - e_badBlock	     */
    E2BIG,		/* 10  e_badEnviron	    */
    ENOEXEC,		/* 11  e_badFormat	    */
    EACCES,		/* 12  e_badAccess	    */
    EINVAL,		/* 13  e_badData	    */
    EFAULT,		/* 14  reserved		    */
    EXDEV,		/* 15  e_badDrive	    */
    EACCES,		/* 16  e_isCurrentDir	    */
    ENOTSAM,		/* 17  e_notSameDevice	    */
    ENOENT,		/* 18  e_noMoreFiles	    */
    EROFS,		/* 19  e_readOnly	    */
    ENXIO,		/* 20  e_unknownUnit	    */
    EBUSY,		/* 21  e_notReady	    */
    EIO,		/* 22  e_unknownCommand     */
    EIO,		/* 23  e_dataError	    */
    EIO,		/* 24  e_badRequestLength   */
    EIO,		/* 25  e_seekError	    */
    EIO,		/* 26  e_unknownMedia	    */
    ENXIO,		/* 27  e_sectorNotFound     */
    EBUSY,		/* 28  e_outOfPaper	    */
    EIO,		/* 29  e_writeFault	    */
    EIO,		/* 30  e_readFault	    */
    EIO,		/* 31  e_generalFault	    */
    EACCES,		/* 32  e_sharing	    */
    EACCES,		/* 33  e_lock		    */
    ENXIO,		/* 34  e_diskChange	    */
    ENFILE,		/* 35  e_FCBunavailable     */
    ENFILE,		/* 36  e_sharingOverflow    */
    EFAULT, EFAULT,
    EFAULT, EFAULT,
    EFAULT, EFAULT,
    EFAULT, EFAULT,
    EFAULT, EFAULT,
    EFAULT, EFAULT,
    EFAULT,		/* 37-49  reserved	    */
    ENODEV,		/* 50  e_networkUnsupported */
    EBUSY,		/* 51  e_notListening	    */
    EEXIST,		/* 52  e_dupNameOnNet	    */
    ENOENT,		/* 53  e_nameNotOnNet	    */
    EBUSY,		/* 54  e_netBusy	    */
    ENODEV,		/* 55  e_netDeviceGone	    */
    EAGAIN,		/* 56  e_netCommandLimit    */
    EIO,		/* 57  e_netHardError	    */
    EIO,		/* 58  e_wrongNetResponse   */
    EIO,		/* 59  e_netError	    */
    EINVAL,		/* 60  e_remoteIncompatible */
    EFBIG,		/* 61  e_printQueueFull     */
    ENOSPC,		/* 62  e_printFileSpace     */
    ENOENT,		/* 63  e_printFileDeleted   */
    ENOENT,		/* 64  e_netNameDeleted     */
    EACCES,		/* 65  e_netAccessDenied    */
    ENODEV,		/* 66  e_netDeviceWrong     */
    ENOENT,		/* 67  e_netNameNotFound    */
    ENFILE,		/* 68  e_netNameLimit	    */
    EIO,		/* 69  e_netBIOSlimit	    */
    EAGAIN,		/* 70  e_paused		    */
    EINVAL,		/* 71  e_netRequestRefused  */
    EAGAIN,		/* 72  e_redirectionPaused  */
    EFAULT, EFAULT,
    EFAULT, EFAULT,
    EFAULT, EFAULT,
    EFAULT,		/* 73- 79  reserved	    */
    EEXIST,		/* 80  e_fileExists	    */
    EFAULT,		/* 81  reserved		    */
    ENOSPC,		/* 82  e_cannotMake	    */
    EIO,		/* 83  e_failInt24	    */
    ENFILE,		/* 84  e_redirectionLimit   */
    EEXIST,		/* 85  e_dupRedirection     */
    EPERM,		/* 86  e_password	    */
    EINVAL,		/* 87  e_parameter	    */
    EIO,		/* 88  e_netDevice	    */
};
#define	NERROR	89

/* Reference count table for open file descriptors */
unsigned *Refcnt;

int
_creat(file,mode)
const char *file;	/* File name to create */
int mode;		/* Ignored */
{
	union REGS regs;
	struct SREGS segregs;
	int fd;

	segregs.ds = FP_SEG(file);
	regs.x.dx = FP_OFF(file);
	regs.x.cx = 0;	/* Normal attributes */
	regs.h.ah = 0x3c;
	intdosx(&regs,&regs,&segregs);
	fd = regs.x.ax;
	if(regs.x.cflag){
		if(fd < NERROR)
			errno = _ioerr[fd];
		return -1;
	}
	Refcnt[fd] = 1;
	return fd;	/* Return handle */
}
int
_open(file,mode)
const char *file;
int mode;
{
	union REGS regs;
	struct SREGS segregs;
	int dosmode,fd;

	if(mode & O_TRUNC){
		remove(file);
		mode |= O_CREAT;
	}
	/* Translate unix to MS-DOS open modes */
	switch(mode & (O_RDONLY|O_WRONLY|O_RDWR)){
	case O_RDONLY:
		dosmode = 0;
		break;
	case O_WRONLY:
		dosmode = 1;
		break;
	case O_RDWR:
		dosmode = 2;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	if(mode & O_EXCL)
		dosmode |= 0x10;
	
	segregs.ds = FP_SEG(file);
	regs.x.dx = FP_OFF(file);
	regs.h.al = dosmode;
	regs.h.ah = 0x3d;
	intdosx(&regs,&regs,&segregs);
	fd = regs.x.ax;
	if(regs.x.cflag){
		if(fd < NERROR){
			errno = _ioerr[fd];
			if(errno == ENOENT && (mode & O_CREAT))
				return _creat(file,0);
		}
		return -1;
	}
	Refcnt[fd] = 1;
	return fd;	/* Return handle */
}
/* Dup a file descriptor. Rather than allocating a new descriptor,
 * as in UNIX or MS-DOS, we maintain a reference count table so we
 * can return the same descriptor that is passed. This saves precious
 * file descriptor space.
 */
int
dup(fd)
int fd;
{
	if(fd < 0 || _fd_type(fd) != _FL_FILE){
		errno = EINVAL;	/* Valid only on files */
		return -1;
	}
	fd = _fd_seq(fd);
	if(fd >= Nfiles || Refcnt[fd] == 0){
		errno = EINVAL;
		return -1;
	}
	Refcnt[fd]++;
	return fd;
}

int
_close(fd)
int fd;
{
	union REGS regs;

	if(fd < 0 || _fd_type(fd) != _FL_FILE){
		errno = EINVAL;	/* Valid only on files */
		return -1;
	}
	fd = _fd_seq(fd);
	if(fd >= Nfiles || Refcnt[fd] == 0){
		errno = EINVAL;
		return -1;
	}
	if(--Refcnt[fd] != 0)
		return 0;	/* Somebody else is still using it */
	regs.x.bx = fd;
	regs.h.ah = 0x3e;
	intdos(&regs,&regs);
	if(regs.x.cflag){
		if(regs.x.ax < NERROR)
			errno = _ioerr[regs.x.ax];
		return -1;
	}
	return 0;
}
int
_read(fd,buf,cnt)
int fd;
void *buf;
unsigned cnt;
{
	union REGS regs;
	struct SREGS segregs;

	if(fd < 0 || _fd_type(fd) != _FL_FILE){
		errno = EINVAL;	/* Valid only on files */
		return -1;
	}
	fd = _fd_seq(fd);
	if(fd >= Nfiles || Refcnt[fd] == 0){
		errno = EINVAL;
		return -1;
	}
	regs.x.bx = fd;
	regs.x.cx = cnt;
	segregs.ds = FP_SEG(buf);
	regs.x.dx = FP_OFF(buf);
	regs.h.ah = 0x3f;
	intdosx(&regs,&regs,&segregs);
	if(regs.x.cflag){
		if(regs.x.ax < NERROR)
			errno = _ioerr[regs.x.ax];
		return -1;
	}
	return regs.x.ax;	/* Return count */
}
int
_write(fd,buf,cnt)
int fd;
const void *buf;
unsigned cnt;
{
	union REGS regs;
	struct SREGS segregs;

	if(fd < 0 || _fd_type(fd) != _FL_FILE){
		errno = EINVAL;	/* Valid only on files */
		return -1;
	}
	fd = _fd_seq(fd);
	if(fd >= Nfiles || Refcnt[fd] == 0){
		errno = EINVAL;
		return -1;
	}
	regs.x.bx = fd;
	regs.x.cx = cnt;
	segregs.ds = FP_SEG(buf);
	regs.x.dx = FP_OFF(buf);
	regs.h.ah = 0x40;
	intdosx(&regs,&regs,&segregs);
	if(regs.x.cflag){
		if(regs.x.ax < NERROR)
			errno = _ioerr[regs.x.ax];
		return -1;
	}
	cnt = regs.x.ax;	/* Return count */

/* Not really necessary when share.exe is loaded, and it really slows down
 * machines without disk write caches
 */
#ifdef	notdef
	/* Call the "commit file" command to flush it out (will fail for
	 * MS-DOS before 3.3)
	 */
	regs.x.bx = fd;
	regs.h.ah = 0x68;
	intdos(&regs,&regs);
#endif
	return cnt;
}
long
_lseek(fd,offset,whence)
int fd;
long offset;
int whence;
{
	union REGS regs;

	if(fd < 0 || _fd_type(fd) != _FL_FILE){
		errno = EINVAL;	/* Valid only on files */
		return -1;
	}
	fd = _fd_seq(fd);
	if(fd >= Nfiles || Refcnt[fd] == 0){
		errno = EINVAL;
		return -1;
	}
	regs.x.bx = fd;
	regs.x.cx = offset >> 16;
	regs.x.dx = offset;
	regs.h.al = whence;
	regs.h.ah = 0x42;
	intdos(&regs,&regs);
	if(regs.x.cflag){
		if(regs.x.ax < NERROR)
			errno = _ioerr[regs.x.ax];
		return -1;
	}
	/* Return new offset */
	return ((long)regs.x.dx << 16) | regs.x.ax;
}
