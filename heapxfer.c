//
//	heapxfer.c
//	Test program to research fastest way to transfer heap to child process
//	MAG 2017/05/05 Original version.
//	MAG 2018/??/?? Many improvements.
//

//done. see if 64k-aligned "heap" in parent makes any difference; lose KLUGE
//done. abstract out a "bail" function and a "note" function
//done. generalize method2 to any config'd number of overlapped I/Os
//done. add method4 where parent heap allocated from mapped space
//done. add method5 where parent does WriteProcessMemory into child space
//done. abstract out a "mapheapfile" function for child process use
//done. allow HEAPSIZE to be set by invocation arg
//done. fill parent heap with different chars 0..9a..zA..Z for each async chunk
//done. add code to checksum the heap before writing and also after mapping
//done. add option to run a specific method once
//done. add method6 where parent uses aio operations to a file
//FIXME add method7 where parent uses TBD extension
//done. convert code to use Win32 calls only where Cygwin provides no support
//moot. method2 needs Cygwin to provide an OVERLAPPED-capable fd
//done. find and fix sharing issue that orphans many heapfiles in recycle bin
//done. add methodnames array to display method names as the methods run
//FIXME find/fix memory leak of heap-size bytes for each method on default run
//done. dynamically find the Windows location of /tmp for heapfile creation

#define _GNU_SOURCE
#include <aio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/cygwin.h>

extern HANDLE _get_osfhandle (int);

#define ASYNCIOS 4
#define MASK64K 0xFFFF
#define SIZE64K 65536
#define HEAPSIZE 128*1024*1024
#define WINPID(p) cygwin_internal (CW_CYGWIN_PID_TO_WINPID, (p))

int asyncios = ASYNCIOS;// default if not overridden by -a option
int heapsize = HEAPSIZE;// default if not overridden by -s option
int onemethod = -1;	// default if not overridden by -m option
char *methodnames[] = {
/*0*/	"ReadProcessMemory",
/*1*/	"Synchronous WriteFile",
/*2*/	"Asynchronous WriteFileEx",
/*3*/	"MapViewOfFile, CopyMemory, FlushViewOfFile",
/*4*/	"Alloc heap from mapped space",
/*5*/	"WriteProcessMemory",
/*6*/	"AIO write",
/*7*/	"TBD extension",
};
int nummethods = sizeof (methodnames) / sizeof (methodnames[0]);
pid_t cpid;  // child's pid
pid_t ppid;  // parent's pid
int fd = -1; // in both parent and child, a file descriptor opened to heapfile
char *cygfilename = "/tmp/heapfile";
char *winfilename; // determined at runtime

void
vnote (const char *fmt, va_list argp)
{
    char	buf[160];
    int		count;
    DWORD	done;
    HANDLE	errh = GetStdHandle (STD_ERROR_HANDLE);

    count = vsprintf (buf, fmt, argp);
    WriteFile (errh, buf, count, &done, NULL);
    FlushFileBuffers (errh);
}

void
note (const char *fmt, ...)
{
    va_list     argp;

    va_start (argp, fmt);
    vnote (fmt, argp);
    va_end (argp);
}

void __attribute__ ((noreturn))
bail (const char *fmt, ...) 
{
    va_list	argp;

    va_start (argp, fmt);
    vnote (fmt, argp);
    va_end (argp);

    exit (1);
}

unsigned int	*crctable = NULL;

void
calcCRCtable (void)
{
    const unsigned int	poly = 0x04C11DB7;
    crctable = malloc (256 * sizeof (unsigned int));

    for (int div = 0; div < 256; div++) {
	unsigned int cb = (unsigned int) div << 24;
	for (int i = 0; i < 8; i++) {
	    if (cb & 0x80000000)
		cb <<= 1, cb ^= poly;
	    else
		cb <<= 1;
	}
	crctable[div] = cb;
    }
}

// return CRC-32 of given buffer
static unsigned int
cksum (char *buf, int count)
{
    unsigned int	crc = 0;

    if (!crctable)
	calcCRCtable ();

    for (int b = 0; b < count; b++) {
	unsigned char byte = buf[b];
	unsigned char pos = (unsigned char) ((crc ^ (byte << 24)) >> 24);
	crc = ((crc << 8) ^ crctable[pos]);
    }

    return crc;
}

char *
getwinfilename (char *name)
{
    static char	 buf[PATH_MAX];
    FILE 	*fp;

    sprintf (buf, "cygpath -w %s", name);
    fp = popen (buf, "r");
    if (!fp)
	bail ("popen failed: %s", strerror (errno));
    fgets (buf, PATH_MAX, fp);
    pclose (fp);

    return buf;
}

