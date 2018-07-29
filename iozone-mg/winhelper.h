/*
 * 	winhelper.h
 * 	Bare minimum versions of some *nix things missing from Windows.
 * 	Yar, there be kluges here...
 * 	If you want a more *nix experience on Windows, use the Cygwin builds.
 */

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <getopt.h>
#include <signal.h>
#include <stdlib.h>
#define _POSIX_SOURCE
#include <unistd.h>
#undef _POSIX_SOURCE
#include <sys/stat.h>

struct flock {
    short	l_type;
    short	l_whence;
    long	l_start;
    long	l_len;
    short	l_pid;
    short	l_xxx;
};

typedef __p_sig_fn_t sighandler_t;

#define F_GETFL		3

#define F_RDLCK		1
#define F_WRLCK		2
#define F_UNLCK		3
#define F_SETLKW	9

#define MAP_FILE	0
#define MAP_SHARED	1
#define MAP_PRIVATE	2

#define MS_SYNC		0
#define MS_ASYNC	1

#define O_SYNC		0 //XXX

#define PROT_READ	1
#define PROT_WRITE	2

#define SIGKILL		9

#define bcopy(src, dst, size) memcpy(dst, src, size)
#define bzero(addr, size) memset(addr, 0, size)

#ifdef WINHELPER_FUNCS
int
wh_creat(const char *name, int mode)
{
    int flags = _O_CREAT | _O_TRUNC;

    // Map *nix 'user' mode values to Windows mode values
    switch ((mode >> 6) & 7) {
    case 4:
	mode = _S_IREAD;
	break;
    case 0:
    case 6:
	mode = _S_IREAD | _S_IWRITE;
	break;
    default:
	mode = 0;
    }

    return open(name, flags, mode);
}

int
fcntl(int fd, int op, ...)
{
    errno = ENOSYS;
    return -1;
}

int
fsync(int fd)
{
#if 1 //XXX just so iozone runs.. fix this later
    return 0;
#else
    errno = ENOSYS;
    return -1;
#endif
}

int
link(char *name1, char *name2)
{
    errno = ENOSYS;
    return -1;
}

int
mkdir2(char *name, int mode)
{
    // Ignore supplied mode values
    return _mkdir(name);
}

void *
mmap(void *addr, size_t size, int prot, int sharing, int fd, int extra)
{
    errno = ENOSYS;
    return (void *) -1;
}

int
msync(void *addr, size_t size, int type)
{
    errno = ENOSYS;
    return -1;
}

int
munmap(void *addr, size_t size)
{
    errno = ENOSYS;
    return -1;
}

int
wh_open(char *name, int flags, int mode)
{
    // Map *nix 'user' mode values to Windows mode values
    switch ((mode >> 6) & 7) {
    case 4:
	mode = _S_IREAD;
	break;
    case 0:
    case 6:
	mode = _S_IREAD | _S_IWRITE;
	break;
    default:
	mode = 0;
    }

    return open(name, flags, mode);
}

int
poll(struct pollfd *desc, size_t size, int flags)
{
    errno = ENOSYS;
    return -1;
}

sighandler_t __cdecl
signal(int signo, sighandler_t handler)
{
    errno = ENOSYS;
    return (sighandler_t) -1;
}

int
sync(void)
{
    errno = ENOSYS;
    return -1;
}

int
wait(void *TBD)
{
    errno = ENOSYS;
    return -1;
}

#else
extern int mkdir2(char *, int);
#endif

#define mkdir(A,B) mkdir2(A,B)
