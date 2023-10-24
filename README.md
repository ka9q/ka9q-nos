# The KA9Q NOS TCP/IP Package
[KA9Q](http://ka9q.net/) Network Operating System

### Introduction

My KA9Q NOS TCP/IP package began life way back in late 1985 on a surplus Xerox 820
computer board running CP/M with a 4 MHz Zilog Z-80 CPU, 64KB of RAM and a 8" floppy
drive holding all of 243KB. ("KB" stands for **kilo** bytes -- not mega or giga).
Shortly after that, it moved to the IBM PC with the 8088 and 80286 CPUs running MS-DOS.

KA9Q NOS was only the second known implementation of the Internet protocols for
low-end computers; the first was MIT's PC/IP, which became the basis of the now-defunct
company [FTP Software, Inc](http://www.ftp.com/).
Unlike PC/IP, KA9Q NOS could simultaneously act as an Internet client, a server and an
IP packet router, and it could handle multiple client and server sessions at once.

KA9Q NOS attracted many contributors and became very widely used throughout the late
1980s and early 1990s in amateur packet radio and in various educational projects.
In a way, it was the [Linux](http://www.linux.com/) of its day, although Linux is now
a far larger and more ambitious project.

KA9Q NOS became the basis for several low-end commercial dialup terminal servers and routers.
It also influenced the development of the Internet protocols and certain implementations,
including the Linux kernel. It was also incorporated in the imbedded software
in [Qualcomm](http://www.qualcomm.com/) CDMA cellular phones.

When I originally conceived NOS, affordable personal computers lacked the hardware
support (especially memory management and a "protected" mode) needed to run a "real"
operating system such as UNIX. The so-called "operating systems" then available for personal
computers (e.g., MS-DOS and Windows 3.1) lacked any native support for the Internet protocols,
so this package filled a real need.

But that was a different era. KA9Q NOS is now largely obsolete, and I have not maintained it
since the mid 1990s when Linux took off. If you are looking simply to connect your PC to the
Internet, I recommend just using the native Internet support in your operating system of choice.

If you need direct support for amateur (ham) packet radio, then [Linux](http://www.linuxdoc.org/HOWTO/AX25-HOWTO/)
is your best bet.
Much of the packet radio code from NOS, including the AX.25 implementation, is now a standard
part of the Linux kernel.
If you want to access packet radio from Windows, the most straightforward way is to set up a
Linux system with AX.25 support and network the two with Ethernet.

KA9Q NOS still has some utility in small imbedded applications.
But you should also check out any of the several imbedded versions of Linux, such as
[Hard Hat Linux](http://www.hardhatlinux.com/).

### Software

For the diehards, and for historical interest, I'm still keeping my package here on the web.
Two versions of my KA9Q NOS TCP/IP package available:

- The traditional real-mode version for Borland C++ 3.1:
  - [export.zip](http://www.ka9q.net/code/ka9qnos/export.zip) source zip archive.
  - [netexe.zip](http://www.ka9q.net/code/ka9qnos/netexe.zip) DOS executable, as a ZIP archive.
- A 32-bit protected-mode version for [DJGPP version 2](http://www.delorie.com/djgpp/):
  - [djdist.zip](http://www.ka9q.net/code/ka9qnos/djdist.zip) source zip archive.

Other flavors of my code contributed by other volunteers along with other Internet-on-ham-radio
packages are available at [UCSD's FTP site](ftp://ftp.ucsd.edu/hamradio/packet/tcpip/).

### Book list

I am often asked questions about TCP/IP and amateur packet radio that entire books have been
written to answer. So for those with a real interest in how TCP/IP works "under the hood",
here is a list of books I can recommend. All but the last were written for the Internet as a
whole and do not specifically cover amateur packet radio.

1. Comer, Douglas E., <Cite>Internetworking with TCP/IP</cite>, ISBN 0-13-468505-9 (2nd ed, 1991.
First volume of a three-volume set).
This first volume, subtitled <cite>Principles, Protocols and Architecture</cite> is probably **the**
classic text for the theory behind the core Internet protocols.

2. Lynch, Daniel C. and Rose, Marshall T., eds., <Cite>Internet System Handbook</cite>,
ISBN 0-201-56741-5 (1993, Addison-Wesley).
A excellent collection of chapters on the various elements of the Internet's design by many of
those who conceived them.

3. Stevens, W. Richard, <Cite>TCP/IP Illustrated</cite>, ISBN 0-201-63346-9, (1994, first volume
of a two-volume set). This book emphasizes the practical aspects of the Internet protocols, such
as performance and scaling issues, with many operational examples.  An excellent companion to the Comer book.

4. Wade, Ian, G3NRW, <Cite>NOSintro: TCP/IP Over Packet Radio; An introduction to the KA9Q Network
Operating System</cite>, ISBN 1-897649-00-2 (1992, Dowermain). This is the only book written specifically
about TCP/IP in the amateur radio environment, and as the title implies it is primarily about using the
KA9Q NOS in that environment. It's basically the exhaustive user's guide I never got around to writing myself.

**README Last updated: 15 Mar 2002**

Code last updated late 1980s/early 1990s. Placed here for historical reasons.

