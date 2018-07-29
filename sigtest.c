#include <aio.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
    struct aiocb    aio;
    struct aiocb   *aiolist[] = {&aio};
    struct __liocb  liocb = {1, 0};
    int             msecs;
    struct timespec ts;

    if (argc < 2) {
	fprintf(stderr, "Usage: %s nnn\n", argv[0]);
	fprintf(stderr, "where nnn is a number of milliseconds\n\n");
	fprintf(stderr, "Success is an EAGAIN error after nnn msecs.\n");
	fprintf(stderr, "Use the 'time' command to verify the delay.\n");
	return 1;
    }

    msecs = atoi(argv[1]);
    if (msecs >= 1000)
	ts.tv_sec = msecs / 1000;
    else
	ts.tv_sec = 0;
    ts.tv_nsec = 1000000 * (msecs % 1000);

    memset(&aio, 0, sizeof(struct aiocb));
    aiolist[0]->aio_errno = EINPROGRESS;
    aiolist[0]->aio_liocb = &liocb;

    int stat = aio_suspend((const struct aiocb *const *) aiolist, 1, &ts);
    if (stat == -1)
	perror("aio_suspend");

    return 0;
}