// SIGALRM signal handler, used by parent or child process depending on method
void
wakeup (int arg)
{
}

// method 0: ReadProcessMemory (Cygwin's existing method I'm trying to beat)
void *
method0 (void *heapptr)
{	// executed only by child process
    HANDLE	 h = OpenProcess (PROCESS_ALL_ACCESS, FALSE, WINPID(getppid ()));
    void	*heap;
    SIZE_T	 len;
    void	*ptr;
    BOOL	 stat;

    // read value of 'heap' in parent
    stat = ReadProcessMemory (h, heapptr, &ptr, sizeof (void *), &len);
    if (stat == FALSE)
	bail ("ReadProcessMemory error %d\n", GetLastError ());
    if (len != sizeof (void *))
	bail ("ReadProcessMemory read %lld bytes, not %lu\n",
	     len, sizeof (void *));

    // reserve heap memory here in child
    heap = VirtualAlloc (ptr, heapsize,
			 MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (heap != ptr)
	bail ("VirtualAlloc returned %p\n", heap);
    note ("child heap allocated at %p\n", heap);

    // read heap from parent into child
    stat = ReadProcessMemory (h, ptr, heap, heapsize, &len);
    if (stat == FALSE)
	bail ("ReadProcessMemory error %d\n", GetLastError ());
    if (len != heapsize)
	bail ("ReadProcessMemory read %lld bytes, not %d\n", len, heapsize);
    note ("ReadProcessMemory read %lld bytes\n", len);

    return heap;
}

int
newheapfile (int flags)
{	// executed only by the parent process
    fd = open (cygfilename, O_RDWR | O_CREAT, 0777);
    if (fd == -1)
	bail ("open error %d\n", errno);

    HANDLE fh = _get_osfhandle (fd);

    DWORD res = SetFilePointer (fh, heapsize, 0, FILE_BEGIN);
    if (res == INVALID_SET_FILE_POINTER)
	bail ("SetFilePointer error %d\n", GetLastError ());

    BOOL stat = SetEndOfFile (fh);
    if (stat == FALSE)
	bail ("SetEndOfFile error %d\n", GetLastError ());

    return fd;
}

// method 1: synchronous WriteFile to heapfile
void
method1 (void *heap)
{	// executed only by the parent process
    int		 fd = newheapfile (0);
    HANDLE	 fh = _get_osfhandle (fd);
    unsigned int len;

    DWORD	res = SetFilePointer (fh, 0, 0, FILE_BEGIN);
    if (res == INVALID_SET_FILE_POINTER)
	bail ("SetFilePointer error %d\n", GetLastError ());

    BOOL	stat = WriteFile (fh, heap, heapsize, &len, NULL);
    if (stat == FALSE)
	bail ("WriteFile error %d\n", GetLastError ());
    if (len != heapsize)
	bail ("WriteFile wrote %d bytes, not %d\n", len, heapsize);

    note ("WriteFile wrote %d bytes\n", len);
    CloseHandle (fh);
}

volatile int writecount;
volatile int writelen;

void CALLBACK
finisher (unsigned int errcode, unsigned int len, OVERLAPPED *over)
{
    note ("finisher %d error %d\n", writecount, errcode);
    InterlockedDecrement (&writecount);
    InterlockedAdd (&writelen, len);
}

// method 2: asynchronous WriteFileEx to heapfile
void
method2 (void *heap)
{	// executed only by the parent process
    int		 fd = newheapfile (FILE_FLAG_OVERLAPPED);
    HANDLE	 fh = _get_osfhandle (fd); /* fh purposely overridden below */
		 fh = CreateFile (winfilename, GENERIC_READ | GENERIC_WRITE,
				  FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
				  NULL, OPEN_EXISTING,
				  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
				  NULL);
    int		 i;
    DWORD	 len = heapsize / asyncios;
    OVERLAPPED	*overs = calloc (asyncios, sizeof (OVERLAPPED));
    BOOL	*stats = calloc (asyncios, sizeof (BOOL));

    for (i = 0; i < asyncios; i++)
	overs[i].Offset = i * len;
    writecount = asyncios;
    writelen = 0;

    for (i = 0; i < asyncios; i++) {
	stats[i] = WriteFileEx (fh, heap + i * len, len, &overs[i], finisher);
	if (stats[i] == FALSE || GetLastError () != ERROR_SUCCESS)
	    bail ("WriteFileEx #%d error %d\n", i, GetLastError ());
    }

    while (InterlockedCompareExchange (&writecount, 0, 0)) {
	for (i = 0; i < asyncios; i++) {
	    if (overs[i].Internal && overs[i].Internal != STATUS_PENDING)
		bail ("overlapped #%d error %llu\n", i, overs[i].Internal);
	}
	SleepEx (10, TRUE);
    }

    note ("WriteFileEx %d times wrote %d bytes\n", asyncios, writelen);
    CloseHandle (fh);
}

// method 3: MapViewOfFile heapfile then CopyMemory then FlushViewOfFile
void
method3 (void *heap)
{	// executed only by the parent process
    int		fd = newheapfile (0);
    HANDLE	fh = _get_osfhandle (fd);

    HANDLE	fm = CreateFileMapping (fh, NULL,
					PAGE_READWRITE | SEC_COMMIT,
					0, heapsize, NULL);
    if (fm == NULL)
	bail ("CreateFileMapping error %d\n", GetLastError ());

    void	*view = MapViewOfFile (fm, FILE_MAP_WRITE,
				       0, 0, (SIZE_T) heapsize);
    if (view == NULL)
	bail ("MapViewOfFile error %d\n", GetLastError ());

    DWORD	time0 = GetTickCount ();
    CopyMemory (view, heap, heapsize);
    note (" (CopyMemory took %dms)\n", GetTickCount () - time0);

    BOOL	stat = FlushViewOfFile (view, heapsize);
    if (stat == FALSE)
	bail ("FlushViewOfFile error %d\n", GetLastError ());

    stat = FlushFileBuffers (fh);
    if (stat == FALSE)
	bail ("FlushFileBuffers error %d\n", GetLastError ());

    note ("Map&Copy&Flush wrote %d bytes\n", heapsize);
    CloseHandle (fh);
}

// method 4: alloc parent heap from mapped space instead of usual heap space
char *
method4 (char *heap)
{	// executed only by the parent process
    static HANDLE fh;
    static HANDLE fm;

    if (!heap) {
	// parent init time
	fd = newheapfile (0);
	fh = _get_osfhandle (fd);
	HANDLE	fm = CreateFileMapping (fh, NULL,
					PAGE_READWRITE | SEC_COMMIT,
					0, heapsize, NULL);
	if (fm == NULL)
	    bail ("CreateFileMapping error %d\n", GetLastError ());

	heap = MapViewOfFile (fm, FILE_MAP_WRITE,
			      0, 0, (SIZE_T) heapsize);
	if (heap == NULL)
	    bail ("MapViewOfFile error %d\n", GetLastError ());
    } else {
	// parent finish time
	BOOL	stat = FlushViewOfFile (heap, heapsize);
	if (stat == FALSE)
	    bail ("FlushViewOfFile error %d\n", GetLastError ());

	CloseHandle (fm);
	CloseHandle (fh);
	heap = NULL;
    }

    return heap;
}

// method 5: WriteProcessMemory
char *
method5 (char **heapptr)
{	// executed by both the parent and child processes
    HANDLE	 h;
    char	*heap;
    SIZE_T	 len;
    void	*ptr;
    BOOL	 stat;

    if (cpid == getpid ()) {
	// this is the child process
	h = OpenProcess (PROCESS_ALL_ACCESS, FALSE, WINPID(ppid));

	// read value of 'heap' in parent
	stat = ReadProcessMemory (h, heapptr, &ptr, sizeof (void *), &len);
	if (stat == FALSE)
	    bail ("ReadProcessMemory error %d\n", GetLastError ());
	if (len != sizeof (void *))
	    bail ("ReadProcessMemory read %lld bytes, not %lu\n",
		  len, sizeof (void *));

	// reserve heap memory here in child
	heap = VirtualAlloc (ptr, heapsize,
			     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (heap != ptr)
	    bail ("VirtualAlloc returned %p\n", heap);

	note ("child heap allocated at %p\n", heap);
    } else {
	// this is the parent process
	heap = *heapptr;
	h = OpenProcess (PROCESS_ALL_ACCESS, FALSE, WINPID(cpid));

	stat = WriteProcessMemory (h, heap, heap, heapsize, &len);
	if (stat == FALSE)
	    bail ("WriteProcessMemory error %d\n", GetLastError ());
	if (len != heapsize)
	    bail ("WriteProcessMemory wrote %lld bytes, not %d\n", len, heapsize);
	note ("WriteProcessMemory wrote %lld bytes\n", len);
    }

    return heap;
}

volatile int completions;

static void
completer (int signo, siginfo_t *si, void *context)
{
    struct aiocb *aio = si->si_value.sival_ptr;

    note ("aio %p completed, %lld bytes at offset %p\n",
	 aio, aio->aio_rbytes, aio->aio_offset);
    InterlockedIncrement (&completions);
}

// method 6: aio operations into heap file
char *
method6 (char *heap)
{       // executed only by the parent process
    int		  fd = newheapfile (0);
    int		  finished;
    int           i;
    DWORD         len = heapsize / asyncios;
    struct aiocb *aiocbs = calloc (asyncios, sizeof (struct aiocb));
    struct aiocb **ptrs = calloc (asyncios, sizeof (struct aiocb *));
    int		  stat;
    struct sigaction sa;
    struct sigevent  se;
    struct timespec  ts = {5, 0};

    completions = 0;
    memset (&sa, 0, sizeof (struct sigaction));
    memset (&se, 0, sizeof (struct sigevent));

    se.sigev_notify = SIGEV_SIGNAL;
    se.sigev_signo  = SIGIO;

    sigemptyset (&sa.sa_mask);
    sigaddset (&sa.sa_mask, SIGIO);
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = completer;
    stat = sigaction (SIGIO, &sa, NULL);
    if (stat == -1)
	bail ("sigaction error %s\n", strerror (errno));

    for (i = 0; i < asyncios; i++) {
	ptrs[i] = &aiocbs[i];
	ptrs[i]->aio_fildes = fd;
	ptrs[i]->aio_buf = heap + i * len;
	ptrs[i]->aio_nbytes = len;
	ptrs[i]->aio_offset = i * len;

	ptrs[i]->aio_sigevent = se;
	ptrs[i]->aio_sigevent.sigev_value.sival_ptr = ptrs[i];

	stat = aio_write (ptrs[i]);
	if (stat == -1 && errno != EAGAIN)
	    bail ("aio_write #%d error %s\n", i, strerror (errno));
    }

    writecount = asyncios;
    writelen = 0;
    finished = 0;

    while (finished < asyncios) {
	stat = aio_suspend ((const struct aiocb *const *) ptrs, asyncios, &ts);
	if (stat == -1)
	    bail ("aio_suspend error %s\n", strerror (errno));
	finished = 0;
	for (i = 0; i < asyncios; i++) {
	    stat = aio_error (ptrs[i]);
	    if (stat == EAGAIN)
		stat = aio_write (ptrs[i]);
	    else if (stat != EINPROGRESS)
		++finished;
	}
    }

    for (i = 0; i < asyncios; i++) {
	stat = aio_return (ptrs[i]);
	if (stat >= 0)
	    writelen += stat;
	else
	    note ("aio_return #%d error %s\n", i, strerror (errno));
    }

    note ("aio write %d times wrote %d bytes\n", asyncios, writelen);
    note ("%d completion signals received\n", completions);
    return heap;
}

// method 7: TBD extension
char *
method7 (char *heap)
{       // executed only by the parent process

    //XXX
    //
    return heap;
}

char *
mapheapfile ()
{	// executed only by the child process
    fd = open (cygfilename, O_RDONLY, 0);
    if (fd == -1)
	bail ("open error %d\n", errno);

    char *heap = mmap (0, heapsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (heap == MAP_FAILED)
	bail ("mmap error %d\n", errno);

    return heap;
}

int
main (int argc, char **argv)
{
    char	*data = NULL;
    char	*heap = NULL;
    int		 i;
    int		 method;
    DWORD	 time0;
    DWORD	 time1;
    DWORD	 time2;
    DWORD	 timep;
    DWORD	 timeq;
    pid_t	 wpid;
    int		 wstat;

    for (i = 1; i < argc; i++) {
	if (argv[i][0] == '-') {
	    switch (argv[i][1]) {
	    case 'a':
		if ((i + 1) < argc) {
		    asyncios = strtol (argv[i + 1], NULL, 10);
		    i++;
		    continue;
		}
		goto usage;
	    case 'm':
		if ((i + 1) < argc) {
		    onemethod = strtol (argv[i + 1], NULL, 10);
		    if (onemethod < 0 || onemethod > nummethods)
			goto usage;
		    i++;
		    continue;
		}
		goto usage;
	    case 's':
		if ((i + 1) < argc) {
		    long size = strtol (argv[i + 1], NULL, 10);
		    size *= 1024 * 1024;
		    if (size < 0 || size > 0x7FFFFFFF)
			bail ("size %ld is too big, sorry.\n", size);
		    heapsize = size;
		    i++;
		    continue;
		}
		goto usage;
	    }
	}
usage:
	note ("\nSupported options are:\n");
	int foo = ASYNCIOS;
	note ("    -a N	perform N asynchronous writes (default %d)\n", foo);
	note ("    -m M	just run method M once; M in [0..%d]\n", nummethods-1);
	foo = HEAPSIZE / 1024 / 1024;
	note ("    -s N	set heap size to N megabytes (default %d)\n\n", foo);
	return 1;
    }

    if (heapsize % asyncios)
	bail ("%d asyncios don't divide heapsize %d evenly\n",
	     asyncios, heapsize);

    winfilename = getwinfilename (cygfilename);

    note ("running with heapsize %d (%d MB), asyncios %d\n",
	 heapsize, heapsize / (1024 * 1024), asyncios);
    note ("parent pid %d started\n", ppid = getpid ());

    for (method = 0; method < nummethods; method++) {
	if (onemethod != -1 && method != onemethod)
	    continue;
	note ("\n*** method %d - %s ***\n", method, methodnames[method]);
	time0 = GetTickCount ();

	cpid = fork ();
	switch (cpid) {
	case -1: // fork error
	    bail ("fork error\n");

	case 0: // this is the child process
	    sigset (SIGALRM, wakeup);
	    note ("child pid %d started\n", cpid = getpid ());
	    sigpause (SIGALRM); // wait for parent's signal
	    note ("begin child work %dms\n", time1 = GetTickCount () - time0);

	    switch (method) {
	    case 0:
		heap = method0 (&heap);
		break;

	    case 1:
	    case 2:
	    case 3:
	    case 4:
	    case 6:
		// open and map heapfile
		heap = mapheapfile ();
		break;

	    case 5:
		heap = method5 (&heap);
		kill (ppid, SIGALRM);
		timep = GetTickCount ();
		sigpause (SIGALRM);
		timeq = GetTickCount ();
		time1 += (timeq - timep); // no charge for waiting
		break;

	    case 7:
		//FIXME TBD
		break;
	    }
	    note ("end child work %dms\n", time2 = GetTickCount () - time0);
	    note ("total child time %dms ***\n", time2 - time1);

	    note ("child cksum %08X\n", cksum (heap, heapsize));
	    if (heap[0] != '+' )
		bail ("read first byte of heap failed\n");
	    if (heap[heapsize - 1] != '+')
		bail ("read last byte of heap failed\n");

	    return 0;

	default: // this is the parent process
	    if (method == 4) {
		heap = method4 (NULL);
	    } else {
		data = (char *) malloc (heapsize + SIZE64K);
		if ((unsigned long) data & MASK64K)
		    heap = (char *) (( (unsigned long) data + SIZE64K) & ~MASK64K);
		else
		    heap = data;
	    }

	    note ("parent heap allocated at %p\n", heap);

	    for (i = 0; i < asyncios; i++) {
		size_t sz = heapsize / asyncios;
		memset (&heap[sz * i], 64 + i, sz);
	    }
	    heap[0] = heap[heapsize - 1] = '+';

	    note ("parent cksum %08X\n", cksum (heap, heapsize));
	    note ("begin parent work %dms\n", time1 = GetTickCount () - time0);

	    switch (method) {
	    case 0:
		// everything is done by child
		break;

	    case 1:
		method1 (heap);
		break;

	    case 2:
		method2 (heap);
		break;

	    case 3:
		method3 (heap);
		break;

	    case 4:
		heap = method4 (heap);
		break;

	    case 5:
		sigset (SIGALRM, wakeup);
		kill (cpid, SIGALRM);
		timep = GetTickCount ();
		sigpause (SIGALRM);
		timeq = GetTickCount ();
		time1 += (timeq - timep); // no charge for waiting
		method5 (&heap);
		break;

	    case 6:
		method6 (heap);
		break;

	    case 7:
		method7 (heap);
		break;
	    }

	    note ("end parent work %dms\n", time2 = GetTickCount () - time0);
	    note ("total parent time %dms ***\n", time2 - time1);
	    kill (cpid, SIGALRM); // signal waiting child
again:
	    wpid = wait (&wstat);
	    if (wpid == -1) {
		if (errno != EINTR)
		    bail ("wait error %s\n", strerror (errno));
		goto again;
	    }
	    if (wpid != cpid)
		bail ("wait() returned wrong pid %d\n", wpid);

	    if (fd != -1)
		close (fd);
	    unlink (cygfilename);
	}
    }

    return 0;
}
