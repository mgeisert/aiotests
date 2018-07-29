all::	example.exe heapxfer.exe iozone.exe sigtest.exe

example.exe:	example.c
	gcc -Wall -g -o example example.c

heapxfer.exe:	heapxfer.c
	gcc -Wall -g -o heapxfer heapxfer.c

iozone.exe:	iozone-mg/iozone.c
	make -C iozone-mg Cygwin64
	mv iozone-mg/iozone.exe .

sigtest.exe:	sigtest.c
	gcc -Wall -g -o sigtest sigtest.c

clean:
	rm -f example.exe heapxfer.exe iozone.exe sigtest.exe
