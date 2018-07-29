# _aiotests_
Test programs for Cygwin's POSIX Asynchronous I/O feature:

- _example_

An example program that appears on the aio(7) _man_ page on Linux systems.

- _sigtest_

A small program that tests the wait in aio_suspend() and thus sigtimedwait().

- _heapxfer_

A program that times various methods of transferring a large memory allocation
from parent to child processes.  One method (_-m 6_) uses the aio interfaces.

- _iozone_

A long-established disk I/O benchmark.  This version is a snapshot of the
unofficial iozone-mg project that re-ported _iozone_ to Cygwin and native Windows.


_Note: POSIX Asynchronous I/O is available in Cygwin 2.11 and later versions._
